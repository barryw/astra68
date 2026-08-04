if {$argc != 1} {
    puts "usage: vivado -mode batch -source report_dsp_pipeline.tcl -tclargs <checkpoint>"
    exit 1
}

open_checkpoint [lindex $argv 0]

set dsp_cells [lsort [get_cells -hier -quiet -filter {REF_NAME == DSP48E1}]]
puts "ASTRA_DSP_PIPELINE count=[llength $dsp_cells]"
foreach cell $dsp_cells {
    puts [format "ASTRA_DSP_PIPELINE cell=%s AREG=%s BREG=%s MREG=%s PREG=%s" \
        $cell \
        [get_property AREG $cell] \
        [get_property BREG $cell] \
        [get_property MREG $cell] \
        [get_property PREG $cell]]
}

close_design
