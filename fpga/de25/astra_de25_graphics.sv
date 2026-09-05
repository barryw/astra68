// Copyright (c) 2026 Astra68 contributors
//
// DE25-Nano adapter for the shared Astra graphics, audio, and panel engines.
`timescale 1ns/1ps
`default_nettype none

module astra_de25_graphics (
    input  wire        clock_50,
    input  wire        build_clk,
    input  wire        reset_n,
    input  wire [1:0]  buttons,
    input  wire [3:0]  switches,
    output wire [7:0]  leds,
    output wire        render_interrupt,

    output wire        hdmi_tx_clk,
    output reg         hdmi_tx_hs,
    output reg         hdmi_tx_vs,
    output reg  [23:0] hdmi_tx_d,
    output reg         hdmi_tx_de,
    output wire        hdmi_lrclk,
    output wire        hdmi_mclk,
    output wire        hdmi_sclk,
    output wire        hdmi_i2s,
    output wire        hdmi_i2c_scl,
    inout  wire        hdmi_i2c_sda,
    input  wire        hdmi_tx_int,

    input  wire [15:0] control_awaddr,
    input  wire [2:0]  control_awprot,
    input  wire        control_awvalid,
    output wire        control_awready,
    input  wire [31:0] control_wdata,
    input  wire [3:0]  control_wstrb,
    input  wire        control_wvalid,
    output wire        control_wready,
    output wire [1:0]  control_bresp,
    output wire        control_bvalid,
    input  wire        control_bready,
    input  wire [15:0] control_araddr,
    input  wire [2:0]  control_arprot,
    input  wire        control_arvalid,
    output wire        control_arready,
    output wire [31:0] control_rdata,
    output wire [1:0]  control_rresp,
    output wire        control_rvalid,
    input  wire        control_rready,

    output wire        fb_arid,
    output wire [31:0] fb_araddr,
    output wire [7:0]  fb_arlen,
    output wire [2:0]  fb_arsize,
    output wire [1:0]  fb_arburst,
    output wire [3:0]  fb_arcache,
    output wire [2:0]  fb_arprot,
    output wire        fb_arvalid,
    input  wire        fb_arready,
    input  wire        fb_rid,
    input  wire [63:0] fb_rdata,
    input  wire [1:0]  fb_rresp,
    input  wire        fb_rlast,
    input  wire        fb_rvalid,
    output wire        fb_rready,

    output wire [1:0]  scene_arid,
    output wire [31:0] scene_araddr,
    output wire [7:0]  scene_arlen,
    output wire [2:0]  scene_arsize,
    output wire [1:0]  scene_arburst,
    output wire [3:0]  scene_arcache,
    output wire [2:0]  scene_arprot,
    output wire        scene_arvalid,
    input  wire        scene_arready,
    input  wire [1:0]  scene_rid,
    input  wire [63:0] scene_rdata,
    input  wire [1:0]  scene_rresp,
    input  wire        scene_rlast,
    input  wire        scene_rvalid,
    output wire        scene_rready,

    output wire [2:0]  render_awid,
    output wire [31:0] render_awaddr,
    output wire [7:0]  render_awlen,
    output wire [2:0]  render_awsize,
    output wire [1:0]  render_awburst,
    output wire [3:0]  render_awcache,
    output wire [2:0]  render_awprot,
    output wire        render_awvalid,
    input  wire        render_awready,
    output wire [63:0] render_wdata,
    output wire [7:0]  render_wstrb,
    output wire        render_wlast,
    output wire        render_wvalid,
    input  wire        render_wready,
    input  wire [2:0]  render_bid,
    input  wire [1:0]  render_bresp,
    input  wire        render_bvalid,
    output wire        render_bready,
    output wire [2:0]  render_arid,
    output wire [31:0] render_araddr,
    output wire [7:0]  render_arlen,
    output wire [2:0]  render_arsize,
    output wire [1:0]  render_arburst,
    output wire [3:0]  render_arcache,
    output wire [2:0]  render_arprot,
    output wire        render_arvalid,
    input  wire        render_arready,
    input  wire [2:0]  render_rid,
    input  wire [63:0] render_rdata,
    input  wire [1:0]  render_rresp,
    input  wire        render_rlast,
    input  wire        render_rvalid,
    output wire        render_rready
);
    wire build_reset = ~reset_n;
    wire pixel_clk;
    wire audio_mclk;
    wire pixel_locked;
    wire audio_locked;

    sys_pll pixel_pll_i (
        .refclk(clock_50), .rst(build_reset), .outclk_0(pixel_clk),
        .locked(pixel_locked)
    );
    av_pll audio_pll_i (
        .refclk(clock_50), .rst(build_reset), .outclk_0(audio_mclk),
        .locked(audio_locked)
    );

    (* ASYNC_REG = "TRUE" *) reg [3:0] pixel_reset_sync_q = 4'hf;
    (* ASYNC_REG = "TRUE" *) reg [3:0] audio_reset_sync_q = 4'hf;
    always @(posedge pixel_clk or negedge pixel_locked) begin
        if (!pixel_locked)
            pixel_reset_sync_q <= 4'hf;
        else
            pixel_reset_sync_q <= {pixel_reset_sync_q[2:0], 1'b0};
    end
    always @(posedge audio_mclk or negedge audio_locked) begin
        if (!audio_locked)
            audio_reset_sync_q <= 4'hf;
        else
            audio_reset_sync_q <= {audio_reset_sync_q[2:0], 1'b0};
    end
    wire pixel_reset = pixel_reset_sync_q[3];
    wire audio_reset = audio_reset_sync_q[3];
    assign hdmi_tx_clk = pixel_clk;

    wire [10:0] pixel_x;
    wire [9:0] pixel_y;
    wire [10:0] frame_width;
    wire [9:0] frame_height;
    wire [10:0] screen_width;
    wire [9:0] screen_height;
    wire hsync;
    wire vsync;
    wire video_active;
    video_timing #(.VIDEO_ID_CODE(4)) timing_i (
        .clk_pixel(pixel_clk), .reset(pixel_reset),
        .cx(pixel_x), .cy(pixel_y),
        .frame_width(frame_width), .frame_height(frame_height),
        .screen_width(screen_width), .screen_height(screen_height),
        .hsync(hsync), .vsync(vsync), .video_data_period(video_active)
    );

    wire pipeline_valid;
    wire [23:0] pipeline_rgb;
    // ADV7513 captures on the rising pixel-clock edge. Launching the parallel
    // bus on the falling edge provides the data-sheet setup and hold window.
    always @(negedge pixel_clk) begin
        if (pixel_reset) begin
            hdmi_tx_hs <= 1'b0;
            hdmi_tx_vs <= 1'b0;
            hdmi_tx_d <= 24'd0;
            hdmi_tx_de <= 1'b0;
        end else begin
            hdmi_tx_hs <= hsync;
            hdmi_tx_vs <= vsync;
            hdmi_tx_d <= pipeline_valid ? pipeline_rgb : 24'd0;
            hdmi_tx_de <= video_active;
        end
    end

    wire hdmi_ready;
    (* ASYNC_REG = "TRUE" *) reg [1:0] hdmi_int_sync_q = 2'b11;
    always @(posedge clock_50 or negedge reset_n) begin
        if (!reset_n)
            hdmi_int_sync_q <= 2'b11;
        else
            hdmi_int_sync_q <= {hdmi_int_sync_q[0], hdmi_tx_int};
    end
    I2C_HDMI_Config hdmi_config_i (
        .iCLK(clock_50), .iRST_N(reset_n),
        .I2C_SCLK(hdmi_i2c_scl), .I2C_SDAT(hdmi_i2c_sda),
        .HDMI_TX_INT(hdmi_int_sync_q[1]), .READY(hdmi_ready)
    );

    wire [1:0][23:0] audio_sample_word;
    wire hdmi_output_requested;
    wire audio_sample_clk;
    wire audio_lrclk;
    wire [3:0] audio_i2s;
    astra_i2s_transmitter i2s_i (
        .mclk(audio_mclk), .reset(audio_reset),
        .sample_word(audio_sample_word), .bclk(hdmi_sclk),
        .sample_clk(audio_sample_clk), .lrclk(audio_lrclk), .i2s(audio_i2s)
    );
    assign hdmi_mclk = audio_mclk;
    assign hdmi_lrclk = audio_lrclk;
    assign hdmi_i2s = audio_i2s[0];

    wire [31:0] host_awaddr = {16'd0, control_awaddr};
    wire [31:0] host_araddr = {16'd0, control_araddr};
    wire [31:0] graphics_awaddr, graphics_araddr;
    wire [2:0] graphics_awprot, graphics_arprot;
    wire graphics_awvalid, graphics_awready;
    wire [31:0] graphics_wdata;
    wire [3:0] graphics_wstrb;
    wire graphics_wvalid, graphics_wready;
    wire [1:0] graphics_bresp;
    wire graphics_bvalid, graphics_bready;
    wire graphics_arvalid, graphics_arready;
    wire [31:0] graphics_rdata;
    wire [1:0] graphics_rresp;
    wire graphics_rvalid, graphics_rready;
    wire [31:0] peripheral_awaddr, peripheral_araddr;
    wire [2:0] peripheral_awprot, peripheral_arprot;
    wire peripheral_awvalid, peripheral_awready;
    wire [31:0] peripheral_wdata;
    wire [3:0] peripheral_wstrb;
    wire peripheral_wvalid, peripheral_wready;
    wire [1:0] peripheral_bresp;
    wire peripheral_bvalid, peripheral_bready;
    wire peripheral_arvalid, peripheral_arready;
    wire [31:0] peripheral_rdata;
    wire [1:0] peripheral_rresp;
    wire peripheral_rvalid, peripheral_rready;
    wire [31:0] audio_awaddr, audio_araddr;
    wire [2:0] audio_awprot, audio_arprot;
    wire audio_awvalid, audio_awready;
    wire [31:0] audio_wdata;
    wire [3:0] audio_wstrb;
    wire audio_wvalid, audio_wready;
    wire [1:0] audio_bresp;
    wire audio_bvalid, audio_bready;
    wire audio_arvalid, audio_arready;
    wire [31:0] audio_rdata;
    wire [1:0] audio_rresp;
    wire audio_rvalid, audio_rready;
    wire [31:0] panel_awaddr, panel_araddr;
    wire [2:0] panel_awprot, panel_arprot;
    wire panel_awvalid, panel_awready;
    wire [31:0] panel_wdata;
    wire [3:0] panel_wstrb;
    wire panel_wvalid, panel_wready;
    wire [1:0] panel_bresp;
    wire panel_bvalid, panel_bready;
    wire panel_arvalid, panel_arready;
    wire [31:0] panel_rdata;
    wire [1:0] panel_rresp;
    wire panel_rvalid, panel_rready;

    astra_axi_lite_1to2 #(
        .SLAVE1_MASK(32'h0000ff00), .SLAVE1_VALUE(32'h00006000),
        .SLAVE1_ALT_MASK(32'h0000ff00), .SLAVE1_ALT_VALUE(32'h00007000)
    ) control_split_i (
        .clk(build_clk), .reset(build_reset),
        .s_awaddr(host_awaddr), .s_awprot(control_awprot),
        .s_awvalid(control_awvalid), .s_awready(control_awready),
        .s_wdata(control_wdata), .s_wstrb(control_wstrb),
        .s_wvalid(control_wvalid), .s_wready(control_wready),
        .s_bresp(control_bresp), .s_bvalid(control_bvalid),
        .s_bready(control_bready), .s_araddr(host_araddr),
        .s_arprot(control_arprot), .s_arvalid(control_arvalid),
        .s_arready(control_arready), .s_rdata(control_rdata),
        .s_rresp(control_rresp), .s_rvalid(control_rvalid),
        .s_rready(control_rready),
        .m0_awaddr(graphics_awaddr), .m0_awprot(graphics_awprot),
        .m0_awvalid(graphics_awvalid), .m0_awready(graphics_awready),
        .m0_wdata(graphics_wdata), .m0_wstrb(graphics_wstrb),
        .m0_wvalid(graphics_wvalid), .m0_wready(graphics_wready),
        .m0_bresp(graphics_bresp), .m0_bvalid(graphics_bvalid),
        .m0_bready(graphics_bready), .m0_araddr(graphics_araddr),
        .m0_arprot(graphics_arprot), .m0_arvalid(graphics_arvalid),
        .m0_arready(graphics_arready), .m0_rdata(graphics_rdata),
        .m0_rresp(graphics_rresp), .m0_rvalid(graphics_rvalid),
        .m0_rready(graphics_rready),
        .m1_awaddr(peripheral_awaddr), .m1_awprot(peripheral_awprot),
        .m1_awvalid(peripheral_awvalid), .m1_awready(peripheral_awready),
        .m1_wdata(peripheral_wdata), .m1_wstrb(peripheral_wstrb),
        .m1_wvalid(peripheral_wvalid), .m1_wready(peripheral_wready),
        .m1_bresp(peripheral_bresp), .m1_bvalid(peripheral_bvalid),
        .m1_bready(peripheral_bready), .m1_araddr(peripheral_araddr),
        .m1_arprot(peripheral_arprot), .m1_arvalid(peripheral_arvalid),
        .m1_arready(peripheral_arready), .m1_rdata(peripheral_rdata),
        .m1_rresp(peripheral_rresp), .m1_rvalid(peripheral_rvalid),
        .m1_rready(peripheral_rready)
    );

    astra_axi_lite_1to2 #(
        .SLAVE1_MASK(32'h0000ff00), .SLAVE1_VALUE(32'h00007000),
        .SLAVE1_ALT_MASK(32'hffffffff), .SLAVE1_ALT_VALUE(32'hffffffff)
    ) peripheral_split_i (
        .clk(build_clk), .reset(build_reset),
        .s_awaddr(peripheral_awaddr), .s_awprot(peripheral_awprot),
        .s_awvalid(peripheral_awvalid), .s_awready(peripheral_awready),
        .s_wdata(peripheral_wdata), .s_wstrb(peripheral_wstrb),
        .s_wvalid(peripheral_wvalid), .s_wready(peripheral_wready),
        .s_bresp(peripheral_bresp), .s_bvalid(peripheral_bvalid),
        .s_bready(peripheral_bready), .s_araddr(peripheral_araddr),
        .s_arprot(peripheral_arprot), .s_arvalid(peripheral_arvalid),
        .s_arready(peripheral_arready), .s_rdata(peripheral_rdata),
        .s_rresp(peripheral_rresp), .s_rvalid(peripheral_rvalid),
        .s_rready(peripheral_rready),
        .m0_awaddr(audio_awaddr), .m0_awprot(audio_awprot),
        .m0_awvalid(audio_awvalid), .m0_awready(audio_awready),
        .m0_wdata(audio_wdata), .m0_wstrb(audio_wstrb),
        .m0_wvalid(audio_wvalid), .m0_wready(audio_wready),
        .m0_bresp(audio_bresp), .m0_bvalid(audio_bvalid),
        .m0_bready(audio_bready), .m0_araddr(audio_araddr),
        .m0_arprot(audio_arprot), .m0_arvalid(audio_arvalid),
        .m0_arready(audio_arready), .m0_rdata(audio_rdata),
        .m0_rresp(audio_rresp), .m0_rvalid(audio_rvalid),
        .m0_rready(audio_rready),
        .m1_awaddr(panel_awaddr), .m1_awprot(panel_awprot),
        .m1_awvalid(panel_awvalid), .m1_awready(panel_awready),
        .m1_wdata(panel_wdata), .m1_wstrb(panel_wstrb),
        .m1_wvalid(panel_wvalid), .m1_wready(panel_wready),
        .m1_bresp(panel_bresp), .m1_bvalid(panel_bvalid),
        .m1_bready(panel_bready), .m1_araddr(panel_araddr),
        .m1_arprot(panel_arprot), .m1_arvalid(panel_arvalid),
        .m1_arready(panel_arready), .m1_rdata(panel_rdata),
        .m1_rresp(panel_rresp), .m1_rvalid(panel_rvalid),
        .m1_rready(panel_rready)
    );

    astra_hdmi_audio audio_i (
        .build_clk(build_clk), .build_reset(build_reset),
        .audio_clk(audio_sample_clk), .audio_reset(audio_reset),
        .audio_sample_word(audio_sample_word),
        .hdmi_output_active(hdmi_ready && hdmi_output_requested),
        .hdmi_output_requested(hdmi_output_requested),
        .s_axi_awaddr(audio_awaddr), .s_axi_awprot(audio_awprot),
        .s_axi_awvalid(audio_awvalid), .s_axi_awready(audio_awready),
        .s_axi_wdata(audio_wdata), .s_axi_wstrb(audio_wstrb),
        .s_axi_wvalid(audio_wvalid), .s_axi_wready(audio_wready),
        .s_axi_bresp(audio_bresp), .s_axi_bvalid(audio_bvalid),
        .s_axi_bready(audio_bready), .s_axi_araddr(audio_araddr),
        .s_axi_arprot(audio_arprot), .s_axi_arvalid(audio_arvalid),
        .s_axi_arready(audio_arready), .s_axi_rdata(audio_rdata),
        .s_axi_rresp(audio_rresp), .s_axi_rvalid(audio_rvalid),
        .s_axi_rready(audio_rready)
    );

    wire [2:0] fb_arid_full;
    wire [2:0] fb_rid_full = {2'd0, fb_rid};
    wire [2:0] tile0_arid, tile1_arid, sprite_arid;
    wire [31:0] tile0_araddr, tile1_araddr, sprite_araddr;
    wire [7:0] tile0_arlen, tile1_arlen, sprite_arlen;
    wire [2:0] tile0_arsize, tile1_arsize, sprite_arsize;
    wire [1:0] tile0_arburst, tile1_arburst, sprite_arburst;
    wire [3:0] tile0_arcache, tile1_arcache, sprite_arcache;
    wire [2:0] tile0_arprot, tile1_arprot, sprite_arprot;
    wire [3:0] tile0_arqos, tile1_arqos, sprite_arqos;
    wire tile0_arvalid, tile1_arvalid, sprite_arvalid;
    wire tile0_arready, tile1_arready, sprite_arready;
    wire [2:0] tile0_rid, tile1_rid, sprite_rid;
    wire [63:0] tile0_rdata, tile1_rdata, sprite_rdata;
    wire [1:0] tile0_rresp, tile1_rresp, sprite_rresp;
    wire tile0_rlast, tile1_rlast, sprite_rlast;
    wire tile0_rvalid, tile1_rvalid, sprite_rvalid;
    wire tile0_rready, tile1_rready, sprite_rready;
    wire [2:0] scene_arid_full;
    wire [2:0] scene_rid_full = {1'b0, scene_rid};
    wire [2:0] scene_client_arready;
    wire [8:0] scene_client_rid;
    wire [191:0] scene_client_rdata;
    wire [5:0] scene_client_rresp;
    wire [2:0] scene_client_rlast, scene_client_rvalid;

    assign fb_arid = fb_arid_full[0];
    assign scene_arid = scene_arid_full[1:0];
    assign {sprite_arready, tile1_arready, tile0_arready} =
        scene_client_arready;
    assign {sprite_rid, tile1_rid, tile0_rid} = scene_client_rid;
    assign {sprite_rdata, tile1_rdata, tile0_rdata} = scene_client_rdata;
    assign {sprite_rresp, tile1_rresp, tile0_rresp} = scene_client_rresp;
    assign {sprite_rlast, tile1_rlast, tile0_rlast} = scene_client_rlast;
    assign {sprite_rvalid, tile1_rvalid, tile0_rvalid} = scene_client_rvalid;

    astra_axi_read_3to1 #(.AXI_ID_WIDTH(3)) scene_arbiter_i (
        .aclk(build_clk), .aresetn(reset_n),
        .s_axi_arid({sprite_arid, tile1_arid, tile0_arid}),
        .s_axi_araddr({sprite_araddr, tile1_araddr, tile0_araddr}),
        .s_axi_arlen({sprite_arlen, tile1_arlen, tile0_arlen}),
        .s_axi_arsize({sprite_arsize, tile1_arsize, tile0_arsize}),
        .s_axi_arburst({sprite_arburst, tile1_arburst, tile0_arburst}),
        .s_axi_arcache({sprite_arcache, tile1_arcache, tile0_arcache}),
        .s_axi_arprot({sprite_arprot, tile1_arprot, tile0_arprot}),
        .s_axi_arqos({sprite_arqos, tile1_arqos, tile0_arqos}),
        .s_axi_arvalid({sprite_arvalid, tile1_arvalid, tile0_arvalid}),
        .s_axi_arready(scene_client_arready),
        .s_axi_rid(scene_client_rid), .s_axi_rdata(scene_client_rdata),
        .s_axi_rresp(scene_client_rresp), .s_axi_rlast(scene_client_rlast),
        .s_axi_rvalid(scene_client_rvalid),
        .s_axi_rready({sprite_rready, tile1_rready, tile0_rready}),
        .m_axi_arid(scene_arid_full), .m_axi_araddr(scene_araddr),
        .m_axi_arlen(scene_arlen), .m_axi_arsize(scene_arsize),
        .m_axi_arburst(scene_arburst), .m_axi_arcache(scene_arcache),
        .m_axi_arprot(scene_arprot), .m_axi_arqos(),
        .m_axi_arvalid(scene_arvalid), .m_axi_arready(scene_arready),
        .m_axi_rid(scene_rid_full), .m_axi_rdata(scene_rdata),
        .m_axi_rresp(scene_rresp), .m_axi_rlast(scene_rlast),
        .m_axi_rvalid(scene_rvalid), .m_axi_rready(scene_rready)
    );

    wire [3:0] fb_arqos;
    wire [3:0] render_arqos, render_awqos;
    wire [31:0] active_generation, lines_built, lines_failed;
    wire [31:0] scheduler_overruns, pixel_underruns;
    wire [31:0] commit_errors, commit_deferrals;
    wire [31:0] framebuffer_axi_debug_status;
    wire [31:0] framebuffer_axi_ar_accept_count;
    wire [31:0] framebuffer_axi_r_accept_count;
    wire [31:0] framebuffer_axi_last_ar_address;
    wire [31:0] framebuffer_axi_response_stall_cycles;
    wire scene_active;
    astra_graphics_pipeline #(
        .ARENA_BASE(32'h40000000), .ARENA_LIMIT(32'h80000000),
        .AXI_ID_WIDTH(3)
    ) pipeline_i (
        .build_clk(build_clk), .build_reset(build_reset),
        .pixel_clk(pixel_clk), .pixel_reset(pixel_reset),
        .pixel_x(pixel_x), .pixel_y(pixel_y),
        .pixel_output_valid(pipeline_valid), .pixel_output_rgb(pipeline_rgb),
        .active_generation(active_generation), .lines_built(lines_built),
        .lines_failed(lines_failed), .scheduler_overruns(scheduler_overruns),
        .pixel_underruns(pixel_underruns), .commit_errors(commit_errors),
        .commit_deferrals(commit_deferrals), .scene_active(scene_active),
        .render_interrupt(render_interrupt),
        .framebuffer_axi_debug_status(framebuffer_axi_debug_status),
        .framebuffer_axi_ar_accept_count(framebuffer_axi_ar_accept_count),
        .framebuffer_axi_r_accept_count(framebuffer_axi_r_accept_count),
        .framebuffer_axi_last_ar_address(framebuffer_axi_last_ar_address),
        .framebuffer_axi_response_stall_cycles(
            framebuffer_axi_response_stall_cycles),
        .s_axi_awaddr(graphics_awaddr), .s_axi_awprot(graphics_awprot),
        .s_axi_awvalid(graphics_awvalid), .s_axi_awready(graphics_awready),
        .s_axi_wdata(graphics_wdata), .s_axi_wstrb(graphics_wstrb),
        .s_axi_wvalid(graphics_wvalid), .s_axi_wready(graphics_wready),
        .s_axi_bresp(graphics_bresp), .s_axi_bvalid(graphics_bvalid),
        .s_axi_bready(graphics_bready), .s_axi_araddr(graphics_araddr),
        .s_axi_arprot(graphics_arprot), .s_axi_arvalid(graphics_arvalid),
        .s_axi_arready(graphics_arready), .s_axi_rdata(graphics_rdata),
        .s_axi_rresp(graphics_rresp), .s_axi_rvalid(graphics_rvalid),
        .s_axi_rready(graphics_rready),
        .fb_axi_arid(fb_arid_full), .fb_axi_araddr(fb_araddr),
        .fb_axi_arlen(fb_arlen), .fb_axi_arsize(fb_arsize),
        .fb_axi_arburst(fb_arburst), .fb_axi_arcache(fb_arcache),
        .fb_axi_arprot(fb_arprot), .fb_axi_arqos(fb_arqos),
        .fb_axi_arvalid(fb_arvalid), .fb_axi_arready(fb_arready),
        .fb_axi_rid(fb_rid_full), .fb_axi_rdata(fb_rdata),
        .fb_axi_rresp(fb_rresp), .fb_axi_rlast(fb_rlast),
        .fb_axi_rvalid(fb_rvalid), .fb_axi_rready(fb_rready),
        .tile0_axi_arid(tile0_arid), .tile0_axi_araddr(tile0_araddr),
        .tile0_axi_arlen(tile0_arlen), .tile0_axi_arsize(tile0_arsize),
        .tile0_axi_arburst(tile0_arburst), .tile0_axi_arcache(tile0_arcache),
        .tile0_axi_arprot(tile0_arprot), .tile0_axi_arqos(tile0_arqos),
        .tile0_axi_arvalid(tile0_arvalid), .tile0_axi_arready(tile0_arready),
        .tile0_axi_rid(tile0_rid), .tile0_axi_rdata(tile0_rdata),
        .tile0_axi_rresp(tile0_rresp), .tile0_axi_rlast(tile0_rlast),
        .tile0_axi_rvalid(tile0_rvalid), .tile0_axi_rready(tile0_rready),
        .tile1_axi_arid(tile1_arid), .tile1_axi_araddr(tile1_araddr),
        .tile1_axi_arlen(tile1_arlen), .tile1_axi_arsize(tile1_arsize),
        .tile1_axi_arburst(tile1_arburst), .tile1_axi_arcache(tile1_arcache),
        .tile1_axi_arprot(tile1_arprot), .tile1_axi_arqos(tile1_arqos),
        .tile1_axi_arvalid(tile1_arvalid), .tile1_axi_arready(tile1_arready),
        .tile1_axi_rid(tile1_rid), .tile1_axi_rdata(tile1_rdata),
        .tile1_axi_rresp(tile1_rresp), .tile1_axi_rlast(tile1_rlast),
        .tile1_axi_rvalid(tile1_rvalid), .tile1_axi_rready(tile1_rready),
        .sprite_axi_arid(sprite_arid), .sprite_axi_araddr(sprite_araddr),
        .sprite_axi_arlen(sprite_arlen), .sprite_axi_arsize(sprite_arsize),
        .sprite_axi_arburst(sprite_arburst),
        .sprite_axi_arcache(sprite_arcache), .sprite_axi_arprot(sprite_arprot),
        .sprite_axi_arqos(sprite_arqos), .sprite_axi_arvalid(sprite_arvalid),
        .sprite_axi_arready(sprite_arready), .sprite_axi_rid(sprite_rid),
        .sprite_axi_rdata(sprite_rdata), .sprite_axi_rresp(sprite_rresp),
        .sprite_axi_rlast(sprite_rlast), .sprite_axi_rvalid(sprite_rvalid),
        .sprite_axi_rready(sprite_rready),
        .render_axi_arid(render_arid), .render_axi_araddr(render_araddr),
        .render_axi_arlen(render_arlen), .render_axi_arsize(render_arsize),
        .render_axi_arburst(render_arburst), .render_axi_arcache(render_arcache),
        .render_axi_arprot(render_arprot), .render_axi_arqos(render_arqos),
        .render_axi_arvalid(render_arvalid), .render_axi_arready(render_arready),
        .render_axi_rid(render_rid), .render_axi_rdata(render_rdata),
        .render_axi_rresp(render_rresp), .render_axi_rlast(render_rlast),
        .render_axi_rvalid(render_rvalid), .render_axi_rready(render_rready),
        .render_axi_awid(render_awid), .render_axi_awaddr(render_awaddr),
        .render_axi_awlen(render_awlen), .render_axi_awsize(render_awsize),
        .render_axi_awburst(render_awburst), .render_axi_awcache(render_awcache),
        .render_axi_awprot(render_awprot), .render_axi_awqos(render_awqos),
        .render_axi_awvalid(render_awvalid), .render_axi_awready(render_awready),
        .render_axi_wdata(render_wdata), .render_axi_wstrb(render_wstrb),
        .render_axi_wlast(render_wlast), .render_axi_wvalid(render_wvalid),
        .render_axi_wready(render_wready), .render_axi_bid(render_bid),
        .render_axi_bresp(render_bresp), .render_axi_bvalid(render_bvalid),
        .render_axi_bready(render_bready)
    );

    astra_front_panel_axi #(
        .CLK_HZ(100000000), .CAPABILITIES(32'h1f040208), .ACTIVITY_LED(3)
    ) panel_i (
        .clk(build_clk), .reset(build_reset),
        .buttons({4'd0, buttons}), .switches(switches),
        .diagnostic_leds({5'd0, scene_active, ~build_reset, hdmi_ready}),
        .leds(leds),
        .s_axi_awaddr(panel_awaddr), .s_axi_awprot(panel_awprot),
        .s_axi_awvalid(panel_awvalid), .s_axi_awready(panel_awready),
        .s_axi_wdata(panel_wdata), .s_axi_wstrb(panel_wstrb),
        .s_axi_wvalid(panel_wvalid), .s_axi_wready(panel_wready),
        .s_axi_bresp(panel_bresp), .s_axi_bvalid(panel_bvalid),
        .s_axi_bready(panel_bready), .s_axi_araddr(panel_araddr),
        .s_axi_arprot(panel_arprot), .s_axi_arvalid(panel_arvalid),
        .s_axi_arready(panel_arready), .s_axi_rdata(panel_rdata),
        .s_axi_rresp(panel_rresp), .s_axi_rvalid(panel_rvalid),
        .s_axi_rready(panel_rready)
    );

    wire unused = &{1'b0, frame_width, frame_height, screen_width,
                    screen_height, fb_arid_full[2:1], scene_arid_full[2],
                    fb_arqos, render_arqos, render_awqos,
                    active_generation, lines_built, lines_failed,
                    scheduler_overruns, pixel_underruns, commit_errors,
                    commit_deferrals, audio_i2s[3:1]};
endmodule

`default_nettype wire
