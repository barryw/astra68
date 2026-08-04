// Copyright (c) 2026 Astra68 contributors
//
// Astra Arty Z7-20 production graphics integration. The Zynq PS owns DDR and
// software control; the PL owns deterministic line construction, composition,
// and fixed 1280x720p60 DVI-over-HDMI transport.
`timescale 1ns/1ps
`default_nettype none

module astra_arty_graphics_top (
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
    wire fclk_clk1;
    wire [0:0] graphics_resetn;

    wire [31:0] ctrl_araddr;
    wire [2:0] ctrl_arprot;
    wire ctrl_arready;
    wire ctrl_arvalid;
    wire [31:0] ctrl_awaddr;
    wire [2:0] ctrl_awprot;
    wire ctrl_awready;
    wire ctrl_awvalid;
    wire ctrl_bready;
    wire [1:0] ctrl_bresp;
    wire ctrl_bvalid;
    wire [31:0] ctrl_rdata;
    wire ctrl_rready;
    wire [1:0] ctrl_rresp;
    wire ctrl_rvalid;
    wire [31:0] ctrl_wdata;
    wire ctrl_wready;
    wire [3:0] ctrl_wstrb;
    wire ctrl_wvalid;

    wire [5:0] fb_arid;
    wire [31:0] fb_araddr;
    wire [7:0] fb_arlen;
    wire [2:0] fb_arsize;
    wire [1:0] fb_arburst;
    wire [3:0] fb_arcache;
    wire [2:0] fb_arprot;
    wire [3:0] fb_arqos;
    wire fb_arvalid;
    wire fb_arready;
    wire [5:0] fb_rid;
    wire [63:0] fb_rdata;
    wire [1:0] fb_rresp;
    wire fb_rlast;
    wire fb_rvalid;
    wire fb_rready;

    wire [5:0] tile0_arid;
    wire [31:0] tile0_araddr;
    wire [7:0] tile0_arlen;
    wire [2:0] tile0_arsize;
    wire [1:0] tile0_arburst;
    wire [3:0] tile0_arcache;
    wire [2:0] tile0_arprot;
    wire [3:0] tile0_arqos;
    wire tile0_arvalid;
    wire tile0_arready;
    wire [5:0] tile0_rid;
    wire [63:0] tile0_rdata;
    wire [1:0] tile0_rresp;
    wire tile0_rlast;
    wire tile0_rvalid;
    wire tile0_rready;

    wire [5:0] tile1_arid;
    wire [31:0] tile1_araddr;
    wire [7:0] tile1_arlen;
    wire [2:0] tile1_arsize;
    wire [1:0] tile1_arburst;
    wire [3:0] tile1_arcache;
    wire [2:0] tile1_arprot;
    wire [3:0] tile1_arqos;
    wire tile1_arvalid;
    wire tile1_arready;
    wire [5:0] tile1_rid;
    wire [63:0] tile1_rdata;
    wire [1:0] tile1_rresp;
    wire tile1_rlast;
    wire tile1_rvalid;
    wire tile1_rready;

    wire [5:0] sprite_arid;
    wire [31:0] sprite_araddr;
    wire [7:0] sprite_arlen;
    wire [2:0] sprite_arsize;
    wire [1:0] sprite_arburst;
    wire [3:0] sprite_arcache;
    wire [2:0] sprite_arprot;
    wire [3:0] sprite_arqos;
    wire sprite_arvalid;
    wire sprite_arready;
    wire [5:0] sprite_rid;
    wire [63:0] sprite_rdata;
    wire [1:0] sprite_rresp;
    wire sprite_rlast;
    wire sprite_rvalid;
    wire sprite_rready;

    wire [5:0] render_read_arid;
    wire [31:0] render_read_araddr;
    wire [7:0] render_read_arlen;
    wire [2:0] render_read_arsize;
    wire [1:0] render_read_arburst;
    wire [3:0] render_read_arcache;
    wire [2:0] render_read_arprot;
    wire [3:0] render_read_arqos;
    wire render_read_arvalid;
    wire render_read_arready;
    wire [5:0] render_read_rid;
    wire [63:0] render_read_rdata;
    wire [1:0] render_read_rresp;
    wire render_read_rlast;
    wire render_read_rvalid;
    wire render_read_rready;

    wire [5:0] render_write_awid;
    wire [31:0] render_write_awaddr;
    wire [7:0] render_write_awlen;
    wire [2:0] render_write_awsize;
    wire [1:0] render_write_awburst;
    wire [3:0] render_write_awcache;
    wire [2:0] render_write_awprot;
    wire [3:0] render_write_awqos;
    wire render_write_awvalid;
    wire render_write_awready;
    wire [63:0] render_write_wdata;
    wire [7:0] render_write_wstrb;
    wire render_write_wlast;
    wire render_write_wvalid;
    wire render_write_wready;
    wire [5:0] render_write_bid;
    wire [1:0] render_write_bresp;
    wire render_write_bvalid;
    wire render_write_bready;
    wire render_interrupt;

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
        .M_AXI_CTRL_araddr(ctrl_araddr),
        .M_AXI_CTRL_arprot(ctrl_arprot),
        .M_AXI_CTRL_arready(ctrl_arready),
        .M_AXI_CTRL_arvalid(ctrl_arvalid),
        .M_AXI_CTRL_awaddr(ctrl_awaddr),
        .M_AXI_CTRL_awprot(ctrl_awprot),
        .M_AXI_CTRL_awready(ctrl_awready),
        .M_AXI_CTRL_awvalid(ctrl_awvalid),
        .M_AXI_CTRL_bready(ctrl_bready),
        .M_AXI_CTRL_bresp(ctrl_bresp),
        .M_AXI_CTRL_bvalid(ctrl_bvalid),
        .M_AXI_CTRL_rdata(ctrl_rdata),
        .M_AXI_CTRL_rready(ctrl_rready),
        .M_AXI_CTRL_rresp(ctrl_rresp),
        .M_AXI_CTRL_rvalid(ctrl_rvalid),
        .M_AXI_CTRL_wdata(ctrl_wdata),
        .M_AXI_CTRL_wready(ctrl_wready),
        .M_AXI_CTRL_wstrb(ctrl_wstrb),
        .M_AXI_CTRL_wvalid(ctrl_wvalid),
        .S_AXI_FB_araddr(fb_araddr),
        .S_AXI_FB_arburst(fb_arburst),
        .S_AXI_FB_arcache(fb_arcache),
        .S_AXI_FB_arid(fb_arid),
        .S_AXI_FB_arlen(fb_arlen),
        .S_AXI_FB_arlock(1'b0),
        .S_AXI_FB_arprot(fb_arprot),
        .S_AXI_FB_arqos(fb_arqos),
        .S_AXI_FB_arready(fb_arready),
        .S_AXI_FB_arsize(fb_arsize),
        .S_AXI_FB_arvalid(fb_arvalid),
        .S_AXI_FB_rdata(fb_rdata),
        .S_AXI_FB_rid(fb_rid),
        .S_AXI_FB_rlast(fb_rlast),
        .S_AXI_FB_rready(fb_rready),
        .S_AXI_FB_rresp(fb_rresp),
        .S_AXI_FB_rvalid(fb_rvalid),
        .S_AXI_TILE0_araddr(tile0_araddr),
        .S_AXI_TILE0_arburst(tile0_arburst),
        .S_AXI_TILE0_arcache(tile0_arcache),
        .S_AXI_TILE0_arid(tile0_arid),
        .S_AXI_TILE0_arlen(tile0_arlen),
        .S_AXI_TILE0_arlock(1'b0),
        .S_AXI_TILE0_arprot(tile0_arprot),
        .S_AXI_TILE0_arqos(tile0_arqos),
        .S_AXI_TILE0_arready(tile0_arready),
        .S_AXI_TILE0_arsize(tile0_arsize),
        .S_AXI_TILE0_arvalid(tile0_arvalid),
        .S_AXI_TILE0_rdata(tile0_rdata),
        .S_AXI_TILE0_rid(tile0_rid),
        .S_AXI_TILE0_rlast(tile0_rlast),
        .S_AXI_TILE0_rready(tile0_rready),
        .S_AXI_TILE0_rresp(tile0_rresp),
        .S_AXI_TILE0_rvalid(tile0_rvalid),
        .S_AXI_TILE1_araddr(tile1_araddr),
        .S_AXI_TILE1_arburst(tile1_arburst),
        .S_AXI_TILE1_arcache(tile1_arcache),
        .S_AXI_TILE1_arid(tile1_arid),
        .S_AXI_TILE1_arlen(tile1_arlen),
        .S_AXI_TILE1_arlock(1'b0),
        .S_AXI_TILE1_arprot(tile1_arprot),
        .S_AXI_TILE1_arqos(tile1_arqos),
        .S_AXI_TILE1_arready(tile1_arready),
        .S_AXI_TILE1_arsize(tile1_arsize),
        .S_AXI_TILE1_arvalid(tile1_arvalid),
        .S_AXI_TILE1_rdata(tile1_rdata),
        .S_AXI_TILE1_rid(tile1_rid),
        .S_AXI_TILE1_rlast(tile1_rlast),
        .S_AXI_TILE1_rready(tile1_rready),
        .S_AXI_TILE1_rresp(tile1_rresp),
        .S_AXI_TILE1_rvalid(tile1_rvalid),
        .S_AXI_SPRITE_araddr(sprite_araddr),
        .S_AXI_SPRITE_arburst(sprite_arburst),
        .S_AXI_SPRITE_arcache(sprite_arcache),
        .S_AXI_SPRITE_arid(sprite_arid),
        .S_AXI_SPRITE_arlen(sprite_arlen),
        .S_AXI_SPRITE_arlock(1'b0),
        .S_AXI_SPRITE_arprot(sprite_arprot),
        .S_AXI_SPRITE_arqos(sprite_arqos),
        .S_AXI_SPRITE_arready(sprite_arready),
        .S_AXI_SPRITE_arsize(sprite_arsize),
        .S_AXI_SPRITE_arvalid(sprite_arvalid),
        .S_AXI_SPRITE_rdata(sprite_rdata),
        .S_AXI_SPRITE_rid(sprite_rid),
        .S_AXI_SPRITE_rlast(sprite_rlast),
        .S_AXI_SPRITE_rready(sprite_rready),
        .S_AXI_SPRITE_rresp(sprite_rresp),
        .S_AXI_SPRITE_rvalid(sprite_rvalid),
        .S_AXI_RENDER_READ_araddr(render_read_araddr),
        .S_AXI_RENDER_READ_arburst(render_read_arburst),
        .S_AXI_RENDER_READ_arcache(render_read_arcache),
        .S_AXI_RENDER_READ_arid(render_read_arid),
        .S_AXI_RENDER_READ_arlen(render_read_arlen),
        .S_AXI_RENDER_READ_arlock(1'b0),
        .S_AXI_RENDER_READ_arprot(render_read_arprot),
        .S_AXI_RENDER_READ_arqos(render_read_arqos),
        .S_AXI_RENDER_READ_arready(render_read_arready),
        .S_AXI_RENDER_READ_arsize(render_read_arsize),
        .S_AXI_RENDER_READ_arvalid(render_read_arvalid),
        .S_AXI_RENDER_READ_rdata(render_read_rdata),
        .S_AXI_RENDER_READ_rid(render_read_rid),
        .S_AXI_RENDER_READ_rlast(render_read_rlast),
        .S_AXI_RENDER_READ_rready(render_read_rready),
        .S_AXI_RENDER_READ_rresp(render_read_rresp),
        .S_AXI_RENDER_READ_rvalid(render_read_rvalid),
        .S_AXI_RENDER_WRITE_awaddr(render_write_awaddr),
        .S_AXI_RENDER_WRITE_awburst(render_write_awburst),
        .S_AXI_RENDER_WRITE_awcache(render_write_awcache),
        .S_AXI_RENDER_WRITE_awid(render_write_awid),
        .S_AXI_RENDER_WRITE_awlen(render_write_awlen),
        .S_AXI_RENDER_WRITE_awlock(1'b0),
        .S_AXI_RENDER_WRITE_awprot(render_write_awprot),
        .S_AXI_RENDER_WRITE_awqos(render_write_awqos),
        .S_AXI_RENDER_WRITE_awready(render_write_awready),
        .S_AXI_RENDER_WRITE_awsize(render_write_awsize),
        .S_AXI_RENDER_WRITE_awvalid(render_write_awvalid),
        .S_AXI_RENDER_WRITE_bid(render_write_bid),
        .S_AXI_RENDER_WRITE_bready(render_write_bready),
        .S_AXI_RENDER_WRITE_bresp(render_write_bresp),
        .S_AXI_RENDER_WRITE_bvalid(render_write_bvalid),
        .S_AXI_RENDER_WRITE_wdata(render_write_wdata),
        .S_AXI_RENDER_WRITE_wlast(render_write_wlast),
        .S_AXI_RENDER_WRITE_wready(render_write_wready),
        .S_AXI_RENDER_WRITE_wstrb(render_write_wstrb),
        .S_AXI_RENDER_WRITE_wvalid(render_write_wvalid),
        .fclk_clk0(fclk_clk0),
        .fclk_clk1(fclk_clk1),
        .graphics_resetn(graphics_resetn),
        .render_interrupt(render_interrupt)
    );

    // Exact 74.25/371.25 MHz clocks from the qualified transport shell.
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
        .RST(~graphics_resetn[0])
    );

    BUFG feedback_buf_i (.I(clk_feedback), .O(clk_feedback_buf));
    BUFG pixel_buf_i    (.I(clk_pixel_raw), .O(clk_pixel));
    BUFG tmds_buf_i     (.I(clk_tmds_raw), .O(clk_tmds_x5));

    (* ASYNC_REG = "TRUE" *) reg [3:0] reset_sync = 4'hf;
    always @(posedge clk_pixel or negedge video_locked) begin
        if (!video_locked)
            reset_sync <= 4'hf;
        else
            reset_sync <= {reset_sync[2:0], 1'b0};
    end
    wire video_reset = reset_sync[3];
    wire build_reset = ~graphics_resetn[0];

    wire [2:0] tmds;
    wire tmds_clock;
    wire [10:0] cx;
    wire [9:0] cy;
    wire [10:0] frame_width;
    wire [9:0] frame_height;
    wire [10:0] screen_width;
    wire [9:0] screen_height;
    wire pipeline_valid;
    wire [23:0] pipeline_rgb;
    wire [23:0] raster_rgb = pipeline_valid ? pipeline_rgb : 24'd0;
    wire [31:0] active_generation;
    wire [31:0] lines_built;
    wire [31:0] lines_failed;
    wire [31:0] scheduler_overruns;
    wire [31:0] pixel_underruns;
    wire [31:0] commit_errors;
    wire [31:0] commit_deferrals;
    wire scene_active;

    astra_graphics_pipeline pipeline_i (
        .build_clk(fclk_clk1),
        .build_reset(build_reset),
        .pixel_clk(clk_pixel),
        .pixel_reset(video_reset),
        .pixel_x(cx),
        .pixel_y(cy),
        .pixel_output_valid(pipeline_valid),
        .pixel_output_rgb(pipeline_rgb),
        .active_generation(active_generation),
        .lines_built(lines_built),
        .lines_failed(lines_failed),
        .scheduler_overruns(scheduler_overruns),
        .pixel_underruns(pixel_underruns),
        .commit_errors(commit_errors),
        .commit_deferrals(commit_deferrals),
        .scene_active(scene_active),
        .render_interrupt(render_interrupt),
        .s_axi_awaddr(ctrl_awaddr),
        .s_axi_awprot(ctrl_awprot),
        .s_axi_awvalid(ctrl_awvalid),
        .s_axi_awready(ctrl_awready),
        .s_axi_wdata(ctrl_wdata),
        .s_axi_wstrb(ctrl_wstrb),
        .s_axi_wvalid(ctrl_wvalid),
        .s_axi_wready(ctrl_wready),
        .s_axi_bresp(ctrl_bresp),
        .s_axi_bvalid(ctrl_bvalid),
        .s_axi_bready(ctrl_bready),
        .s_axi_araddr(ctrl_araddr),
        .s_axi_arprot(ctrl_arprot),
        .s_axi_arvalid(ctrl_arvalid),
        .s_axi_arready(ctrl_arready),
        .s_axi_rdata(ctrl_rdata),
        .s_axi_rresp(ctrl_rresp),
        .s_axi_rvalid(ctrl_rvalid),
        .s_axi_rready(ctrl_rready),
        .fb_axi_arid(fb_arid),
        .fb_axi_araddr(fb_araddr),
        .fb_axi_arlen(fb_arlen),
        .fb_axi_arsize(fb_arsize),
        .fb_axi_arburst(fb_arburst),
        .fb_axi_arcache(fb_arcache),
        .fb_axi_arprot(fb_arprot),
        .fb_axi_arqos(fb_arqos),
        .fb_axi_arvalid(fb_arvalid),
        .fb_axi_arready(fb_arready),
        .fb_axi_rid(fb_rid),
        .fb_axi_rdata(fb_rdata),
        .fb_axi_rresp(fb_rresp),
        .fb_axi_rlast(fb_rlast),
        .fb_axi_rvalid(fb_rvalid),
        .fb_axi_rready(fb_rready),
        .tile0_axi_arid(tile0_arid),
        .tile0_axi_araddr(tile0_araddr),
        .tile0_axi_arlen(tile0_arlen),
        .tile0_axi_arsize(tile0_arsize),
        .tile0_axi_arburst(tile0_arburst),
        .tile0_axi_arcache(tile0_arcache),
        .tile0_axi_arprot(tile0_arprot),
        .tile0_axi_arqos(tile0_arqos),
        .tile0_axi_arvalid(tile0_arvalid),
        .tile0_axi_arready(tile0_arready),
        .tile0_axi_rid(tile0_rid),
        .tile0_axi_rdata(tile0_rdata),
        .tile0_axi_rresp(tile0_rresp),
        .tile0_axi_rlast(tile0_rlast),
        .tile0_axi_rvalid(tile0_rvalid),
        .tile0_axi_rready(tile0_rready),
        .tile1_axi_arid(tile1_arid),
        .tile1_axi_araddr(tile1_araddr),
        .tile1_axi_arlen(tile1_arlen),
        .tile1_axi_arsize(tile1_arsize),
        .tile1_axi_arburst(tile1_arburst),
        .tile1_axi_arcache(tile1_arcache),
        .tile1_axi_arprot(tile1_arprot),
        .tile1_axi_arqos(tile1_arqos),
        .tile1_axi_arvalid(tile1_arvalid),
        .tile1_axi_arready(tile1_arready),
        .tile1_axi_rid(tile1_rid),
        .tile1_axi_rdata(tile1_rdata),
        .tile1_axi_rresp(tile1_rresp),
        .tile1_axi_rlast(tile1_rlast),
        .tile1_axi_rvalid(tile1_rvalid),
        .tile1_axi_rready(tile1_rready),
        .sprite_axi_arid(sprite_arid),
        .sprite_axi_araddr(sprite_araddr),
        .sprite_axi_arlen(sprite_arlen),
        .sprite_axi_arsize(sprite_arsize),
        .sprite_axi_arburst(sprite_arburst),
        .sprite_axi_arcache(sprite_arcache),
        .sprite_axi_arprot(sprite_arprot),
        .sprite_axi_arqos(sprite_arqos),
        .sprite_axi_arvalid(sprite_arvalid),
        .sprite_axi_arready(sprite_arready),
        .sprite_axi_rid(sprite_rid),
        .sprite_axi_rdata(sprite_rdata),
        .sprite_axi_rresp(sprite_rresp),
        .sprite_axi_rlast(sprite_rlast),
        .sprite_axi_rvalid(sprite_rvalid),
        .sprite_axi_rready(sprite_rready),
        .render_axi_arid(render_read_arid),
        .render_axi_araddr(render_read_araddr),
        .render_axi_arlen(render_read_arlen),
        .render_axi_arsize(render_read_arsize),
        .render_axi_arburst(render_read_arburst),
        .render_axi_arcache(render_read_arcache),
        .render_axi_arprot(render_read_arprot),
        .render_axi_arqos(render_read_arqos),
        .render_axi_arvalid(render_read_arvalid),
        .render_axi_arready(render_read_arready),
        .render_axi_rid(render_read_rid),
        .render_axi_rdata(render_read_rdata),
        .render_axi_rresp(render_read_rresp),
        .render_axi_rlast(render_read_rlast),
        .render_axi_rvalid(render_read_rvalid),
        .render_axi_rready(render_read_rready),
        .render_axi_awid(render_write_awid),
        .render_axi_awaddr(render_write_awaddr),
        .render_axi_awlen(render_write_awlen),
        .render_axi_awsize(render_write_awsize),
        .render_axi_awburst(render_write_awburst),
        .render_axi_awcache(render_write_awcache),
        .render_axi_awprot(render_write_awprot),
        .render_axi_awqos(render_write_awqos),
        .render_axi_awvalid(render_write_awvalid),
        .render_axi_awready(render_write_awready),
        .render_axi_wdata(render_write_wdata),
        .render_axi_wstrb(render_write_wstrb),
        .render_axi_wlast(render_write_wlast),
        .render_axi_wvalid(render_write_wvalid),
        .render_axi_wready(render_write_wready),
        .render_axi_bid(render_write_bid),
        .render_axi_bresp(render_write_bresp),
        .render_axi_bvalid(render_write_bvalid),
        .render_axi_bready(render_write_bready)
    );

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
        .rgb(raster_rgb),
        .audio_sample_word(32'd0),
        .tmds(tmds),
        .tmds_clock(tmds_clock),
        .cx(cx),
        .cy(cy),
        .frame_width(frame_width),
        .frame_height(frame_height),
        .screen_width(screen_width),
        .screen_height(screen_height)
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
            frame_counter <= frame_counter + 8'd1;
    end

    assign led[0] = video_locked;
    assign led[1] = ~build_reset;
    assign led[2] = scene_active;
    assign led[3] = frame_counter[5];

    wire unused_status = &{
        1'b0,
        hdmi_tx_hpdn,
        frame_width,
        frame_height,
        screen_width,
        screen_height,
        active_generation,
        lines_built,
        lines_failed,
        scheduler_overruns,
        pixel_underruns,
        commit_errors,
        commit_deferrals
    };
endmodule

`default_nettype wire
