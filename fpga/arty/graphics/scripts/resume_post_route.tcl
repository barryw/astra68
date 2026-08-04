if {![info exists ::env(ASTRA_ROUTE_DCP)] ||
    ![info exists ::env(ASTRA_ROUTE_OUT)]} {
    puts "ASTRA_ROUTE_RECOVERY ERROR ASTRA_ROUTE_DCP and ASTRA_ROUTE_OUT are required"
    exit 3
}

set input_dcp [file normalize $::env(ASTRA_ROUTE_DCP)]
set output_dir [file normalize $::env(ASTRA_ROUTE_OUT)]
file mkdir $output_dir

open_checkpoint $input_dcp

set before_setup [get_timing_paths -quiet -delay_type max -max_paths 1 -nworst 1]
set before_hold [get_timing_paths -quiet -delay_type min -max_paths 1 -nworst 1]
if {[llength $before_setup] == 0 || [llength $before_hold] == 0} {
    puts "ASTRA_ROUTE_RECOVERY ERROR timing paths are missing"
    exit 3
}

puts "ASTRA_ROUTE_RECOVERY before_setup_slack_ns=[get_property SLACK $before_setup]"
puts "ASTRA_ROUTE_RECOVERY before_hold_slack_ns=[get_property SLACK $before_hold]"
report_timing_summary -delay_type min_max -max_paths 50 \
    -file [file join $output_dir timing_before.rpt]

# Post-route physical optimization can legally change placement and routing.
# Vivado normally repairs those changes itself, but a run can occasionally
# leave an incomplete route in the saved checkpoint. UG835 defines
# route_design -preserve for this case: retain every completed route and route
# only the incomplete connections before timing and bitstream acceptance.
route_design -preserve

report_timing_summary -delay_type min_max -max_paths 50 \
    -file [file join $output_dir timing_after.rpt]
report_utilization -file [file join $output_dir utilization.rpt]
report_methodology -file [file join $output_dir methodology.rpt]
report_route_status -file [file join $output_dir route_status.rpt]
write_checkpoint -force [file join $output_dir post_route_recovered.dcp]

set after_setup [get_timing_paths -quiet -delay_type max -max_paths 1 -nworst 1]
set after_hold [get_timing_paths -quiet -delay_type min -max_paths 1 -nworst 1]
if {[llength $after_setup] == 0 || [llength $after_hold] == 0} {
    puts "ASTRA_ROUTE_RECOVERY ERROR optimized timing paths are missing"
    exit 3
}

set setup_slack [get_property SLACK $after_setup]
set hold_slack [get_property SLACK $after_hold]
puts "ASTRA_ROUTE_RECOVERY after_setup_slack_ns=$setup_slack"
puts "ASTRA_ROUTE_RECOVERY after_hold_slack_ns=$hold_slack"

if {$setup_slack < 0.0 || $hold_slack < 0.0} {
    puts "ASTRA_ROUTE_RECOVERY ERROR routed timing failed"
    exit 2
}

write_bitstream -force [file join $output_dir astra_arty_graphics.bit]
puts "ASTRA_ROUTE_RECOVERY PASS bitstream=[file join $output_dir astra_arty_graphics.bit]"
exit 0
