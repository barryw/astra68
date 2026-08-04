# Reset the Arty Z7 processing system and allow the boot ROM to restart from
# the board's selected boot device. This script does not program PL or load an
# executable; it is intended for serial-captured boot qualification.

connect
after 500
targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
rst -srst
after 1000
exit
