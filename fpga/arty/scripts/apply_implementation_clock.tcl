# Tighten placement/route pressure without changing the PS runtime clock.
set render_clock [get_clocks clk_fpga_1]
create_clock -name clk_fpga_1 \
    -period [expr {1000000000.0 / $::env(ASTRA_ARTY_IMPLEMENT_FREQ_HZ)}] \
    [get_pins [get_property SOURCE_PINS $render_clock]]
puts "ASTRA_ARTY_GRAPHICS implementation clock period_ns=[get_property PERIOD [get_clocks clk_fpga_1]]"
