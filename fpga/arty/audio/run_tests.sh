#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
BUILD=${BUILD:-"$ROOT/build/arty-audio"}
mkdir -p "$BUILD"

iverilog -g2012 -Wall \
    -s tb_astra_hdmi_audio \
    -o "$BUILD/tb_astra_hdmi_audio" \
    "$ROOT/fpga/arty/common/astra_async_fifo.sv" \
    "$ROOT/fpga/arty/audio/astra_hdmi_audio.sv" \
    "$ROOT/fpga/arty/audio/sim/tb_astra_hdmi_audio.sv"
vvp "$BUILD/tb_astra_hdmi_audio"

iverilog -g2012 -Wall \
    -s tb_astra_i2s_transmitter \
    -o "$BUILD/tb_astra_i2s_transmitter" \
    "$ROOT/fpga/common/astra_i2s_transmitter.sv" \
    "$ROOT/fpga/arty/audio/sim/tb_astra_i2s_transmitter.sv"
vvp "$BUILD/tb_astra_i2s_transmitter"
