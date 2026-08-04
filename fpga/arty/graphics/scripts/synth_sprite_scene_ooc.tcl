set script_dir [file dirname [file normalize [info script]]]
set graphics_dir [file normalize [file join $script_dir ..]]

if {[info exists ::env(ASTRA_OOC_OUT)]} {
    set output_dir [file normalize $::env(ASTRA_OOC_OUT)]
} else {
    set output_dir [file normalize [file join $graphics_dir ../../../build/arty-graphics/sprite-scene-ooc]]
}

file mkdir $output_dir
read_verilog -sv [file join $graphics_dir astra_sprite_scene_store.sv]
read_xdc [file join $script_dir sprite_scene_ooc.xdc]
synth_design -top astra_sprite_scene_store -part xc7z020clg400-1 \
    -mode out_of_context -flatten_hierarchy rebuilt

set bram_cells [get_cells -hier -quiet -filter {REF_NAME =~ RAMB*}]
if {[llength $bram_cells] == 0} {
    puts "ASTRA_SPRITE_SCENE_OOC ERROR no block RAM primitives inferred"
    exit 4
}

opt_design
place_design
phys_opt_design
route_design

report_utilization -file [file join $output_dir utilization.rpt]
report_timing_summary -delay_type min_max -max_paths 20 \
    -file [file join $output_dir timing_summary.rpt]
report_methodology -file [file join $output_dir methodology.rpt]
report_route_status -file [file join $output_dir route_status.rpt]
write_checkpoint -force \
    [file join $output_dir astra_sprite_scene_store_routed.dcp]

set build_path [get_timing_paths -quiet -group build_clk \
    -delay_type max -max_paths 1 -nworst 1]
if {[llength $build_path] == 0} {
    puts "ASTRA_SPRITE_SCENE_OOC ERROR no build-clock timing path"
    exit 3
}
set build_slack [get_property SLACK $build_path]
puts "ASTRA_SPRITE_SCENE_OOC bram_primitives=[llength $bram_cells] build_period_ns=5.000 build_setup_slack_ns=$build_slack"
if {$build_slack < 0.0} {
    exit 2
}
