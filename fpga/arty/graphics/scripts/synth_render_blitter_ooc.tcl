set script_dir [file dirname [file normalize [info script]]]
set graphics_dir [file normalize [file join $script_dir ..]]

if {[info exists ::env(ASTRA_OOC_OUT)]} {
    set output_dir [file normalize $::env(ASTRA_OOC_OUT)]
} else {
    set output_dir [file normalize [file join $graphics_dir ../../../build/arty-graphics/render-blitter-ooc]]
}

file mkdir $output_dir
read_verilog -sv [file join $graphics_dir astra_render_copy_burst.sv]
read_verilog -sv [file join $graphics_dir astra_render_blitter.sv]
read_xdc [file join $script_dir render_blitter_ooc.xdc]
synth_design -top astra_render_blitter -part xc7z020clg400-1 \
    -mode out_of_context -flatten_hierarchy rebuilt

opt_design
place_design
phys_opt_design
route_design

report_utilization -file [file join $output_dir utilization.rpt]
report_timing_summary -delay_type min_max -max_paths 50 \
    -file [file join $output_dir timing_summary.rpt]
report_timing -delay_type max -max_paths 50 -nworst 10 \
    -sort_by group -input_pins -file [file join $output_dir timing_paths.rpt]
report_methodology -file [file join $output_dir methodology.rpt]
report_route_status -file [file join $output_dir route_status.rpt]
write_checkpoint -force \
    [file join $output_dir astra_render_blitter_routed.dcp]

set path [get_timing_paths -quiet -group render_clk \
    -delay_type max -max_paths 1 -nworst 1]
if {[llength $path] == 0} {
    puts "ASTRA_RENDER_BLITTER_OOC ERROR no render-clock timing path"
    exit 3
}
set slack [get_property SLACK $path]
set startpoint [get_property STARTPOINT_PIN $path]
set endpoint [get_property ENDPOINT_PIN $path]
puts "ASTRA_RENDER_BLITTER_OOC period_ns=5.000 setup_slack_ns=$slack start=$startpoint end=$endpoint"
if {$slack < 0.0} {
    exit 2
}
