if {![info exists ::env(ASTRA_OOC_DCP)] ||
    ![info exists ::env(ASTRA_OOC_OUT)]} {
    puts "ASTRA_OOC_PHYSOPT ERROR ASTRA_OOC_DCP and ASTRA_OOC_OUT are required"
    exit 3
}

set input_dcp [file normalize $::env(ASTRA_OOC_DCP)]
set output_dir [file normalize $::env(ASTRA_OOC_OUT)]
set directive Explore
if {[info exists ::env(ASTRA_OOC_PHYSOPT_DIRECTIVE)]} {
    set directive $::env(ASTRA_OOC_PHYSOPT_DIRECTIVE)
}
file mkdir $output_dir

open_checkpoint $input_dcp
set before_path [get_timing_paths -quiet -delay_type max -max_paths 1 -nworst 1]
set before_hold [get_timing_paths -quiet -delay_type min -max_paths 1 -nworst 1]
if {[llength $before_path] == 0 || [llength $before_hold] == 0} {
    puts "ASTRA_OOC_PHYSOPT ERROR timing paths are missing"
    exit 3
}
puts "ASTRA_OOC_PHYSOPT before_setup_slack_ns=[get_property SLACK $before_path]"
puts "ASTRA_OOC_PHYSOPT before_hold_slack_ns=[get_property SLACK $before_hold]"
puts "ASTRA_OOC_PHYSOPT directive=$directive"

# Match the post-route step in Vivado's Performance_ExplorePostRoutePhysOpt
# implementation strategy by default, while allowing measured checkpoints to
# exercise another documented directive without duplicating this flow.
phys_opt_design -directive $directive

report_utilization -file [file join $output_dir utilization.rpt]
report_timing_summary -delay_type min_max -max_paths 20 \
    -file [file join $output_dir timing_summary.rpt]
report_methodology -file [file join $output_dir methodology.rpt]
report_route_status -file [file join $output_dir route_status.rpt]
write_checkpoint -force [file join $output_dir post_route_physopt.dcp]

set after_path [get_timing_paths -quiet -delay_type max -max_paths 1 -nworst 1]
set after_hold [get_timing_paths -quiet -delay_type min -max_paths 1 -nworst 1]
if {[llength $after_path] == 0 || [llength $after_hold] == 0} {
    puts "ASTRA_OOC_PHYSOPT ERROR optimized timing paths are missing"
    exit 3
}
set after_slack [get_property SLACK $after_path]
set after_hold_slack [get_property SLACK $after_hold]
puts "ASTRA_OOC_PHYSOPT after_setup_slack_ns=$after_slack"
puts "ASTRA_OOC_PHYSOPT after_hold_slack_ns=$after_hold_slack"
if {$after_slack < 0.0 || $after_hold_slack < 0.0} {
    exit 2
}
