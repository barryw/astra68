-- Motorola-directed regression for an interrupt accepted in supervisor master mode.
-- MC68030 UM 8.1.9 requires a Format $0 frame on MSP followed by a Format $1
-- throwaway frame on ISP. The throwaway SR retains M=1 so RTE chains to MSP.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_interrupt_master_dual_frame_motorola is
end entity;

architecture behavior of tb_interrupt_master_dual_frame_motorola is
    signal clk        : std_logic := '0';
    signal nReset     : std_logic := '0';
    signal clkena_in  : std_logic := '1';
    signal data_in    : std_logic_vector(15 downto 0);
    signal data_write : std_logic_vector(15 downto 0);
    signal addr_out   : std_logic_vector(31 downto 0);
    signal nWr        : std_logic;
    signal nUDS       : std_logic;
    signal nLDS       : std_logic;
    signal busstate   : std_logic_vector(1 downto 0);
    signal FC         : std_logic_vector(2 downto 0);
    signal ipl_sig    : std_logic_vector(2 downto 0) := "111";
    signal debug_msp  : std_logic_vector(31 downto 0);

    constant CLK_PERIOD : time := 10 ns;
    type mem_array_t is array(0 to 8191) of std_logic_vector(15 downto 0);
    shared variable mem : mem_array_t;
    signal test_done : boolean := false;

    procedure init_memory is
    begin
        for i in 0 to 8191 loop
            mem(i) := x"4E71";
        end loop;
    end procedure;
