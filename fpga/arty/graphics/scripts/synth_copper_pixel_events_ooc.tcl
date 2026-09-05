set script_dir [file dirname [file normalize [info script]]]
set repo_root [file normalize [file join $script_dir .. .. .. ..]]
set graphics_dir [file join $repo_root fpga arty graphics]
set output_dir [lindex $argv 0]
if {$output_dir eq ""} {
    error "usage: vivado -mode batch -source synth_copper_pixel_events_ooc.tcl -tclargs OUTPUT_DIR"
}
file mkdir $output_dir

read_verilog -sv [file join $repo_root fpga arty common astra_async_fifo.sv]
read_verilog -sv [file join $graphics_dir astra_copper_pixel_events.sv]
synth_design -top astra_copper_pixel_events -part xc7z020clg400-1 \
    -mode out_of_context -flatten_hierarchy rebuilt
create_clock -name build_clk -period 6.000 [get_ports build_clk]
create_clock -name pixel_clk -period 13.468 [get_ports pixel_clk]
set_clock_groups -asynchronous -group [get_clocks build_clk] \
    -group [get_clocks pixel_clk]
opt_design
place_design
phys_opt_design
route_design
report_utilization -file [file join $output_dir utilization.rpt]
report_utilization -hierarchical -hierarchical_depth 4 \
    -file [file join $output_dir utilization_hierarchical.rpt]
report_timing_summary -delay_type min_max -max_paths 20 \
    -file [file join $output_dir timing_summary.rpt]
report_route_status -file [file join $output_dir route_status.rpt]
write_checkpoint -force \
    [file join $output_dir astra_copper_pixel_events_routed.dcp]
