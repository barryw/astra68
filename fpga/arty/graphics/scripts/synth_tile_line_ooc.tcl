set script_dir [file dirname [file normalize [info script]]]
set graphics_dir [file normalize [file join $script_dir ..]]

if {[info exists ::env(ASTRA_OOC_OUT)]} {
    set output_dir [file normalize $::env(ASTRA_OOC_OUT)]
} else {
    set output_dir [file normalize [file join $graphics_dir ../../../build/arty-graphics/tile-line-ooc]]
}

file mkdir $output_dir
read_verilog -sv [file join $graphics_dir astra_tile_span_walker.sv]
read_verilog -sv [file join $graphics_dir astra_tile_line_store.sv]
read_verilog -sv [file join $graphics_dir astra_tile_config_validator.sv]
read_verilog -sv [file join $graphics_dir astra_tile_line_builder.sv]
read_xdc [file join $script_dir tile_line_ooc.xdc]
synth_design -top astra_tile_line_builder -part xc7z020clg400-1 \
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
    [file join $output_dir astra_tile_line_builder_routed.dcp]

set build_path [get_timing_paths -quiet -group build_clk \
    -delay_type max -max_paths 1 -nworst 1]
set pixel_path [get_timing_paths -quiet -group pixel_clk \
    -delay_type max -max_paths 1 -nworst 1]
if {[llength $build_path] == 0} {
    puts "ASTRA_TILE_LINE_OOC ERROR no build-clock timing path"
    exit 3
}
set build_slack [get_property SLACK $build_path]
if {[llength $pixel_path] == 0} {
    set pixel_slack "N/A"
} else {
    set pixel_slack [get_property SLACK $pixel_path]
}
puts "ASTRA_TILE_LINE_OOC build_period_ns=5.000 build_setup_slack_ns=$build_slack"
puts "ASTRA_TILE_LINE_OOC pixel_period_ns=13.468 pixel_setup_slack_ns=$pixel_slack"
if {$build_slack < 0.0 ||
    ([llength $pixel_path] != 0 && $pixel_slack < 0.0)} {
    exit 2
}
