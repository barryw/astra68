if {$argc != 2} {
    puts stderr "usage: report_hierarchical_utilization.tcl DCP OUTPUT"
    exit 2
}

set dcp [lindex $argv 0]
set output [lindex $argv 1]
open_checkpoint $dcp
report_utilization -hierarchical -hierarchical_depth 8 -file $output
close_design
