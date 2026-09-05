#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
QUARTUS_ROOT=${QUARTUS_ROOT:-/home/barry/altera_pro/26.1.1/quartus}
GHRD=$(realpath "${1:?usage: build_astra_shell.sh GHRD.qar RESOURCE.zip [quartus-license]}")
RESOURCE=$(realpath "${2:?usage: build_astra_shell.sh GHRD.qar RESOURCE.zip [quartus-license]}")
LICENSE=$(realpath "${3:-/home/barry/.altera.quartus/quartus2_lic.dat}")
BUILD_ROOT="$ROOT/build/de25"
OUT="$BUILD_ROOT/astra-shell"
INCOMING="$BUILD_ROOT/astra-shell.incoming"
FAILED="$BUILD_ROOT/astra-shell.failed"
QUARTUS_BIN="$QUARTUS_ROOT/bin"
. "$ROOT/fpga/de25/build_common.sh"

python3 "$ROOT/fpga/de25/check_platform.py" \
    --quartus-root "$QUARTUS_ROOT" --ghrd "$GHRD" --resource "$RESOURCE" \
    --license "$LICENSE"

preserve_failed_build() {
    local status=$?
    if (( status != 0 )) && [[ -d "$INCOMING" ]]; then
        rm -rf "$FAILED"
        mv "$INCOMING" "$FAILED"
        echo "DE25 failed build retained at $FAILED" >&2
    fi
    trap - EXIT
    exit "$status"
}
trap preserve_failed_build EXIT
prepare_de25_tree "$INCOMING"
mkdir -p "$INCOMING/ip/qsys_top" "$INCOMING/vendor_reference"

extract_resource() {
    local member=$1 output=$2 expected=$3
    unzip -p "$RESOURCE" "$member" >"$output"
    printf '%s  %s\n' "$expected" "$output" | sha256sum -c -
}

mapfile -t RESOURCE_FIELDS < <(python3 - "$ROOT/fpga/de25/platform.json" <<'PY'
import json, sys
manifest = json.load(open(sys.argv[1], encoding="utf-8"))
for key in ("lpddr4b_ip", "lpddr4b_ip_sha256", "lpddr4b_calibration",
            "lpddr4b_calibration_sha256", "lpddr4b_reference_qsf",
            "lpddr4b_reference_qsf_sha256", "hdmi_pixel_pll",
            "hdmi_pixel_pll_sha256", "hdmi_audio_pll",
            "hdmi_audio_pll_sha256", "hdmi_i2c_config",
            "hdmi_i2c_config_sha256", "hdmi_i2c_controller",
            "hdmi_i2c_controller_sha256", "hdmi_i2c_writer",
            "hdmi_i2c_writer_sha256"):
    print(manifest[key])
PY
)
LPDDR4B_IP=${RESOURCE_FIELDS[0]}
LPDDR4B_IP_SHA256=${RESOURCE_FIELDS[1]}
LPDDR4B_CALIBRATION=${RESOURCE_FIELDS[2]}
LPDDR4B_CALIBRATION_SHA256=${RESOURCE_FIELDS[3]}
LPDDR4B_REFERENCE_QSF=${RESOURCE_FIELDS[4]}
LPDDR4B_REFERENCE_QSF_SHA256=${RESOURCE_FIELDS[5]}
HDMI_PIXEL_PLL=${RESOURCE_FIELDS[6]}
HDMI_PIXEL_PLL_SHA256=${RESOURCE_FIELDS[7]}
HDMI_AUDIO_PLL=${RESOURCE_FIELDS[8]}
HDMI_AUDIO_PLL_SHA256=${RESOURCE_FIELDS[9]}
HDMI_I2C_CONFIG=${RESOURCE_FIELDS[10]}
HDMI_I2C_CONFIG_SHA256=${RESOURCE_FIELDS[11]}
HDMI_I2C_CONTROLLER=${RESOURCE_FIELDS[12]}
HDMI_I2C_CONTROLLER_SHA256=${RESOURCE_FIELDS[13]}
HDMI_I2C_WRITER=${RESOURCE_FIELDS[14]}
HDMI_I2C_WRITER_SHA256=${RESOURCE_FIELDS[15]}
extract_resource "$LPDDR4B_IP" "$INCOMING/ip/qsys_top/astra_lpddr4b.ip" \
    "$LPDDR4B_IP_SHA256"
extract_resource "$LPDDR4B_CALIBRATION" \
    "$INCOMING/axil_driver_calibration.sv" "$LPDDR4B_CALIBRATION_SHA256"
extract_resource "$LPDDR4B_REFERENCE_QSF" \
    "$INCOMING/vendor_reference/lpddr4b_golden_top.qsf" \
    "$LPDDR4B_REFERENCE_QSF_SHA256"

mkdir -p "$INCOMING/astra/graphics" "$INCOMING/astra/audio" \
    "$INCOMING/astra/common" "$INCOMING/vendor_hdmi"
