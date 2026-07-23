-- MC68030 UM 9.2.2 processor-reset and boot-flush regression.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_pmmu_reset_atc_motorola is
end entity;

architecture tb of tb_pmmu_reset_atc_motorola is
  constant CLK_PERIOD : time := 10 ns;

  signal clk          : std_logic := '0';
  signal nreset       : std_logic := '0';
  signal running      : boolean := true;
  signal reg_we       : std_logic := '0';
  signal reg_re       : std_logic := '0';
  signal reg_sel      : std_logic_vector(4 downto 0) := (others => '0');
  signal reg_wdat     : std_logic_vector(31 downto 0) := (others => '0');
  signal reg_rdat     : std_logic_vector(31 downto 0);
  signal reg_part     : std_logic := '0';
  signal reg_fd       : std_logic := '0';
  signal ptest_req    : std_logic := '0';
  signal pflush_req   : std_logic := '0';
  signal pload_req    : std_logic := '0';
  signal pmmu_fc      : std_logic_vector(2 downto 0) := "101";
  signal pmmu_addr    : std_logic_vector(31 downto 0) := (others => '0');
  signal pmmu_brief   : std_logic_vector(15 downto 0) := (others => '0');
  signal req          : std_logic := '0';
  signal is_insn      : std_logic := '0';
  signal rw           : std_logic := '1';
  signal fc           : std_logic_vector(2 downto 0) := "101";
  signal addr_log     : std_logic_vector(31 downto 0) := (others => '0');
  signal addr_phys    : std_logic_vector(31 downto 0);
  signal cache_inhibit : std_logic;
  signal write_protect : std_logic;
  signal fault        : std_logic;
  signal fault_status : std_logic_vector(31 downto 0);
  signal tc_enable    : std_logic;
  signal mem_req      : std_logic;
  signal mem_we       : std_logic;
  signal mem_addr     : std_logic_vector(31 downto 0);
  signal mem_wdat     : std_logic_vector(31 downto 0);
  signal mem_ack      : std_logic := '0';
  signal mem_rdat     : std_logic_vector(31 downto 0) := (others => '0');
  signal busy         : std_logic;
  signal mmu_config_err : std_logic;
  signal ptest_desc_addr : std_logic_vector(31 downto 0);
  signal debug_tc     : std_logic_vector(31 downto 0);
  signal debug_tt0    : std_logic_vector(31 downto 0);
  signal debug_tt1    : std_logic_vector(31 downto 0);
  signal debug_crp_hi : std_logic_vector(31 downto 0);
  signal debug_crp_lo : std_logic_vector(31 downto 0);
  signal debug_srp_hi : std_logic_vector(31 downto 0);
  signal debug_srp_lo : std_logic_vector(31 downto 0);
  signal debug_atc_valid : std_logic_vector(21 downto 0);

  type memory_t is array (0 to 4095) of std_logic_vector(31 downto 0);
  signal memory : memory_t := (others => (others => '0'));
