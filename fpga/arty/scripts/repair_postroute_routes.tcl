# Post-route phys_opt_design normally reroutes every connection it changes.
# Guard bit generation against the rare case where an AXI/IP net remains
# incomplete while preserving all timing-clean routes.
set incomplete_nets [get_nets -quiet -hierarchical -filter {
    ROUTE_STATUS == "UNROUTED" || ROUTE_STATUS == "PARTIAL"
}]
if {[llength $incomplete_nets] != 0} {
    puts "ASTRA_ARTY_GRAPHICS repairing [llength $incomplete_nets] post-physopt net(s)"
    route_design -preserve
    set incomplete_nets [get_nets -quiet -hierarchical -filter {
        ROUTE_STATUS == "UNROUTED" || ROUTE_STATUS == "PARTIAL"
    }]
    if {[llength $incomplete_nets] != 0} {
        error "post-physopt route repair left [llength $incomplete_nets] incomplete net(s)"
    }
}
