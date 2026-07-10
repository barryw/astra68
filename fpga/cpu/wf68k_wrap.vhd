-- Astra 68 — generic-free VHDL wrapper around WF68K30L_TOP.
-- Two jobs:
--  1. Fix the core's std_logic_vector VERSION generic (Synplify mixed-language
--     SV→VHDL can't bind non-integer/string generics), and open the unused
--     status/arbitration outputs.
--  2. Expose the six inactive bus-control inputs (BERRn/HALT_INn/AVECn/STERMn/
--     BRn/BGACKn) as PORTS rather than tying them to '1' in here.
--
-- WHY the control inputs are ports and not local '1' constants:
--   Synplify constant-propagates ANY compile-time constant on these inputs
--   straight through the core and folds the ENTIRE CPU away (~0 LUT). This is
--   independent of where the constant lives (VHDL here or SV parent). The SoC
--   must drive them from a syn_preserve'd register so the optimizer sees a real
--   register output, not a foldable constant. On silicon they are still held at
--   the inactive level '1' — identical function, the synthesizer's view differs.
library ieee;
use ieee.std_logic_1164.all;

entity wf68k_wrap is
    port(
        CLK       : in  std_logic;
        ADR_OUT   : out std_logic_vector(31 downto 0);
        DATA_IN   : in  std_logic_vector(31 downto 0);
        DATA_OUT  : out std_logic_vector(31 downto 0);
        DATA_EN   : out std_logic;
        RESET_INn : in  std_logic;
        FC_OUT    : out std_logic_vector(2 downto 0);
        IPLn      : in  std_logic_vector(2 downto 0);
        DSACKn    : in  std_logic_vector(1 downto 0);
        SIZE      : out std_logic_vector(1 downto 0);
        ASn       : out std_logic;
        RWn       : out std_logic;
        RMCn      : out std_logic;
        DSn       : out std_logic;
        -- Inactive bus-control inputs, driven from a preserved reg in the SoC.
        BERRn     : in  std_logic;
        HALT_INn  : in  std_logic;
        AVECn     : in  std_logic;
        STERMn    : in  std_logic;
        BRn       : in  std_logic;
        BGACKn    : in  std_logic;
        -- DEBUG taps (HW bring-up):
        DBG_D2C   : out std_logic_vector(31 downto 0);
        DBG_IMM   : out std_logic_vector(31 downto 0);
        DBG_ARIN  : out std_logic_vector(31 downto 0)
    );
end entity;

architecture rtl of wf68k_wrap is
begin
    u_cpu : entity work.WF68K30L_TOP
        generic map(VERSION => x"1904", NO_PIPELINE => false, NO_LOOP => false)
        port map(
            CLK => CLK, ADR_OUT => ADR_OUT, DATA_IN => DATA_IN,
            DATA_OUT => DATA_OUT, DATA_EN => DATA_EN,
            BERRn => BERRn, RESET_INn => RESET_INn, RESET_OUT => open,
            HALT_INn => HALT_INn, HALT_OUTn => open, FC_OUT => FC_OUT,
            AVECn => AVECn, IPLn => IPLn, IPENDn => open, DSACKn => DSACKn,
            SIZE => SIZE, ASn => ASn, RWn => RWn, RMCn => RMCn, DSn => DSn,
            ECSn => open, OCSn => open, DBENn => open, BUS_EN => open,
            STERMn => STERMn, STATUSn => open, REFILLn => open,
            BRn => BRn, BGn => open, BGACKn => BGACKn,
            DBG_D2C => DBG_D2C, DBG_IMM => DBG_IMM, DBG_ARIN => DBG_ARIN
        );
end architecture;