begin
    clk <= not clk after CLK_PERIOD / 2 when not test_done;

    dut: entity work.TG68KdotC_Kernel
        generic map(
            SR_Read        => 2,
            VBR_Stackframe => 1,
            extAddr_Mode   => 1,
            MUL_Hardware   => 1,
            BarrelShifter  => 2
        )
        port map(
            clk => clk,
            nReset => nReset,
            clkena_in => clkena_in,
            data_in => data_in,
            IPL => ipl_sig,
            IPL_autovector => '1',
            berr => '0',
            CPU => "10",
            addr_out => addr_out,
            data_write => data_write,
            nWr => nWr,
            nUDS => nUDS,
            nLDS => nLDS,
            busstate => busstate,
            FC => FC,
            longword => open,
            nResetOut => open,
            clr_berr => open,
            skipFetch => open,
            regin_out => open,
            CACR_out => open,
            VBR_out => open,
            cache_inv_req => open,
            cache_op_scope => open,
            cache_op_cache => open,
            cache_op_addr => open,
            pmmu_reg_we => open,
            pmmu_reg_re => open,
            pmmu_reg_sel => open,
            pmmu_reg_wdat => open,
            pmmu_reg_part => open,
            pmmu_addr_log => open,
            pmmu_addr_phys => open,
            pmmu_cache_inhibit => open,
            pmmu_walker_req => open,
            pmmu_walker_we => open,
            pmmu_walker_addr => open,
            pmmu_walker_wdat => open,
            pmmu_walker_ack => '0',
            pmmu_walker_data => (others => '0'),
            pmmu_walker_berr => '0',
            debug_SVmode => open,
            debug_preSVmode => open,
            debug_FlagsSR_S => open,
            debug_changeMode => open,
            debug_setopcode => open,
            debug_exec_directSR => open,
            debug_exec_to_SR => open,
            debug_pmove_dn_mode => open,
            debug_pmove_dn_regnum => open,
            debug_MSP => debug_msp,
            debug_ISP => open
        );

    data_in <= mem(to_integer(unsigned(addr_out(15 downto 1))))
               when to_integer(unsigned(addr_out(15 downto 1))) <= 8191 else x"4E71";

    mem_write: process(clk)
    begin
        if rising_edge(clk) then
            if busstate = "11" and nWr = '0' then
                if to_integer(unsigned(addr_out(15 downto 1))) <= 8191 then
                    if nUDS = '0' then
                        mem(to_integer(unsigned(addr_out(15 downto 1))))(15 downto 8) := data_write(15 downto 8);
                    end if;
                    if nLDS = '0' then
                        mem(to_integer(unsigned(addr_out(15 downto 1))))(7 downto 0) := data_write(7 downto 0);
                    end if;
                end if;
            end if;
        end if;
    end process;

    test: process
        variable saw_handler : boolean := false;
        variable saw_success : boolean := false;
        variable saw_failure : boolean := false;
        variable stalled_resume : boolean := false;
    begin
        init_memory;

        -- Reset in interrupt mode at ISP=$0900 and begin setup at $1000.
        mem(0) := x"0000";
        mem(1) := x"0900";
        mem(2) := x"0000";
        mem(3) := x"1000";

        -- Level-7 autovector -> $1100.
        mem(16#007C# / 2) := x"0000";
        mem(16#007E# / 2) := x"1100";

        -- Set MSP=$0A10, then use a normal frame on ISP to enter supervisor
        -- master mode. This isolates interrupt behavior from MOVE/STOP-to-SR
        -- stack switching. The interrupt return PC is $1024, where a second
        -- RTE verifies that the original interrupt frame was consumed from MSP.
        -- The interrupt request arrives while MOVE.L is fetching its extension
        -- word. MC68030 interrupts are recognized between instructions, so the
        -- saved PC must be $1024, never the extension word at $1022.
        mem(16#1000# / 2) := x"203C";
        mem(16#1002# / 2) := x"0000";
        mem(16#1004# / 2) := x"0A10";
        mem(16#1006# / 2) := x"4E7B";
        mem(16#1008# / 2) := x"0803";
        mem(16#100A# / 2) := x"4E73";

        mem(16#0900# / 2) := x"3000";
        mem(16#0902# / 2) := x"0000";
        mem(16#0904# / 2) := x"1020";
        mem(16#0906# / 2) := x"0000";

        mem(16#1020# / 2) := x"262E"; -- MOVE.L 12(A6),D3
        mem(16#1022# / 2) := x"000C";
        mem(16#1024# / 2) := x"6600"; -- BNE.W $1040 (Z=0)
        mem(16#1026# / 2) := x"0018";
        -- If chained RTE incorrectly resumes from the extension word, $0018
        -- consumes $4E71 as its immediate and falls through to this failure.
        mem(16#1028# / 2) := x"4E71";
        mem(16#102A# / 2) := x"6000"; -- BRA.W $1300
        mem(16#102C# / 2) := x"02D2";
        mem(16#1040# / 2) := x"4E73";

        -- Exercise extension-word fetches in the handler before its
        -- unmodified RTE, matching a real interrupt stub rather than a
        -- one-instruction synthetic handler.
        mem(16#1100# / 2) := x"203C"; -- MOVE.L #$12345678,D0
        mem(16#1102# / 2) := x"1234";
        mem(16#1104# / 2) := x"5678";
        mem(16#1106# / 2) := x"6600"; -- BNE.W $1122
        mem(16#1108# / 2) := x"0018";
        mem(16#1122# / 2) := x"4E73";

        -- After the interrupt frame is removed, MSP points here. The branch at
        -- the restored PC verifies that RTE restarts a multiword instruction;
        -- its target RTE reaches success only if MSP is active.
        mem(16#0A10# / 2) := x"3000";
        mem(16#0A12# / 2) := x"0000";
        mem(16#0A14# / 2) := x"1400";
        mem(16#0A16# / 2) := x"0000";

        -- The setup RTE advances ISP to $0908. If the throwaway SR incorrectly
        -- clears M, the interrupt RTE remains on ISP and consumes this frame.
        mem(16#0908# / 2) := x"2000";
        mem(16#090A# / 2) := x"0000";
        mem(16#090C# / 2) := x"1300";
        mem(16#090E# / 2) := x"0000";

        mem(16#1300# / 2) := x"4E72";
        mem(16#1302# / 2) := x"2700";
        mem(16#1400# / 2) := x"4E72";
        mem(16#1402# / 2) := x"2700";

        report "=== real M=1 interrupt Format $1 dual-frame regression ===" severity note;

        nReset <= '0';
        wait for 100 ns;
        nReset <= '1';

        -- Wait until the setup RTE has established S=1/M=1 and MOVE.L is
        -- fetching the MOVE.L opcode, then request level 7 while the external
        -- bus stalls the core. The request stays asserted while the extension
        -- word is consumed.
        for i in 0 to 12000 loop
            wait until rising_edge(clk);
            if addr_out(15 downto 0) = x"1020" then
                ipl_sig <= "000";
                clkena_in <= '0';
                exit;
            end if;
        end loop;
        for i in 0 to 7 loop
            wait until rising_edge(clk);
        end loop;
        clkena_in <= '1';

        for i in 0 to 12000 loop
            wait until rising_edge(clk);
            if addr_out(15 downto 0) = x"1100" then
                saw_handler := true;
                ipl_sig <= "111";
            elsif saw_handler and not stalled_resume and
                  addr_out(15 downto 0) = x"1024" then
                clkena_in <= '0';
                for delay in 0 to 7 loop
                    wait until rising_edge(clk);
                end loop;
                clkena_in <= '1';
                stalled_resume := true;
            elsif addr_out(15 downto 0) = x"1300" then
                saw_failure := true;
                exit;
            elsif addr_out(15 downto 0) = x"1400" then
                saw_success := true;
                exit;
            end if;
        end loop;

        assert saw_handler
            report "FAIL: master-mode interrupt handler was not reached"
            severity failure;
        assert not saw_failure
            report "FAIL: Format $1 RTE stayed on ISP because the throwaway SR lost M"
            severity failure;
        assert saw_success
            report "FAIL: dual-frame RTE did not return through the MSP frame"
            severity failure;
        assert debug_msp(15 downto 0) = x"0A10"
            report "FAIL: dual-frame completion did not save the post-incremented MSP"
            severity failure;

        assert mem(16#0900# / 2) = x"3000"
            report "FAIL: throwaway frame SR did not preserve S=1/M=1"
            severity failure;
        assert mem(16#0902# / 2) = x"0000" and mem(16#0904# / 2) = x"1024"
            report "FAIL: throwaway frame PC does not match the interrupted PC"
            severity failure;
        assert mem(16#0906# / 2) = x"107C"
            report "FAIL: throwaway frame format/vector word is not $107C"
            severity failure;

        assert mem(16#0A08# / 2) = x"3000"
            report "FAIL: master frame SR is not the interrupted S=1/M=1 SR"
            severity failure;
        assert mem(16#0A0A# / 2) = x"0000" and mem(16#0A0C# / 2) = x"1024"
            report "FAIL: master frame PC does not match the interrupted PC"
            severity failure;
        assert mem(16#0A0E# / 2) = x"007C"
            report "FAIL: master frame format/vector word is not $007C"
            severity failure;

        report "PASS: real M=1 interrupt built both frames and RTE returned through MSP" severity note;
        test_done <= true;
        wait;
    end process;
end architecture;
