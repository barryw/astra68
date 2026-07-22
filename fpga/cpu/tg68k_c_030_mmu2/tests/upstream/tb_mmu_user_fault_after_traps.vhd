-- Regression: a user PMMU data fault after repeated format-0 TRAP/RTE returns
-- must still build and dispatch the vector-2 long frame. Astra's K1 faulting
-- task performs eight progress syscalls before its deliberate bad access.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.std_logic_unsigned.all;

entity tb_mmu_user_fault_after_traps is
    generic(
        INSERT_MEMORY_WAIT : boolean := true
    );
end entity;

architecture behavioral of tb_mmu_user_fault_after_traps is

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

    function memory_word_index(address : std_logic_vector(31 downto 0))
        return integer is
    begin
        if is_x(address) then
            return -1;
        end if;
        if unsigned(address) < x"00008000" or
           (unsigned(address) >= x"02000000" and
            unsigned(address) < x"02008000") then
            return to_integer(unsigned(address(14 downto 1)));
        end if;
        return -1;
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
    signal debug_state         : std_logic_vector(1 downto 0);
    signal debug_micro_state   : integer range 0 to 255;
    signal debug_clkena_lw     : std_logic;
    signal debug_trap_berr     : std_logic;
    signal debug_trap_mmu_berr : std_logic;
    signal debug_make_berr     : std_logic;
    signal debug_pmmu_fault    : std_logic;
    signal debug_trap_vector   : std_logic_vector(31 downto 0);
    signal debug_cpu_halted    : std_logic;
    signal debug_stop_sig      : std_logic;
    signal debug_pmmu_busy     : std_logic;
    signal debug_SVmode        : std_logic;
    signal debug_preSVmode     : std_logic;
    signal debug_FlagsSR       : std_logic_vector(7 downto 0);
    signal debug_changeMode    : std_logic;
    signal debug_regfile_a7    : std_logic_vector(31 downto 0);
    signal debug_USP           : std_logic_vector(31 downto 0);
    signal debug_ISP           : std_logic_vector(31 downto 0);
    signal ipl                 : std_logic_vector(2 downto 0) := "111";
    signal saw_fault_handler   : boolean := false;
    signal saw_irq_handler     : boolean := false;
    signal irq_handler_count   : integer range 0 to 2 := 0;

    signal stall_cooldown : integer range 0 to 3 := 0;
    signal walker_req_prev : std_logic := '0';
    signal mem_wait : std_logic := '0';

    type mem_type is array(0 to 16383) of std_logic_vector(15 downto 0);

    -- Astra topology: TC=$82C0AA00 (SRE, 4 KiB, 10/10/12), separate
    -- short-descriptor CRP/SRP roots, and an invalid user root entry for the
    -- deliberate $DFFFFFFC data access.
    function init_mem return mem_type is
        variable m : mem_type := (others => x"4E71");
    begin
        -- Reset vectors: supervisor image and ISP use Astra's SDRAM window.
        m(0) := x"0200"; m(1) := x"4000";
        m(2) := x"0200"; m(3) := x"0100";

        -- Vector 2 before VBR relocation: bus error -> masked entry $02000078.
        m(4) := x"0200"; m(5) := x"0078";

        -- Other vectors -> unexpected trap handler $00C8
        for i in 3 to 63 loop
            m(i*2)   := x"0200";
            m(i*2+1) := x"00C8";
        end loop;

        -- Mask interrupts before the bus-error handler, matching Astra's
        -- _kernel_access_fault_entry first instruction.
        m(60) := x"46FC"; m(61) := x"2700";
        m(62) := x"4E71"; m(63) := x"4E71";

        -- Bus error handler body at $0080.
        -- Save frame details, mark DF, then RTE.
        -- BTST #0,($0A,SP) -> DF bit from SSW
        m(64) := x"082F"; m(65) := x"0000"; m(66) := x"000A";
        -- BEQ.B $00C0 if DF=0
        m(67) := x"671C";
        -- MOVE.L #$AA550001,$1F20.L
        m(68) := x"23FC"; m(69) := x"AA55"; m(70) := x"0001";
        m(71) := x"0200"; m(72) := x"1F20";
        -- MOVE.L A7,$1F24.L
        m(73) := x"23CF"; m(74) := x"0200"; m(75) := x"1F24";
        -- MOVE.W (A7),$1F28.L      ; stacked SR
        m(76) := x"33D7"; m(77) := x"0200"; m(78) := x"1F28";
        -- MOVE.L 2(A7),$1F2C.L     ; stacked PC
        m(79) := x"23EF"; m(80) := x"0002"; m(81) := x"0200"; m(82) := x"1F2C";
        -- MOVE.W 6(A7),$1F30.L     ; format/vector word
        m(83) := x"33EF"; m(84) := x"0006"; m(85) := x"0200"; m(86) := x"1F30";
        -- MOVE.W $0A(A7),$1F32.L   ; SSW
        m(87) := x"33EF"; m(88) := x"000A"; m(89) := x"0200"; m(90) := x"1F32";
        -- MOVE.L $10(A7),$1F34.L   ; fault address
        m(91) := x"23EF"; m(92) := x"0010"; m(93) := x"0200"; m(94) := x"1F34";
        -- ADDQ.L #6,$02(SP) - advance stacked PC past the 6-byte faulting
        -- MOVE.L abs.L,Dn instruction. The page tables still mark entry 13
        -- invalid, so without this RTE would return to the same instruction
        -- and re-fault forever. Real bus-error handlers must either fix the
        -- underlying issue or skip the access.
        m(95) := x"5CAF"; m(96) := x"0002";
        -- RTE
        m(97) := x"4E73";

        -- Unexpected trap handler at $00C8
        m(100) := x"2E3C"; m(101) := x"FF00"; m(102) := x"0000";
        m(103) := x"23C7"; m(104) := x"0200"; m(105) := x"1F00";
        m(106) := x"4E72"; m(107) := x"2700";

        -- VBR table at $02000800. Only the exercised vectors need entries.
        m(16#0404#) := x"0200"; m(16#0405#) := x"0078"; -- vector 2
        m(16#0438#) := x"0200"; m(16#0439#) := x"0340"; -- vector 28
        m(16#045E#) := x"0200"; m(16#045F#) := x"0300"; -- vector 47

        -- TRAP #15 handler at $0300: record entry, then use the same stack
        -- reset and synthesized format-0 return as the kernel dispatcher.
        m(384) := x"46FC"; m(385) := x"2700";
        m(386) := x"23FC"; m(387) := x"5452"; m(388) := x"4150";
        m(389) := x"0200"; m(390) := x"1F40";
        m(391) := x"4EF9"; m(392) := x"0200"; m(393) := x"0380";

        -- Level-4 autovector handler at $0340. A timer may be accepted after
        -- another exception vectors but before that handler masks interrupts.
        -- Supervisor-origin IRQs must RTE to the interrupted handler; only a
        -- user-origin IRQ may take the scheduler's synthesized return path.
        m(416) := x"46FC"; m(417) := x"2700";
        m(418) := x"23FC"; m(419) := x"4952"; m(420) := x"5134";
        m(421) := x"0200"; m(422) := x"1F44";
        m(423) := x"082F"; m(424) := x"0005"; m(425) := x"0000";
        m(426) := x"6606";
        m(427) := x"4EF9"; m(428) := x"0200"; m(429) := x"0380";
        m(430) := x"4E73";

        -- Shared dispatcher return at $0380. Preserve SR/PC from the incoming
        -- format-0 frame, abandon it, rebuild at ISP top, and RTE to user.
        m(448) := x"46FC"; m(449) := x"2700"; -- MOVE.W #$2700,SR
        m(450) := x"202F"; m(451) := x"0002"; -- MOVE.L 2(SP),D0
        m(452) := x"3217";                     -- MOVE.W (SP),D1
        m(453) := x"2E7C"; m(454) := x"0200"; m(455) := x"4000";
        m(456) := x"4267";                     -- CLR.W -(SP), format
        m(457) := x"2F00";                     -- MOVE.L D0,-(SP)
        m(458) := x"3F01";                     -- MOVE.W D1,-(SP)
        m(459) := x"4E73";

        -- Main program at $0100
        -- PMOVE ($1080).W,CRP
        m(128) := x"F038"; m(129) := x"4C00"; m(130) := x"1080";
        -- PMOVE ($1088).W,SRP
        m(131) := x"F038"; m(132) := x"4800"; m(133) := x"1088";
        -- PFLUSHA
        m(134) := x"F000"; m(135) := x"2400";
        -- PMOVE ($1090).W,TC
        m(136) := x"F038"; m(137) := x"4000"; m(138) := x"1090";
        -- MOVE.L #$02000800,D0 / MOVEC D0,VBR
        m(139) := x"203C"; m(140) := x"0200"; m(141) := x"0800";
        m(142) := x"4E7B"; m(143) := x"0801";
        -- Enter from a memory-resident KernelCpuContext using the exact
        -- _kernel_restore_user_context instruction sequence, including the
        -- full register MOVEM immediately before RTE.
        m(144) := x"203C"; m(145) := x"0200"; m(146) := x"1D02";
        m(147) := x"2040";                         -- MOVEA.L D0,A0
        m(148) := x"2268"; m(149) := x"003C";     -- MOVEA.L 60(A0),A1
        m(150) := x"4E61";                         -- MOVE.L A1,USP
        m(151) := x"4FF9"; m(152) := x"0200"; m(153) := x"4000";
        m(154) := x"4267";                         -- CLR.W -(SP), format
        m(155) := x"2F28"; m(156) := x"0040";     -- MOVE.L 64(A0),-(SP)
        m(157) := x"3F28"; m(158) := x"0044";     -- MOVE.W 68(A0),-(SP)
        m(159) := x"4CD0"; m(160) := x"7FFF";     -- MOVEM.L (A0),D0-D7/A0-A6
        m(161) := x"4E73";                         -- RTE
        m(162) := x"4E72"; m(163) := x"2700";     -- must not fall through

        -- KernelCpuContext at physical $02001D02. A second process in an array
        -- exposed this legal MC68030 data alignment, so retain the exact
        -- word-aligned MOVEM restore as part of the exception regression. The
        -- 15 saved registers are zero; offsets 60, 64, and 68 hold USP, PC,
        -- and user SR.
        for i in 16#0E81# to 16#0E9E# loop
            m(i) := x"0000";
        end loop;
        m(16#0E9F#) := x"7000"; m(16#0EA0#) := x"1000";
        m(16#0EA1#) := x"0010"; m(16#0EA2#) := x"0000";
        m(16#0EA3#) := x"0000";

        -- User image at logical $00100000, physical $02001000.
        -- Eight progress syscalls, matching the K1 faulting user task.
        for i in 2048 to 2055 loop
            m(i) := x"4E4F";
        end loop;
        -- MOVE.L $DFFFFFFC,D0 ; user-data MMU access fault
        m(2056) := x"2039"; m(2057) := x"DFFF"; m(2058) := x"FFFC";
        -- MOVE.L #$55AA0001,$00100F00.L ; must execute after RTE
        m(2059) := x"23FC"; m(2060) := x"55AA"; m(2061) := x"0001";
        m(2062) := x"0010"; m(2063) := x"0F00";
        -- BRA.S *-2
        m(2064) := x"60FE";

        -- CRP at $1080, SRP at $1088, TC at $1090.
        m(2112) := x"03FF"; m(2113) := x"0002";
        m(2114) := x"0200"; m(2115) := x"4000";
        m(2116) := x"03FF"; m(2117) := x"0002";
        m(2118) := x"0200"; m(2119) := x"7000";
        m(2120) := x"82C0"; m(2121) := x"AA00";

        -- Clear complete CRP root, two user final tables, and SRP root.
        for i in 8192 to 16383 loop
            m(i) := x"0000";
        end loop;
        -- CRP root[0] -> code table $02005000; root[$1C0] -> stack table
        -- $02006000. Root[$180] for $60000000 remains invalid.
        m(8192) := x"0200"; m(8193) := x"5002";
        m(9088) := x"0200"; m(9089) := x"6002";
        -- User code final[256] -> physical $02001000.
        m(10752) := x"0200"; m(10753) := x"1001";
        -- User stack final[0] -> physical $02002000.
        m(12288) := x"0200"; m(12289) := x"2001";
        -- SRP root[8] directly maps supervisor logical $02000000-$023FFFFF.
        m(14352) := x"0200"; m(14353) := x"0001";

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
            clk              => clk,
            nReset           => nReset,
            clkena_in        => clkena_in,
            data_in          => data_in,
            IPL              => ipl,
            IPL_autovector   => '1',
            berr             => '0',
            CPU              => "10",
            addr_out         => addr_out,
            data_write       => data_write,
            nWr              => nWr,
            nUDS             => nUDS,
            nLDS             => nLDS,
            busstate         => busstate,
            longword         => open,
            nResetOut        => open,
            FC               => FC,
            clr_berr         => open,
            skipFetch        => open,
            regin_out        => open,
            CACR_out         => open,
            VBR_out          => open,
            cache_inv_req    => open,
            cache_op_scope   => open,
            cache_op_cache   => open,
            cache_op_addr    => open,
            cacr_ie          => open,
            cacr_de          => open,
            cacr_ifreeze     => open,
            cacr_dfreeze     => open,
            cacr_ibe         => open,
            cacr_dbe         => open,
            cacr_wa          => open,
            pmmu_reg_we      => open,
            pmmu_reg_re      => open,
            pmmu_reg_sel     => open,
            pmmu_reg_wdat    => open,
            pmmu_reg_part    => open,
            pmmu_addr_log    => pmmu_addr_log,
            pmmu_addr_phys   => pmmu_addr_phys,
            pmmu_cache_inhibit => pmmu_cache_inhibit,
            pmmu_walker_req  => pmmu_walker_req,
            pmmu_walker_we   => pmmu_walker_we,
            pmmu_walker_addr => pmmu_walker_addr,
            pmmu_walker_wdat => pmmu_walker_wdat,
            pmmu_walker_ack  => pmmu_walker_ack,
            pmmu_walker_data => pmmu_walker_data,
            pmmu_walker_berr => pmmu_walker_berr,
            debug_SVmode     => debug_SVmode,
            debug_preSVmode  => debug_preSVmode,
            debug_FlagsSR_S  => open,
            debug_changeMode => debug_changeMode,
            debug_setopcode  => open,
            debug_exec_directSR => open,
            debug_exec_to_SR => open,
            debug_pmove_dn_mode => open,
            debug_pmove_dn_regnum => open,
            debug_opcode     => open,
            debug_state      => debug_state,
            debug_setstate   => open,
            debug_last_opc_read => open,
            debug_data_read  => open,
            debug_direct_data => open,
            debug_setnextpass => open,
            debug_TG68_PC    => debug_TG68_PC,
            debug_memaddr_reg => open,
            debug_memaddr_delta => open,
            debug_oddout     => open,
            debug_decodeOPC  => open,
            debug_brief      => open,
            debug_moves_bus_pending => open,
            debug_moves_writeback_pending => open,
            debug_clkena_lw  => debug_clkena_lw,
            debug_regfile_d0 => open,
            debug_regfile_a0 => open,
            debug_fline_context_valid => open,
            debug_trap_1111  => open,
            debug_trapmake   => open,
            debug_pmmu_brief => open,
            debug_use_base   => open,
            debug_rf_source_addr => open,
            debug_pmove_ea_latched => open,
            debug_reg_QA     => open,
            debug_last_data_read => open,
            debug_last_opc_pc => open,
            debug_getbrief => open,
            debug_get_2ndopc => open,
            debug_fline_brief_pending => open,
            debug_fline_opcode_pc => open,
            debug_exe_PC => open,
            debug_memaddr_delta_rega => open,
            debug_memaddr_delta_regb => open,
            debug_addsub_q => open,
            debug_memmaskmux => open,
            debug_fline_opcode_latch => open,
            debug_pmmu_ea_mode_latched => open,
            debug_exec_direct_delta => open,
            debug_exec_directPC => open,
            debug_exec_mem_addsub => open,
            debug_set_addrlong => open,
            debug_mdelta_src => open,
            debug_pc_brw => open,
            debug_pc_word => open,
            debug_regfile_d1 => open,
            debug_regfile_d2 => open,
            debug_regfile_d3 => open,
            debug_regfile_d4 => open,
            debug_regfile_d5 => open,
            debug_regfile_d6 => open,
            debug_regfile_d7 => open,
            debug_regfile_a1 => open,
            debug_regfile_a2 => open,
            debug_regfile_a3 => open,
            debug_regfile_a4 => open,
            debug_regfile_a5 => open,
            debug_regfile_a6 => open,
            debug_regfile_a7 => debug_regfile_a7,
            debug_regfile_we => open,
            debug_regfile_waddr => open,
            debug_regfile_wdata => open,
            debug_trap_illegal => open,
            debug_trap_priv => open,
            debug_trap_addr_error => open,
            debug_trap_berr => debug_trap_berr,
            debug_trap_mmu_berr => debug_trap_mmu_berr,
            debug_trap_vector => debug_trap_vector,
            debug_pc_add => open,
            debug_pc_dataa => open,
            debug_pc_datab => open,
            debug_pmmu_busy  => debug_pmmu_busy,
            debug_cpu_halted => debug_cpu_halted,
            debug_stop       => debug_stop_sig,
            debug_interrupt  => open,
            debug_setendOPC  => open,
            debug_IPL_nr     => open,
            debug_micro_state => debug_micro_state,
            debug_next_micro_state => open,
            debug_memmask => open,
            debug_sndOPC => open,
            debug_pmmu_reg_we => open,
            debug_pmmu_reg_re => open,
            debug_pmmu_reg_sel => open,
            debug_pmmu_reg_wdat => open,
            debug_pmmu_reg_part => open,
            debug_pmmu_reg_rdat => open,
            debug_make_berr => debug_make_berr,
            debug_pmmu_fault => debug_pmmu_fault,
            debug_trap_format_error => open,
            debug_format_error_rte_word => open,
            debug_format_error_pc => open,
            debug_format_error_addr => open,
            debug_format_error_sr => open,
            debug_pmmu_tc  => open,
            debug_pmmu_tt0 => open,
            debug_pmmu_tt1 => open,
            debug_pmmu_crp_hi => open,
            debug_pmmu_crp_lo => open,
            debug_pmmu_srp_hi => open,
            debug_pmmu_srp_lo => open,
            debug_pmmu_wstate => open,
            debug_pmmu_atc_buserr => open,
            debug_pmmu_atc_valid  => open,
            debug_pmmu_fault_status => open,
            debug_pmmu_saved_addr   => open,
            debug_pmmu_walk_desc_addr => open,
            debug_pmmu_walk_desc_data => open,
            debug_pmmu_ptr1_desc_addr => open,
            debug_pmmu_ptr1_desc_data => open,
            debug_pmmu_ptr2_desc_addr => open,
            debug_pmmu_ptr2_desc_data => open,
            debug_pmmu_ptr3_desc_addr => open,
            debug_pmmu_ptr3_desc_data => open,
            debug_pmmu_saved_fc       => open,
            debug_FlagsSR             => debug_FlagsSR,
            debug_USP                 => debug_USP,
            debug_ISP                 => debug_ISP
        );

    mem_read: process(pmmu_addr_phys, mem)
        variable word_index : integer;
    begin
        word_index := memory_word_index(pmmu_addr_phys);
        if word_index < 0 then
            data_in <= x"4E71";
        else
            data_in <= mem(word_index);
        end if;
    end process;

    -- First preempt immediately after user entry, then reproduce the production
    -- collision by raising level 4 again while the data fault is live.
    irq_control: process(clk)
        variable irq_phase : integer range 0 to 3 := 0;
        variable previous_pc : std_logic_vector(31 downto 0) := (others => '0');
        variable trace_count : integer range 0 to 16 := 0;
    begin
        if rising_edge(clk) then
            if nReset = '0' then
                ipl <= "111";
                irq_phase := 0;
                saw_fault_handler <= false;
                saw_irq_handler <= false;
                irq_handler_count <= 0;
                trace_count := 0;
            else
                if trace_count < 16 and debug_TG68_PC /= previous_pc and
                   (debug_TG68_PC = x"00100000" or
                    debug_TG68_PC = x"02000300" or
                    debug_TG68_PC = x"02000340" or
                    debug_TG68_PC = x"02000380" or
                    debug_TG68_PC = x"02000078") then
                    report "TRACE: PC=$" & slv_to_hex(debug_TG68_PC) &
                           " A7=$" & slv_to_hex(debug_regfile_a7) &
                           " ISP=$" & slv_to_hex(debug_ISP) &
                           " USP=$" & slv_to_hex(debug_USP) &
                           " SRH=$" & slv_to_hex(debug_FlagsSR) &
                           " preS=" & std_logic'image(debug_preSVmode) &
                           " S=" & std_logic'image(debug_SVmode) &
                           " change=" & std_logic'image(debug_changeMode)
                           severity note;
                    trace_count := trace_count + 1;
                end if;
                previous_pc := debug_TG68_PC;
                if debug_TG68_PC = x"02000078" then
                    saw_fault_handler <= true;
                end if;
                if debug_TG68_PC = x"02000340" then
                    saw_irq_handler <= true;
                    ipl <= "111";
                    if irq_phase = 1 then
                        irq_handler_count <= 1;
                        irq_phase := 2;
                    elsif irq_phase = 3 then
                        irq_handler_count <= 2;
                    end if;
                elsif irq_phase = 0 and debug_TG68_PC = x"00100000" then
                    irq_phase := 1;
                    ipl <= "011";
                elsif irq_phase = 2 and debug_pmmu_fault = '1' then
                    irq_phase := 3;
                    ipl <= "011";
                end if;
            end if;
        end if;
    end process;

    mem_and_walker: process(clk)
        variable phys_word   : integer;
        variable walker_word : integer;
    begin
        if rising_edge(clk) then
            if busstate = "11" and nWr = '0' and clkena_in = '1' then
                phys_word := memory_word_index(pmmu_addr_phys);
                if phys_word >= 0 then
                    mem(phys_word) <= data_write;
                end if;
            end if;

            if pmmu_walker_req = '1' then
                walker_word := memory_word_index(pmmu_walker_addr);
                if walker_word >= 0 and walker_word < mem'high then
                    if pmmu_walker_we = '1' then
                        mem(walker_word)     <= pmmu_walker_wdat(31 downto 16);
                        mem(walker_word + 1) <= pmmu_walker_wdat(15 downto 0);
                    else
                        pmmu_walker_data <= mem(walker_word) & mem(walker_word + 1);
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
                           or (INSERT_MEMORY_WAIT and mem_wait = '1')) else '1';

    main_test: process
        variable marker      : std_logic_vector(31 downto 0);
        variable df_marker   : std_logic_vector(31 downto 0);
        variable frame_base  : std_logic_vector(31 downto 0);
        variable stacked_sr  : std_logic_vector(15 downto 0);
        variable stacked_pc  : std_logic_vector(31 downto 0);
        variable fmtvec      : std_logic_vector(15 downto 0);
        variable ssw         : std_logic_vector(15 downto 0);
        variable fault_addr  : std_logic_vector(31 downto 0);
        variable trap_mark   : std_logic_vector(31 downto 0);
        variable irq_mark    : std_logic_vector(31 downto 0);
    begin
        report "=== USER-MODE MMU DATA FAULT RECOVERY TEST ===" severity note;

        wait for 100 ns;
        nReset <= '1';

        for i in 0 to 50000 loop
            wait until rising_edge(clk);
            marker := mem(16#0F80#) & mem(16#0F81#);
            if marker = x"55AA0001" or debug_cpu_halted = '1' then
                exit;
            end if;
        end loop;

        marker := mem(16#0F80#) & mem(16#0F81#);
        df_marker := mem(16#0F90#) & mem(16#0F91#);
        frame_base := mem(16#0F92#) & mem(16#0F93#);
        stacked_sr := mem(16#0F94#);
        stacked_pc := mem(16#0F96#) & mem(16#0F97#);
        fmtvec := mem(16#0F98#);
        ssw := mem(16#0F99#);
        fault_addr := mem(16#0F9A#) & mem(16#0F9B#);
        trap_mark := mem(16#0FA0#) & mem(16#0FA1#);
        irq_mark := mem(16#0FA2#) & mem(16#0FA3#);

        report "DIAG: marker=$" & slv_to_hex(marker) &
               " df=$" & slv_to_hex(df_marker) &
               " A7=$" & slv_to_hex(frame_base) severity note;
        report "DIAG: PC=$" & slv_to_hex(stacked_pc) &
               " SR=$" & slv_to_hex(stacked_sr) &
               " fmtvec=$" & slv_to_hex(fmtvec) severity note;
        report "DIAG: SSW=$" & slv_to_hex(ssw) &
               " fault_addr=$" & slv_to_hex(fault_addr) severity note;
        report "DIAG: trap=$" & slv_to_hex(trap_mark) &
               " irq=$" & slv_to_hex(irq_mark) &
               " irq_count=" & integer'image(irq_handler_count) &
               " live_pc=$" & slv_to_hex(debug_TG68_PC) severity note;
        if debug_cpu_halted = '1' then
            report "FAIL: cpu_halted asserted on user-mode MMU data fault" severity error;
        elsif df_marker /= x"AA550001" then
            report "FAIL: handler did not confirm DF=1, got $" & slv_to_hex(df_marker) severity error;
        elsif trap_mark /= x"54524150" then
            report "FAIL: TRAP/RTE sequence did not reach vector 47" severity error;
        elsif not saw_fault_handler then
            report "FAIL: pending level 4 displaced vector 2" severity error;
        elsif not saw_irq_handler or irq_handler_count /= 2 or
              irq_mark /= x"49525134" then
            report "FAIL: both level-4 interrupts were not serviced" severity error;
        elsif marker /= x"55AA0001" then
            report "FAIL: user code after RTE did not execute, marker=$" & slv_to_hex(marker) severity error;
        elsif stacked_sr(13) /= '0' then
            report "FAIL: stacked SR S-bit was not user mode, SR=$" & slv_to_hex(stacked_sr) severity error;
        elsif stacked_pc /= x"00100010" then
            -- Per WinUAE exception_pc() in newcpu_common.cpp:1399, vector 2
            -- stacks regs.instruction_pc (the faulting instruction's PC),
            -- not the post-instruction PC. The handler skips past it via
            -- ADDQ.L #6,2(SP) above.
            report "FAIL: stacked PC=$" & slv_to_hex(stacked_pc) & " expected $00100010" severity error;
        elsif frame_base /= x"02003FA4" then
            report "FAIL: handler saved A7=$" & slv_to_hex(frame_base) & " expected $02003FA4" severity error;
        elsif fmtvec(15 downto 12) /= x"B" then
            report "FAIL: format/vector word=$" & slv_to_hex(fmtvec) & " expected Format $B" severity error;
        elsif ssw(8) /= '1' or ssw(6) /= '1' or ssw(5 downto 4) /= "00" then
            report "FAIL: SSW=$" & slv_to_hex(ssw) & " expected DF=1 RW=1 SIZE=long" severity error;
        elsif fault_addr /= x"DFFFFFFC" then
            report "FAIL: fault address=$" & slv_to_hex(fault_addr) & " expected $DFFFFFFC" severity error;
        else
            report "PASS: user PMMU fault dispatched after eight TRAP/RTE cycles" severity note;
        end if;

        test_done <= true;
        wait;
    end process;

end architecture;
