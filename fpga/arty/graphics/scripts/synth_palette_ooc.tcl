if {$argc != 2} {
    puts stderr "usage: synth_palette_ooc.tcl REPO_ROOT OUTPUT_DIR"
    exit 2
}

set repo_root [file normalize [lindex $argv 0]]
set output_dir [file normalize [lindex $argv 1]]
file mkdir $output_dir

read_verilog -sv [file join $repo_root fpga arty common astra_async_fifo.sv]
read_verilog -sv [file join $repo_root fpga arty graphics astra_palette_store.sv]
synth_design -top astra_palette_store -part xc7z020clg400-1
report_utilization -hierarchical -hierarchical_depth 4 \
    -file [file join $output_dir utilization.rpt]
write_checkpoint -force [file join $output_dir palette_ooc.dcp]
