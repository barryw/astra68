// Copyright (c) 2026 Astra68 contributors
//
// Astra Arty Z7-20 production graphics integration. The Zynq PS owns DDR and
// software control; the PL owns deterministic line construction, composition,
// and fixed 1280x720p60 DVI-over-HDMI transport.
`timescale 1ns/1ps
`default_nettype none

module astra_arty_graphics_top (
    output wire [3:0]  led,
    output wire        led4_r,
    output wire        led4_g,
    output wire        led4_b,
    input  wire [3:0]  btn,
    input  wire [1:0]  sw,

    output wire        hdmi_tx_clk_p,
    output wire        hdmi_tx_clk_n,
    output wire [2:0]  hdmi_tx_d_p,
    output wire [2:0]  hdmi_tx_d_n,
    inout  wire        hdmi_tx_hpdn,
    inout  wire        hdmi_tx_scl,
    inout  wire        hdmi_tx_sda,

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

    wire [31:0] host_ctrl_araddr;
    wire [2:0] host_ctrl_arprot;
    wire host_ctrl_arready;
    wire host_ctrl_arvalid;
    wire [31:0] host_ctrl_awaddr;
    wire [2:0] host_ctrl_awprot;
    wire host_ctrl_awready;
    wire host_ctrl_awvalid;
    wire host_ctrl_bready;
    wire [1:0] host_ctrl_bresp;
    wire host_ctrl_bvalid;
    wire [31:0] host_ctrl_rdata;
    wire host_ctrl_rready;
    wire [1:0] host_ctrl_rresp;
    wire host_ctrl_rvalid;
    wire [31:0] host_ctrl_wdata;
    wire host_ctrl_wready;
    wire [3:0] host_ctrl_wstrb;
    wire host_ctrl_wvalid;

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

    wire [31:0] audio_ctrl_araddr;
    wire [2:0] audio_ctrl_arprot;
    wire audio_ctrl_arready;
    wire audio_ctrl_arvalid;
    wire [31:0] audio_ctrl_awaddr;
    wire [2:0] audio_ctrl_awprot;
    wire audio_ctrl_awready;
    wire audio_ctrl_awvalid;
    wire audio_ctrl_bready;
    wire [1:0] audio_ctrl_bresp;
    wire audio_ctrl_bvalid;
    wire [31:0] audio_ctrl_rdata;
    wire audio_ctrl_rready;
    wire [1:0] audio_ctrl_rresp;
    wire audio_ctrl_rvalid;
    wire [31:0] audio_ctrl_wdata;
    wire audio_ctrl_wready;
    wire [3:0] audio_ctrl_wstrb;
    wire audio_ctrl_wvalid;

    wire [31:0] peripheral_ctrl_araddr;
    wire [2:0] peripheral_ctrl_arprot;
    wire peripheral_ctrl_arready;
    wire peripheral_ctrl_arvalid;
    wire [31:0] peripheral_ctrl_awaddr;
    wire [2:0] peripheral_ctrl_awprot;
    wire peripheral_ctrl_awready;
    wire peripheral_ctrl_awvalid;
    wire peripheral_ctrl_bready;
    wire [1:0] peripheral_ctrl_bresp;
    wire peripheral_ctrl_bvalid;
    wire [31:0] peripheral_ctrl_rdata;
    wire peripheral_ctrl_rready;
    wire [1:0] peripheral_ctrl_rresp;
    wire peripheral_ctrl_rvalid;
    wire [31:0] peripheral_ctrl_wdata;
    wire peripheral_ctrl_wready;
    wire [3:0] peripheral_ctrl_wstrb;
    wire peripheral_ctrl_wvalid;

    wire [31:0] panel_ctrl_araddr;
    wire [2:0] panel_ctrl_arprot;
    wire panel_ctrl_arready;
    wire panel_ctrl_arvalid;
    wire [31:0] panel_ctrl_awaddr;
    wire [2:0] panel_ctrl_awprot;
    wire panel_ctrl_awready;
    wire panel_ctrl_awvalid;
    wire panel_ctrl_bready;
    wire [1:0] panel_ctrl_bresp;
    wire panel_ctrl_bvalid;
    wire [31:0] panel_ctrl_rdata;
    wire panel_ctrl_rready;
    wire [1:0] panel_ctrl_rresp;
    wire panel_ctrl_rvalid;
    wire [31:0] panel_ctrl_wdata;
    wire panel_ctrl_wready;
    wire [3:0] panel_ctrl_wstrb;
    wire panel_ctrl_wvalid;

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

    wire [5:0] scene_arid;
    wire [31:0] scene_araddr;
    wire [7:0] scene_arlen;
    wire [2:0] scene_arsize;
    wire [1:0] scene_arburst;
    wire [3:0] scene_arcache;
    wire [2:0] scene_arprot;
    wire [3:0] scene_arqos;
    wire scene_arvalid;
    wire scene_arready;
    wire [5:0] scene_rid;
    wire [63:0] scene_rdata;
    wire [1:0] scene_rresp;
    wire scene_rlast;
    wire scene_rvalid;
    wire scene_rready;
    wire [2:0] scene_client_arready;
    wire [17:0] scene_client_rid;
    wire [191:0] scene_client_rdata;
    wire [5:0] scene_client_rresp;
    wire [2:0] scene_client_rlast;
    wire [2:0] scene_client_rvalid;

    assign {sprite_arready, tile1_arready, tile0_arready} =
        scene_client_arready;
    assign {sprite_rid, tile1_rid, tile0_rid} = scene_client_rid;
    assign {sprite_rdata, tile1_rdata, tile0_rdata} = scene_client_rdata;
    assign {sprite_rresp, tile1_rresp, tile0_rresp} = scene_client_rresp;
    assign {sprite_rlast, tile1_rlast, tile0_rlast} = scene_client_rlast;
    assign {sprite_rvalid, tile1_rvalid, tile0_rvalid} = scene_client_rvalid;

    astra_axi_read_3to1 scene_read_arbiter (
        .aclk(fclk_clk1),
        .aresetn(graphics_resetn[0]),
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
        .s_axi_rid(scene_client_rid),
        .s_axi_rdata(scene_client_rdata),
        .s_axi_rresp(scene_client_rresp),
        .s_axi_rlast(scene_client_rlast),
        .s_axi_rvalid(scene_client_rvalid),
        .s_axi_rready({sprite_rready, tile1_rready, tile0_rready}),
        .m_axi_arid(scene_arid),
        .m_axi_araddr(scene_araddr),
        .m_axi_arlen(scene_arlen),
        .m_axi_arsize(scene_arsize),
        .m_axi_arburst(scene_arburst),
        .m_axi_arcache(scene_arcache),
        .m_axi_arprot(scene_arprot),
        .m_axi_arqos(scene_arqos),
        .m_axi_arvalid(scene_arvalid),
        .m_axi_arready(scene_arready),
        .m_axi_rid(scene_rid),
        .m_axi_rdata(scene_rdata),
        .m_axi_rresp(scene_rresp),
        .m_axi_rlast(scene_rlast),
        .m_axi_rvalid(scene_rvalid),
        .m_axi_rready(scene_rready)
    );

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
        .GPIO_0_tri_io(hdmi_tx_hpdn),
        .IIC_0_scl_io(hdmi_tx_scl),
        .IIC_0_sda_io(hdmi_tx_sda),
        .M_AXI_CTRL_araddr(host_ctrl_araddr),
        .M_AXI_CTRL_arprot(host_ctrl_arprot),
        .M_AXI_CTRL_arready(host_ctrl_arready),
        .M_AXI_CTRL_arvalid(host_ctrl_arvalid),
        .M_AXI_CTRL_awaddr(host_ctrl_awaddr),
        .M_AXI_CTRL_awprot(host_ctrl_awprot),
        .M_AXI_CTRL_awready(host_ctrl_awready),
        .M_AXI_CTRL_awvalid(host_ctrl_awvalid),
        .M_AXI_CTRL_bready(host_ctrl_bready),
        .M_AXI_CTRL_bresp(host_ctrl_bresp),
        .M_AXI_CTRL_bvalid(host_ctrl_bvalid),
        .M_AXI_CTRL_rdata(host_ctrl_rdata),
        .M_AXI_CTRL_rready(host_ctrl_rready),
        .M_AXI_CTRL_rresp(host_ctrl_rresp),
        .M_AXI_CTRL_rvalid(host_ctrl_rvalid),
        .M_AXI_CTRL_wdata(host_ctrl_wdata),
        .M_AXI_CTRL_wready(host_ctrl_wready),
        .M_AXI_CTRL_wstrb(host_ctrl_wstrb),
        .M_AXI_CTRL_wvalid(host_ctrl_wvalid),
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
        .S_AXI_SCENE_araddr(scene_araddr),
        .S_AXI_SCENE_arburst(scene_arburst),
        .S_AXI_SCENE_arcache(scene_arcache),
        .S_AXI_SCENE_arid(scene_arid),
        .S_AXI_SCENE_arlen(scene_arlen),
        .S_AXI_SCENE_arlock(1'b0),
        .S_AXI_SCENE_arprot(scene_arprot),
        .S_AXI_SCENE_arqos(scene_arqos),
        .S_AXI_SCENE_arready(scene_arready),
        .S_AXI_SCENE_arsize(scene_arsize),
        .S_AXI_SCENE_arvalid(scene_arvalid),
        .S_AXI_SCENE_rdata(scene_rdata),
        .S_AXI_SCENE_rid(scene_rid),
        .S_AXI_SCENE_rlast(scene_rlast),
        .S_AXI_SCENE_rready(scene_rready),
        .S_AXI_SCENE_rresp(scene_rresp),
        .S_AXI_SCENE_rvalid(scene_rvalid),
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

    astra_axi_lite_1to2 #(
        .SLAVE1_MASK(32'h0000ff00),
        .SLAVE1_VALUE(32'h00006000),
        .SLAVE1_ALT_MASK(32'h0000ff00),
        .SLAVE1_ALT_VALUE(32'h00007000)
    ) host_control_split_i (
        .clk(fclk_clk1),
        .reset(~graphics_resetn[0]),
        .s_awaddr(host_ctrl_awaddr),
        .s_awprot(host_ctrl_awprot),
        .s_awvalid(host_ctrl_awvalid),
        .s_awready(host_ctrl_awready),
        .s_wdata(host_ctrl_wdata),
        .s_wstrb(host_ctrl_wstrb),
        .s_wvalid(host_ctrl_wvalid),
        .s_wready(host_ctrl_wready),
        .s_bresp(host_ctrl_bresp),
        .s_bvalid(host_ctrl_bvalid),
        .s_bready(host_ctrl_bready),
        .s_araddr(host_ctrl_araddr),
        .s_arprot(host_ctrl_arprot),
        .s_arvalid(host_ctrl_arvalid),
        .s_arready(host_ctrl_arready),
        .s_rdata(host_ctrl_rdata),
        .s_rresp(host_ctrl_rresp),
        .s_rvalid(host_ctrl_rvalid),
        .s_rready(host_ctrl_rready),
        .m0_awaddr(ctrl_awaddr),
        .m0_awprot(ctrl_awprot),
        .m0_awvalid(ctrl_awvalid),
        .m0_awready(ctrl_awready),
        .m0_wdata(ctrl_wdata),
        .m0_wstrb(ctrl_wstrb),
        .m0_wvalid(ctrl_wvalid),
        .m0_wready(ctrl_wready),
        .m0_bresp(ctrl_bresp),
        .m0_bvalid(ctrl_bvalid),
        .m0_bready(ctrl_bready),
        .m0_araddr(ctrl_araddr),
        .m0_arprot(ctrl_arprot),
        .m0_arvalid(ctrl_arvalid),
        .m0_arready(ctrl_arready),
        .m0_rdata(ctrl_rdata),
        .m0_rresp(ctrl_rresp),
        .m0_rvalid(ctrl_rvalid),
        .m0_rready(ctrl_rready),
        .m1_awaddr(peripheral_ctrl_awaddr),
        .m1_awprot(peripheral_ctrl_awprot),
        .m1_awvalid(peripheral_ctrl_awvalid),
        .m1_awready(peripheral_ctrl_awready),
        .m1_wdata(peripheral_ctrl_wdata),
        .m1_wstrb(peripheral_ctrl_wstrb),
        .m1_wvalid(peripheral_ctrl_wvalid),
        .m1_wready(peripheral_ctrl_wready),
        .m1_bresp(peripheral_ctrl_bresp),
        .m1_bvalid(peripheral_ctrl_bvalid),
        .m1_bready(peripheral_ctrl_bready),
        .m1_araddr(peripheral_ctrl_araddr),
        .m1_arprot(peripheral_ctrl_arprot),
        .m1_arvalid(peripheral_ctrl_arvalid),
        .m1_arready(peripheral_ctrl_arready),
        .m1_rdata(peripheral_ctrl_rdata),
        .m1_rresp(peripheral_ctrl_rresp),
        .m1_rvalid(peripheral_ctrl_rvalid),
        .m1_rready(peripheral_ctrl_rready)
    );

    astra_axi_lite_1to2 #(
        .SLAVE1_MASK(32'h0000ff00),
        .SLAVE1_VALUE(32'h00007000),
        .SLAVE1_ALT_MASK(32'hffffffff),
        .SLAVE1_ALT_VALUE(32'hffffffff)
    ) peripheral_control_split_i (
        .clk(fclk_clk1),
        .reset(~graphics_resetn[0]),
        .s_awaddr(peripheral_ctrl_awaddr),
        .s_awprot(peripheral_ctrl_awprot),
        .s_awvalid(peripheral_ctrl_awvalid),
        .s_awready(peripheral_ctrl_awready),
        .s_wdata(peripheral_ctrl_wdata),
        .s_wstrb(peripheral_ctrl_wstrb),
        .s_wvalid(peripheral_ctrl_wvalid),
        .s_wready(peripheral_ctrl_wready),
        .s_bresp(peripheral_ctrl_bresp),
        .s_bvalid(peripheral_ctrl_bvalid),
        .s_bready(peripheral_ctrl_bready),
        .s_araddr(peripheral_ctrl_araddr),
        .s_arprot(peripheral_ctrl_arprot),
        .s_arvalid(peripheral_ctrl_arvalid),
        .s_arready(peripheral_ctrl_arready),
        .s_rdata(peripheral_ctrl_rdata),
        .s_rresp(peripheral_ctrl_rresp),
        .s_rvalid(peripheral_ctrl_rvalid),
        .s_rready(peripheral_ctrl_rready),
        .m0_awaddr(audio_ctrl_awaddr),
        .m0_awprot(audio_ctrl_awprot),
        .m0_awvalid(audio_ctrl_awvalid),
        .m0_awready(audio_ctrl_awready),
        .m0_wdata(audio_ctrl_wdata),
        .m0_wstrb(audio_ctrl_wstrb),
        .m0_wvalid(audio_ctrl_wvalid),
        .m0_wready(audio_ctrl_wready),
        .m0_bresp(audio_ctrl_bresp),
        .m0_bvalid(audio_ctrl_bvalid),
        .m0_bready(audio_ctrl_bready),
        .m0_araddr(audio_ctrl_araddr),
        .m0_arprot(audio_ctrl_arprot),
        .m0_arvalid(audio_ctrl_arvalid),
        .m0_arready(audio_ctrl_arready),
        .m0_rdata(audio_ctrl_rdata),
        .m0_rresp(audio_ctrl_rresp),
        .m0_rvalid(audio_ctrl_rvalid),
        .m0_rready(audio_ctrl_rready),
        .m1_awaddr(panel_ctrl_awaddr),
        .m1_awprot(panel_ctrl_awprot),
        .m1_awvalid(panel_ctrl_awvalid),
        .m1_awready(panel_ctrl_awready),
        .m1_wdata(panel_ctrl_wdata),
        .m1_wstrb(panel_ctrl_wstrb),
        .m1_wvalid(panel_ctrl_wvalid),
        .m1_wready(panel_ctrl_wready),
        .m1_bresp(panel_ctrl_bresp),
        .m1_bvalid(panel_ctrl_bvalid),
        .m1_bready(panel_ctrl_bready),
        .m1_araddr(panel_ctrl_araddr),
        .m1_arprot(panel_ctrl_arprot),
        .m1_arvalid(panel_ctrl_arvalid),
        .m1_arready(panel_ctrl_arready),
        .m1_rdata(panel_ctrl_rdata),
        .m1_rresp(panel_ctrl_rresp),
        .m1_rvalid(panel_ctrl_rvalid),
        .m1_rready(panel_ctrl_rready)
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

    // A separate exact 48 MHz MMCM avoids coupling the HDMI audio rate to
    // either the quantized renderer clock or the 74.25 MHz video clock.
    wire audio_feedback;
    wire audio_feedback_buf;
    wire clk_audio_48m_raw;
    wire clk_audio_48m;
    wire audio_locked;
    MMCME2_BASE #(
        .BANDWIDTH("OPTIMIZED"),
        .CLKIN1_PERIOD(10.000),
        .DIVCLK_DIVIDE(5),
        .CLKFBOUT_MULT_F(48.000),
        .CLKOUT0_DIVIDE_F(20.000),
        .CLKOUT0_DUTY_CYCLE(0.5),
        .CLKOUT0_PHASE(0.0),
        .CLKFBOUT_PHASE(0.0),
        .STARTUP_WAIT("FALSE")
    ) audio_mmcm_i (
        .CLKIN1(fclk_clk0),
        .CLKFBIN(audio_feedback_buf),
        .CLKFBOUT(audio_feedback),
        .CLKFBOUTB(),
        .CLKOUT0(clk_audio_48m_raw),
        .CLKOUT0B(),
        .CLKOUT1(),
        .CLKOUT1B(),
        .CLKOUT2(),
        .CLKOUT2B(),
        .CLKOUT3(),
        .CLKOUT3B(),
        .CLKOUT4(),
        .CLKOUT5(),
        .CLKOUT6(),
        .LOCKED(audio_locked),
        .PWRDWN(1'b0),
        .RST(~graphics_resetn[0])
    );
    BUFG audio_feedback_buf_i (
        .I(audio_feedback),
        .O(audio_feedback_buf)
    );
    BUFG audio_48m_buf_i (.I(clk_audio_48m_raw), .O(clk_audio_48m));

    reg [8:0] audio_divider_q = 9'd0;
    reg audio_clock_raw_q = 1'b0;
    always @(posedge clk_audio_48m or negedge audio_locked) begin
        if (!audio_locked) begin
            audio_divider_q <= 9'd0;
            audio_clock_raw_q <= 1'b0;
        end else if (audio_divider_q == 9'd499) begin
            audio_divider_q <= 9'd0;
            audio_clock_raw_q <= ~audio_clock_raw_q;
        end else begin
            audio_divider_q <= audio_divider_q + 9'd1;
        end
    end
    wire clk_audio;
    BUFG audio_sample_buf_i (.I(audio_clock_raw_q), .O(clk_audio));

    (* ASYNC_REG = "TRUE" *) reg [3:0] audio_reset_sync = 4'hf;
    always @(posedge clk_audio or negedge audio_locked) begin
        if (!audio_locked)
            audio_reset_sync <= 4'hf;
        else
            audio_reset_sync <= {audio_reset_sync[2:0], 1'b0};
    end
    wire audio_reset = audio_reset_sync[3];

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
    wire [1:0][23:0] audio_sample_word;
    wire hdmi_output_requested;
    wire hdmi_output_active;

    (* ASYNC_REG = "TRUE" *) reg [1:0] hdmi_request_pixel_sync_q = 2'd0;
    (* ASYNC_REG = "TRUE" *) reg [1:0] hdmi_active_build_sync_q = 2'd0;
    always @(posedge fclk_clk1) begin
        if (build_reset) begin
            hdmi_active_build_sync_q <= 2'd0;
        end else begin
            hdmi_active_build_sync_q <= {hdmi_active_build_sync_q[0],
                                         hdmi_output_active};
        end
    end
    always @(posedge clk_pixel) begin
        if (video_reset)
            hdmi_request_pixel_sync_q <= 2'd0;
        else
            hdmi_request_pixel_sync_q <=
                {hdmi_request_pixel_sync_q[0], hdmi_output_requested};
    end

    astra_hdmi_audio audio_i (
        .build_clk(fclk_clk1),
        .build_reset(build_reset),
        .audio_clk(clk_audio),
        .audio_reset(audio_reset),
        .audio_sample_word(audio_sample_word),
        .hdmi_output_active(hdmi_active_build_sync_q[1]),
        .hdmi_output_requested(hdmi_output_requested),
        .s_axi_awaddr(audio_ctrl_awaddr),
        .s_axi_awprot(audio_ctrl_awprot),
        .s_axi_awvalid(audio_ctrl_awvalid),
        .s_axi_awready(audio_ctrl_awready),
        .s_axi_wdata(audio_ctrl_wdata),
        .s_axi_wstrb(audio_ctrl_wstrb),
        .s_axi_wvalid(audio_ctrl_wvalid),
        .s_axi_wready(audio_ctrl_wready),
        .s_axi_bresp(audio_ctrl_bresp),
        .s_axi_bvalid(audio_ctrl_bvalid),
        .s_axi_bready(audio_ctrl_bready),
        .s_axi_araddr(audio_ctrl_araddr),
        .s_axi_arprot(audio_ctrl_arprot),
        .s_axi_arvalid(audio_ctrl_arvalid),
        .s_axi_arready(audio_ctrl_arready),
        .s_axi_rdata(audio_ctrl_rdata),
        .s_axi_rresp(audio_ctrl_rresp),
        .s_axi_rvalid(audio_ctrl_rvalid),
        .s_axi_rready(audio_ctrl_rready)
    );

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
        .DVI_OUTPUT(1'b0),
        .VIDEO_REFRESH_RATE_MILLIHZ(60000),
        .AUDIO_RATE(48000),
        .AUDIO_BIT_WIDTH(24),
        .VENDOR_NAME({"Astra68", 8'd0}),
        .PRODUCT_DESCRIPTION("Astra68 Computer"),
        .START_X(0),
        .START_Y(0)
    ) hdmi_i (
        .clk_pixel_x5(clk_tmds_x5),
        .clk_pixel(clk_pixel),
        .clk_audio(clk_audio),
        .reset(video_reset),
        .hdmi_output_enable(hdmi_request_pixel_sync_q[1]),
        .rgb(raster_rgb),
        .audio_sample_word(audio_sample_word),
        .tmds(tmds),
        .tmds_clock(tmds_clock),
        .cx(cx),
        .cy(cy),
        .frame_width(frame_width),
        .frame_height(frame_height),
        .screen_width(screen_width),
        .screen_height(screen_height),
        .hdmi_output_active(hdmi_output_active)
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

    wire [7:0] panel_leds;
    astra_front_panel_axi #(
        .CLK_HZ(187500000),
        .CAPABILITIES(32'h1f020407),
        .ACTIVITY_LED(3)
    ) front_panel_i (
        .clk(fclk_clk1),
        .reset(build_reset),
        .buttons({2'd0, btn}),
        .switches({2'd0, sw}),
        .diagnostic_leds({5'd0, scene_active, ~build_reset, video_locked}),
        .leds(panel_leds),
        .s_axi_awaddr(panel_ctrl_awaddr),
        .s_axi_awprot(panel_ctrl_awprot),
        .s_axi_awvalid(panel_ctrl_awvalid),
        .s_axi_awready(panel_ctrl_awready),
        .s_axi_wdata(panel_ctrl_wdata),
        .s_axi_wstrb(panel_ctrl_wstrb),
        .s_axi_wvalid(panel_ctrl_wvalid),
        .s_axi_wready(panel_ctrl_wready),
        .s_axi_bresp(panel_ctrl_bresp),
        .s_axi_bvalid(panel_ctrl_bvalid),
        .s_axi_bready(panel_ctrl_bready),
        .s_axi_araddr(panel_ctrl_araddr),
        .s_axi_arprot(panel_ctrl_arprot),
        .s_axi_arvalid(panel_ctrl_arvalid),
        .s_axi_arready(panel_ctrl_arready),
        .s_axi_rdata(panel_ctrl_rdata),
        .s_axi_rresp(panel_ctrl_rresp),
        .s_axi_rvalid(panel_ctrl_rvalid),
        .s_axi_rready(panel_ctrl_rready)
    );
    assign led = panel_leds[3:0];
    assign led4_r = panel_leds[4];
    assign led4_g = panel_leds[5];
    assign led4_b = panel_leds[6];

    wire unused_status = &{
        1'b0,
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
        commit_deferrals,
        frame_counter
    };
endmodule

`default_nettype wire
