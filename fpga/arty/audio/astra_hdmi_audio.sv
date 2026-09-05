// Copyright (c) 2026 Astra68 contributors
//
// Bounded Linux-to-HDMI PCM queue and HDMI/DVI link control. Linux supplies
// signed 24-bit stereo samples and enables HDMI only after validating E-EDID.
`timescale 1ns/1ps
`default_nettype none

module astra_hdmi_audio (
    input  wire        build_clk,
    input  wire        build_reset,
    input  wire        audio_clk,
    input  wire        audio_reset,
    output wire [1:0][23:0] audio_sample_word,
    input  wire        hdmi_output_active,
    output reg         hdmi_output_requested,

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
    localparam [31:0] DEVICE_ID = 32'h41554430; /* AUD0 */
    localparam [31:0] VERSION = 32'h00010000;
    localparam [31:0] CAPABILITIES = 32'h00183002; /* 24-bit, 48 kHz, stereo */

    reg [1:0] control_q;
    reg [23:0] left_q;
    reg [31:0] overflow_count_q;

    reg aw_pending_q;
    reg [7:0] awaddr_q;
    reg w_pending_q;
    reg [31:0] wdata_q;
    reg [3:0] wstrb_q;
    wire write_fire = aw_pending_q && w_pending_q && !s_axi_bvalid;
    wire write_sample = write_fire && awaddr_q == 8'h18 &&
                        wstrb_q == 4'hf;

    wire [63:0] fifo_wr_data = {8'd0, wdata_q[23:0], 8'd0, left_q};
    wire fifo_wr_ready;
    wire [9:0] fifo_wr_level;
    wire [63:0] fifo_rd_data;
    wire fifo_rd_valid;
    wire [9:0] fifo_rd_level;
    wire fifo_overflow;
    wire fifo_underflow;
    reg [9:0] fifo_wr_level_status_q;

    (* ASYNC_REG = "TRUE" *) reg [1:0] control_audio_sync1_q;
    (* ASYNC_REG = "TRUE" *) reg [1:0] control_audio_sync2_q;
    wire audio_enable = control_audio_sync2_q[0];
    wire audio_drain = control_audio_sync2_q[1];
    wire fifo_rd_ready = (audio_enable || audio_drain) && fifo_rd_valid;

    reg [31:0] underflow_count_q;
    (* preserve *) reg [31:0] underflow_gray_q;
    (* ASYNC_REG = "TRUE" *) reg [31:0] underflow_gray_sync1_q;
    (* ASYNC_REG = "TRUE" *) reg [31:0] underflow_gray_sync2_q;

    function automatic [31:0] gray_to_binary(input [31:0] gray);
        integer bit_index;
        begin
            gray_to_binary[31] = gray[31];
            for (bit_index = 30; bit_index >= 0; bit_index = bit_index - 1)
                gray_to_binary[bit_index] =
                    gray_to_binary[bit_index + 1] ^ gray[bit_index];
        end
    endfunction

    wire [31:0] underflow_count = gray_to_binary(underflow_gray_sync2_q);
    wire [31:0] status = {13'd0, control_q[0],
                          underflow_gray_sync2_q != 32'd0,
                          overflow_count_q != 32'd0, 6'd0,
                          fifo_wr_level_status_q};
    wire [31:0] link_status = {30'd0, hdmi_output_active,
                               hdmi_output_requested};

    astra_async_fifo #(
        .DATA_WIDTH(64),
        .ADDR_WIDTH(9)
    ) sample_fifo_i (
        .wr_clk(build_clk),
        .wr_rst(build_reset),
        .wr_data(fifo_wr_data),
        .wr_valid(write_sample && fifo_wr_ready),
        .wr_ready(fifo_wr_ready),
        .wr_level(fifo_wr_level),
        .rd_clk(audio_clk),
        .rd_rst(audio_reset),
        .rd_data(fifo_rd_data),
        .rd_valid(fifo_rd_valid),
        .rd_ready(fifo_rd_ready),
        .rd_level(fifo_rd_level),
        .overflow(fifo_overflow),
        .underflow(fifo_underflow)
    );

    assign audio_sample_word[0] =
        audio_enable && fifo_rd_valid ? fifo_rd_data[23:0] : 24'd0;
    assign audio_sample_word[1] =
        audio_enable && fifo_rd_valid ? fifo_rd_data[55:32] : 24'd0;

    assign s_axi_awready = !aw_pending_q && !s_axi_bvalid;
    assign s_axi_wready = !w_pending_q && !s_axi_bvalid;
    assign s_axi_arready = !s_axi_rvalid;

    always @(posedge audio_clk or posedge audio_reset) begin
        if (audio_reset) begin
            control_audio_sync1_q <= 2'd0;
            control_audio_sync2_q <= 2'd0;
            underflow_count_q <= 32'd0;
            underflow_gray_q <= 32'd0;
        end else begin
            control_audio_sync1_q <= control_q;
            control_audio_sync2_q <= control_audio_sync1_q;
            if (audio_enable && !fifo_rd_valid) begin
                underflow_count_q <= underflow_count_q + 32'd1;
                underflow_gray_q <= ((underflow_count_q + 32'd1) >> 1) ^
                                    (underflow_count_q + 32'd1);
            end
        end
    end

    always @(posedge build_clk) begin
        if (build_reset) begin
            control_q <= 2'd0;
            hdmi_output_requested <= 1'b0;
            left_q <= 24'd0;
            overflow_count_q <= 32'd0;
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
            underflow_gray_sync1_q <= 32'd0;
            underflow_gray_sync2_q <= 32'd0;
            fifo_wr_level_status_q <= 10'd0;
        end else begin
            underflow_gray_sync1_q <= underflow_gray_q;
            underflow_gray_sync2_q <= underflow_gray_sync1_q;
            fifo_wr_level_status_q <= fifo_wr_level;

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
                s_axi_bresp <= 2'b00;
                case (awaddr_q)
                    8'h0c: begin
                        if (wstrb_q != 4'hf || wdata_q[31:2] != 30'd0)
                            s_axi_bresp <= 2'b10;
                        else
                            control_q <= wdata_q[1:0];
                    end
                    8'h14: begin
                        if (wstrb_q != 4'hf || wdata_q[31:24] != 8'd0)
                            s_axi_bresp <= 2'b10;
                        else
                            left_q <= wdata_q[23:0];
                    end
                    8'h18: begin
                        if (wstrb_q != 4'hf || wdata_q[31:24] != 8'd0)
                            s_axi_bresp <= 2'b10;
                        else if (!fifo_wr_ready) begin
                            s_axi_bresp <= 2'b10;
                            overflow_count_q <= overflow_count_q + 32'd1;
                        end
                    end
                    8'h2c: begin
                        if (wstrb_q != 4'hf || wdata_q[31:1] != 31'd0)
                            s_axi_bresp <= 2'b10;
                        else
                            hdmi_output_requested <= wdata_q[0];
                    end
                    default: s_axi_bresp <= 2'b11;
                endcase
            end

            if (s_axi_rvalid && s_axi_rready)
                s_axi_rvalid <= 1'b0;
            if (s_axi_arvalid && s_axi_arready) begin
                s_axi_rvalid <= 1'b1;
                s_axi_rresp <= 2'b00;
                case (s_axi_araddr[7:0])
                    8'h00: s_axi_rdata <= DEVICE_ID;
                    8'h04: s_axi_rdata <= VERSION;
                    8'h08: s_axi_rdata <= CAPABILITIES;
                    8'h0c: s_axi_rdata <= {30'd0, control_q};
                    8'h10: s_axi_rdata <= status;
                    8'h14: s_axi_rdata <= {8'd0, left_q};
                    8'h1c: s_axi_rdata <= underflow_count;
                    8'h20: s_axi_rdata <= overflow_count_q;
                    8'h24: s_axi_rdata <= 32'd48000;
                    8'h28: s_axi_rdata <= 32'd512;
                    8'h2c: s_axi_rdata <= {31'd0,
                                            hdmi_output_requested};
                    8'h30: s_axi_rdata <= link_status;
                    default: begin
                        s_axi_rdata <= 32'd0;
                        s_axi_rresp <= 2'b11;
                    end
                endcase
            end
        end
    end

    wire unused = &{1'b0, s_axi_awprot, s_axi_arprot, fifo_rd_level,
                    fifo_overflow};
endmodule

`default_nettype wire
