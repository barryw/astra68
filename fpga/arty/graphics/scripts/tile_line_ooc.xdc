create_clock -name build_clk -period 5.000 [get_ports build_clk]
create_clock -name pixel_clk -period 13.468 [get_ports pixel_clk]

# Scanout sees a line slot only after the owning control path transfers it.
# Pixel RAM is intentionally dual-clock; there is no direct logic CDC path.
set_clock_groups -asynchronous \
    -group [get_clocks build_clk] \
    -group [get_clocks pixel_clk]
