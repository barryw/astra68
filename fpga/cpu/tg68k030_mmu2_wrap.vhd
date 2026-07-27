-- Astra 68 - common-interface wrapper for the pinned TG68K.C 030_mmu2 core.
--
-- The entity keeps the concise tg68k_wrap name used at the SoC boundary. This
-- is the sole supported CPU wrapper and is compiled directly by every build.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

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
        DBG_EXE_PC : out std_logic_vector(31 downto 0);
        DBG_IMM   : out std_logic_vector(31 downto 0);
        DBG_ARIN  : out std_logic_vector(31 downto 0)
    );
end entity;

architecture rtl of tg68k_wrap is
    signal tg_addr       : std_logic_vector(31 downto 0);
    signal tg_logical_addr : std_logic_vector(31 downto 0);
    signal bus_addr      : std_logic_vector(31 downto 0);
    signal tg_data_in    : std_logic_vector(15 downto 0);
    signal tg_data_out   : std_logic_vector(15 downto 0);
    signal tg_nwr        : std_logic;
    signal tg_nuds       : std_logic;
    signal tg_nlds       : std_logic;
    signal tg_busstate   : std_logic_vector(1 downto 0);
    signal tg_fc         : std_logic_vector(2 downto 0);
    signal tg_rmcn       : std_logic;
    signal tg_longword   : std_logic;
    signal tg_skip_fetch : std_logic;
    signal tg_regin      : std_logic_vector(31 downto 0);
    signal tg_cacr       : std_logic_vector(31 downto 0);
    signal tg_vbr        : std_logic_vector(31 downto 0);
    signal tg_debug_pc   : std_logic_vector(31 downto 0);
    signal tg_debug_exe_pc : std_logic_vector(31 downto 0);
    signal tg_trap_vector : std_logic_vector(31 downto 0);
    signal pmmu_walker_req  : std_logic;
    signal pmmu_walker_we   : std_logic;
    signal pmmu_walker_addr : std_logic_vector(31 downto 0);
    signal pmmu_walker_wdat : std_logic_vector(31 downto 0);
    signal pmmu_walker_ack  : std_logic;
    signal pmmu_walker_data : std_logic_vector(31 downto 0);
    signal pmmu_walker_berr : std_logic;
    signal pmmu_cache_inhibit_int : std_logic;
    signal cache_inv_req_int : std_logic;
    signal cacr_ie_int : std_logic;
    signal cacr_ifreeze_int : std_logic;
    signal cacr_de_int : std_logic;
    signal cacr_dfreeze_int : std_logic;
    signal cacr_ibe_int : std_logic;
    signal cacr_dbe_int : std_logic;
    signal dbg_trap_addr_error : std_logic;
    signal dbg_trap_berr       : std_logic;
    signal dbg_trap_mmu_berr   : std_logic;
    signal dbg_make_berr       : std_logic;
    signal dbg_sv_mode         : std_logic;
    signal dbg_pre_sv_mode     : std_logic;
    signal dbg_flags_sr        : std_logic_vector(7 downto 0);
    signal dbg_change_mode     : std_logic;
    signal dbg_setopcode       : std_logic;
    signal dbg_exec_direct_sr  : std_logic;
    signal dbg_exec_to_sr      : std_logic;
    signal dbg_trap_priv       : std_logic;
    signal dbg_opcode          : std_logic_vector(15 downto 0);
    signal dbg_state           : std_logic_vector(1 downto 0);
    signal dbg_data_read       : std_logic_vector(31 downto 0);
    signal dbg_regfile_a7      : std_logic_vector(31 downto 0);
    signal dbg_regfile_we      : std_logic;
    signal dbg_regfile_waddr   : std_logic_vector(3 downto 0);
    signal dbg_regfile_wdata   : std_logic_vector(31 downto 0);
    signal dbg_pmmu_busy       : std_logic;
    signal dbg_pmmu_fault      : std_logic;
    signal dbg_pmmu_wstate     : std_logic_vector(4 downto 0);
    signal dbg_pmmu_saved_addr : std_logic_vector(31 downto 0);
    signal dbg_pmmu_desc_data  : std_logic_vector(31 downto 0);
    signal pmmu_table_search   : std_logic;
    type bus_fsm_t is (BUS_IDLE, BUS_ASSERT, BUS_GAP);
    signal bus_fsm       : bus_fsm_t := BUS_IDLE;
    signal tg_req_cycle  : std_logic;
    signal req_cycle     : std_logic;
    signal bus_active    : std_logic;
    signal external_ack  : std_logic;
    signal cache_ack     : std_logic;
    signal combine_ack   : std_logic;
    signal ack_now       : std_logic;
    signal walker_cycle  : std_logic := '0';
    signal walker_we_latched   : std_logic := '0';
    signal walker_addr_latched : std_logic_vector(31 downto 0) := (others => '0');
    signal walker_wdat_latched : std_logic_vector(31 downto 0) := (others => '0');
    signal clkena        : std_logic;
    signal berr          : std_logic;
    signal berr_latched  : std_logic := '0';
    signal autovector    : std_logic;

    signal icache_program_read : std_logic;
    signal icache_lookup_int : std_logic;
    signal icache_hit_int : std_logic;
    signal icache_hits : unsigned(31 downto 0) := (others => '0');
    signal icache_misses : unsigned(31 downto 0) := (others => '0');
    signal dcache_data_read : std_logic;
    signal dcache_lookup_int : std_logic;
    signal dcache_hit_int : std_logic;
    signal dcache_hits : unsigned(31 downto 0) := (others => '0');
    signal dcache_misses : unsigned(31 downto 0) := (others => '0');
    signal cache_hit_int : std_logic;
    signal icache_data_now : std_logic_vector(15 downto 0);
    signal dcache_data_now : std_logic_vector(15 downto 0);
    signal dcache_response_valid : std_logic := '0';
    signal dcache_response_data : std_logic_vector(15 downto 0) := (others => '0');
    signal icache_hit_start : std_logic;
    signal dcache_hit_start : std_logic;
    signal cache_hit_start : std_logic;
    signal cache_miss_start : std_logic;
    signal dcache_miss_start : std_logic;
    signal memory_sync_wait : std_logic;
    signal write_postable : std_logic;
    signal combine_first : std_logic;
    signal combine_second : std_logic;
    signal combine_pending : std_logic := '0';
    signal combine_addr : std_logic_vector(31 downto 0) := (others => '0');
    signal combine_fault_addr : std_logic_vector(31 downto 0) := (others => '0');
    signal combine_hi : std_logic_vector(15 downto 0) := (others => '0');
    signal long_write_partial : std_logic := '0';

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
    -- TG's busstate already distinguishes program fetches from data reads.
    -- Do not derive the cache-hit acknowledgement from FC: MOVES generates FC
    -- through logic that depends on clkena, which would feed cache_ack back
    -- into clkena and create a combinational loop in the packed design.
    icache_program_read <= '1' when tg_nwr = '1' and tg_busstate = "00"
                           else '0';
    icache_lookup_int <= '1' when tg_req_cycle = '1' and
                                  icache_program_read = '1' and
                                  CACHEABLE_IN = '1' and
                                  pmmu_cache_inhibit_int = '0' and
                                  cacr_ie_int = '1' else '0';
    icache_hit_int <= CACHE_IHIT_IN and icache_lookup_int;
    dcache_data_read <= '1' when tg_nwr = '1' and tg_busstate = "10"
                        else '0';
    dcache_lookup_int <= '1' when tg_req_cycle = '1' and
                                  dcache_data_read = '1' and
                                  tg_rmcn = '1' and
                                  CACHEABLE_IN = '1' and
                                  pmmu_cache_inhibit_int = '0' and
                                  cacr_de_int = '1' else '0';
    dcache_hit_int <= CACHE_DHIT_IN and dcache_lookup_int;
    cache_hit_int <= icache_hit_int or dcache_hit_int;

    CACHE_LOOKUP_ADDR <= bus_addr;
    CACHE_LOOKUP_INSN <= icache_lookup_int;
    CACHE_LOOKUP_DATA <= dcache_lookup_int;
    CACHE_STORE_VALID <= '1' when external_ack = '1' and
                                  walker_cycle = '0' and tg_nwr = '1' and
                                  CACHEABLE_IN = '1' and
                                  pmmu_cache_inhibit_int = '0' and
                                  ((icache_program_read = '1' and
                                    cacr_ie_int = '1' and
                                    cacr_ifreeze_int = '0') or
                                   (dcache_data_read = '1' and
                                    cacr_de_int = '1' and
                                    cacr_dfreeze_int = '0')) else '0';
    CACHE_STORE_ADDR <= bus_addr;
    CACHE_STORE_DATA <= DATA_IN;
    CACHE_STORE_INSN <= icache_program_read;
    CACHE_INVALIDATE_VALID <= '1' when external_ack = '1' and
                                      walker_cycle = '0' and tg_nwr = '0' and
                                      CACHEABLE_IN = '1' else '0';
    CACHE_INVALIDATE_ADDR <= bus_addr;
    CACHE_INVALIDATE_ALL <= cache_inv_req_int;
    CACHE_IFREEZE_OUT <= cacr_ifreeze_int;
    CACHE_DFREEZE_OUT <= cacr_dfreeze_int;

    -- ClOUT-equivalent qualification for the external SDRAM line buffer.
    -- Only ordinary cacheable reads with the matching MC68030 burst-enable
    -- bit may fetch adjacent words. PMMU table walks and locked cycles bypass.
    CACHEABLE_OUT <= '1' when bus_active = '1' and walker_cycle = '0' and
                              tg_nwr = '1' and tg_rmcn = '1' and
                              CACHEABLE_IN = '1' and
                              pmmu_cache_inhibit_int = '0' and
                              ((icache_program_read = '1' and
                                cacr_ie_int = '1' and cacr_ibe_int = '1') or
                               (dcache_data_read = '1' and
                                cacr_de_int = '1' and cacr_dbe_int = '1'))
                     else '0';
    write_postable <= '1' when walker_cycle = '0' and tg_nwr = '0' and
                               tg_rmcn = '1' and CACHEABLE_IN = '1' and
                               pmmu_cache_inhibit_int = '0' else '0';
    POSTABLE_OUT <= bus_active and write_postable;
    combine_first <= '1' when combine_pending = '0' and
                              tg_req_cycle = '1' and write_postable = '1' and
                              long_write_partial = '0' and
                              tg_longword = '1' and tg_nuds = '0' and
                              tg_nlds = '0' and bus_addr(1 downto 0) = "00"
                     else '0';
    combine_second <= '1' when combine_pending = '1' and
                               tg_req_cycle = '1' and tg_nwr = '0' and
                               tg_nuds = '0' and tg_nlds = '0' and
                               unsigned(bus_addr) = unsigned(combine_addr) + 2
                      else '0';
    -- MC68030 UM 7.6: NOP synchronizes the execution and bus units. A posted
    -- write may overlap cache-only instructions, but NOP cannot retire until
    -- the external write has completed.
    memory_sync_wait <= '1' when MEMORY_BUSY_IN = '1' and
                                dbg_opcode = x"4E71" else '0';

    bus_addr <= tg_addr(31 downto 1) & '0' when tg_nuds = '0' else
                tg_addr(31 downto 1) & '1' when tg_nlds = '0' else
                tg_addr;

    -- TG68K.C emits one 16-bit external bus cycle at a time. Feed it the
    -- 16-bit halfword that contains the addressed byte lane.
    icache_data_now <= CACHE_IDATA_IN(31 downto 16)
                       when bus_addr(1) = '0' else CACHE_IDATA_IN(15 downto 0);
    dcache_data_now <= CACHE_DDATA_IN(31 downto 16)
                       when bus_addr(1) = '0' else CACHE_DDATA_IN(15 downto 0);
    tg_data_in <= icache_data_now when icache_hit_start = '1' else
                  dcache_response_data when dcache_response_valid = '1' else
                  DATA_IN(31 downto 16) when bus_addr(1) = '0' else
                  DATA_IN(15 downto 0);

    -- A PMMU/address fault suppresses both TG byte strobes before the internal
    -- write state retires. Busstate alone would turn that suppressed transfer
    -- into a zero-valued byte write in this adapter.
    tg_req_cycle <= '1' when tg_busstate /= "01" and tg_skip_fetch = '0' and
                             (tg_nuds = '0' or tg_nlds = '0') else
                    '0';
    req_cycle <= pmmu_walker_req or tg_req_cycle;
    bus_active <= '1' when bus_fsm = BUS_ASSERT else '0';
    -- The measured load-to-register path crosses the asynchronous data cache.
    -- Register only data-cache responses while TG is stalled; instruction hits
    -- retain their existing zero-wait response and do not pay the extra cycle.
    cache_ack <= icache_hit_start or dcache_response_valid;
    combine_ack <= '1' when
        (bus_fsm = BUS_IDLE or bus_fsm = BUS_GAP) and combine_first = '1'
        else '0';
    external_ack <= '1' when bus_active = '1' and
                             (DSACKn /= "11" or BERRn = '0') else '0';
    ack_now <= external_ack or cache_ack or combine_ack;
    icache_hit_start <= '1' when
        (bus_fsm = BUS_IDLE or bus_fsm = BUS_GAP) and
        pmmu_walker_req = '0' and tg_req_cycle = '1' and
        icache_hit_int = '1' else '0';
    dcache_hit_start <= '1' when
        (bus_fsm = BUS_IDLE or bus_fsm = BUS_GAP) and
        dcache_response_valid = '0' and
        pmmu_walker_req = '0' and tg_req_cycle = '1' and
        dcache_hit_int = '1' else '0';
    cache_hit_start <= icache_hit_start or dcache_hit_start;
    cache_miss_start <= '1' when
        (bus_fsm = BUS_IDLE or bus_fsm = BUS_GAP) and
        pmmu_walker_req = '0' and tg_req_cycle = '1' and
        cache_hit_int = '0' and icache_program_read = '1' and
        CACHEABLE_IN = '1' and pmmu_cache_inhibit_int = '0' and
        cacr_ie_int = '1' else '0';
    dcache_miss_start <= '1' when
        (bus_fsm = BUS_IDLE or bus_fsm = BUS_GAP) and
        pmmu_walker_req = '0' and tg_req_cycle = '1' and
        dcache_hit_int = '0' and dcache_data_read = '1' and
        tg_rmcn = '1' and CACHEABLE_IN = '1' and
        pmmu_cache_inhibit_int = '0' and cacr_de_int = '1' else '0';
    -- The PMMU runs from CLK independently of the TG core enable. Keep the CPU
    -- frozen for the complete translation, including W_FILL/W_COMPLETE cycles
    -- where the walker has no external request. Releasing it on walker ACK
    -- corrupts the logical address before the ATC result becomes visible.
    clkena <= '0' when dbg_pmmu_busy = '1' or memory_sync_wait = '1' else
              '1' when req_cycle = '0' or tg_skip_fetch = '1' or ack_now = '1' else
              '0';

    process(CLK)
    begin
        if rising_edge(CLK) then
            if RESET_INn = '0' or HALT_INn = '0' then
                bus_fsm <= BUS_IDLE;
                dcache_response_valid <= '0';
                dcache_response_data <= (others => '0');
                berr_latched <= '0';
                walker_cycle <= '0';
                walker_we_latched <= '0';
                walker_addr_latched <= (others => '0');
                walker_wdat_latched <= (others => '0');
                combine_pending <= '0';
                combine_addr <= (others => '0');
                combine_fault_addr <= (others => '0');
                combine_hi <= (others => '0');
                long_write_partial <= '0';
            else
                if dcache_response_valid = '1' then
                    dcache_response_valid <= '0';
                elsif dcache_hit_start = '1' then
                    dcache_response_valid <= '1';
                    dcache_response_data <= dcache_data_now;
                end if;
                if tg_longword = '0' then
                    long_write_partial <= '0';
                elsif tg_req_cycle = '1' and tg_nwr = '0' and
                      combine_pending = '0' and
                      not (tg_nuds = '0' and tg_nlds = '0' and
                           bus_addr(1 downto 0) = "00") then
                    -- A byte/word prefix means this is a misaligned longword.
                    -- Do not reinterpret a later aligned constituent beat as
                    -- the start of a combinable aligned transfer.
                    long_write_partial <= '1';
                end if;
                case bus_fsm is
                    when BUS_IDLE =>
                        berr_latched <= '0';
                        if pmmu_walker_req = '1' then
                            walker_cycle <= '1';
                            walker_we_latched <= pmmu_walker_we;
                            walker_addr_latched <= pmmu_walker_addr;
                            walker_wdat_latched <= pmmu_walker_wdat;
                            bus_fsm <= BUS_ASSERT;
                        elsif tg_req_cycle = '1' then
                            walker_cycle <= '0';
                            if combine_first = '1' then
                                combine_pending <= '1';
                                combine_addr <= bus_addr(31 downto 2) & "00";
                                combine_fault_addr <= tg_logical_addr(31 downto 2) & "00";
                                combine_hi <= tg_data_out;
                                bus_fsm <= BUS_GAP;
                            elsif combine_second = '1' then
                                bus_fsm <= BUS_ASSERT;
                            elsif cache_hit_int = '1' then
                                bus_fsm <= BUS_GAP;
                            else
                                bus_fsm <= BUS_ASSERT;
                            end if;
                        end if;
                    when BUS_ASSERT =>
                        if BERRn = '0' and walker_cycle = '0' then
                            berr_latched <= '1';
                        end if;
                        if ack_now = '1' then
                            if combine_second = '1' then
                                combine_pending <= '0';
                            end if;
                            bus_fsm <= BUS_GAP;
                        end if;
                    when BUS_GAP =>
                        if pmmu_walker_req = '1' then
                            walker_cycle <= '1';
                            walker_we_latched <= pmmu_walker_we;
                            walker_addr_latched <= pmmu_walker_addr;
                            walker_wdat_latched <= pmmu_walker_wdat;
                            bus_fsm <= BUS_ASSERT;
                        elsif tg_req_cycle = '1' then
                            walker_cycle <= '0';
                            if combine_first = '1' then
                                combine_pending <= '1';
                                combine_addr <= bus_addr(31 downto 2) & "00";
                                combine_fault_addr <= tg_logical_addr(31 downto 2) & "00";
                                combine_hi <= tg_data_out;
                                bus_fsm <= BUS_GAP;
                            elsif combine_second = '1' then
                                bus_fsm <= BUS_ASSERT;
                            elsif cache_hit_int = '1' then
                                bus_fsm <= BUS_GAP;
                            else
                                bus_fsm <= BUS_ASSERT;
                            end if;
                        else
                            bus_fsm <= BUS_IDLE;
                        end if;
                end case;
            end if;
        end if;
    end process;

    process(CLK)
    begin
        if rising_edge(CLK) then
            if RESET_INn = '0' or HALT_INn = '0' then
                icache_hits <= (others => '0');
                icache_misses <= (others => '0');
                dcache_hits <= (others => '0');
                dcache_misses <= (others => '0');
            else
                if cache_hit_start = '1' and icache_hit_int = '1' then
                    icache_hits <= icache_hits + 1;
                elsif cache_miss_start = '1' then
                    icache_misses <= icache_misses + 1;
                end if;
                if cache_hit_start = '1' and dcache_hit_int = '1' then
                    dcache_hits <= dcache_hits + 1;
                elsif dcache_miss_start = '1' then
                    dcache_misses <= dcache_misses + 1;
                end if;
            end if;
        end if;
    end process;

    ADR_OUT <= walker_addr_latched when bus_active = '1' and walker_cycle = '1' else
               combine_addr when bus_active = '1' and combine_second = '1' else
               bus_addr;
    DATA_OUT <= walker_wdat_latched when bus_active = '1' and walker_cycle = '1' else
                combine_hi & tg_data_out when bus_active = '1' and
                                               combine_second = '1' else
                align_write(tg_data_out, bus_addr(1 downto 0), tg_nuds, tg_nlds);
    DATA_EN <= '1' when bus_active = '1' and
                       ((walker_cycle = '1' and walker_we_latched = '1') or
                        (walker_cycle = '0' and tg_nwr = '0' and
                         (tg_nuds = '0' or tg_nlds = '0'))) else '0';
    FC_OUT <= "101" when bus_active = '1' and walker_cycle = '1' else tg_fc;
    ASn <= not bus_active;
    DSn <= not bus_active;
    RWn <= not walker_we_latched when bus_active = '1' and walker_cycle = '1' else
           tg_nwr;
    -- MC68030 UM 9.5.2 requires RMC asserted for the complete table search,
    -- not only while an individual descriptor transfer is active. W_IDLE is
    -- encoding zero; include walker_req so lock assertion covers the first
    -- descriptor even on the state-transition cycle. Do not use PMMU busy
    -- here because it also pulses for ordinary ATC-hit result registration.
    pmmu_table_search <= '1' when pmmu_walker_req = '1' or
                                 dbg_pmmu_wstate /= "00000" else '0';
    RMCn <= '0' when pmmu_table_search = '1' else tg_rmcn;
    SIZE <= "00" when bus_active = '1' and walker_cycle = '1' else
            "00" when bus_active = '1' and combine_second = '1' else
            "10" when bus_active = '1' and tg_nuds = '0' and tg_nlds = '0' else
            "01";
    berr <= '0' when walker_cycle = '1' else berr_latched or (not BERRn);
    autovector <= not AVECn;

    pmmu_walker_ack <= '1' when bus_active = '1' and walker_cycle = '1' and
                                DSACKn /= "11" else '0';
    pmmu_walker_data <= DATA_IN;
    pmmu_walker_berr <= '1' when bus_active = '1' and walker_cycle = '1' and
                                 BERRn = '0' else '0';

    DBG_D2C <= tg_debug_pc;
    DBG_EXE_PC <= tg_debug_exe_pc;
    DBG_ICACHE_HITS <= std_logic_vector(icache_hits);
    DBG_ICACHE_MISSES <= std_logic_vector(icache_misses);
    DBG_DCACHE_HITS <= std_logic_vector(dcache_hits);
    DBG_DCACHE_MISSES <= std_logic_vector(dcache_misses);
    DBG_IMM <= dbg_pmmu_wstate & pmmu_walker_req & pmmu_walker_we &
               dbg_pmmu_fault & dbg_pmmu_desc_data(23 downto 0)
               when dbg_pmmu_busy = '1' else
               dbg_flags_sr & dbg_sv_mode & dbg_pre_sv_mode &
               dbg_change_mode & dbg_exec_direct_sr & dbg_exec_to_sr &
               dbg_trap_priv & dbg_regfile_we & dbg_regfile_waddr &
               dbg_state & dbg_opcode(10 downto 0);
    DBG_ARIN <= pmmu_walker_addr when pmmu_walker_req = '1' else
                dbg_pmmu_saved_addr when dbg_pmmu_busy = '1' else
                dbg_regfile_wdata when dbg_regfile_we = '1' else
                dbg_regfile_a7;
    u_cpu : entity work.TG68KdotC_Kernel
        generic map(
            SR_Read => 1,
            VBR_Stackframe => 1,
            extAddr_Mode => 1,
            MUL_Mode => 1,
            DIV_Mode => 1,
            BitField => 1,
            BarrelShifter => 1,
            MUL_Hardware => 1
        )
        port map(
            clk => CLK,
            nReset => RESET_INn and HALT_INn,
            clkena_in => clkena,
            data_in => tg_data_in,
            IPL => IPLn,
            IPL_autovector => autovector,
            berr => berr,
            berr_addr_override => combine_second,
            berr_addr_in => combine_fault_addr,
            CPU => "10",
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
            RMCn_out => tg_rmcn,
            skipFetch => tg_skip_fetch,
            regin_out => tg_regin,
            CACR_out => tg_cacr,
            VBR_out => tg_vbr,
            cache_inv_req => cache_inv_req_int,
            cache_op_scope => open,
            cache_op_cache => open,
            cacr_ie => cacr_ie_int,
            cacr_de => cacr_de_int,
            cacr_ifreeze => cacr_ifreeze_int,
            cacr_dfreeze => cacr_dfreeze_int,
            cacr_ibe => cacr_ibe_int,
            cacr_dbe => cacr_dbe_int,
            cacr_wa => open,
            pmmu_reg_we => open,
            pmmu_reg_re => open,
            pmmu_reg_sel => open,
            pmmu_reg_wdat => open,
            pmmu_reg_part => open,
            pmmu_addr_log => tg_logical_addr,
            pmmu_addr_phys => open,
            pmmu_cache_inhibit => pmmu_cache_inhibit_int,
            cache_op_addr => open,
            pmmu_walker_req => pmmu_walker_req,
            pmmu_walker_we => pmmu_walker_we,
            pmmu_walker_addr => pmmu_walker_addr,
            pmmu_walker_wdat => pmmu_walker_wdat,
            pmmu_walker_ack => pmmu_walker_ack,
            pmmu_walker_data => pmmu_walker_data,
            pmmu_walker_berr => pmmu_walker_berr,
            debug_SVmode => dbg_sv_mode,
            debug_preSVmode => dbg_pre_sv_mode,
            debug_FlagsSR_S => open,
            debug_changeMode => dbg_change_mode,
            debug_setopcode => dbg_setopcode,
            debug_exec_directSR => dbg_exec_direct_sr,
            debug_exec_to_SR => dbg_exec_to_sr,
            debug_pmove_dn_mode => open,
            debug_pmove_dn_regnum => open,
            debug_opcode => dbg_opcode,
            debug_state => dbg_state,
            debug_setstate => open,
            debug_last_opc_read => open,
            debug_data_read => dbg_data_read,
            debug_direct_data => open,
            debug_setnextpass => open,
            debug_TG68_PC => tg_debug_pc,
            debug_memaddr_reg => open,
            debug_memaddr_delta => open,
            debug_oddout => open,
            debug_decodeOPC => open,
            debug_brief => open,
            debug_moves_bus_pending => open,
            debug_moves_writeback_pending => open,
            debug_clkena_lw => open,
            debug_regfile_d0 => open,
            debug_regfile_a0 => open,
            debug_fline_context_valid => open,
            debug_trap_1111 => open,
            debug_trapmake => open,
            debug_pmmu_brief => open,
            debug_use_base => open,
            debug_rf_source_addr => open,
            debug_pmove_ea_latched => open,
            debug_reg_QA => open,
            debug_last_data_read => open,
            debug_last_opc_pc => open,
            debug_getbrief => open,
            debug_get_2ndopc => open,
            debug_fline_brief_pending => open,
            debug_fline_opcode_pc => open,
            debug_exe_PC => tg_debug_exe_pc,
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
            debug_regfile_a7 => dbg_regfile_a7,
            debug_regfile_we => dbg_regfile_we,
            debug_regfile_waddr => dbg_regfile_waddr,
            debug_regfile_wdata => dbg_regfile_wdata,
            debug_trap_illegal => open,
            debug_trap_priv => dbg_trap_priv,
            debug_trap_addr_error => dbg_trap_addr_error,
            debug_trap_berr => dbg_trap_berr,
            debug_trap_mmu_berr => dbg_trap_mmu_berr,
            debug_trap_vector => tg_trap_vector,
            debug_pc_add => open,
            debug_pc_dataa => open,
            debug_pc_datab => open,
            debug_pmmu_busy => dbg_pmmu_busy,
            debug_cpu_halted => open,
            debug_stop => open,
            debug_interrupt => open,
            debug_setendOPC => open,
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
            debug_make_berr => dbg_make_berr,
            debug_pmmu_fault => dbg_pmmu_fault,
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
            debug_pmmu_wstate => dbg_pmmu_wstate,
            debug_pmmu_atc_buserr => open,
            debug_pmmu_atc_valid => open,
            debug_pmmu_pending_flags => open,
            debug_pmmu_fault_status => open,
            debug_pmmu_saved_addr => dbg_pmmu_saved_addr,
            debug_pmmu_walk_desc_addr => open,
            debug_pmmu_walk_desc_data => dbg_pmmu_desc_data,
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
            debug_FlagsSR => dbg_flags_sr,
            debug_USP => open,
            debug_MSP => open,
            debug_ISP => open,
            debug_a7_is_msp => open,
            debug_interrupt_mode => open,
            debug_rte_saved_mbit => open,
            debug_rte_format_word => open,
            debug_rte_mmu_fix_ssw => open,
            debug_rte_mmu_fix_opcode => open,
            debug_rte_mmu_fix_write => open,
            debug_rte_format_b_version_error => open
        );
end architecture;
