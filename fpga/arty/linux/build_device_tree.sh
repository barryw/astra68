#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
base_dtb=${ASTRA_ARTY_BASE_DTB:-/home/barry/nova-arty/images/linux/system.dtb}
base_hash=${ASTRA_ARTY_BASE_DTB_SHA256:-f0348d6122d6604a3c12c944219d7d23f481c27ca9d1254dfd30e6bb0972addd}
out_dir=${ASTRA_ARTY_LINUX_OUT:-"$repo_root/build/arty-graphics/linux"}
dtc_root=${ASTRA_DTC_ROOT:-/home/barry/nova-arty/build/tmp/sysroots-components/x86_64/dtc-native/usr/bin}
fdtget=${FDTGET:-"$dtc_root/fdtget"}
fdtput=${FDTPUT:-"$dtc_root/fdtput"}
dtc=${DTC:-"$dtc_root/dtc"}
fclk1_hz=${ASTRA_ARTY_FCLK1_HZ:-200000000}

case "$fclk1_hz" in
    200000000|187500000|166666672) ;;
    *)
        echo "unsupported FCLK1 rate: $fclk1_hz" >&2
        exit 3
        ;;
esac

sha256() {
    sha256sum "$1" | awk '{print $1}'
}

remove_node() {
    local dtb=$1
    local node=$2
    if "$fdtget" -p "$dtb" "$node" >/dev/null 2>&1; then
        "$fdtput" -r "$dtb" "$node"
    fi
}

remove_property() {
    local dtb=$1
    local node=$2
    local property=$3
    if "$fdtget" "$dtb" "$node" "$property" >/dev/null 2>&1; then
        "$fdtput" -d "$dtb" "$node" "$property"
    fi
}

assert_node_absent() {
    local dtb=$1
    local node=$2
    if "$fdtget" -p "$dtb" "$node" >/dev/null 2>&1; then
        echo "retired device-tree node remains: $node" >&2
        exit 1
    fi
}

test -x "$fdtget"
test -x "$fdtput"
test -x "$dtc"
test -s "$base_dtb"
actual_base_hash=$(sha256 "$base_dtb")
if [[ "$actual_base_hash" != "$base_hash" ]]; then
    echo "base DTB hash mismatch: $base_dtb" >&2
    echo "expected $base_hash" >&2
    echo "actual   $actual_base_hash" >&2
    exit 1
fi

mkdir -p "$out_dir"
work="$out_dir/astra-system.dtb.work"
output="$out_dir/astra-system.dtb"
if [[ -e "$work" || -e "$output" ]]; then
    echo "device-tree output already exists in $out_dir" >&2
    exit 2
fi
cp "$base_dtb" "$work"

remove_node "$work" /fio-bridge@40000000
remove_node "$work" /capture@40010000
remove_node "$work" /reserved-memory/xram@10000000
remove_node "$work" /reserved-memory/novavm-capture@11000000
remove_property "$work" /__symbols__ fio_bridge
remove_property "$work" /__symbols__ xram_reserved
remove_property "$work" /__symbols__ novavm_capture
remove_property "$work" /__symbols__ novavm_capture_mem

"$fdtput" -t s "$work" / model 'Astra 68 Arty Z7-20'

# Keep the appliance network scripts on their existing eth0 contract and give
# Linux ownership of the Arty Z7 RTL8211F reset line.  The inherited
# "enet-reset" property is not a Linux binding and leaves a wedged PHY stuck
# across warm boots.
chosen=/chosen
bootargs=$("$fdtget" -t s "$work" "$chosen" bootargs)
case " $bootargs " in
    *' net.ifnames=0 '*) ;;
    *) bootargs="$bootargs net.ifnames=0" ;;
esac
"$fdtput" -t s "$work" "$chosen" bootargs "$bootargs"

ethernet=$("$fdtget" -t s "$work" /__symbols__ gem0)
gpio=$("$fdtget" -t s "$work" /__symbols__ gpio0)
gpio_phandle=$("$fdtget" -t u "$work" "$gpio" phandle)
phy=$ethernet/ethernet-phy@0
phy_phandle=256
remove_property "$work" "$ethernet" enet-reset
"$fdtput" -p -c "$work" "$phy"
"$fdtput" -t s "$work" "$phy" compatible ethernet-phy-id001c.c916
"$fdtput" -t u "$work" "$phy" reg 0
"$fdtput" -t u "$work" "$phy" phandle "$phy_phandle"
"$fdtput" -t u "$work" "$phy" reset-gpios "$gpio_phandle" 9 1
"$fdtput" -t u "$work" "$phy" reset-assert-us 10000
"$fdtput" -t u "$work" "$phy" reset-deassert-us 1000000
"$fdtput" -t u "$work" "$ethernet" phy-handle "$phy_phandle"

