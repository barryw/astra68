set sof $::env(ASTRA_DE25_SOF)
if {![file isfile $sof]} {
    error "missing Astra DE25 SOF: $sof"
}

after 10000
set devices [lsearch -all -inline -glob \
    [get_service_paths device] "*#DE25-Nano"]
if {[llength $devices] == 0} {
    puts stderr "waiting for DE25-Nano JTAG device"
    exit 2
}
if {[llength $devices] != 1} {
    error "expected one DE25-Nano JTAG device, found [llength $devices]"
}

set design [design_load $sof]
set instance [design_instantiate $design]
design_link $instance [lindex $devices 0]
puts "DE25 running shell: PASS $sof"
exit
