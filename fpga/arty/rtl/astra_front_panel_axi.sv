// Copyright (c) 2026 Astra68 contributors
//
// AXI4-Lite wrapper for the shared Astra front-panel register contract.
`timescale 1ns/1ps
`default_nettype none

module astra_front_panel_axi #(
    parameter integer CLK_HZ = 200000000,
    parameter [31:0] CAPABILITIES = 32'h1f020407,
    parameter integer ACTIVITY_LED = 3
) (
    input  wire        clk,
    input  wire        reset,
    input  wire [5:0]  buttons,
    input  wire [3:0]  switches,
    input  wire [7:0]  diagnostic_leds,
    output wire [7:0]  leds,

    input  wire [31:0] s_axi_awaddr,
    input  wire [2:0]  s_axi_awprot,
    input  wire        s_axi_awvalid,
    output wire        s_axi_awready,
    input  wire [31:0] s_axi_wdata,
    input  wire [3:0]  s_axi_wstrb,
    input  wire        s_axi_wvalid,
    output wire        s_axi_wready,
    output reg  [1:0]  s_axi_bresp,
    output reg         s_axi_bvalid,
    input  wire        s_axi_bready,
    input  wire [31:0] s_axi_araddr,
    input  wire [2:0]  s_axi_arprot,
    input  wire        s_axi_arvalid,
    output wire        s_axi_arready,
    output reg  [31:0] s_axi_rdata,
    output reg  [1:0]  s_axi_rresp,
    output reg         s_axi_rvalid,
    input  wire        s_axi_rready
);
    reg aw_pending_q;
    reg [7:0] awaddr_q;
    reg w_pending_q;
    reg [31:0] wdata_q;
    reg [3:0] wstrb_q;
    wire write_fire = aw_pending_q && w_pending_q && !s_axi_bvalid;

    function automatic read_address_valid(input [7:0] address);
        begin
            case (address)
                8'h00, 8'h04, 8'h08, 8'h0c, 8'h10, 8'h14,
                8'h18, 8'h1c, 8'h2c, 8'h30:
                    read_address_valid = 1'b1;
                default: read_address_valid = 1'b0;
            endcase
        end
    endfunction

    function automatic write_address_valid(input [7:0] address);
        begin
            case (address)
                8'h14, 8'h18, 8'h1c, 8'h20, 8'h24, 8'h28,
                8'h2c, 8'h30:
                    write_address_valid = 1'b1;
                default: write_address_valid = 1'b0;
            endcase
        end
    endfunction

    wire write_valid = write_address_valid(awaddr_q);
    wire read_fire = s_axi_arvalid && s_axi_arready;
    wire [5:0] panel_reg_index = write_fire ? awaddr_q[7:2] :
                                              s_axi_araddr[7:2];
    wire [31:0] panel_read_data;

    astra_front_panel #(
        .CLK_HZ(CLK_HZ),
        .CAPABILITIES(CAPABILITIES),
        .ACTIVITY_LED(ACTIVITY_LED)
    ) panel_i (
        .clk(clk),
        .rst(reset),
        .buttons(buttons),
        .switches(switches),
        .select(write_fire && write_valid),
        .reg_index(panel_reg_index),
        .write_strobe(write_fire && write_valid),
        .write_data(wdata_q),
        .byte_enable(wstrb_q),
        .read_data(panel_read_data),
        .diagnostic_leds(diagnostic_leds),
        .leds(leds)
    );

    assign s_axi_awready = !aw_pending_q && !s_axi_bvalid;
    assign s_axi_wready = !w_pending_q && !s_axi_bvalid;
    assign s_axi_arready = !s_axi_rvalid && !write_fire;

    always @(posedge clk) begin
        if (reset) begin
            aw_pending_q <= 1'b0;
            awaddr_q <= 8'd0;
            w_pending_q <= 1'b0;
            wdata_q <= 32'd0;
            wstrb_q <= 4'd0;
            s_axi_bresp <= 2'b00;
            s_axi_bvalid <= 1'b0;
            s_axi_rdata <= 32'd0;
            s_axi_rresp <= 2'b00;
            s_axi_rvalid <= 1'b0;
        end else begin
            if (s_axi_awvalid && s_axi_awready) begin
                aw_pending_q <= 1'b1;
                awaddr_q <= s_axi_awaddr[7:0];
            end
            if (s_axi_wvalid && s_axi_wready) begin
                w_pending_q <= 1'b1;
                wdata_q <= s_axi_wdata;
                wstrb_q <= s_axi_wstrb;
            end
            if (s_axi_bvalid && s_axi_bready)
                s_axi_bvalid <= 1'b0;
            if (write_fire) begin
                aw_pending_q <= 1'b0;
                w_pending_q <= 1'b0;
                s_axi_bvalid <= 1'b1;
                s_axi_bresp <= write_valid ? 2'b00 : 2'b11;
            end

            if (s_axi_rvalid && s_axi_rready)
                s_axi_rvalid <= 1'b0;
            if (read_fire) begin
                s_axi_rvalid <= 1'b1;
                if (read_address_valid(s_axi_araddr[7:0])) begin
                    s_axi_rdata <= panel_read_data;
                    s_axi_rresp <= 2'b00;
                end else begin
                    s_axi_rdata <= 32'd0;
                    s_axi_rresp <= 2'b11;
                end
            end
        end
    end

    wire unused = &{1'b0, s_axi_awprot, s_axi_arprot};
endmodule

`default_nettype wire
