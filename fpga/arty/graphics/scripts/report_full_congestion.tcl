# Report the physical causes of congestion in one routed graphics checkpoint.
foreach required {ASTRA_DCP ASTRA_REPORT_DIR} {
    if {![info exists ::env($required)] || $::env($required) eq ""} {
        puts stderr "missing required environment variable $required"
        exit 2
    }
}

file mkdir $::env(ASTRA_REPORT_DIR)
open_checkpoint $::env(ASTRA_DCP)
report_utilization -hierarchical -hierarchical_depth 8 \
    -file [file join $::env(ASTRA_REPORT_DIR) hierarchical_utilization.rpt]
report_control_sets -verbose \
    -file [file join $::env(ASTRA_REPORT_DIR) control_sets.rpt]
report_high_fanout_nets -load_types -max_nets 200 \
    -file [file join $::env(ASTRA_REPORT_DIR) high_fanout.rpt]
report_design_analysis -complexity -hierarchical_depth 4 \
    -file [file join $::env(ASTRA_REPORT_DIR) complexity.rpt]
report_design_analysis -congestion \
    -file [file join $::env(ASTRA_REPORT_DIR) congestion.rpt]
report_qor_suggestions \
    -file [file join $::env(ASTRA_REPORT_DIR) qor_suggestions.rpt]
write_qor_suggestions -force \
    [file join $::env(ASTRA_REPORT_DIR) qor_suggestions.rqs]
exit 0
