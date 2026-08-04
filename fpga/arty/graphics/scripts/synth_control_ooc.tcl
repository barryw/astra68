set script_dir [file dirname [file normalize [info script]]]
set graphics_dir [file normalize [file join $script_dir ..]]

if {[info exists ::env(ASTRA_OOC_OUT)]} {
    set output_dir [file normalize $::env(ASTRA_OOC_OUT)]
} else {
    set output_dir [file normalize [file join $graphics_dir ../../../build/arty-graphics/control-ooc]]
}

file mkdir $output_dir
read_verilog -sv [file join $graphics_dir astra_framebuffer_config_validator.sv]
read_verilog -sv [file join $graphics_dir astra_tile_config_validator.sv]
read_verilog -sv [file join $graphics_dir astra_graphics_control.sv]
read_xdc [file join $script_dir control_ooc.xdc]
synth_design -top astra_graphics_control -part xc7z020clg400-1 \
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
    [file join $output_dir astra_graphics_control_routed.dcp]

set path [get_timing_paths -quiet -group control_clk \
    -delay_type max -max_paths 1 -nworst 1]
if {[llength $path] == 0} {
    puts "ASTRA_CONTROL_OOC ERROR no control-clock timing path"
    exit 3
}
set slack [get_property SLACK $path]
puts "ASTRA_CONTROL_OOC period_ns=5.000 setup_slack_ns=$slack"
if {$slack < 0.0} {
    exit 2
}
