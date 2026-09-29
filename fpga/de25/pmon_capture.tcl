# Run PMON library commands against the running DE25 shell.
# ASTRA_DE25_SOF: the exact running SOF; PMON_CMD: ';'-separated commands,
# e.g. "pmon_set wo 0 0;pmon_reset_counter_data 0 0" or "pmon_read 0 0".
for {set t 0} {$t < 30} {incr t} {
    set devices [lsearch -all -inline -glob [get_service_paths device] "*#DE25-Nano"]
    if {[llength $devices] == 1} break
    after 1000
}
set design [design_load $::env(ASTRA_DE25_SOF)]
design_link [design_instantiate $design] [lindex $devices 0]
source $::env(PMON_LIBRARY)
foreach command [split $::env(PMON_CMD) ";"] {
    puts ">> $command"
    eval $command
}
exit
