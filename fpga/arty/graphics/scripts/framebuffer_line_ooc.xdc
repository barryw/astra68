create_clock -name build_clk -period 5.000 [get_ports build_clk]
create_clock -name pixel_clk -period 13.468 [get_ports pixel_clk]

# The line store is true dual-clock RAM. Slot ownership crosses separately in
# the parent scheduler, so there is no combinational timing path between these
# clock domains inside the builder.
set_clock_groups -asynchronous \
    -group [get_clocks build_clk] \
    -group [get_clocks pixel_clk]
