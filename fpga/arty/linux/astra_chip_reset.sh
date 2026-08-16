#!/bin/sh
set -eu

# UG585 FPGA_RST_CTRL bit 1 drives the FCLK_RESET1_N domain used by every
# Astra PL peripheral. Future audio/math blocks belong on this same reset.
DEVMEM=${ASTRA_DEVMEM:-devmem}
IP=${ASTRA_IP:-ip}
ETHERNET_PATH=${ASTRA_ETHERNET_PATH:-/sys/class/net/eth0}
SLCR_LOCK=0xF8000004
SLCR_UNLOCK=0xF8000008
FPGA_RST_CTRL=0xF8000240
FCLK1_RESET=0x00000002

# On this Zynq platform, pulsing the FCLK1 fabric reset before Linux has
# attached the RTL8211F leaves its first MDIO transaction timing out.  Bring
# the PHY through its normal Linux attach path first; this is idempotent once
# networking is already running and does not require a cable or DHCP lease.
if [ -e "$ETHERNET_PATH" ] && ! "$IP" link set eth0 up; then
    echo "warning: could not prime eth0 before fabric reset" >&2
fi

write32()
{
    "$DEVMEM" "$1" 32 "$2" >/dev/null
}

read32()
{
    "$DEVMEM" "$1" 32
}

check32()
{
    actual=$(read32 "$1")
    expected=$2
    if [ "$((actual))" -ne "$((expected))" ]; then
        printf '%s reset mismatch: expected 0x%08x, got %s\n' \
            "$3" "$((expected))" "$actual" >&2
        exit 1
    fi
}

original=$(read32 "$FPGA_RST_CTRL")
released=$((original & ~FCLK1_RESET))
unlocked=1
asserted=0
restore()
{
    if [ "$asserted" -eq 1 ]; then
        write32 "$FPGA_RST_CTRL" "$released" || true
    fi
    if [ "$unlocked" -eq 1 ]; then
        write32 "$SLCR_LOCK" 0x0000767B || true
    fi
}
trap restore 0 1 2 15

write32 "$SLCR_UNLOCK" 0x0000DF0D
asserted=1
write32 "$FPGA_RST_CTRL" "$((released | FCLK1_RESET))"
write32 "$FPGA_RST_CTRL" "$released"
asserted=0
write32 "$SLCR_LOCK" 0x0000767B
unlocked=0
trap - 0 1 2 15

check32 "$FPGA_RST_CTRL" "$released" "fabric"
check32 0x43C00000 0x41535452 "graphics"
check32 0x43C0000C 0 "graphics control"
check32 0x43C06000 0x41554430 "HDMI audio"
check32 0x43C0600C 0 "HDMI audio control"
check32 0x43C07000 0x504E4C30 "front panel"
check32 0x43C07018 0 "front-panel LEDs"
check32 0x43C0701C 0 "front-panel ownership"
check32 0x43C0702C 0 "disk activity"
