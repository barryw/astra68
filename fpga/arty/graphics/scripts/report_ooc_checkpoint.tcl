if {![info exists ::env(ASTRA_OOC_DCP)] ||
    ![info exists ::env(ASTRA_OOC_OUT)]} {
    puts "ASTRA_OOC_REPORT ERROR ASTRA_OOC_DCP and ASTRA_OOC_OUT are required"
    exit 3
}

set input_dcp [file normalize $::env(ASTRA_OOC_DCP)]
set output_dir [file normalize $::env(ASTRA_OOC_OUT)]
file mkdir $output_dir

open_checkpoint $input_dcp
report_timing -delay_type max -max_paths 50 -nworst 1 -sort_by group \
    -path_type full -input_pins -nets \
    -file [file join $output_dir timing_paths.rpt]
report_timing_summary -delay_type min_max -max_paths 50 \
    -file [file join $output_dir timing_summary.rpt]

set paths [get_timing_paths -quiet -delay_type max -max_paths 50 -nworst 1]
if {[llength $paths] == 0} {
    puts "ASTRA_OOC_REPORT ERROR no setup timing paths"
    exit 3
}

set index 0
foreach path $paths {
    incr index
    puts [format \
        "ASTRA_OOC_PATH index=%d slack_ns=%s requirement_ns=%s datapath_ns=%s logic_levels=%s start=%s end=%s" \
        $index \
        [get_property SLACK $path] \
        [get_property REQUIREMENT $path] \
        [get_property DATAPATH_DELAY $path] \
        [get_property LOGIC_LEVELS $path] \
        [get_property STARTPOINT_PIN $path] \
        [get_property ENDPOINT_PIN $path]]
}