cp "$ROOT"/fpga/arty/graphics/*.sv "$INCOMING/astra/graphics/"
cp "$ROOT/fpga/arty/graphics/astra_render_protocol.vh" \
    "$ROOT/fpga/arty/graphics/post_fonts.hex" "$INCOMING/astra/graphics/"
cp "$ROOT/fpga/arty/audio/astra_hdmi_audio.sv" "$INCOMING/astra/audio/"
cp "$ROOT/fpga/arty/common/astra_async_fifo.sv" \
    "$ROOT/fpga/arty/common/astra_front_panel.sv" \
    "$ROOT/fpga/arty/rtl/astra_front_panel_axi.sv" \
    "$ROOT/fpga/common/astra_i2s_transmitter.sv" \
    "$ROOT/third_party/hdl-util-hdmi/video_timing.sv" \
    "$ROOT/fpga/de25/astra_de25_graphics.sv" "$INCOMING/astra/common/"
cp "$ROOT/fpga/de25/astra_de25.dawf" "$INCOMING/"
extract_resource "$HDMI_PIXEL_PLL" "$INCOMING/vendor_hdmi/sys_pll.ip" \
    "$HDMI_PIXEL_PLL_SHA256"
extract_resource "$HDMI_AUDIO_PLL" "$INCOMING/vendor_hdmi/av_pll.ip" \
    "$HDMI_AUDIO_PLL_SHA256"
extract_resource "$HDMI_I2C_CONFIG" \
    "$INCOMING/vendor_hdmi/I2C_HDMI_Config.v" "$HDMI_I2C_CONFIG_SHA256"
extract_resource "$HDMI_I2C_CONTROLLER" \
    "$INCOMING/vendor_hdmi/I2C_Controller.v" "$HDMI_I2C_CONTROLLER_SHA256"
extract_resource "$HDMI_I2C_WRITER" \
    "$INCOMING/vendor_hdmi/I2C_WRITE_WDATA.v" "$HDMI_I2C_WRITER_SHA256"
python3 "$ROOT/fpga/de25/patch_vendor_hdmi.py" \
    "$INCOMING/vendor_hdmi/I2C_HDMI_Config.v"

{
    printf '%s\n' \
        'set_global_assignment -name SEARCH_PATH astra/graphics' \
        'set_global_assignment -name IP_FILE vendor_hdmi/sys_pll.ip' \
        'set_global_assignment -name IP_FILE vendor_hdmi/av_pll.ip' \
        'set_global_assignment -name VERILOG_FILE vendor_hdmi/I2C_HDMI_Config.v' \
        'set_global_assignment -name VERILOG_FILE vendor_hdmi/I2C_Controller.v' \
        'set_global_assignment -name VERILOG_FILE vendor_hdmi/I2C_WRITE_WDATA.v'
    for source in "$INCOMING"/astra/graphics/*.sv \
                  "$INCOMING"/astra/audio/*.sv \
                  "$INCOMING"/astra/common/*.sv; do
        printf 'set_global_assignment -name SYSTEMVERILOG_FILE %s\n' \
            "${source#"$INCOMING"/}"
    done
} >>"$INCOMING/golden_top.qsf"

(
    cd "$INCOMING"
    "$QUARTUS_ROOT/sopc_builder/bin/qsys-script" \
        --quartus-project=golden_top --package-version=26.1 \
        --cmd='set ::ASTRA_TARGET_QSYS qsys_top.qsys; set ::ASTRA_LPDDR4B_IP ip/qsys_top/astra_lpddr4b.ip' \
        --script="$ROOT/fpga/de25/add_lpddr4b.tcl"
)
python3 "$ROOT/fpga/de25/patch_vendor_top.py" \
    "$INCOMING/golden_top.v" "$INCOMING/golden_top.qsf" \
    "$INCOMING/ghrd_timing.sdc"

compile_de25_tree "$INCOMING"
(
    cd "$INCOMING"
    "$QUARTUS_BIN/quartus_pfg" -c output_files/golden_top.sof \
        output_files/golden_top_boot.rbf \
        -o hps_path=u-boot-spl-dtb.hex -o hps=1
    "$QUARTUS_BIN/quartus_pfg" -c output_files/golden_top_hps.sof \
        output_files/astra68.hps.jic \
        -o device=MT25QU128 -o flash_loader=A5EB013BB23BE4SCS \
        -o mode=ASX4
    test -s output_files/golden_top_boot.core.rbf
    test -s output_files/astra68.hps.jic
    rm -f output_files/golden_top_boot.hps.rbf
)
mapfile -t ASTRA_INPUTS < <(cd "$INCOMING" && find astra vendor_hdmi -type f | sort)
record_de25_build "$INCOMING" golden_top.v golden_top.qsf qsys_top.qsys \
    astra_de25.dawf axil_driver_calibration.sv ip/qsys_top/astra_lpddr4b.ip \
    vendor_reference/lpddr4b_golden_top.qsf \
    "${ASTRA_INPUTS[@]}" \
    output_files/golden_top_boot.core.rbf output_files/astra68.hps.jic
publish_de25_tree "$INCOMING" "$OUT"
trap - EXIT
echo "DE25 Astra shell build: PASS"
