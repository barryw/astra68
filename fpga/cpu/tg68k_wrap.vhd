-- Astra 68 - common-interface wrapper for TobiFlex TG68K.C.
--
-- This instantiates the upstream TG68KdotC kernel directly in 68020 mode and
-- adapts its 16-bit 68K-style bus to the SoC's existing 32-bit WF68K-shaped
-- memory interface.  The files in tg68k_c/ are an upstream import with small
-- local Astra compatibility fixes documented in tg68k_c/README.astra.md.
library ieee;
use ieee.std_logic_1164.all;

entity tg68k_wrap is
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
        BERRn     : in  std_logic;
        HALT_INn  : in  std_logic;
        AVECn     : in  std_logic;
        STERMn    : in  std_logic;
        BRn       : in  std_logic;
        BGACKn    : in  std_logic;
        CACHEABLE_IN   : in  std_logic;
        CACHE_FLUSH_IN : in  std_logic;
        CACHE_FILL_VALID : in std_logic;
        CACHE_FILL_ADDR  : in std_logic_vector(31 downto 0);
        CACHE_FILL_DATA  : in std_logic_vector(127 downto 0);
        CACHE_FILL_INSN  : in std_logic;
        CACHE_IHIT_IN    : in std_logic;
        CACHE_IDATA_IN   : in std_logic_vector(31 downto 0);
        CACHE_DHIT_IN    : in std_logic;
        CACHE_DDATA_IN   : in std_logic_vector(31 downto 0);
        CACHE_LOOKUP_ADDR : out std_logic_vector(31 downto 0);
        CACHE_LOOKUP_INSN : out std_logic;
        CACHE_LOOKUP_DATA : out std_logic;
        CACHE_STORE_VALID : out std_logic;
        CACHE_STORE_ADDR  : out std_logic_vector(31 downto 0);
        CACHE_STORE_DATA  : out std_logic_vector(31 downto 0);
        CACHE_STORE_INSN  : out std_logic;
        CACHE_INVALIDATE_VALID : out std_logic;
        CACHE_INVALIDATE_ADDR  : out std_logic_vector(31 downto 0);
        CACHE_INVALIDATE_ALL   : out std_logic;
        CACHE_IFREEZE_OUT : out std_logic;
        CACHE_DFREEZE_OUT : out std_logic;
        CACHEABLE_OUT  : out std_logic;
        POSTABLE_OUT   : out std_logic;
        MEMORY_BUSY_IN : in  std_logic;
        DBG_ICACHE_HITS   : out std_logic_vector(31 downto 0);
        DBG_ICACHE_MISSES : out std_logic_vector(31 downto 0);
        DBG_DCACHE_HITS   : out std_logic_vector(31 downto 0);
        DBG_DCACHE_MISSES : out std_logic_vector(31 downto 0);
        DBG_D2C   : out std_logic_vector(31 downto 0);
        DBG_IMM   : out std_logic_vector(31 downto 0);
        DBG_ARIN  : out std_logic_vector(31 downto 0)
    );
end entity;

architecture rtl of tg68k_wrap is
    signal tg_addr       : std_logic_vector(31 downto 0);
    signal bus_addr      : std_logic_vector(31 downto 0);
    signal tg_data_in    : std_logic_vector(15 downto 0);
    signal tg_data_out   : std_logic_vector(15 downto 0);
    signal tg_nwr        : std_logic;
    signal tg_nuds       : std_logic;
    signal tg_nlds       : std_logic;
    signal tg_busstate   : std_logic_vector(1 downto 0);
    signal tg_fc         : std_logic_vector(2 downto 0);
    signal tg_longword   : std_logic;
    signal tg_skip_fetch : std_logic;
    signal tg_regin      : std_logic_vector(31 downto 0);
    signal tg_cacr       : std_logic_vector(3 downto 0);
    signal tg_vbr        : std_logic_vector(31 downto 0);
    signal tg_debug      : std_logic_vector(31 downto 0);
    type bus_fsm_t is (BUS_IDLE, BUS_ASSERT, BUS_GAP);
    signal bus_fsm       : bus_fsm_t := BUS_IDLE;
    signal req_cycle     : std_logic;
    signal bus_active    : std_logic;
    signal ack_now       : std_logic;
    signal clkena        : std_logic;
    signal berr          : std_logic;
    signal berr_latched  : std_logic := '0';
    signal autovector    : std_logic;

    function align_write(
        data16 : std_logic_vector(15 downto 0);
        addr   : std_logic_vector(1 downto 0);
        nuds   : std_logic;
        nlds   : std_logic
    ) return std_logic_vector is
        variable d : std_logic_vector(31 downto 0) := (others => '0');
    begin
        if nuds = '0' and nlds = '0' then
            if addr(1) = '0' then
                d(31 downto 16) := data16;
            else
                d(15 downto 0) := data16;
            end if;
        elsif nuds = '0' then
            if addr(1) = '0' then
                d(31 downto 24) := data16(15 downto 8);
            else
                d(15 downto 8) := data16(15 downto 8);
            end if;
        elsif nlds = '0' then
            if addr(1) = '0' then
                d(23 downto 16) := data16(7 downto 0);
            else
                d(7 downto 0) := data16(7 downto 0);
            end if;
        end if;
        return d;
    end function;