# Keep Linux out of the graphics arena even on kernels that fail to remove a
# no-map reservation from System RAM.
memory=/memory@0
"$fdtput" -t x "$work" "$memory" reg 00000000 18000000

arena=/reserved-memory/astra-graphics@18000000
"$fdtput" -p -c "$work" "$arena"
"$fdtput" -t x "$work" "$arena" reg 18000000 08000000
"$fdtput" "$work" "$arena" no-map

control=/astra-graphics@43c00000
"$fdtput" -p -c "$work" "$control"
"$fdtput" -t s "$work" "$control" compatible astra68,graphics-1.0
"$fdtput" -t x "$work" "$control" reg 43c00000 00010000
"$fdtput" -t s "$work" "$control" status okay

# The PS7 routes I2C0 through EMIO to the Arty HDMI TX DDC pins. HDMI 1.3a
# section 8.4 limits DDC to I2C Standard Mode (100 kHz).
i2c0=$("$fdtget" -t s "$work" /__symbols__ i2c0)
"$fdtput" -t s "$work" "$i2c0" status okay
"$fdtput" -t u "$work" "$i2c0" clock-frequency 100000

clkc=$("$fdtget" -t s "$work" /__symbols__ clkc)
clkc_phandle=$("$fdtget" -t u "$work" "$clkc" phandle)
"$fdtput" -t u "$work" "$clkc" fclk-enable 3
"$fdtput" -t u "$work" "$clkc" assigned-clocks \
    "$clkc_phandle" 15 "$clkc_phandle" 16
"$fdtput" -t u "$work" "$clkc" assigned-clock-rates \
    100000000 "$fclk1_hz"

# Parsing the complete result catches malformed structured edits. The inherited
# SPI partition warnings are unchanged and are not promoted to errors here.
"$dtc" -q -I dtb -O dtb -o "$out_dir/astra-system.validated.dtb" "$work"

[[ $("$fdtget" -t s "$work" / model) == 'Astra 68 Arty Z7-20' ]]
[[ $("$fdtget" -t s "$work" "$chosen" bootargs) == *' net.ifnames=0' ]]
[[ $("$fdtget" -t s "$work" "$phy" compatible) == 'ethernet-phy-id001c.c916' ]]
[[ $("$fdtget" -t u "$work" "$phy" reset-gpios) == "$gpio_phandle 9 1" ]]
[[ $("$fdtget" -t u "$work" "$phy" reset-assert-us) == '10000' ]]
[[ $("$fdtget" -t u "$work" "$phy" reset-deassert-us) == '1000000' ]]
[[ $("$fdtget" -t u "$work" "$ethernet" phy-handle) == "$phy_phandle" ]]
if "$fdtget" "$work" "$ethernet" enet-reset >/dev/null 2>&1; then
    echo "obsolete enet-reset property remains" >&2
    exit 1
fi
[[ $("$fdtget" -t x "$work" "$memory" reg) == '0 18000000' ]]
[[ $("$fdtget" -t x "$work" "$arena" reg) == '18000000 8000000' ]]
"$fdtget" -p "$work" "$arena" | grep -Fxq no-map
[[ $("$fdtget" -t x "$work" "$control" reg) == '43c00000 10000' ]]
[[ $("$fdtget" -t s "$work" "$i2c0" status) == 'okay' ]]
[[ $("$fdtget" -t u "$work" "$i2c0" clock-frequency) == '100000' ]]
[[ $("$fdtget" -t u "$work" "$clkc" fclk-enable) == '3' ]]
[[ $("$fdtget" -t u "$work" "$clkc" assigned-clocks) == \
   "$clkc_phandle 15 $clkc_phandle 16" ]]
[[ $("$fdtget" -t u "$work" "$clkc" assigned-clock-rates) == \
   "100000000 $fclk1_hz" ]]
assert_node_absent "$work" /fio-bridge@40000000
assert_node_absent "$work" /capture@40010000
assert_node_absent "$work" /reserved-memory/xram@10000000
assert_node_absent "$work" /reserved-memory/novavm-capture@11000000

mv "$work" "$output"
sha256sum "$base_dtb" "$output" >"$out_dir/DTB_SHA256SUMS"
echo "ASTRA_ARTY_DTB PASS $output"
