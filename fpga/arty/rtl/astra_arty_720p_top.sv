// Copyright (c) 2026 Astra68 contributors
//
// Astra-owned Arty Z7-20 processing-system and fixed 1280x720p60 HDMI shell.
`timescale 1ns/1ps
`default_nettype none

module astra_arty_720p_top (
    output wire [3:0]  led,

    output wire        hdmi_tx_clk_p,
    output wire        hdmi_tx_clk_n,
    output wire [2:0]  hdmi_tx_d_p,
    output wire [2:0]  hdmi_tx_d_n,
    input  wire        hdmi_tx_hpdn,

    inout  wire [14:0] DDR_addr,
    inout  wire [2:0]  DDR_ba,
    inout  wire        DDR_cas_n,
    inout  wire        DDR_ck_n,
    inout  wire        DDR_ck_p,
    inout  wire        DDR_cke,
    inout  wire        DDR_cs_n,
    inout  wire [3:0]  DDR_dm,
    inout  wire [31:0] DDR_dq,
    inout  wire [3:0]  DDR_dqs_n,
    inout  wire [3:0]  DDR_dqs_p,
    inout  wire        DDR_odt,
    inout  wire        DDR_ras_n,
    inout  wire        DDR_reset_n,
    inout  wire        DDR_we_n,
    inout  wire        FIXED_IO_ddr_vrn,
    inout  wire        FIXED_IO_ddr_vrp,
    inout  wire [53:0] FIXED_IO_mio,
    inout  wire        FIXED_IO_ps_clk,
    inout  wire        FIXED_IO_ps_porb,
    inout  wire        FIXED_IO_ps_srstb
);
    wire fclk_clk0;
    wire fclk_resetn;

    astra_ps_wrapper ps_i (
        .DDR_addr(DDR_addr),
        .DDR_ba(DDR_ba),
        .DDR_cas_n(DDR_cas_n),
        .DDR_ck_n(DDR_ck_n),
        .DDR_ck_p(DDR_ck_p),
        .DDR_cke(DDR_cke),
        .DDR_cs_n(DDR_cs_n),
        .DDR_dm(DDR_dm),
        .DDR_dq(DDR_dq),
        .DDR_dqs_n(DDR_dqs_n),
        .DDR_dqs_p(DDR_dqs_p),
        .DDR_odt(DDR_odt),
        .DDR_ras_n(DDR_ras_n),
        .DDR_reset_n(DDR_reset_n),
        .DDR_we_n(DDR_we_n),
        .FIXED_IO_ddr_vrn(FIXED_IO_ddr_vrn),
        .FIXED_IO_ddr_vrp(FIXED_IO_ddr_vrp),
        .FIXED_IO_mio(FIXED_IO_mio),
        .FIXED_IO_ps_clk(FIXED_IO_ps_clk),
        .FIXED_IO_ps_porb(FIXED_IO_ps_porb),
        .FIXED_IO_ps_srstb(FIXED_IO_ps_srstb),
        .fclk_clk0(fclk_clk0),
        .fclk_resetn(fclk_resetn)
    );

    // UG472 permits the 0.125 feedback multiplier used here. A 100 MHz PS
    // fabric clock produces a 742.5 MHz VCO exactly:
    //   100 * 37.125 / 5 = 742.5 MHz
    // Dividers 10 and 2 produce the exact VIC-4 pixel and 5x DDR clocks.
    wire clk_feedback;
    wire clk_feedback_buf;
    wire clk_pixel_raw;
    wire clk_tmds_raw;
    wire clk_pixel;
    wire clk_tmds_x5;
    wire video_locked;

    MMCME2_BASE #(
        .BANDWIDTH("OPTIMIZED"),
        .CLKIN1_PERIOD(10.000),
        .DIVCLK_DIVIDE(5),
        .CLKFBOUT_MULT_F(37.125),
        .CLKOUT0_DIVIDE_F(2.000),
        .CLKOUT1_DIVIDE(10),
        .CLKOUT0_DUTY_CYCLE(0.5),
        .CLKOUT1_DUTY_CYCLE(0.5),
        .CLKOUT0_PHASE(0.0),
        .CLKOUT1_PHASE(0.0),
        .CLKFBOUT_PHASE(0.0),
        .STARTUP_WAIT("FALSE")
    ) video_mmcm_i (
        .CLKIN1(fclk_clk0),
        .CLKFBIN(clk_feedback_buf),
        .CLKFBOUT(clk_feedback),
        .CLKFBOUTB(),
        .CLKOUT0(clk_tmds_raw),
        .CLKOUT0B(),
        .CLKOUT1(clk_pixel_raw),
        .CLKOUT1B(),
        .CLKOUT2(),
        .CLKOUT2B(),
        .CLKOUT3(),
        .CLKOUT3B(),
        .CLKOUT4(),
        .CLKOUT5(),
        .CLKOUT6(),
        .LOCKED(video_locked),
        .PWRDWN(1'b0),
        .RST(~fclk_resetn)
    );

    BUFG feedback_buf_i (.I(clk_feedback), .O(clk_feedback_buf));
    BUFG pixel_buf_i    (.I(clk_pixel_raw), .O(clk_pixel));
    BUFG tmds_buf_i     (.I(clk_tmds_raw), .O(clk_tmds_x5));

    // The MMCM is held in reset whenever fclk_resetn is low, so LOCKED is the
    // sole asynchronous reset source. Keeping it direct avoids a glitchable
    // LUT on the preset pins while retaining asynchronous assertion and a
    // four-cycle synchronous release in the pixel domain.
    (* ASYNC_REG = "TRUE" *) reg [3:0] reset_sync = 4'hf;
    always @(posedge clk_pixel or negedge video_locked) begin
        if (!video_locked)
            reset_sync <= 4'hf;
        else
            reset_sync <= {reset_sync[2:0], 1'b0};
    end
    wire video_reset = reset_sync[3];

    wire [2:0] tmds;
    wire tmds_clock;
    wire [10:0] cx;
    wire [9:0] cy;
    wire [10:0] frame_width;
    wire [9:0] frame_height;
    wire [10:0] screen_width;
    wire [9:0] screen_height;
    wire [23:0] raster_rgb;

    astra_720p_pattern pattern_i (
        .x(cx),
        .y(cy),
        .rgb(raster_rgb)
    );

    // The first shell qualifies video transport only. DVI mode carries the
    // same RGB/TMDS raster over the HDMI connector without inventing an audio
    // clock before the PCM and wavetable engines exist.
    hdmi #(
        .VIDEO_ID_CODE(4),
        .IT_CONTENT(1'b1),
        .DVI_OUTPUT(1'b1),
        .VIDEO_REFRESH_RATE_MILLIHZ(60000),
        .START_X(0),
        .START_Y(0)
    ) hdmi_i (
        .clk_pixel_x5(clk_tmds_x5),
        .clk_pixel(clk_pixel),
        .clk_audio(1'b0),
        .reset(video_reset),
        .hdmi_output_enable(1'b0),
        .rgb(raster_rgb),
        .audio_sample_word(32'd0),
        .tmds(tmds),
        .tmds_clock(tmds_clock),
        .cx(cx),
        .cy(cy),
        .frame_width(frame_width),
        .frame_height(frame_height),
        .screen_width(screen_width),
        .screen_height(screen_height),
        .hdmi_output_active()
    );

    OBUFDS hdmi_clock_buf_i (
        .I(tmds_clock),
        .O(hdmi_tx_clk_p),
        .OB(hdmi_tx_clk_n)
    );

    genvar lane;
    generate
        for (lane = 0; lane < 3; lane = lane + 1) begin : g_hdmi_lane
            OBUFDS hdmi_data_buf_i (
                .I(tmds[lane]),
                .O(hdmi_tx_d_p[lane]),
                .OB(hdmi_tx_d_n[lane])
            );
        end
    endgenerate

    reg [7:0] frame_counter = 8'd0;
    always @(posedge clk_pixel) begin
        if (video_reset)
            frame_counter <= 8'd0;
        else if (cx == 11'd0 && cy == 10'd0)
            frame_counter <= frame_counter + 1'b1;
    end

    assign led[0] = video_locked;
    assign led[1] = ~video_reset;
    assign led[2] = frame_counter[5];
    assign led[3] = ~hdmi_tx_hpdn;

    wire unused_status = &{
        1'b0,
        frame_width,
        frame_height,
        screen_width,
        screen_height
    };
endmodule

`default_nettype wire
