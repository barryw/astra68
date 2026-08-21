# Fresh Astra Arty Z7-20 PS + fixed 1280x720p60 HDMI build.

set script_dir [file dirname [file normalize [info script]]]
set repo_root [file normalize [file join $script_dir ../../..]]
if {[info exists ::env(ASTRA_ARTY_OUT)]} {
    set out_dir [file normalize $::env(ASTRA_ARTY_OUT)]
} else {
    set out_dir [file normalize [file join $repo_root build arty-720p]]
}

set part xc7z020clg400-1
set board digilentinc.com:arty-z7-20:part0:1.1
set project_dir [file join $out_dir project]

file mkdir $out_dir
create_project -force astra_arty_720p $project_dir -part $part
set_property board_part $board [current_project]

create_bd_design astra_ps
create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7 ps7
apply_bd_automation -rule xilinx.com:bd_rule:processing_system7 \
    -config {make_external "FIXED_IO, DDR" apply_board_preset "1" Master "Disable" Slave "Disable"} \
    [get_bd_cells ps7]
set_property -dict [list \
    CONFIG.PCW_EN_CLK0_PORT {1} \
    CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ {100.000000} \
    CONFIG.PCW_USE_M_AXI_GP0 {0} \
] [get_bd_cells ps7]

make_bd_pins_external -name fclk_clk0 [get_bd_pins ps7/FCLK_CLK0]
make_bd_pins_external -name fclk_resetn [get_bd_pins ps7/FCLK_RESET0_N]
regenerate_bd_layout
validate_bd_design
save_bd_design

set bd_file [get_files -quiet *astra_ps.bd]
generate_target all $bd_file
make_wrapper -files $bd_file -top
set wrapper [lindex [glob [file join $project_dir astra_arty_720p.gen sources_1 bd astra_ps hdl astra_ps_wrapper.v]] 0]
add_files -norecurse $wrapper

set hdmi_dir [file join $repo_root third_party hdl-util-hdmi]
add_files -norecurse [list \
    [file join $repo_root fpga arty rtl astra_720p_pattern.sv] \
    [file join $repo_root fpga arty rtl astra_arty_720p_top.sv] \
[file join $hdmi_dir hdmi.sv] \
[file join $hdmi_dir hdmi_mode_control.sv] \
    [file join $hdmi_dir tmds_channel.sv] \
    [file join $hdmi_dir serializer.sv] \
    [file join $hdmi_dir packet_assembler.sv] \
    [file join $hdmi_dir packet_picker.sv] \
    [file join $hdmi_dir audio_clock_regeneration_packet.sv] \
    [file join $hdmi_dir audio_info_frame.sv] \
    [file join $hdmi_dir audio_sample_packet.sv] \
    [file join $hdmi_dir auxiliary_video_information_info_frame.sv] \
    [file join $hdmi_dir source_product_description_info_frame.sv] \
]
add_files -fileset constrs_1 -norecurse \
    [file join $repo_root fpga arty constraints astra_arty_720p.xdc]

set_property top astra_arty_720p_top [current_fileset]
set_property -name {STEPS.SYNTH_DESIGN.ARGS.MORE OPTIONS} \
    -value {-verilog_define SYNTHESIS=1} \
    -objects [get_runs synth_1]
set_property INCREMENTAL_CHECKPOINT "" [get_runs synth_1]
update_compile_order -fileset sources_1

launch_runs impl_1 -to_step write_bitstream -jobs 8
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {
    puts "ASTRA_ARTY_720P ERROR implementation did not complete"
    puts "STATUS: [get_property STATUS [get_runs impl_1]]"
    exit 1
}

open_run impl_1
report_timing_summary -delay_type min_max -max_paths 20 \
    -file [file join $out_dir timing_summary.rpt]
report_utilization -file [file join $out_dir utilization.rpt]
report_methodology -file [file join $out_dir methodology.rpt]
report_route_status -file [file join $out_dir route_status.rpt]
report_clock_utilization -file [file join $out_dir clock_utilization.rpt]
write_checkpoint -force [file join $out_dir astra_arty_720p_routed.dcp]

set bit_file [lindex [glob [file join $project_dir astra_arty_720p.runs impl_1 *.bit]] 0]
file copy -force $bit_file [file join $out_dir astra_arty_720p.bit]
write_hw_platform -fixed -include_bit -force \
    [file join $out_dir astra_arty_720p.xsa]

set worst_setup [get_timing_paths -quiet -delay_type max -max_paths 1 -nworst 1]
set worst_hold [get_timing_paths -quiet -delay_type min -max_paths 1 -nworst 1]
if {[llength $worst_setup] == 0 || [llength $worst_hold] == 0} {
    puts "ASTRA_ARTY_720P ERROR timing paths are missing"
    exit 2
}
set setup_slack [get_property SLACK $worst_setup]
set hold_slack [get_property SLACK $worst_hold]
puts "ASTRA_ARTY_720P setup_slack_ns=$setup_slack hold_slack_ns=$hold_slack"
if {$setup_slack < 0.0 || $hold_slack < 0.0} {
    puts "ASTRA_ARTY_720P ERROR routed timing failed"
    exit 2
}

puts "ASTRA_ARTY_720P PASS bitstream=[file join $out_dir astra_arty_720p.bit]"
exit 0
