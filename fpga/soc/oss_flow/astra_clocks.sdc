# Explicit constraints keep split placement/routing runs equivalent to the
# canonical one-shot flow. Generated PLL constraints are not serialized in a
# placed nextpnr JSON file.
create_clock -name cpu_clk -period 80.000 [get_nets {$glbnet$clk}]
create_clock -name sd_input_clk -period 50.000 [get_nets {$glbnet$sd_clk_in}]
create_clock -name sdram_clk -period 16.666 [get_nets {$glbnet$sdram_domain_clk}]
create_clock -name pixel_clk -period 37.037 [get_nets {$glbnet$video_pixel_clk}]
create_clock -name shift_clk -period 7.407 [get_nets {$glbnet$video_shift_clk}]
create_clock -name usb_phy_clk -period 20.833 [get_nets {$glbnet$usb_phy_domain_clk}]
create_clock -name board_clk -period 40.000 [get_nets {$glbnet$clk25_mhz$TRELLIS_IO_IN}]