begin
  dut : entity work.TG68K_PMMU_030
    port map (
      clk => clk,
      nreset => nreset,
      reg_we => reg_we,
      reg_re => reg_re,
      reg_sel => reg_sel,
      reg_wdat => reg_wdat,
      reg_rdat => reg_rdat,
      reg_part => reg_part,
      reg_fd => reg_fd,
      ptest_req => ptest_req,
      pflush_req => pflush_req,
      pload_req => pload_req,
      pmmu_fc => pmmu_fc,
      pmmu_addr => pmmu_addr,
      pmmu_brief => pmmu_brief,
      req => req,
      is_insn => is_insn,
      rw => rw,
      fc => fc,
      addr_log => addr_log,
      addr_phys => addr_phys,
      cache_inhibit => cache_inhibit,
      write_protect => write_protect,
      fault => fault,
      fault_status => fault_status,
      tc_enable => tc_enable,
      mem_req => mem_req,
      mem_we => mem_we,
      mem_addr => mem_addr,
      mem_wdat => mem_wdat,
      mem_ack => mem_ack,
      mem_berr => '0',
      mem_rdat => mem_rdat,
      busy => busy,
      mmu_config_err => mmu_config_err,
      mmu_config_ack => '0',
      ptest_desc_addr => ptest_desc_addr,
      debug_tc => debug_tc,
      debug_tt0 => debug_tt0,
      debug_tt1 => debug_tt1,
      debug_crp_hi => debug_crp_hi,
      debug_crp_lo => debug_crp_lo,
      debug_srp_hi => debug_srp_hi,
      debug_srp_lo => debug_srp_lo,
      debug_atc_valid => debug_atc_valid,
      cpu_reset => '0'
    );

  clock_process : process
  begin
    while running loop
      clk <= '0';
      wait for CLK_PERIOD / 2;
      clk <= '1';
      wait for CLK_PERIOD / 2;
    end loop;
    wait;
  end process;

  memory_process : process(clk)
    variable index : integer;
  begin
    if rising_edge(clk) then
      if nreset = '0' then
        mem_ack <= '0';
      elsif mem_req = '1' and mem_ack = '0' then
        index := to_integer(unsigned(mem_addr(13 downto 2)));
        if index < memory'length then
          mem_rdat <= memory(index);
        else
          mem_rdat <= (others => '0');
        end if;
        mem_ack <= '1';
      elsif mem_req = '0' then
        mem_ack <= '0';
      end if;
    end if;
  end process;

  test_process : process
    procedure write_register(
      constant selector : in std_logic_vector(4 downto 0);
      constant value    : in std_logic_vector(31 downto 0);
      constant high     : in std_logic
    ) is
    begin
      wait until rising_edge(clk);
      reg_sel <= selector;
      reg_wdat <= value;
      reg_part <= high;
      reg_we <= '1';
      wait until rising_edge(clk);
      reg_we <= '0';
      wait until rising_edge(clk);
    end procedure;

    procedure read_register(
      constant selector : in std_logic_vector(4 downto 0);
      constant high     : in std_logic
    ) is
    begin
      wait until rising_edge(clk);
      reg_sel <= selector;
      reg_part <= high;
      reg_re <= '1';
      wait until rising_edge(clk);
      reg_re <= '0';
      wait for 1 ns;
    end procedure;

    procedure translate(constant address : in std_logic_vector(31 downto 0)) is
    begin
      wait until falling_edge(clk);
      addr_log <= address;
      req <= '1';
      wait until rising_edge(clk);
      wait for 1 ns;
      while busy = '1' loop
        wait until rising_edge(clk);
        wait for 1 ns;
      end loop;
      wait until falling_edge(clk);
      req <= '0';
      wait until rising_edge(clk);
    end procedure;

    procedure ptest_level_zero(constant address : in std_logic_vector(31 downto 0)) is
    begin
      pmmu_brief <= x"8200";
      pmmu_addr <= address;
      pmmu_fc <= "101";
      ptest_req <= '1';
      wait until rising_edge(clk);
      ptest_req <= '0';
      wait until rising_edge(clk);
      wait until rising_edge(clk);
      read_register("11000", '0');
    end procedure;

    procedure pflusha is
    begin
      pmmu_brief <= x"2400";
      pflush_req <= '1';
      wait until rising_edge(clk);
      pflush_req <= '0';
      wait until rising_edge(clk);
      wait until rising_edge(clk);
    end procedure;

    variable valid_before_reset : std_logic_vector(21 downto 0);
    variable physical_before_reset : std_logic_vector(31 downto 0);
  begin
    nreset <= '0';
    wait for 10 * CLK_PERIOD;
    nreset <= '1';
    wait for 5 * CLK_PERIOD;

    -- PS=15, TIA=10, TIB=7. CRP points to a short-descriptor root at 0x400.
    write_register("10000", x"80F0A700", '0');
    write_register("10011", x"00000002", '1');
    write_register("10011", x"00000400", '0');
    write_register("10010", x"00000002", '1');
    write_register("10010", x"00000C00", '0');
    for i in 0 to 31 loop
      memory(256 + i) <= x"00000802";
    end loop;
    for i in 0 to 127 loop
      memory(512 + i) <= std_logic_vector(to_unsigned(i * 32768, 24)) & x"01";
    end loop;
    wait for 10 * CLK_PERIOD;

    translate(x"00011000");
    assert fault = '0' report "FAIL: initial translation faulted" severity error;
    assert debug_atc_valid /= "0000000000000000000000"
      report "FAIL: initial translation did not populate ATC" severity error;
    valid_before_reset := debug_atc_valid;
    physical_before_reset := addr_phys;
    ptest_level_zero(x"00011000");
    assert reg_rdat(15 downto 0) = x"0000"
      report "FAIL: populated ATC entry missed before reset" severity error;

    -- Make the cached translation stale before RESET. For this TC image,
    -- logical 0x00011000 uses the second-level descriptor at 0x00000808.
    memory(514) <= memory(514) xor x"01000000";
    wait for 2 * CLK_PERIOD;

    -- Use PMOVEFD so these unrelated register writes do not flush the entry.
    reg_fd <= '1';
    write_register("00010", x"F0008000", '0');
    write_register("00011", x"E1008000", '0');
    reg_fd <= '0';

    nreset <= '0';
    wait for 3 * CLK_PERIOD;
    nreset <= '1';
    wait for 3 * CLK_PERIOD;

    assert tc_enable = '0' report "FAIL: RESET left TC.E enabled" severity error;
    assert debug_tc = x"00F0A700"
      report "FAIL: RESET changed TC fields other than E" severity error;
    assert debug_tt0 = x"F0000000"
      report "FAIL: RESET changed TT0 fields other than E" severity error;
    assert debug_tt1 = x"E1000000"
      report "FAIL: RESET changed TT1 fields other than E" severity error;
    assert debug_crp_hi = x"00000002" and debug_crp_lo = x"00000400"
      report "FAIL: RESET changed CRP" severity error;
    assert debug_srp_hi = x"00000002" and debug_srp_lo = x"00000C00"
      report "FAIL: RESET changed SRP" severity error;
    assert debug_atc_valid = valid_before_reset
      report "FAIL: RESET invalidated an ATC entry" severity error;

    ptest_level_zero(x"00011000");
    assert reg_rdat(15 downto 0) = x"0000"
      report "FAIL: retained ATC entry was not usable with TC disabled" severity error;

    -- This is the kernel boot invariant: explicit flush precedes TC.E.
    pflusha;
    assert debug_atc_valid = "0000000000000000000000"
      report "FAIL: PFLUSHA did not invalidate the retained ATC" severity error;
    ptest_level_zero(x"00011000");
    assert reg_rdat(15 downto 0) /= x"0000"
      report "FAIL: PTEST still hit after boot PFLUSHA" severity error;

    write_register("10000", x"80F0A700", '0');
    translate(x"00011000");
    assert fault = '0' report "FAIL: translation did not re-walk after flush" severity error;
    assert debug_atc_valid /= "0000000000000000000000"
      report "FAIL: post-flush walk did not refill ATC" severity error;
    assert addr_phys /= physical_before_reset
      report "FAIL: post-flush walk reused the stale physical translation" severity error;

    report "PASS: MC68030 RESET preserves PMMU state and boot flushes ATC"
      severity note;
    running <= false;
    wait;
  end process;
end architecture;
