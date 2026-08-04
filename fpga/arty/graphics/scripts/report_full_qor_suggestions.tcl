# Report implementation-specific QoR suggestions for a routed graphics design.
#
# Required environment:
#   ASTRA_DCP         routed design checkpoint
#   ASTRA_QOR_REPORT  destination text report
#   ASTRA_RQS_FILE    destination reusable suggestion file

foreach required {ASTRA_DCP ASTRA_QOR_REPORT ASTRA_RQS_FILE} {
    if {![info exists ::env($required)] || $::env($required) eq ""} {
        puts stderr "missing required environment variable $required"
        exit 2
    }
}

open_checkpoint $::env(ASTRA_DCP)
report_qor_suggestions -file $::env(ASTRA_QOR_REPORT)
write_qor_suggestions -force $::env(ASTRA_RQS_FILE)

puts "ASTRA_QOR report=$::env(ASTRA_QOR_REPORT)"
puts "ASTRA_QOR rqs=$::env(ASTRA_RQS_FILE)"
exit 0
