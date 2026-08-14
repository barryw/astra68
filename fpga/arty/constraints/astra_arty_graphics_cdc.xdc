# The audio sample clock is a registered divide-by-1000 of the exact 48 MHz
# MMCM output. Naming it explicitly keeps the FIFO and HDMI CDC paths visible
# to timing and CDC reports.
create_generated_clock -name clk_audio_sample \
    -source [get_pins audio_clock_raw_q_reg/C] -divide_by 1000 \
    [get_pins audio_sample_buf_i/O]

# The PS build/control, HDMI pixel, and audio domains communicate only through
# the synchronizers and asynchronous FIFO audited by report_cdc.
set_clock_groups -asynchronous \
    -group [get_clocks clk_fpga_1] \
    -group [get_clocks clk_pixel_raw] \
    -group [get_clocks {clk_audio_48m_raw clk_audio_sample}]
