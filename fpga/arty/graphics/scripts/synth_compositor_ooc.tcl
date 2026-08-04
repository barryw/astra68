set script_dir [file dirname [file normalize [info script]]]
set graphics_dir [file normalize [file join $script_dir ..]]

if {[info exists ::env(ASTRA_OOC_OUT)]} {
    set output_dir [file normalize $::env(ASTRA_OOC_OUT)]
} else {
    set output_dir [file normalize [file join $graphics_dir ../../../build/arty-graphics/compositor-ooc]]
}

file mkdir $output_dir
read_verilog -sv [file join $graphics_dir astra_pixel_compositor.sv]
read_xdc [file join $script_dir compositor_ooc.xdc]
synth_design -top astra_pixel_compositor -part xc7z020clg400-1 \
    -mode out_of_context -flatten_hierarchy rebuilt

opt_design
place_design
phys_opt_design
route_design

report_utilization -file [file join $output_dir utilization.rpt]
report_timing_summary -delay_type min_max -max_paths 20 \
    -file [file join $output_dir timing_summary.rpt]
report_methodology -file [file join $output_dir methodology.rpt]
write_checkpoint -force \
    [file join $output_dir astra_pixel_compositor_routed.dcp]

set path [get_timing_paths -quiet -group pixel_clk \
    -delay_type max -max_paths 1 -nworst 1]
if {[llength $path] == 0} {
    puts "ASTRA_COMPOSITOR_OOC ERROR no pixel-clock timing path"
    exit 3
}
set slack [get_property SLACK $path]
puts "ASTRA_COMPOSITOR_OOC period_ns=13.468 setup_slack_ns=$slack"
if {$slack < 0.0} {
    exit 2
}
