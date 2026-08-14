// Copyright (c) 2026 Astra68 contributors
//
// One-outstanding AXI4-Lite splitter. Address and data channels are buffered
// independently so masters may present AW and W in either order.
`timescale 1ns/1ps
`default_nettype none

module astra_axi_lite_1to2 #(
    parameter [31:0] SLAVE1_MASK = 32'h00008000,
    parameter [31:0] SLAVE1_VALUE = 32'h00008000,
    parameter [31:0] SLAVE1_ALT_MASK = 32'h0000ff00,
    parameter [31:0] SLAVE1_ALT_VALUE = 32'h00004000
) (
    input  wire        clk,
    input  wire        reset,

    input  wire [31:0] s_awaddr,
    input  wire [2:0]  s_awprot,
    input  wire        s_awvalid,
    output wire        s_awready,
    input  wire [31:0] s_wdata,
    input  wire [3:0]  s_wstrb,
    input  wire        s_wvalid,
    output wire        s_wready,
    output reg  [1:0]  s_bresp,
    output reg         s_bvalid,
    input  wire        s_bready,
    input  wire [31:0] s_araddr,
    input  wire [2:0]  s_arprot,
    input  wire        s_arvalid,
    output wire        s_arready,
    output reg  [31:0] s_rdata,
    output reg  [1:0]  s_rresp,
    output reg         s_rvalid,
    input  wire        s_rready,

    output wire [31:0] m0_awaddr,
    output wire [2:0]  m0_awprot,
    output wire        m0_awvalid,
    input  wire        m0_awready,
    output wire [31:0] m0_wdata,
    output wire [3:0]  m0_wstrb,
    output wire        m0_wvalid,
    input  wire        m0_wready,
    input  wire [1:0]  m0_bresp,
    input  wire        m0_bvalid,
    output wire        m0_bready,
    output wire [31:0] m0_araddr,
    output wire [2:0]  m0_arprot,
    output wire        m0_arvalid,
    input  wire        m0_arready,
    input  wire [31:0] m0_rdata,
    input  wire [1:0]  m0_rresp,
    input  wire        m0_rvalid,
    output wire        m0_rready,

    output wire [31:0] m1_awaddr,
    output wire [2:0]  m1_awprot,
    output wire        m1_awvalid,
    input  wire        m1_awready,
    output wire [31:0] m1_wdata,
    output wire [3:0]  m1_wstrb,
    output wire        m1_wvalid,
    input  wire        m1_wready,
    input  wire [1:0]  m1_bresp,
    input  wire        m1_bvalid,
    output wire        m1_bready,
    output wire [31:0] m1_araddr,
    output wire [2:0]  m1_arprot,
    output wire        m1_arvalid,
    input  wire        m1_arready,
    input  wire [31:0] m1_rdata,
    input  wire [1:0]  m1_rresp,
    input  wire        m1_rvalid,
    output wire        m1_rready
);
    wire s_aw_select =
        (s_awaddr & SLAVE1_MASK) == SLAVE1_VALUE ||
        (s_awaddr & SLAVE1_ALT_MASK) == SLAVE1_ALT_VALUE;
    wire s_ar_select =
        (s_araddr & SLAVE1_MASK) == SLAVE1_VALUE ||
        (s_araddr & SLAVE1_ALT_MASK) == SLAVE1_ALT_VALUE;

    reg aw_buffer_valid_q;
    reg [31:0] awaddr_q;
    reg [2:0] awprot_q;
    reg aw_select_q;
    reg w_buffer_valid_q;
    reg [31:0] wdata_q;
    reg [3:0] wstrb_q;
    reg aw_sent_q;
    reg w_sent_q;
    reg write_response_q;

    wire write_selected = aw_select_q;
    wire selected_aw_ready = write_selected ? m1_awready : m0_awready;
    wire selected_w_ready = write_selected ? m1_wready : m0_wready;
    wire selected_bvalid = write_selected ? m1_bvalid : m0_bvalid;
    wire [1:0] selected_bresp = write_selected ? m1_bresp : m0_bresp;

    assign s_awready = !aw_buffer_valid_q && !write_response_q && !s_bvalid;
    assign s_wready = !w_buffer_valid_q && !write_response_q && !s_bvalid;
    assign m0_awaddr = awaddr_q;
    assign m1_awaddr = awaddr_q;
    assign m0_awprot = awprot_q;
    assign m1_awprot = awprot_q;
    assign m0_awvalid = aw_buffer_valid_q && !aw_sent_q && !write_selected;
    assign m1_awvalid = aw_buffer_valid_q && !aw_sent_q && write_selected;
    assign m0_wdata = wdata_q;
    assign m1_wdata = wdata_q;
    assign m0_wstrb = wstrb_q;
    assign m1_wstrb = wstrb_q;
    assign m0_wvalid = aw_buffer_valid_q && w_buffer_valid_q &&
                       !w_sent_q && !write_selected;
    assign m1_wvalid = aw_buffer_valid_q && w_buffer_valid_q &&
                       !w_sent_q && write_selected;
    assign m0_bready = write_response_q && !write_selected && !s_bvalid;
    assign m1_bready = write_response_q && write_selected && !s_bvalid;

    reg ar_buffer_valid_q;
    reg [31:0] araddr_q;
    reg [2:0] arprot_q;
    reg ar_select_q;
    reg ar_sent_q;
    reg read_response_q;

    wire selected_ar_ready = ar_select_q ? m1_arready : m0_arready;
    wire selected_rvalid = ar_select_q ? m1_rvalid : m0_rvalid;
    wire [31:0] selected_rdata = ar_select_q ? m1_rdata : m0_rdata;
    wire [1:0] selected_rresp = ar_select_q ? m1_rresp : m0_rresp;

    assign s_arready = !ar_buffer_valid_q && !read_response_q && !s_rvalid;
    assign m0_araddr = araddr_q;
    assign m1_araddr = araddr_q;
    assign m0_arprot = arprot_q;
    assign m1_arprot = arprot_q;
    assign m0_arvalid = ar_buffer_valid_q && !ar_sent_q && !ar_select_q;
    assign m1_arvalid = ar_buffer_valid_q && !ar_sent_q && ar_select_q;
    assign m0_rready = read_response_q && !ar_select_q && !s_rvalid;
    assign m1_rready = read_response_q && ar_select_q && !s_rvalid;

    always @(posedge clk) begin
        if (reset) begin
            aw_buffer_valid_q <= 1'b0;
            awaddr_q <= 32'd0;
            awprot_q <= 3'd0;
            aw_select_q <= 1'b0;
            w_buffer_valid_q <= 1'b0;
            wdata_q <= 32'd0;
            wstrb_q <= 4'd0;
            aw_sent_q <= 1'b0;
            w_sent_q <= 1'b0;
            write_response_q <= 1'b0;
            s_bresp <= 2'b00;
            s_bvalid <= 1'b0;
            ar_buffer_valid_q <= 1'b0;
            araddr_q <= 32'd0;
            arprot_q <= 3'd0;
            ar_select_q <= 1'b0;
            ar_sent_q <= 1'b0;
            read_response_q <= 1'b0;
            s_rdata <= 32'd0;
            s_rresp <= 2'b00;
            s_rvalid <= 1'b0;
        end else begin
            if (s_awvalid && s_awready) begin
                aw_buffer_valid_q <= 1'b1;
                awaddr_q <= s_awaddr;
                awprot_q <= s_awprot;
                aw_select_q <= s_aw_select;
                aw_sent_q <= 1'b0;
            end
            if (s_wvalid && s_wready) begin
                w_buffer_valid_q <= 1'b1;
                wdata_q <= s_wdata;
                wstrb_q <= s_wstrb;
                w_sent_q <= 1'b0;
            end
            if (aw_buffer_valid_q && !aw_sent_q && selected_aw_ready)
                aw_sent_q <= 1'b1;
            if (aw_buffer_valid_q && w_buffer_valid_q && !w_sent_q &&
                selected_w_ready)
                w_sent_q <= 1'b1;
            // Enter response handling only after both downstream handshakes
            // have been registered.  This keeps slave ready logic out of the
            // splitter's response-state timing path.
            if (aw_buffer_valid_q && w_buffer_valid_q &&
                aw_sent_q && w_sent_q) begin
                write_response_q <= 1'b1;
                aw_buffer_valid_q <= 1'b0;
                w_buffer_valid_q <= 1'b0;
                aw_sent_q <= 1'b0;
                w_sent_q <= 1'b0;
            end
            if (write_response_q && selected_bvalid && !s_bvalid) begin
                write_response_q <= 1'b0;
                s_bresp <= selected_bresp;
                s_bvalid <= 1'b1;
            end
            if (s_bvalid && s_bready)
                s_bvalid <= 1'b0;

            if (s_arvalid && s_arready) begin
                ar_buffer_valid_q <= 1'b1;
                araddr_q <= s_araddr;
                arprot_q <= s_arprot;
                ar_select_q <= s_ar_select;
                ar_sent_q <= 1'b0;
            end
            if (ar_buffer_valid_q && !ar_sent_q && selected_ar_ready) begin
                ar_sent_q <= 1'b1;
                read_response_q <= 1'b1;
                ar_buffer_valid_q <= 1'b0;
            end
            if (read_response_q && selected_rvalid && !s_rvalid) begin
                read_response_q <= 1'b0;
                s_rdata <= selected_rdata;
                s_rresp <= selected_rresp;
                s_rvalid <= 1'b1;
            end
            if (s_rvalid && s_rready)
                s_rvalid <= 1'b0;
        end
    end
endmodule

`default_nettype wire
