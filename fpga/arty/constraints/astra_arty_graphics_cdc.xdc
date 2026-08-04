# The PS FCLK1 build/control domain and the HDMI MMCM pixel domain have no
# common primary clock. Every crossing between them is implemented by the
# synchronizers and bundled-toggle protocols audited by report_cdc.
set_clock_groups -asynchronous \
    -group [get_clocks clk_fpga_1] \
    -group [get_clocks clk_pixel_raw]
