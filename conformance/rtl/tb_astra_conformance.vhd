library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.std_logic_textio.all;
use std.textio.all;
use std.env.all;

entity tb_astra_conformance is
    generic (
        MEMORY_FILE : string := "memory.hex";
        CONFIG_FILE : string := "config.txt";
        RESULT_FILE : string := "result.txt"
    );
end entity;

architecture behavioral of tb_astra_conformance is
    constant CLK_PERIOD : time := 10 ns;
    constant MAX_MEMORY_BYTES : natural := 262144;
    constant MAX_OBSERVATIONS : natural := 4096;

    subtype byte_t is std_logic_vector(7 downto 0);
    subtype word_t is std_logic_vector(31 downto 0);
    type address_array_t is array (0 to MAX_MEMORY_BYTES - 1)
        of std_logic_vector(31 downto 0);
    type byte_array_t is array (0 to MAX_MEMORY_BYTES - 1) of byte_t;

    type sparse_memory_t is protected
        procedure write8(address : std_logic_vector(31 downto 0); value : byte_t);
        impure function read8(address : std_logic_vector(31 downto 0)) return byte_t;
        impure function present8(address : std_logic_vector(31 downto 0)) return boolean;
        impure function read32(address : std_logic_vector(31 downto 0)) return word_t;
    end protected;

    type sparse_memory_t is protected body
        variable addresses : address_array_t;
        variable content : byte_array_t;
        variable count : natural range 0 to MAX_MEMORY_BYTES := 0;

        procedure write8(address : std_logic_vector(31 downto 0); value : byte_t) is
        begin
            if count > 0 then
                for index in 0 to count - 1 loop
                    if addresses(index) = address then
                        content(index) := value;
                        return;
                    end if;
                end loop;
            end if;
            assert count < MAX_MEMORY_BYTES
                report "RTL conformance sparse memory capacity exceeded"
                severity failure;
            addresses(count) := address;
            content(count) := value;
            count := count + 1;
        end procedure;

        impure function read8(address : std_logic_vector(31 downto 0)) return byte_t is
        begin
            if count > 0 then
                for index in 0 to count - 1 loop
                    if addresses(index) = address then
                        return content(index);
                    end if;
                end loop;
            end if;
            return x"00";
        end function;

        impure function present8(address : std_logic_vector(31 downto 0)) return boolean is
        begin
            if count > 0 then
                for index in 0 to count - 1 loop
                    if addresses(index) = address then
                        return true;
                    end if;
                end loop;
            end if;
            return false;
        end function;

        impure function read32(address : std_logic_vector(31 downto 0)) return word_t is
            variable base : unsigned(31 downto 0) := unsigned(address);
        begin
            return read8(std_logic_vector(base)) &
                   read8(std_logic_vector(base + 1)) &
                   read8(std_logic_vector(base + 2)) &
                   read8(std_logic_vector(base + 3));
        end function;
    end protected body;

    shared variable memory : sparse_memory_t;

    type observation_address_array_t is array (0 to MAX_OBSERVATIONS - 1)
        of std_logic_vector(31 downto 0);
    type observation_length_array_t is array (0 to MAX_OBSERVATIONS - 1)
        of natural;

    signal clk : std_logic := '0';
    signal nreset : std_logic := '0';
    signal test_done : boolean := false;
    signal load_done : std_logic := '0';
    signal memory_epoch : std_logic := '0';

    signal clkena_in : std_logic := '0';
    signal mem_wait : std_logic := '0';
    signal stall_cooldown : integer range 0 to 3 := 0;
    signal walker_req_previous : std_logic := '0';

    signal data_in : std_logic_vector(15 downto 0) := x"4E71";
    signal data_write : std_logic_vector(15 downto 0);
    signal addr_out : std_logic_vector(31 downto 0);
    signal busstate : std_logic_vector(1 downto 0);
    signal nwr : std_logic;
    signal nuds : std_logic;
    signal nlds : std_logic;
    signal fc : std_logic_vector(2 downto 0);

    signal pmmu_addr_log : std_logic_vector(31 downto 0);
    signal pmmu_addr_phys : std_logic_vector(31 downto 0);
    signal pmmu_walker_req : std_logic;
    signal pmmu_walker_we : std_logic;
    signal pmmu_walker_addr : std_logic_vector(31 downto 0);
    signal pmmu_walker_wdat : std_logic_vector(31 downto 0);
    signal pmmu_walker_ack : std_logic := '0';
    signal pmmu_walker_data : std_logic_vector(31 downto 0) := (others => '0');
    signal pmmu_walker_berr : std_logic := '0';
    signal debug_pmmu_busy : std_logic;
    signal debug_pmmu_fault : std_logic;

    signal debug_pc : std_logic_vector(31 downto 0);
    signal debug_last_opc_pc : std_logic_vector(31 downto 0);
    signal debug_setopcode : std_logic;
    signal debug_setendopc : std_logic;
    signal debug_clkena_lw : std_logic;
    signal debug_trap_vector : std_logic_vector(31 downto 0);
    signal debug_trap_1111 : std_logic;
    signal debug_trapmake : std_logic;
    signal debug_flags_sr : std_logic_vector(7 downto 0);
    signal debug_flags : std_logic_vector(7 downto 0);
    signal debug_sfc : std_logic_vector(2 downto 0);
    signal debug_dfc : std_logic_vector(2 downto 0);
    signal debug_caar : std_logic_vector(31 downto 0);
    signal debug_usp : std_logic_vector(31 downto 0);
    signal debug_isp : std_logic_vector(31 downto 0);
    signal debug_msp : std_logic_vector(31 downto 0);
    signal debug_d0 : std_logic_vector(31 downto 0);
    signal debug_d1 : std_logic_vector(31 downto 0);
    signal debug_d2 : std_logic_vector(31 downto 0);
    signal debug_d3 : std_logic_vector(31 downto 0);
    signal debug_d4 : std_logic_vector(31 downto 0);
    signal debug_d5 : std_logic_vector(31 downto 0);
    signal debug_d6 : std_logic_vector(31 downto 0);
    signal debug_d7 : std_logic_vector(31 downto 0);
    signal debug_a0 : std_logic_vector(31 downto 0);
    signal debug_a1 : std_logic_vector(31 downto 0);
    signal debug_a2 : std_logic_vector(31 downto 0);
    signal debug_a3 : std_logic_vector(31 downto 0);
    signal debug_a4 : std_logic_vector(31 downto 0);
    signal debug_a5 : std_logic_vector(31 downto 0);
    signal debug_a6 : std_logic_vector(31 downto 0);
    signal debug_a7 : std_logic_vector(31 downto 0);
    signal vbr_out : std_logic_vector(31 downto 0);
    signal cacr_out : std_logic_vector(31 downto 0);

    function is_binary(value : std_logic_vector) return boolean is
    begin
        for index in value'range loop
            if value(index) /= '0' and value(index) /= '1' then
                return false;
            end if;
        end loop;
        return true;
    end function;

