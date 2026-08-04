#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
VIVADO_SETTINGS=${VIVADO_SETTINGS:-/tools/Xilinx/Vivado/2024.2/settings64.sh}
ASTRA_OOC_COMPONENT=${ASTRA_OOC_COMPONENT:-tile-span}

python3 "$SCRIPT_DIR/protocol/generate_protocol.py"

if [[ ! -f "$VIVADO_SETTINGS" ]]; then
    echo "Vivado settings not found: $VIVADO_SETTINGS" >&2
    exit 2
fi

# shellcheck disable=SC1090
source "$VIVADO_SETTINGS"

case "$ASTRA_OOC_COMPONENT" in
    tile-span)
        OOC_SCRIPT=synth_tile_span_ooc.tcl
        ;;
    tile-line)
        OOC_SCRIPT=synth_tile_line_ooc.tcl
        ;;
    framebuffer-line)
        OOC_SCRIPT=synth_framebuffer_line_ooc.tcl
        ;;
    sprite-scene)
        OOC_SCRIPT=synth_sprite_scene_ooc.tcl
        ;;
    sprite-line)
        OOC_SCRIPT=synth_sprite_line_ooc.tcl
        ;;
    compositor)
        OOC_SCRIPT=synth_compositor_ooc.tcl
        ;;
    control)
        OOC_SCRIPT=synth_control_ooc.tcl
        ;;
    render-blitter)
        OOC_SCRIPT=synth_render_blitter_ooc.tcl
        ;;
    render-command)
        OOC_SCRIPT=synth_render_command_ooc.tcl
        ;;
    *)
        echo "Unsupported ASTRA_OOC_COMPONENT: $ASTRA_OOC_COMPONENT" >&2
        exit 2
        ;;
esac

exec vivado -mode batch -nolog -nojournal \
    -source "$SCRIPT_DIR/scripts/$OOC_SCRIPT"