begin
    bus_addr <= tg_addr(31 downto 1) & '0' when tg_nuds = '0' else
                tg_addr(31 downto 1) & '1' when tg_nlds = '0' else
                tg_addr;

    -- TG68K.C emits one 16-bit external bus cycle at a time.  Feed it the
    -- 16-bit halfword that contains the addressed byte lane.
    tg_data_in <= DATA_IN(31 downto 16) when bus_addr(1) = '0' else DATA_IN(15 downto 0);

    req_cycle <= '1' when tg_busstate /= "01" and tg_skip_fetch = '0' else '0';
    bus_active <= '1' when bus_fsm = BUS_ASSERT else '0';
    ack_now <= '1' when bus_active = '1' and DSACKn /= "11" else '0';
    clkena <= '1' when req_cycle = '0' or tg_skip_fetch = '1' or ack_now = '1' else '0';

    process(CLK)
    begin
        if rising_edge(CLK) then
            if RESET_INn = '0' then
                bus_fsm <= BUS_IDLE;
                berr_latched <= '0';
            else
                case bus_fsm is
                    when BUS_IDLE =>
                        berr_latched <= '0';
                        if req_cycle = '1' then
                            bus_fsm <= BUS_ASSERT;
                        end if;
                    when BUS_ASSERT =>
                        if BERRn = '0' then
                            berr_latched <= '1';
                        end if;
                        if DSACKn /= "11" then
                            bus_fsm <= BUS_GAP;
                        end if;
                    when BUS_GAP =>
                        bus_fsm <= BUS_IDLE;
                end case;
            end if;
        end if;
    end process;

    ADR_OUT <= bus_addr;
    DATA_OUT <= align_write(tg_data_out, bus_addr(1 downto 0), tg_nuds, tg_nlds);
    DATA_EN <= '1' when bus_active = '1' and tg_nwr = '0' else '0';
    FC_OUT <= tg_fc;
    ASn <= not bus_active;
    DSn <= not bus_active;
    RWn <= tg_nwr;
    SIZE <= "10" when bus_active = '1' and tg_nuds = '0' and tg_nlds = '0' else "01";
    berr <= berr_latched or (not BERRn);
    autovector <= not AVECn;

    DBG_D2C <= tg_debug;
    CACHE_LOOKUP_ADDR <= bus_addr;
    CACHE_LOOKUP_INSN <= '0';
    CACHE_LOOKUP_DATA <= '0';
    CACHE_STORE_VALID <= '0';
    CACHE_STORE_ADDR <= bus_addr;
    CACHE_STORE_DATA <= DATA_IN;
    CACHE_STORE_INSN <= '0';
    CACHE_INVALIDATE_VALID <= '0';
    CACHE_INVALIDATE_ADDR <= bus_addr;
    CACHE_INVALIDATE_ALL <= '0';
    CACHE_IFREEZE_OUT <= '0';
    CACHE_DFREEZE_OUT <= '0';
    CACHEABLE_OUT <= '0';
    POSTABLE_OUT <= '0';
    DBG_ICACHE_HITS <= (others => '0');
    DBG_ICACHE_MISSES <= (others => '0');
    DBG_DCACHE_HITS <= (others => '0');
    DBG_DCACHE_MISSES <= (others => '0');
    DBG_IMM <= x"0000000" & tg_cacr;
    DBG_ARIN <= tg_vbr;

    u_cpu : entity work.TG68KdotC_Kernel
        generic map(
            SR_Read => 2,
            VBR_Stackframe => 2,
            extAddr_Mode => 2,
            MUL_Mode => 2,
            DIV_Mode => 2,
            BitField => 2,
            BarrelShifter => 0,
            MUL_Hardware => 1
        )
        port map(
            clk => CLK,
            nReset => RESET_INn,
            clkena_in => clkena,
            data_in => tg_data_in,
            IPL => IPLn,
            IPL_autovector => autovector,
            berr => berr,
            CPU => "11",
            addr_out => tg_addr,
            data_write => tg_data_out,
            nWr => tg_nwr,
            nUDS => tg_nuds,
            nLDS => tg_nlds,
            busstate => tg_busstate,
            longword => tg_longword,
            nResetOut => open,
            FC => tg_fc,
            clr_berr => open,
            RMCn_out => RMCn,
            skipFetch => tg_skip_fetch,
            regin_out => tg_regin,
            CACR_out => tg_cacr,
            VBR_out => tg_vbr,
            debug_status_out => tg_debug
        );
end architecture;
