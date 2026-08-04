set script_dir [file dirname [file normalize [info script]]]
set graphics_dir [file normalize [file join $script_dir ..]]

if {[info exists ::env(ASTRA_OOC_OUT)]} {
    set output_dir [file normalize $::env(ASTRA_OOC_OUT)]
} else {
    set output_dir [file normalize [file join $graphics_dir ../../../build/arty-graphics/copper-ooc]]
}

file mkdir $output_dir
read_verilog -sv [file join $graphics_dir astra_copper.sv]
read_xdc [file join $script_dir copper_ooc.xdc]
synth_design -top astra_copper -part xc7z020clg400-1 \
    -mode out_of_context -flatten_hierarchy rebuilt

set bram36_cells [get_cells -hier -quiet -filter {REF_NAME =~ RAMB36*}]
set bram18_cells [get_cells -hier -quiet -filter {REF_NAME =~ RAMB18*}]
set lutram_cells [get_cells -hier -quiet -filter {PRIMITIVE_SUBGROUP == distributed}]
if {[llength $bram36_cells] != 16 || [llength $bram18_cells] != 0} {
    puts "ASTRA_COPPER_OOC ERROR expected 16 RAMB36 and 0 RAMB18; got RAMB36=[llength $bram36_cells] RAMB18=[llength $bram18_cells]"
    exit 4
}

opt_design
place_design
phys_opt_design
route_design

report_utilization -file [file join $output_dir utilization.rpt]
report_utilization -hierarchical -hierarchical_depth 4 \
    -file [file join $output_dir utilization_hierarchical.rpt]
report_timing_summary -delay_type min_max -max_paths 20 \
    -file [file join $output_dir timing_summary.rpt]
report_methodology -file [file join $output_dir methodology.rpt]
report_route_status -file [file join $output_dir route_status.rpt]
write_checkpoint -force [file join $output_dir astra_copper_routed.dcp]

set path [get_timing_paths -quiet -group copper_clk \
    -delay_type max -max_paths 1 -nworst 1]
if {[llength $path] == 0} {
    puts "ASTRA_COPPER_OOC ERROR no copper-clock timing path"
    exit 3
}
set slack [get_property SLACK $path]
puts "ASTRA_COPPER_OOC RAMB36=[llength $bram36_cells] RAMB18=[llength $bram18_cells] LUTRAM=[llength $lutram_cells] period_ns=6.000 setup_slack_ns=$slack"
if {$slack < 0.0} {
    exit 2
}
