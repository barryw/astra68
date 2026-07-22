library ieee;
use ieee.std_logic_1164.all;

use work.TG68K_Pack.all;

entity tb_cacr_command_decode_motorola is
end entity;

architecture test of tb_cacr_command_decode_motorola is
begin
  process
    procedure check(
      constant value : in std_logic_vector(31 downto 0);
      constant expected_scope : in cache_op_code;
      constant expected_cache : in cache_op_code) is
    begin
      assert cacr_cache_op_scope(value) = expected_scope
        report "CACR command scope mismatch"
        severity failure;
      assert cacr_cache_op_cache(value) = expected_cache
        report "CACR cache selection mismatch"
        severity failure;
    end procedure;
  begin
    check(x"00000000", "10", "00"); -- no command
    check(x"00000004", "00", "10"); -- CEI
    check(x"00000400", "00", "01"); -- CED
    check(x"00000404", "00", "11"); -- CEI + CED
    check(x"00000008", "10", "10"); -- CI
    check(x"00000800", "10", "01"); -- CD
    check(x"00000808", "10", "11"); -- CI + CD
    check(x"00000408", "10", "11"); -- CI + CED
    check(x"00000804", "10", "11"); -- CEI + CD
    check(x"0000000c", "10", "10"); -- CI + CEI
    check(x"00000c00", "10", "01"); -- CD + CED
    check(x"00000c0c", "10", "11"); -- every command

    report "CACR COMMAND DECODE PASS" severity note;
    wait;
  end process;
end architecture;
