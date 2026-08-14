# Report the placed resource footprint of the stable graphics boundaries.
# Usage: vivado -mode batch -source report_partition_footprints.tcl \
#        -tclargs routed.dcp output.txt

if {$argc != 2} {
    puts "usage: report_partition_footprints.tcl ROUTED_DCP OUTPUT"
    exit 3
}

set checkpoint [file normalize [lindex $argv 0]]
set output [file normalize [lindex $argv 1]]
open_checkpoint $checkpoint

proc report_footprint {stream label root} {
    set root_cell [get_cells -quiet $root]
    if {[llength $root_cell] != 1} {
        puts $stream "$label ERROR root_not_found $root"
        return
    }

    set cells [get_cells -quiet -hierarchical -filter \
        "IS_PRIMITIVE && NAME =~ $root/*"]
    puts $stream "$label ROOT=$root PRIMITIVES=[llength $cells]"

    foreach kind {SLICE RAMB18 RAMB36 DSP48E1} {
        set count 0
        set min_x 1000000
        set min_y 1000000
        set max_x -1
        set max_y -1
        foreach cell $cells {
            set loc [get_property LOC $cell]
            if {[regexp "^${kind}_X([0-9]+)Y([0-9]+)$" $loc -> x y]} {
                incr count
                if {$x < $min_x} { set min_x $x }
                if {$x > $max_x} { set max_x $x }
                if {$y < $min_y} { set min_y $y }
                if {$y > $max_y} { set max_y $y }
            }
        }
        if {$count != 0} {
            puts $stream "$label $kind COUNT=$count RANGE=${kind}_X${min_x}Y${min_y}:${kind}_X${max_x}Y${max_y}"
        }
    }
}

set stream [open $output w]
puts $stream "CHECKPOINT=$checkpoint"
foreach item {
    {RENDER pipeline_i/render_command_i}
    {RENDER_HP2_SLICE ps_i/astra_ps_i/hp2_render_slice}
    {RENDER_HP3_SLICE ps_i/astra_ps_i/hp3_render_slice}
    {SPRITE pipeline_i/sprite_builder_i}
    {COPPER_CONTROL pipeline_i/copper_control_i}
    {COPPER_EVENTS pipeline_i/copper_pixel_events_i}
} {
    report_footprint $stream [lindex $item 0] [lindex $item 1]
}
close $stream
exit 0