begin
    clock_process : process
    begin
        while not test_done loop
            clk <= '0';
            wait for CLK_PERIOD / 2;
            clk <= '1';
            wait for CLK_PERIOD / 2;
        end loop;
        wait;
    end process;

    uut : entity work.TG68KdotC_Kernel
        generic map (
            SR_Read => 2,
            VBR_Stackframe => 2,
            extAddr_Mode => 2,
            MUL_Mode => 2,
            DIV_Mode => 2,
            BitField => 2,
            MUL_Hardware => 1,
            BarrelShifter => 2
        )
        port map (
            clk => clk,
            nReset => nreset,
            clkena_in => clkena_in,
            data_in => data_in,
            IPL => "111",
            IPL_autovector => '1',
            berr => '0',
            CPU => "10",
            addr_out => addr_out,
            data_write => data_write,
            nWr => nwr,
            nUDS => nuds,
            nLDS => nlds,
            busstate => busstate,
            FC => fc,
            CACR_out => cacr_out,
            VBR_out => vbr_out,
            pmmu_addr_log => pmmu_addr_log,
            pmmu_addr_phys => pmmu_addr_phys,
            pmmu_walker_req => pmmu_walker_req,
            pmmu_walker_we => pmmu_walker_we,
            pmmu_walker_addr => pmmu_walker_addr,
            pmmu_walker_wdat => pmmu_walker_wdat,
            pmmu_walker_ack => pmmu_walker_ack,
            pmmu_walker_data => pmmu_walker_data,
            pmmu_walker_berr => pmmu_walker_berr,
            debug_setopcode => debug_setopcode,
            debug_TG68_PC => debug_pc,
            debug_last_opc_pc => debug_last_opc_pc,
            debug_setendOPC => debug_setendopc,
            debug_clkena_lw => debug_clkena_lw,
            debug_trap_vector => debug_trap_vector,
            debug_trap_1111 => debug_trap_1111,
            debug_trapmake => debug_trapmake,
            debug_pmmu_busy => debug_pmmu_busy,
            debug_pmmu_fault => debug_pmmu_fault,
            debug_FlagsSR => debug_flags_sr,
            debug_USP => debug_usp,
            debug_ISP => debug_isp,
            debug_MSP => debug_msp,
            debug_Flags => debug_flags,
            debug_SFC => debug_sfc,
            debug_DFC => debug_dfc,
            debug_CAAR => debug_caar,
            debug_regfile_d0 => debug_d0,
            debug_regfile_d1 => debug_d1,
            debug_regfile_d2 => debug_d2,
            debug_regfile_d3 => debug_d3,
            debug_regfile_d4 => debug_d4,
            debug_regfile_d5 => debug_d5,
            debug_regfile_d6 => debug_d6,
            debug_regfile_d7 => debug_d7,
            debug_regfile_a0 => debug_a0,
            debug_regfile_a1 => debug_a1,
            debug_regfile_a2 => debug_a2,
            debug_regfile_a3 => debug_a3,
            debug_regfile_a4 => debug_a4,
            debug_regfile_a5 => debug_a5,
            debug_regfile_a6 => debug_a6,
            debug_regfile_a7 => debug_a7,
            longword => open,
            nResetOut => open,
            clr_berr => open,
            RMCn_out => open,
            skipFetch => open,
            regin_out => open,
            cache_inv_req => open,
            cache_op_scope => open,
            cache_op_cache => open,
            cacr_ie => open,
            cacr_de => open,
            cacr_ifreeze => open,
            cacr_dfreeze => open,
            cacr_ibe => open,
            cacr_dbe => open,
            cacr_wa => open,
            pmmu_reg_we => open,
            pmmu_reg_re => open,
            pmmu_reg_sel => open,
            pmmu_reg_wdat => open,
            pmmu_reg_part => open,
            pmmu_cache_inhibit => open,
            cache_op_addr => open,
            debug_SVmode => open,
            debug_preSVmode => open,
            debug_FlagsSR_S => open,
            debug_changeMode => open,
            debug_exec_directSR => open,
            debug_exec_to_SR => open,
            debug_pmove_dn_mode => open,
            debug_pmove_dn_regnum => open,
            debug_opcode => open,
            debug_state => open,
            debug_setstate => open,
            debug_last_opc_read => open,
            debug_data_read => open,
            debug_direct_data => open,
            debug_setnextpass => open,
            debug_memaddr_reg => open,
            debug_memaddr_delta => open,
            debug_oddout => open,
            debug_decodeOPC => open,
            debug_brief => open,
            debug_moves_bus_pending => open,
            debug_moves_writeback_pending => open,
            debug_fline_context_valid => open,
            debug_pmmu_brief => open,
            debug_use_base => open,
            debug_rf_source_addr => open,
            debug_pmove_ea_latched => open,
            debug_reg_QA => open,
            debug_last_data_read => open,
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
            debug_regfile_we => open,
            debug_regfile_waddr => open,
            debug_regfile_wdata => open,
            debug_trap_illegal => open,
            debug_trap_priv => open,
            debug_trap_addr_error => open,
            debug_trap_berr => open,
            debug_trap_mmu_berr => open,
            debug_pc_add => open,
            debug_pc_dataa => open,
            debug_pc_datab => open,
            debug_cpu_halted => open,
            debug_stop => open,
            debug_interrupt => open,
            debug_IPL_nr => open,
            debug_micro_state => open,
            debug_next_micro_state => open,
            debug_memmask => open,
            debug_sndOPC => open,
            debug_pmmu_reg_we => open,
            debug_pmmu_reg_re => open,
            debug_pmmu_reg_sel => open,
            debug_pmmu_reg_wdat => open,
            debug_pmmu_reg_part => open,
            debug_pmmu_reg_rdat => open,
            debug_make_berr => open,
            debug_berr_exception_active => open,
            debug_pmmu_fault_dispatched => open,
            debug_pmmu_fault_was_cleared => open,
            debug_pmmu_fault_rw => open,
            debug_pmmu_fault_is_insn => open,
            debug_pmmu_fault_fc => open,
            debug_trap_format_error => open,
            debug_format_error_rte_word => open,
            debug_format_error_pc => open,
            debug_format_error_addr => open,
            debug_format_error_sr => open,
            debug_pmmu_tc => open,
            debug_pmmu_tt0 => open,
            debug_pmmu_tt1 => open,
            debug_pmmu_crp_hi => open,
            debug_pmmu_crp_lo => open,
            debug_pmmu_srp_hi => open,
            debug_pmmu_srp_lo => open,
            debug_pmmu_wstate => open,
            debug_pmmu_atc_buserr => open,
            debug_pmmu_atc_valid => open,
            debug_pmmu_pending_flags => open,
            debug_pmmu_fault_status => open,
            debug_pmmu_saved_addr => open,
            debug_pmmu_walk_desc_addr => open,
            debug_pmmu_walk_desc_data => open,
            debug_pmmu_ptr1_desc_addr => open,
            debug_pmmu_ptr1_desc_data => open,
            debug_pmmu_ptr2_desc_addr => open,
            debug_pmmu_ptr2_desc_data => open,
            debug_pmmu_ptr3_desc_addr => open,
            debug_pmmu_ptr3_desc_data => open,
            debug_pmmu_saved_fc => open,
            debug_make_trace => open,
            debug_trace_pending_grp2 => open,
            debug_useStackframe2 => open,
            debug_exec_trap_chk => open,
            debug_set_trap_chk => open,
            debug_data_write_tmp => open,
            debug_a7_is_msp => open,
            debug_interrupt_mode => open,
            debug_rte_saved_mbit => open,
            debug_rte_format_word => open,
            debug_rte_mmu_fix_ssw => open,
            debug_rte_mmu_fix_opcode => open,
            debug_rte_mmu_fix_write => open,
            debug_rte_format_b_version_error => open
        );

    memory_read_process : process(pmmu_addr_phys, busstate, memory_epoch, load_done)
        variable aligned : unsigned(31 downto 0);
    begin
        data_in <= x"4E71";
        if load_done = '1' and is_binary(pmmu_addr_phys) then
            aligned := unsigned(pmmu_addr_phys);
            aligned(0) := '0';
            if memory.present8(std_logic_vector(aligned)) or
               memory.present8(std_logic_vector(aligned + 1)) then
                data_in <= memory.read8(std_logic_vector(aligned)) &
                           memory.read8(std_logic_vector(aligned + 1));
            elsif busstate /= "00" then
                data_in <= x"0000";
            end if;
        end if;
    end process;

    memory_and_walker_process : process(clk)
        variable address : unsigned(31 downto 0);
        variable walker_address : unsigned(31 downto 0);
    begin
        if rising_edge(clk) then
            if busstate = "11" and nwr = '0' and clkena_in = '1' and
               is_binary(pmmu_addr_phys) then
                address := unsigned(pmmu_addr_phys);
                address(0) := '0';
                if nuds = '0' then
                    memory.write8(std_logic_vector(address), data_write(15 downto 8));
                end if;
                if nlds = '0' then
                    memory.write8(std_logic_vector(address + 1), data_write(7 downto 0));
                end if;
                memory_epoch <= not memory_epoch;
            end if;

            pmmu_walker_ack <= '0';
            pmmu_walker_berr <= '0';
            if pmmu_walker_req = '1' and is_binary(pmmu_walker_addr) then
                walker_address := unsigned(pmmu_walker_addr);
                if pmmu_walker_we = '1' then
                    memory.write8(std_logic_vector(walker_address), pmmu_walker_wdat(31 downto 24));
                    memory.write8(std_logic_vector(walker_address + 1), pmmu_walker_wdat(23 downto 16));
                    memory.write8(std_logic_vector(walker_address + 2), pmmu_walker_wdat(15 downto 8));
                    memory.write8(std_logic_vector(walker_address + 3), pmmu_walker_wdat(7 downto 0));
                    memory_epoch <= not memory_epoch;
                elsif memory.present8(std_logic_vector(walker_address)) and
                      memory.present8(std_logic_vector(walker_address + 1)) and
                      memory.present8(std_logic_vector(walker_address + 2)) and
                      memory.present8(std_logic_vector(walker_address + 3)) then
                    pmmu_walker_data <= memory.read32(std_logic_vector(walker_address));
                else
                    pmmu_walker_data <= (others => '0');
                    pmmu_walker_berr <= '1';
                end if;
                pmmu_walker_ack <= '1';
            end if;
        end if;
    end process;

    memory_wait_process : process(clk)
    begin
        if rising_edge(clk) then
            if nreset = '0' then
                mem_wait <= '0';
            elsif clkena_in = '1' then
                mem_wait <= '1';
            else
                mem_wait <= '0';
            end if;
        end if;
    end process;

    walker_stall_process : process(clk)
    begin
        if rising_edge(clk) then
            walker_req_previous <= pmmu_walker_req;
            if walker_req_previous = '1' and pmmu_walker_req = '0' then
                stall_cooldown <= 2;
            elsif stall_cooldown > 0 then
                stall_cooldown <= stall_cooldown - 1;
            end if;
        end if;
    end process;

    clkena_in <= '0' when nreset = '0' or load_done = '0' or
                          pmmu_walker_req = '1' or
                          (debug_pmmu_busy = '1' and debug_pmmu_fault = '0') or
                          stall_cooldown > 0 or
                          mem_wait = '1' else '1';

    control_process : process
        file memory_input : text;
        file config_input : text;
        variable input_line : line;
        variable memory_address : std_logic_vector(31 downto 0);
        variable memory_byte : byte_t;
        variable run_mode : integer;
        variable max_cycles : integer;
        variable target_pc : std_logic_vector(31 downto 0);
        variable stop_address : std_logic_vector(31 downto 0);
        variable stop_mask : std_logic_vector(31 downto 0);
        variable stop_value : std_logic_vector(31 downto 0);
        variable observation_count : integer;
        variable observation_addresses : observation_address_array_t;
        variable observation_lengths : observation_length_array_t;
        variable target_started : boolean := false;
        variable target_opcode_seen : boolean := false;
        variable target_exception : boolean := false;
        variable result_pc : std_logic_vector(31 downto 0) := (others => '0');
        variable cycle_count : natural := 0;
        variable global_count : natural := 0;

        procedure write_cpu_value(file output_file : text;
                                  variable output_line : inout line;
                                  value : std_logic_vector) is
        begin
            hwrite(output_line, value);
            write(output_line, string'(" "));
        end procedure;

        procedure dump_result(terminal_value : integer) is
            file output_file : text open write_mode is RESULT_FILE;
            variable output_line : line;
            variable address : unsigned(31 downto 0);
            variable sr : std_logic_vector(15 downto 0);
            variable architectural_usp : std_logic_vector(31 downto 0);
            variable architectural_isp : std_logic_vector(31 downto 0);
            variable architectural_msp : std_logic_vector(31 downto 0);
        begin
            write(output_line, terminal_value);
            write(output_line, string'(" "));
            write(output_line, cycle_count);
            writeline(output_file, output_line);

            sr := debug_flags_sr & debug_flags;
            architectural_usp := debug_usp;
            architectural_isp := debug_isp;
            architectural_msp := debug_msp;
            if debug_flags_sr(5) = '0' then
                architectural_usp := debug_a7;
            elsif debug_flags_sr(4) = '1' then
                architectural_msp := debug_a7;
            else
                architectural_isp := debug_a7;
            end if;
            write_cpu_value(output_file, output_line, debug_d0);
            write_cpu_value(output_file, output_line, debug_d1);
            write_cpu_value(output_file, output_line, debug_d2);
            write_cpu_value(output_file, output_line, debug_d3);
            write_cpu_value(output_file, output_line, debug_d4);
            write_cpu_value(output_file, output_line, debug_d5);
            write_cpu_value(output_file, output_line, debug_d6);
            write_cpu_value(output_file, output_line, debug_d7);
            write_cpu_value(output_file, output_line, debug_a0);
            write_cpu_value(output_file, output_line, debug_a1);
            write_cpu_value(output_file, output_line, debug_a2);
            write_cpu_value(output_file, output_line, debug_a3);
            write_cpu_value(output_file, output_line, debug_a4);
            write_cpu_value(output_file, output_line, debug_a5);
            write_cpu_value(output_file, output_line, debug_a6);
            write_cpu_value(output_file, output_line, debug_a7);
            write_cpu_value(output_file, output_line, result_pc);
            write_cpu_value(output_file, output_line, x"0000" & sr);
            write_cpu_value(output_file, output_line, architectural_usp);
            write_cpu_value(output_file, output_line, architectural_isp);
            write_cpu_value(output_file, output_line, architectural_msp);
            write_cpu_value(output_file, output_line, x"0000000" & '0' & debug_sfc);
            write_cpu_value(output_file, output_line, x"0000000" & '0' & debug_dfc);
            write_cpu_value(output_file, output_line, vbr_out);
            write_cpu_value(output_file, output_line, cacr_out);
            write_cpu_value(output_file, output_line, debug_caar);
            writeline(output_file, output_line);

            write(output_line, observation_count);
            writeline(output_file, output_line);
            for observation in 0 to observation_count - 1 loop
                hwrite(output_line, observation_addresses(observation));
                write(output_line, string'(" "));
                write(output_line, observation_lengths(observation));
                write(output_line, string'(" "));
                address := unsigned(observation_addresses(observation));
                for offset in 0 to observation_lengths(observation) - 1 loop
                    hwrite(output_line, memory.read8(std_logic_vector(address + offset)));
                end loop;
                writeline(output_file, output_line);
            end loop;
            test_done <= true;
            finish;
        end procedure;
    begin
        file_open(memory_input, MEMORY_FILE, read_mode);
        while not endfile(memory_input) loop
            readline(memory_input, input_line);
            hread(input_line, memory_address);
            hread(input_line, memory_byte);
            memory.write8(memory_address, memory_byte);
        end loop;
        file_close(memory_input);

        file_open(config_input, CONFIG_FILE, read_mode);
        readline(config_input, input_line);
        read(input_line, run_mode);
        readline(config_input, input_line);
        read(input_line, max_cycles);
        readline(config_input, input_line);
        hread(input_line, target_pc);
        readline(config_input, input_line);
        hread(input_line, stop_address);
        hread(input_line, stop_mask);
        hread(input_line, stop_value);
        readline(config_input, input_line);
        read(input_line, observation_count);
        assert observation_count >= 0 and observation_count <= MAX_OBSERVATIONS
            report "invalid RTL conformance observation count"
            severity failure;
        for observation in 0 to observation_count - 1 loop
            readline(config_input, input_line);
            hread(input_line, observation_addresses(observation));
            read(input_line, observation_lengths(observation));
        end loop;
        file_close(config_input);

        load_done <= '1';
        for index in 0 to 7 loop
            wait until rising_edge(clk);
        end loop;
        nreset <= '1';

        loop
            wait until rising_edge(clk);
            global_count := global_count + 1;

            if target_started and
               (debug_trap_1111 = '1' or debug_trapmake = '1') then
                target_exception := true;
            end if;

            if not target_opcode_seen and debug_clkena_lw = '1' and
               debug_last_opc_pc = target_pc then
                target_opcode_seen := true;
                target_started := true;
                cycle_count := 0;
            elsif debug_setopcode = '1' and debug_clkena_lw = '1' then
                if run_mode = 1 and target_opcode_seen then
                    if target_exception then
                        result_pc := debug_pc;
                    else
                        result_pc := debug_last_opc_pc;
                    end if;
                    -- setopcode identifies architectural retirement, but the
                    -- corresponding exec vector commits on the next enabled
                    -- long-word clock.  Sample that edge before the newly
                    -- accepted instruction's exec vector can take effect.
                    wait until rising_edge(clk) and debug_clkena_lw = '1';
                    wait for 1 ps;
                    dump_result(1);
                end if;
            end if;

            if target_started then
                cycle_count := cycle_count + 1;
                if run_mode = 3 and
                      (memory.read32(stop_address) and stop_mask) =
                      (stop_value and stop_mask) then
                    result_pc := debug_pc;
                    dump_result(3);
                elsif cycle_count >= natural(max_cycles) * 64 then
                    result_pc := debug_pc;
                    dump_result(2);
                end if;
            elsif global_count >= natural(max_cycles) * 64 + 20000 then
                assert false
                    report "RTL conformance target never reached target PC " &
                           to_hstring(target_pc) & "; pc=" & to_hstring(debug_pc) &
                           " last=" & to_hstring(debug_last_opc_pc) &
                           " sr=" & to_hstring(debug_flags_sr & debug_flags)
                    severity failure;
            end if;
        end loop;
    end process;
end architecture;
