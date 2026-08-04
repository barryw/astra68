# Generate and compile the Zynq-7000 FSBL from an exact Astra graphics XSA.

if {$argc != 2} {
    puts stderr "usage: build_fsbl.tcl <hardware.xsa> <output-directory>"
    exit 2
}

set xsa [file normalize [lindex $argv 0]]
set out_dir [file normalize [lindex $argv 1]]

if {![file isfile $xsa]} {
    puts stderr "FSBL XSA does not exist: $xsa"
    exit 2
}
if {[file exists $out_dir]} {
    puts stderr "FSBL output already exists: $out_dir"
    exit 2
}

file mkdir $out_dir
hsi open_hw_design $xsa

set processors [hsi get_cells -filter {IP_TYPE == PROCESSOR}]
if {[lsearch -exact $processors ps7_cortexa9_0] < 0} {
    puts stderr "XSA does not contain ps7_cortexa9_0: $processors"
    exit 1
}

hsi create_sw_design astra_fsbl \
    -proc ps7_cortexa9_0 -os standalone -app zynq_fsbl
hsi generate_app -app zynq_fsbl -dir $out_dir -compile

if {![file isfile [file join $out_dir executable.elf]]} {
    puts stderr "Vitis did not produce executable.elf"
    exit 1
}

hsi close_sw_design [hsi current_sw_design]
hsi close_hw_design [hsi current_hw_design]
puts "ASTRA_ARTY_FSBL_GENERATE PASS $out_dir/executable.elf"
