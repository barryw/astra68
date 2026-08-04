if {$argc != 2} {
    puts stderr "usage: report_failing_paths.tcl ROUTED_DCP OUTPUT_REPORT"
    exit 1
}

set checkpoint [file normalize [lindex $argv 0]]
set report_file [file normalize [lindex $argv 1]]

open_checkpoint $checkpoint
set failing_paths [get_timing_paths -quiet -delay_type max \
    -slack_lesser_than 0.0 -max_paths 100000 -nworst 1]
set failing_count [llength $failing_paths]

puts "ASTRA_FAILING_PATHS count=$failing_count"
if {$failing_count > 0} {
    report_timing -delay_type max -input_pins -nets \
        -significant_digits 3 -sort_by group \
        -max_paths $failing_count -nworst 1 -slack_lesser_than 0.0 \
        -file $report_file
}

exit 0
