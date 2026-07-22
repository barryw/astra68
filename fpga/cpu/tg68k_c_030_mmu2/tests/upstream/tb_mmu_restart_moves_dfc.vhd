-- tb_mmu_restart_moves_dfc.vhd
-- Regression for kernel copyin/copyout restart through separate SRP/CRP roots.
-- Four MOVES.B forms fault independently on one user page: absolute DFC write,
-- postincrement DFC write, predecrement DFC write, and postincrement SFC read.
-- Each handler invocation repairs the descriptor, PFLUSHes, and executes an
-- unmodified RTE. Every restarted instruction must perform exactly one target
-- transfer, and auto-modified address registers must change exactly once.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.std_logic_unsigned.all;

entity tb_mmu_restart_moves_dfc is
end entity;

architecture behavioral of tb_mmu_restart_moves_dfc is

    function slv_to_hex(value : std_logic_vector) return string is
        constant hex_chars : string := "0123456789ABCDEF";
        variable result : string(1 to value'length/4);
        variable nibble : std_logic_vector(3 downto 0);
        variable v : std_logic_vector(value'length - 1 downto 0);
    begin
        v := value;
        for i in 0 to (v'length/4 - 1) loop
            nibble := v(v'length - 1 - i*4 downto v'length - 4 - i*4);
            result(i+1) := hex_chars(to_integer(unsigned(nibble)) + 1);
        end loop;
        return result;
    end function;

    function is_x(value : std_logic_vector) return boolean is
    begin
        for i in value'range loop
            if value(i) /= '0' and value(i) /= '1' then
                return true;
            end if;
        end loop;
        return false;
    end function;

    constant CLK_PERIOD : time := 10 ns;
    signal clk       : std_logic := '0';
    signal nReset    : std_logic := '0';
    signal test_done : boolean := false;

    signal clkena_in   : std_logic := '1';
    signal data_in     : std_logic_vector(15 downto 0) := x"4E71";
    signal data_write  : std_logic_vector(15 downto 0);
    signal addr_out    : std_logic_vector(31 downto 0);
    signal busstate    : std_logic_vector(1 downto 0);
    signal nWr         : std_logic;
    signal nUDS        : std_logic;
    signal nLDS        : std_logic;
    signal FC          : std_logic_vector(2 downto 0);

    signal pmmu_walker_req  : std_logic;
    signal pmmu_walker_we   : std_logic;
    signal pmmu_walker_addr : std_logic_vector(31 downto 0);
    signal pmmu_walker_wdat : std_logic_vector(31 downto 0);
    signal pmmu_walker_ack  : std_logic := '0';
    signal pmmu_walker_data : std_logic_vector(31 downto 0) := (others => '0');
    signal pmmu_walker_berr : std_logic := '0';

    signal pmmu_addr_phys     : std_logic_vector(31 downto 0);
    signal pmmu_cache_inhibit : std_logic;
    signal pmmu_addr_log      : std_logic_vector(31 downto 0);

    signal debug_TG68_PC       : std_logic_vector(31 downto 0);
    signal debug_trap_berr     : std_logic;
    signal debug_trap_mmu_berr : std_logic;
    signal debug_trap_addr_error : std_logic;
    signal debug_trap_vector   : std_logic_vector(31 downto 0);
    signal debug_pmmu_fault    : std_logic;
    signal debug_pmmu_busy     : std_logic;
    signal debug_cpu_halted    : std_logic;
    signal debug_pmmu_fault_status : std_logic_vector(15 downto 0);
    signal debug_pmmu_saved_addr   : std_logic_vector(31 downto 0);
    signal debug_pmmu_walk_desc_addr : std_logic_vector(31 downto 0);
    signal debug_pmmu_walk_desc_data : std_logic_vector(31 downto 0);
    signal debug_pmmu_ptr1_desc_addr : std_logic_vector(31 downto 0);
    signal debug_pmmu_ptr1_desc_data : std_logic_vector(31 downto 0);
    signal debug_pmmu_ptr2_desc_addr : std_logic_vector(31 downto 0);
    signal debug_pmmu_ptr2_desc_data : std_logic_vector(31 downto 0);
    signal debug_pmmu_ptr3_desc_addr : std_logic_vector(31 downto 0);
    signal debug_pmmu_ptr3_desc_data : std_logic_vector(31 downto 0);
    signal debug_regfile_d0 : std_logic_vector(31 downto 0);
    signal debug_regfile_d2 : std_logic_vector(31 downto 0);
    signal debug_stop      : std_logic;

    signal stall_cooldown : integer range 0 to 3 := 0;
    signal walker_req_prev : std_logic := '0';
    signal mem_wait : std_logic := '0';
    signal saw_handler_pc  : boolean := false;
    signal saw_done_marker : boolean := false;
    signal trace_fault_prev : std_logic := '0';
    signal saw_crp_root_desc : boolean := false;
    signal saw_corrupt_crp_root_desc : boolean := false;
    signal abs_write_count  : natural := 0;
    signal post_write_count : natural := 0;
    signal pre_write_count  : natural := 0;
    signal post_read_count  : natural := 0;

    signal crp_root_desc : std_logic_vector(31 downto 0) := x"4FAA7002";
    signal crp_ptr1_desc : std_logic_vector(31 downto 0) := x"4FAA8002";
    signal crp_indirect_desc : std_logic_vector(31 downto 0) := x"00007002";
    signal srp_low_root_desc : std_logic_vector(31 downto 0) := x"00006802";
    signal srp_stack_root_desc : std_logic_vector(31 downto 0) := x"00006902";

    type mem_type is array(0 to 16383) of std_logic_vector(15 downto 0);

    function init_mem return mem_type is
        variable m : mem_type := (others => x"4E71");
        variable w : integer;
    begin
        -- Reset vectors
        m(0) := x"0000"; m(1) := x"2000";
        m(2) := x"0000"; m(3) := x"0100";
        m(4) := x"0000"; m(5) := x"0080"; -- vector 2
        for i in 3 to 63 loop
            m(i*2)   := x"0000";
            m(i*2+1) := x"00C0";
        end loop;

        -- Vector 2 handler: count faults, save frame base, preserve A6, repair
        -- the indirect descriptor, flush the user page, and RTE unmodified.
        m(64) := x"52B9"; m(65) := x"0000"; m(66) := x"1F00"; -- ADDQ.L #1,$1F00.L
        m(67) := x"23CF"; m(68) := x"0000"; m(69) := x"1F24"; -- MOVE.L A7,$1F24.L
        m(70) := x"2F0E";                                     -- MOVE.L A6,-(A7)
        m(71) := x"23FC"; m(72) := x"0000"; m(73) := x"7C61";
        m(74) := x"0000"; m(75) := x"7000";                   -- MOVE.L #$00007C61,$7000.L
        m(76) := x"2C7C"; m(77) := x"1DFF"; m(78) := x"FFF5"; -- MOVEA.L #$1DFFFFF5,A6
        m(79) := x"F016"; m(80) := x"3810";                   -- PFLUSH #0,#0,(A6)
        m(81) := x"2C5F";                                     -- MOVEA.L (A7)+,A6
        m(82) := x"4E73";                                     -- RTE

        -- Unexpected trap handler
        m(96) := x"23FC"; m(97) := x"00FF"; m(98) := x"0000";
        m(99) := x"0000"; m(100) := x"1F00";
        m(101) := x"4E72"; m(102) := x"2700";

        -- Program: enable separate SRP/CRP translation, set SFC=DFC=1, move
        -- the supervisor stack to an SRP-walked page, and exercise four
        -- independently faulted MOVES forms. Between cases the indirect target
        -- is invalidated and the user page is flushed so every case must fault.
        w := 128;
        m(w) := x"2E7C"; m(w+1) := x"0000"; m(w+2) := x"1080"; w := w+3;
        m(w) := x"F017"; m(w+1) := x"4C00"; w := w+2;           -- PMOVE (A7),CRP
        m(w) := x"2E7C"; m(w+1) := x"0000"; m(w+2) := x"1088"; w := w+3;
        m(w) := x"F017"; m(w+1) := x"4800"; w := w+2;           -- PMOVE (A7),SRP
        m(w) := x"F000"; m(w+1) := x"2400"; w := w+2;           -- PFLUSHA
        m(w) := x"F038"; m(w+1) := x"4000"; m(w+2) := x"1090"; w := w+3; -- PMOVE TC
        m(w) := x"4E71"; m(w+1) := x"4E71"; w := w+2;
        m(w) := x"7001"; w := w+1;                               -- MOVEQ #1,D0
        m(w) := x"4E7B"; m(w+1) := x"0000"; w := w+2;           -- MOVEC D0,SFC
        m(w) := x"4E7B"; m(w+1) := x"0001"; w := w+2;           -- MOVEC D0,DFC
        m(w) := x"243C"; m(w+1) := x"1234"; m(w+2) := x"56A5"; w := w+3;
        m(w) := x"2E7C"; m(w+1) := x"0BAF"; m(w+2) := x"A000"; w := w+3;

        m(w) := x"0E39"; m(w+1) := x"2800";                    -- MOVES.B D2,($1DFFFFF5).L
        m(w+2) := x"1DFF"; m(w+3) := x"FFF5"; w := w+4;

        m(w) := x"23FC"; m(w+1) := x"BADF"; m(w+2) := x"EED0";
        m(w+3) := x"0000"; m(w+4) := x"7000"; w := w+5;        -- invalidate indirect target
        m(w) := x"2C7C"; m(w+1) := x"1DFF"; m(w+2) := x"FFF5"; w := w+3;
        m(w) := x"F016"; m(w+1) := x"3810"; w := w+2;           -- PFLUSH user page
        m(w) := x"227C"; m(w+1) := x"1DFF"; m(w+2) := x"FFF6"; w := w+3;
        m(w) := x"0E19"; m(w+1) := x"2800"; w := w+2;           -- MOVES.B D2,(A1)+
        m(w) := x"23C9"; m(w+1) := x"0000"; m(w+2) := x"1F30"; w := w+3;

        m(w) := x"23FC"; m(w+1) := x"BADF"; m(w+2) := x"EED0";
        m(w+3) := x"0000"; m(w+4) := x"7000"; w := w+5;
        m(w) := x"2C7C"; m(w+1) := x"1DFF"; m(w+2) := x"FFF5"; w := w+3;
        m(w) := x"F016"; m(w+1) := x"3810"; w := w+2;
        m(w) := x"765A"; w := w+1;                               -- MOVEQ #$5A,D3
        m(w) := x"227C"; m(w+1) := x"1DFF"; m(w+2) := x"FFF8"; w := w+3;
        m(w) := x"0E21"; m(w+1) := x"3800"; w := w+2;           -- MOVES.B D3,-(A1)
        m(w) := x"23C9"; m(w+1) := x"0000"; m(w+2) := x"1F34"; w := w+3;

        m(w) := x"23FC"; m(w+1) := x"BADF"; m(w+2) := x"EED0";
        m(w+3) := x"0000"; m(w+4) := x"7000"; w := w+5;
        m(w) := x"2C7C"; m(w+1) := x"1DFF"; m(w+2) := x"FFF5"; w := w+3;
        m(w) := x"F016"; m(w+1) := x"3810"; w := w+2;
        m(w) := x"247C"; m(w+1) := x"1DFF"; m(w+2) := x"FFF8"; w := w+3;
        m(w) := x"7800"; w := w+1;                               -- MOVEQ #0,D4
        m(w) := x"0E1A"; m(w+1) := x"4000"; w := w+2;           -- MOVES.B (A2)+,D4
        m(w) := x"23CA"; m(w+1) := x"0000"; m(w+2) := x"1F38"; w := w+3;
        m(w) := x"23C4"; m(w+1) := x"0000"; m(w+2) := x"1F3C"; w := w+3;
        m(w) := x"23FC"; m(w+1) := x"C0DE"; m(w+2) := x"700D";
        m(w+3) := x"0000"; m(w+4) := x"1F2C"; w := w+5;
        m(w) := x"60FE";                                         -- BRA.S *

        -- CRP / SRP
        m(2112) := x"8000"; m(2113) := x"0002"; m(2114) := x"4FAA"; m(2115) := x"6000";
        m(2116) := x"8000"; m(2117) := x"0002"; m(2118) := x"4052"; m(2119) := x"C000";
        m(2120) := x"82A0"; m(2121) := x"8680";

        -- SRP low tree for vectors/code/handler/result/PTE target:
        -- high SRP root slot 0 is supplied by the walker model below and
        -- points here to table $6800.
        m(13312) := x"0000"; m(13313) := x"6E02";
        m(14080) := x"0000"; m(14081) := x"0061"; -- slot 0, logical $0000 -> phys $0000
        m(14088) := x"0000"; m(14089) := x"1061"; -- slot 4, logical $1000 -> phys $1000
        m(14094) := x"0000"; m(14095) := x"1C61"; -- slot 7, logical $1C00 -> phys $1C00
        m(14136) := x"0000"; m(14137) := x"7061"; -- slot 28, logical $7000 -> phys $7000

        -- CRP target final-level indirect target: BADFEED0 sentinel, not a
        -- page descriptor, until the handler repairs it to $00007C61.
        m(14336) := x"BADF"; m(14337) := x"EED0";
        m(16#0F80#) := x"0000"; m(16#0F81#) := x"0000"; -- fault count at $1F00
        m(16#3FFA#) := x"0000"; -- physical $7FF4: absolute write updates low byte
        m(16#3FFB#) := x"0000"; -- physical $7FF6: post/pre writes update high/low
        m(16#3FFC#) := x"C300"; -- physical $7FF8: SFC read returns high byte $C3

        -- SRP stack tree: high SRP root slot $0B is supplied by the walker
        -- model and points to table $6900. TIB slot $2B points to final table
        -- $6F00, and TIC slot $E7 maps logical $0BAF9C00 to physical $6000.
        m(13526) := x"0000"; m(13527) := x"6F02";
        m(14670) := x"0000"; m(14671) := x"6061";

        return m;
    end function;

    signal mem : mem_type := init_mem;

begin

    clk_gen: process
    begin
        while not test_done loop
            clk <= '0'; wait for CLK_PERIOD/2;
            clk <= '1'; wait for CLK_PERIOD/2;
        end loop;
        wait;
    end process;

    uut: entity work.TG68KdotC_Kernel
        generic map(
            SR_Read        => 2,
            VBR_Stackframe => 2,
            extAddr_Mode   => 2,
            MUL_Mode       => 2,
            DIV_Mode       => 2,
            BitField       => 2,
            MUL_Hardware   => 1,
            BarrelShifter  => 2
        )
        port map(
            clk => clk, nReset => nReset, clkena_in => clkena_in, data_in => data_in,
            IPL => "111", IPL_autovector => '1', berr => '0', CPU => "10",
            addr_out => addr_out, data_write => data_write, nWr => nWr, nUDS => nUDS, nLDS => nLDS,
            busstate => busstate, longword => open, nResetOut => open, FC => FC, clr_berr => open,
            skipFetch => open, regin_out => open, CACR_out => open, VBR_out => open,
            cache_inv_req => open, cache_op_scope => open, cache_op_cache => open, cache_op_addr => open,
            cacr_ie => open, cacr_de => open, cacr_ifreeze => open, cacr_dfreeze => open,
            cacr_ibe => open, cacr_dbe => open, cacr_wa => open,
            pmmu_reg_we => open, pmmu_reg_re => open, pmmu_reg_sel => open, pmmu_reg_wdat => open, pmmu_reg_part => open,
            pmmu_addr_log => pmmu_addr_log, pmmu_addr_phys => pmmu_addr_phys, pmmu_cache_inhibit => pmmu_cache_inhibit,
            pmmu_walker_req => pmmu_walker_req, pmmu_walker_we => pmmu_walker_we, pmmu_walker_addr => pmmu_walker_addr,
            pmmu_walker_wdat => pmmu_walker_wdat, pmmu_walker_ack => pmmu_walker_ack,
            pmmu_walker_data => pmmu_walker_data, pmmu_walker_berr => pmmu_walker_berr,
            debug_SVmode => open, debug_preSVmode => open, debug_FlagsSR_S => open, debug_changeMode => open,
            debug_setopcode => open, debug_exec_directSR => open, debug_exec_to_SR => open,
            debug_pmove_dn_mode => open, debug_pmove_dn_regnum => open, debug_opcode => open,
            debug_state => open, debug_setstate => open, debug_last_opc_read => open, debug_data_read => open,
            debug_direct_data => open, debug_setnextpass => open, debug_TG68_PC => debug_TG68_PC,
            debug_memaddr_reg => open, debug_memaddr_delta => open, debug_oddout => open, debug_decodeOPC => open,
            debug_brief => open, debug_moves_bus_pending => open, debug_moves_writeback_pending => open,
            debug_clkena_lw => open, debug_regfile_d0 => debug_regfile_d0, debug_regfile_a0 => open,
            debug_fline_context_valid => open, debug_trap_1111 => open, debug_trapmake => open,
            debug_pmmu_brief => open, debug_use_base => open, debug_rf_source_addr => open,
            debug_pmove_ea_latched => open, debug_reg_QA => open, debug_last_data_read => open,
            debug_last_opc_pc => open, debug_getbrief => open, debug_get_2ndopc => open,
            debug_fline_brief_pending => open, debug_fline_opcode_pc => open, debug_exe_PC => open,
            debug_memaddr_delta_rega => open, debug_memaddr_delta_regb => open, debug_addsub_q => open,
            debug_memmaskmux => open, debug_fline_opcode_latch => open, debug_pmmu_ea_mode_latched => open,
            debug_exec_direct_delta => open, debug_exec_directPC => open, debug_exec_mem_addsub => open,
            debug_set_addrlong => open, debug_mdelta_src => open, debug_pc_brw => open, debug_pc_word => open,
            debug_regfile_d1 => open, debug_regfile_d2 => debug_regfile_d2, debug_regfile_d3 => open, debug_regfile_d4 => open,
            debug_regfile_d5 => open, debug_regfile_d6 => open, debug_regfile_d7 => open, debug_regfile_a1 => open,
            debug_regfile_a2 => open, debug_regfile_a3 => open, debug_regfile_a4 => open, debug_regfile_a5 => open,
            debug_regfile_a6 => open, debug_regfile_a7 => open, debug_regfile_we => open, debug_regfile_waddr => open,
            debug_regfile_wdata => open, debug_trap_illegal => open, debug_trap_priv => open,
            debug_trap_addr_error => debug_trap_addr_error, debug_trap_berr => debug_trap_berr,
            debug_trap_mmu_berr => debug_trap_mmu_berr, debug_trap_vector => debug_trap_vector,
            debug_pc_add => open, debug_pc_dataa => open, debug_pc_datab => open, debug_pmmu_busy => debug_pmmu_busy,
            debug_cpu_halted => debug_cpu_halted, debug_stop => debug_stop, debug_interrupt => open,
            debug_setendOPC => open, debug_IPL_nr => open, debug_micro_state => open, debug_next_micro_state => open,
            debug_memmask => open, debug_sndOPC => open, debug_pmmu_reg_we => open, debug_pmmu_reg_re => open,
            debug_pmmu_reg_sel => open, debug_pmmu_reg_wdat => open, debug_pmmu_reg_part => open,
            debug_pmmu_reg_rdat => open, debug_make_berr => open, debug_pmmu_fault => debug_pmmu_fault,
            debug_trap_format_error => open, debug_format_error_rte_word => open, debug_format_error_pc => open,
            debug_format_error_addr => open, debug_format_error_sr => open, debug_pmmu_tc => open,
            debug_pmmu_tt0 => open, debug_pmmu_tt1 => open, debug_pmmu_crp_hi => open, debug_pmmu_crp_lo => open,
            debug_pmmu_srp_hi => open, debug_pmmu_srp_lo => open, debug_pmmu_wstate => open,
            debug_pmmu_atc_buserr => open, debug_pmmu_atc_valid => open,
            debug_pmmu_fault_status => debug_pmmu_fault_status,
            debug_pmmu_saved_addr => debug_pmmu_saved_addr,
            debug_pmmu_walk_desc_addr => debug_pmmu_walk_desc_addr,
            debug_pmmu_walk_desc_data => debug_pmmu_walk_desc_data,
            debug_pmmu_ptr1_desc_addr => debug_pmmu_ptr1_desc_addr,
            debug_pmmu_ptr1_desc_data => debug_pmmu_ptr1_desc_data,
            debug_pmmu_ptr2_desc_addr => debug_pmmu_ptr2_desc_addr,
            debug_pmmu_ptr2_desc_data => debug_pmmu_ptr2_desc_data,
            debug_pmmu_ptr3_desc_addr => debug_pmmu_ptr3_desc_addr,
            debug_pmmu_ptr3_desc_data => debug_pmmu_ptr3_desc_data,
            debug_pmmu_saved_fc => open,
            debug_data_write_tmp => open
        );

    mem_read: process(pmmu_addr_phys, mem)
    begin
        if is_x(pmmu_addr_phys) then
            data_in <= x"4E71";
        elsif unsigned(pmmu_addr_phys) < x"00008000" then
            data_in <= mem(to_integer(unsigned(pmmu_addr_phys(14 downto 1))));
        else
            data_in <= x"4E71";
        end if;
    end process;

    mem_and_walker: process(clk)
        variable phys_word   : integer;
        variable walker_word : integer;
    begin
        if rising_edge(clk) then
            if busstate = "11" and nWr = '0' and clkena_in = '1' then
                if not is_x(pmmu_addr_phys) and unsigned(pmmu_addr_phys) < x"00008000" then
                    phys_word := to_integer(unsigned(pmmu_addr_phys(14 downto 1)));
                    if nUDS = '0' or nLDS = '0' then
                        case pmmu_addr_phys is
                            when x"00007FF5" => abs_write_count <= abs_write_count + 1;
                            when x"00007FF6" => post_write_count <= post_write_count + 1;
                            when x"00007FF7" => pre_write_count <= pre_write_count + 1;
                            when others => null;
                        end case;
                    end if;
                    if nUDS = '0' then
                        mem(phys_word)(15 downto 8) <= data_write(15 downto 8);
                    end if;
                    if nLDS = '0' then
                        mem(phys_word)(7 downto 0) <= data_write(7 downto 0);
                    end if;
                end if;
            end if;

            if busstate = "10" and nWr = '1' and clkena_in = '1' and
               not is_x(pmmu_addr_phys) and pmmu_addr_phys = x"00007FF8" and
               (nUDS = '0' or nLDS = '0') then
                post_read_count <= post_read_count + 1;
            end if;

            if pmmu_walker_req = '1' then
                if not is_x(pmmu_walker_addr) then
                    if pmmu_walker_addr = x"4FAA6074" then
                        saw_crp_root_desc <= true;
                        if pmmu_walker_we = '1' then
                            crp_root_desc <= pmmu_walker_wdat;
                        else
                            pmmu_walker_data <= crp_root_desc;
                        end if;
                    elsif pmmu_walker_addr = x"4FFF6074" then
                        saw_corrupt_crp_root_desc <= true;
                        pmmu_walker_data <= x"00000000";
                    elsif pmmu_walker_addr = x"4FAA70FC" then
                        if pmmu_walker_we = '1' then
                            crp_ptr1_desc <= pmmu_walker_wdat;
                        else
                            pmmu_walker_data <= crp_ptr1_desc;
                        end if;
                    elsif pmmu_walker_addr = x"4FAA83FC" then
                        if pmmu_walker_we = '1' then
                            crp_indirect_desc <= pmmu_walker_wdat;
                        else
                            pmmu_walker_data <= crp_indirect_desc;
                        end if;
                    elsif pmmu_walker_addr = x"4052C000" then
                        if pmmu_walker_we = '1' then
                            srp_low_root_desc <= pmmu_walker_wdat;
                        else
                            pmmu_walker_data <= srp_low_root_desc;
                        end if;
                    elsif pmmu_walker_addr = x"4052C02C" then
                        if pmmu_walker_we = '1' then
                            srp_stack_root_desc <= pmmu_walker_wdat;
                        else
                            pmmu_walker_data <= srp_stack_root_desc;
                        end if;
                    elsif unsigned(pmmu_walker_addr) < x"00008000" then
                        walker_word := to_integer(unsigned(pmmu_walker_addr(14 downto 1)));
                        if pmmu_walker_we = '1' then
                            mem(walker_word)     <= pmmu_walker_wdat(31 downto 16);
                            mem(walker_word + 1) <= pmmu_walker_wdat(15 downto 0);
                        else
                            pmmu_walker_data <= mem(walker_word) & mem(walker_word + 1);
                        end if;
                    else
                        pmmu_walker_data <= x"00000000";
                    end if;
                else
                    pmmu_walker_data <= x"00000000";
                end if;
                pmmu_walker_ack <= '1';
            else
                pmmu_walker_ack <= '0';
            end if;
        end if;
    end process;

    mem_wait_gen: process(clk)
    begin
        if rising_edge(clk) then
            if nReset = '0' then
                mem_wait <= '0';
            elsif clkena_in = '1' then
                mem_wait <= '1';
            else
                mem_wait <= '0';
            end if;
        end if;
    end process;

    stall_control: process(clk)
    begin
        if rising_edge(clk) then
            walker_req_prev <= pmmu_walker_req;
            if walker_req_prev = '1' and pmmu_walker_req = '0' then
                stall_cooldown <= 2;
            elsif stall_cooldown > 0 then
                stall_cooldown <= stall_cooldown - 1;
            end if;
        end if;
    end process;

    clkena_in <= '0' when (pmmu_walker_req = '1'
                           or (debug_pmmu_busy = '1' and debug_pmmu_fault = '0')
                           or stall_cooldown > 0
                           or mem_wait = '1') else '1';

    trace_flow: process(clk)
    begin
        if rising_edge(clk) then
            if nReset = '1' then
                if debug_pmmu_fault = '1' and trace_fault_prev = '0' then
                    report "MOVES_DFC_TRACE: fault pc=$" & slv_to_hex(debug_TG68_PC) &
                           " fc=$" & slv_to_hex('0' & FC) &
                           " fault_addr=$" & slv_to_hex(debug_pmmu_saved_addr)
                    severity note;
                end if;
                if debug_TG68_PC = x"00000080" and not saw_handler_pc then
                    saw_handler_pc <= true;
                elsif (mem(16#0F96#) & mem(16#0F97#)) = x"C0DE700D" and not saw_done_marker then
                    saw_done_marker <= true;
                    report "MOVES_DFC_TRACE: restarted MOVES.B completed" severity note;
                end if;
                trace_fault_prev <= debug_pmmu_fault;
            end if;
        end if;
    end process;

    main_test: process
        variable frame_a7 : std_logic_vector(31 downto 0);
        variable marker   : std_logic_vector(31 downto 0);
        variable done_mark : std_logic_vector(31 downto 0);
        variable pte_target : std_logic_vector(31 downto 0);
        variable abs_word : std_logic_vector(15 downto 0);
        variable pair_word : std_logic_vector(15 downto 0);
        variable read_word : std_logic_vector(15 downto 0);
        variable post_a1 : std_logic_vector(31 downto 0);
        variable pre_a1 : std_logic_vector(31 downto 0);
        variable read_a2 : std_logic_vector(31 downto 0);
        variable read_d4 : std_logic_vector(31 downto 0);
        variable frame_word : integer;
        variable frame_ssw : std_logic_vector(15 downto 0);
        variable frame_pc : std_logic_vector(31 downto 0);
        variable frame_fault_addr : std_logic_vector(31 downto 0);
        variable frame_format : std_logic_vector(3 downto 0);
    begin
        report "=== MMU RESTART MOVES.B SFC/DFC AUTO-MODIFY TEST ===" severity note;
        wait for 100 ns;
        nReset <= '1';

        for i in 0 to 50000 loop
            wait until rising_edge(clk);
            done_mark := mem(16#0F96#) & mem(16#0F97#); -- $1F2C
            if debug_cpu_halted = '1' or done_mark = x"C0DE700D" then
                exit;
            end if;
        end loop;

        frame_a7 := mem(16#0F92#) & mem(16#0F93#); -- $1F24
        marker   := mem(16#0F80#) & mem(16#0F81#); -- $1F00
        done_mark := mem(16#0F96#) & mem(16#0F97#); -- $1F2C
        pte_target := mem(16#3800#) & mem(16#3801#); -- physical $7000
        abs_word := mem(16#3FFA#);
        pair_word := mem(16#3FFB#);
        read_word := mem(16#3FFC#);
        post_a1 := mem(16#0F98#) & mem(16#0F99#); -- $1F30
        pre_a1 := mem(16#0F9A#) & mem(16#0F9B#);  -- $1F34
        read_a2 := mem(16#0F9C#) & mem(16#0F9D#); -- $1F38
        read_d4 := mem(16#0F9E#) & mem(16#0F9F#); -- $1F3C
        -- The high logical stack page $0BAF9C00 maps to physical $6000.
        frame_word := 16#3000# + to_integer(unsigned(frame_a7(9 downto 1)));
        frame_ssw := mem(frame_word + 5);
        frame_pc := mem(frame_word + 1) & mem(frame_word + 2);
        frame_fault_addr := mem(frame_word + 8) & mem(frame_word + 9);
        frame_format := mem(frame_word + 3)(15 downto 12);

        if debug_cpu_halted = '1' then
            report "FAIL: cpu_halted asserted"
                   & " PC=$" & slv_to_hex(debug_TG68_PC)
                   & " trapvec=$" & slv_to_hex(debug_trap_vector)
                   & " trap_berr=" & std_logic'image(debug_trap_berr)
                   & " trap_mmu_berr=" & std_logic'image(debug_trap_mmu_berr)
                   & " trap_addr=" & std_logic'image(debug_trap_addr_error)
                   & " pmmu_fault=" & std_logic'image(debug_pmmu_fault)
                   & " mmusr=$" & slv_to_hex(debug_pmmu_fault_status)
                   & " fault_addr=$" & slv_to_hex(debug_pmmu_saved_addr)
	                   & " desc_addr=$" & slv_to_hex(debug_pmmu_walk_desc_addr)
	                   & " desc_data=$" & slv_to_hex(debug_pmmu_walk_desc_data)
	                   & " marker=$" & slv_to_hex(marker)
	                   & " done=$" & slv_to_hex(done_mark)
	                   & " frame_a7=$" & slv_to_hex(frame_a7)
            severity failure;
        elsif done_mark /= x"C0DE700D" then
            report "FAIL: restarted MOVES.B did not reach done marker, PC=$" & slv_to_hex(debug_TG68_PC)
                   & " D0=$" & slv_to_hex(debug_regfile_d0)
                   & " D2=$" & slv_to_hex(debug_regfile_d2)
                   & " marker=$" & slv_to_hex(marker)
                   & " done=$" & slv_to_hex(done_mark)
                   & " frame_a7=$" & slv_to_hex(frame_a7)
                   & " frame_ssw=$" & slv_to_hex(frame_ssw)
                   & " frame_pc=$" & slv_to_hex(frame_pc)
                   & " frame_fault=$" & slv_to_hex(frame_fault_addr)
                   severity failure;
        elsif marker /= x"00000004" or not saw_handler_pc then
            report "FAIL: expected four vector 2 handler entries"
                   & " marker=$" & slv_to_hex(marker)
                   & " saw_handler=" & boolean'image(saw_handler_pc)
                   & " frame_a7=$" & slv_to_hex(frame_a7)
                   severity failure;
        elsif not saw_crp_root_desc then
            report "FAIL: CRP root descriptor read at $4FAA6074 was not observed"
                   & " last_desc_addr=$" & slv_to_hex(debug_pmmu_walk_desc_addr)
                   & " last_desc_data=$" & slv_to_hex(debug_pmmu_walk_desc_data)
                   severity failure;
        elsif saw_corrupt_crp_root_desc then
            report "FAIL: observed corrupted CRP root descriptor address $4FFF6074"
                   severity failure;
        elsif abs_word /= x"00A5" or pair_word /= x"A55A" or read_word /= x"C300" then
            report "FAIL: restarted MOVES.B target data mismatch"
                   & " abs=$" & slv_to_hex(abs_word)
                   & " post_pre=$" & slv_to_hex(pair_word)
                   & " read_source=$" & slv_to_hex(read_word)
                   severity failure;
        elsif abs_write_count /= 1 or post_write_count /= 1 or
              pre_write_count /= 1 or post_read_count /= 1 then
            report "FAIL: a restarted MOVES.B target transfer was not exactly once"
                   & " abs_w=" & integer'image(abs_write_count)
                   & " post_w=" & integer'image(post_write_count)
                   & " pre_w=" & integer'image(pre_write_count)
                   & " post_r=" & integer'image(post_read_count)
            severity failure;
        elsif post_a1 /= x"1DFFFFF7" or pre_a1 /= x"1DFFFFF7" then
            report "FAIL: DFC auto-modified A1 more or less than once"
                   & " post_a1=$" & slv_to_hex(post_a1)
                   & " pre_a1=$" & slv_to_hex(pre_a1)
            severity failure;
        elsif read_a2 /= x"1DFFFFF9" or read_d4 /= x"000000C3" then
            report "FAIL: SFC postincrement read restart mismatch"
                   & " A2=$" & slv_to_hex(read_a2)
                   & " D4=$" & slv_to_hex(read_d4)
            severity failure;
        -- SSP $0BAFA000 - format $B frame $5C = $0BAF9FA4.
        elsif frame_a7 /= x"0BAF9FA4" or frame_format /= "1011" or
              frame_fault_addr /= x"1DFFFFF8" or frame_pc /= x"000001A6" or
              frame_ssw(8) /= '1' or frame_ssw(7) /= '0' or frame_ssw(6) /= '1' or
              frame_ssw(5 downto 4) /= "01" or frame_ssw(2 downto 0) /= "001" then
            report "FAIL: final frame does not describe the MOVES.B SFC read fault"
                   & " frame_a7=$" & slv_to_hex(frame_a7)
                   & " frame_format=$" & slv_to_hex(frame_format)
                   & " frame_ssw=$" & slv_to_hex(frame_ssw)
                   & " frame_pc=$" & slv_to_hex(frame_pc)
                   & " frame_fault=$" & slv_to_hex(frame_fault_addr)
                   severity failure;
        elsif pte_target(31 downto 8) /= x"00007C" or pte_target(1 downto 0) /= "01" then
            report "FAIL: handler did not repair indirect target descriptor"
                   & " pte=$" & slv_to_hex(pte_target)
                   severity failure;
        else
            report "PASS: four MOVES.B SFC/DFC faults restarted exactly once"
                   & " frame_a7=$" & slv_to_hex(frame_a7)
                   & " writes=" & integer'image(abs_write_count + post_write_count + pre_write_count)
                   & " reads=" & integer'image(post_read_count)
                   & " pte=$" & slv_to_hex(pte_target)
            severity note;
        end if;

        test_done <= true;
        wait;
    end process;

end architecture;
