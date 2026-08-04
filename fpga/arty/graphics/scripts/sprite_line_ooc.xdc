create_clock -name build_clk -period 5.000 [get_ports build_clk]
create_clock -name pixel_clk -period 13.468 [get_ports pixel_clk]

# The sprite line store is true dual-clock RAM. Slot ownership crosses in the
# parent scheduler, outside this out-of-context builder checkpoint.
set_clock_groups -asynchronous \
    -group [get_clocks build_clk] \
    -group [get_clocks pixel_clk]
