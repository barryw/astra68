// Copyright (c) 2026 Astra68 contributors
//
// Demand-driven capture of the final physical RGB stream. The pixel path has
// no ready input: congestion drops the current capture instead of delaying
// display output. Completed buffers are published only after every AXI write
// response has succeeded.
`timescale 1ns/1ps
`default_nettype none

module astra_display_capture #(
    parameter [31:0] ARENA_BASE = 32'h40000000,
    parameter [31:0] ARENA_LIMIT = 32'h60000000,
    parameter integer FRAME_WIDTH = 1920,
    parameter integer FRAME_HEIGHT = 1080,
    parameter integer FIFO_ADDR_WIDTH = 6,
    parameter integer BURST_BEATS = 32,
    parameter integer AXI_ID_WIDTH = 1
) (
    input  wire                         build_clk,
    input  wire                         build_reset,
    input  wire                         pixel_clk,
    input  wire                         pixel_reset,
    input  wire                         pixel_frame_start,
    input  wire                         pixel_valid,
    input  wire [23:0]                  pixel_rgb,
    output wire                         interrupt,

    input  wire [31:0]                  s_axi_awaddr,
    input  wire [2:0]                   s_axi_awprot,
    input  wire                         s_axi_awvalid,
    output wire                         s_axi_awready,
    input  wire [31:0]                  s_axi_wdata,
    input  wire [3:0]                   s_axi_wstrb,
    input  wire                         s_axi_wvalid,
    output wire                         s_axi_wready,
    output reg  [1:0]                   s_axi_bresp,
    output reg                          s_axi_bvalid,
    input  wire                         s_axi_bready,
    input  wire [31:0]                  s_axi_araddr,
    input  wire [2:0]                   s_axi_arprot,
    input  wire                         s_axi_arvalid,
    output wire                         s_axi_arready,
    output reg  [31:0]                  s_axi_rdata,
    output reg  [1:0]                   s_axi_rresp,
    output reg                          s_axi_rvalid,
    input  wire                         s_axi_rready,

    output wire [AXI_ID_WIDTH-1:0]      m_axi_awid,
    output wire [31:0]                  m_axi_awaddr,
    output wire [7:0]                   m_axi_awlen,
    output wire [2:0]                   m_axi_awsize,
    output wire [1:0]                   m_axi_awburst,
    output wire [3:0]                   m_axi_awcache,
    output wire [2:0]                   m_axi_awprot,
    output wire [3:0]                   m_axi_awqos,
    output wire                         m_axi_awvalid,
    input  wire                         m_axi_awready,
    output wire [63:0]                  m_axi_wdata,
    output wire [7:0]                   m_axi_wstrb,
    output wire                         m_axi_wlast,
    output wire                         m_axi_wvalid,
    input  wire                         m_axi_wready,
    input  wire [AXI_ID_WIDTH-1:0]      m_axi_bid,
    input  wire [1:0]                   m_axi_bresp,
    input  wire                         m_axi_bvalid,
    output wire                         m_axi_bready
);
    localparam integer FRAME_PIXELS = FRAME_WIDTH * FRAME_HEIGHT;
    localparam integer FRAME_BYTES = FRAME_PIXELS * 3;
    localparam integer FRAME_BEATS = FRAME_BYTES / 8;
    localparam integer FRAME_BURSTS = FRAME_BEATS / BURST_BEATS;
    localparam integer PIXEL_COUNT_WIDTH = $clog2(FRAME_PIXELS + 1);
    localparam integer BEAT_COUNT_WIDTH = $clog2(FRAME_BEATS + 1);
    localparam integer BURST_COUNT_WIDTH = $clog2(FRAME_BURSTS + 1);
    localparam integer BURST_INDEX_WIDTH = $clog2(BURST_BEATS);

    localparam [31:0] DEVICE_ID = 32'h41434150; // "ACAP"
    localparam [31:0] VERSION = 32'h00010000;
    localparam [31:0] CAPABILITIES = 32'h00000007;
    localparam [1:0] WRITER_IDLE = 2'd0;
    localparam [1:0] WRITER_AW = 2'd1;
    localparam [1:0] WRITER_W = 2'd2;

    reg capture_enable_q;
    reg arm_pending_q;
    reg capture_active_q;
    reg completion_pending_q;
    reg [31:0] buffer_base_q;
    reg [31:0] active_base_q;
    reg [31:0] completed_base_q;
    reg [31:0] completed_generation_q;
    reg [31:0] generation_q;
    reg [31:0] completed_count_q;
    reg [31:0] dropped_count_q;
    reg [31:0] overflow_count_q;
    reg [31:0] axi_error_count_q;
    reg [31:0] command_error_count_q;
    reg [31:0] capture_cycles_q;
    reg [31:0] last_capture_cycles_q;
    reg last_dropped_q;
    reg last_axi_error_q;

    assign interrupt = completion_pending_q;

    // A held arm request starts only on the first physical pixel of a frame.
    // Valid control writes guarantee that enable remains asserted until the
    // active capture completes.
    (* ASYNC_REG = "TRUE" *) reg [1:0] request_pixel_sync_q;
    (* ASYNC_REG = "TRUE" *) reg [1:0] ack_pixel_sync_q;
    reg ack_toggle_q;

    reg pixel_capture_active_q;
    reg [2:0] pixel_phase_q;
    reg [63:0] pixel_pack_q;
    reg [PIXEL_COUNT_WIDTH-1:0] pixel_count_q;
    reg [BEAT_COUNT_WIDTH-1:0] pixel_beat_count_q;
    reg pixel_frame_bad_q;
    reg pixel_frame_overflow_q;
    reg pixel_start_toggle_q;
    reg pixel_done_toggle_q;
    reg pixel_done_bad_q;
    reg pixel_done_overflow_q;
    reg [BEAT_COUNT_WIDTH-1:0] pixel_done_beats_q;

    wire [23:0] pixel_bytes = {pixel_rgb[7:0], pixel_rgb[15:8],
                               pixel_rgb[23:16]};
    wire pixel_push_phase = pixel_phase_q == 3'd2 ||
                            pixel_phase_q == 3'd5 ||
                            pixel_phase_q == 3'd7;
    wire fifo_wr_valid = pixel_capture_active_q && pixel_valid &&
                         !pixel_frame_bad_q && pixel_push_phase;
    reg [63:0] fifo_wr_data;
    always @* begin
        case (pixel_phase_q)
            3'd2: fifo_wr_data = {pixel_bytes[15:0], pixel_pack_q[47:0]};
            3'd5: fifo_wr_data = {pixel_bytes[7:0], pixel_pack_q[55:0]};
            default: fifo_wr_data = {pixel_bytes, pixel_pack_q[39:0]};
        endcase
    end

    wire fifo_wr_ready;
    wire [FIFO_ADDR_WIDTH:0] fifo_wr_level;
    wire [63:0] fifo_rd_data;
    wire fifo_rd_valid;
    wire fifo_rd_ready;
    wire [FIFO_ADDR_WIDTH:0] fifo_rd_level;
    wire fifo_overflow;
    wire fifo_underflow;

    astra_async_fifo #(
        .DATA_WIDTH(64), .ADDR_WIDTH(FIFO_ADDR_WIDTH)
    ) pixel_fifo_i (
        .wr_clk(pixel_clk), .wr_rst(pixel_reset),
        .wr_data(fifo_wr_data), .wr_valid(fifo_wr_valid),
        .wr_ready(fifo_wr_ready), .wr_level(fifo_wr_level),
        .rd_clk(build_clk), .rd_rst(build_reset),
        .rd_data(fifo_rd_data), .rd_valid(fifo_rd_valid),
        .rd_ready(fifo_rd_ready), .rd_level(fifo_rd_level),
        .overflow(fifo_overflow), .underflow(fifo_underflow)
    );

    always @(posedge pixel_clk or posedge pixel_reset) begin
        if (pixel_reset) begin
            request_pixel_sync_q <= 2'd0;
            ack_pixel_sync_q <= 2'd0;
            pixel_capture_active_q <= 1'b0;
            pixel_phase_q <= 3'd0;
            pixel_pack_q <= 64'd0;
            pixel_count_q <= {PIXEL_COUNT_WIDTH{1'b0}};
            pixel_beat_count_q <= {BEAT_COUNT_WIDTH{1'b0}};
            pixel_frame_bad_q <= 1'b0;
            pixel_frame_overflow_q <= 1'b0;
            pixel_start_toggle_q <= 1'b0;
            pixel_done_toggle_q <= 1'b0;
            pixel_done_bad_q <= 1'b0;
            pixel_done_overflow_q <= 1'b0;
            pixel_done_beats_q <= {BEAT_COUNT_WIDTH{1'b0}};
        end else begin
            request_pixel_sync_q <= {request_pixel_sync_q[0], arm_pending_q};
            ack_pixel_sync_q <= {ack_pixel_sync_q[0], ack_toggle_q};

            if (pixel_capture_active_q && pixel_frame_start) begin
                // A new frame before the expected pixel count proves that the
                // previous stream was incomplete.
                pixel_capture_active_q <= 1'b0;
                pixel_done_bad_q <= 1'b1;
                pixel_done_overflow_q <= pixel_frame_overflow_q;
                pixel_done_beats_q <= pixel_beat_count_q;
                pixel_done_toggle_q <= ~pixel_done_toggle_q;
            end else if (!pixel_capture_active_q && pixel_frame_start &&
                         pixel_valid && request_pixel_sync_q[1] &&
                         pixel_done_toggle_q == ack_pixel_sync_q[1]) begin
                pixel_capture_active_q <= 1'b1;
                pixel_phase_q <= 3'd1;
                pixel_pack_q <= {40'd0, pixel_bytes};
                pixel_count_q <= {{(PIXEL_COUNT_WIDTH-1){1'b0}}, 1'b1};
                pixel_beat_count_q <= {BEAT_COUNT_WIDTH{1'b0}};
                pixel_frame_bad_q <= 1'b0;
                pixel_frame_overflow_q <= 1'b0;
                pixel_start_toggle_q <= ~pixel_start_toggle_q;
            end else if (pixel_capture_active_q && pixel_valid) begin
                case (pixel_phase_q)
                    3'd0: pixel_pack_q <= {40'd0, pixel_bytes};
                    3'd1: pixel_pack_q[47:24] <= pixel_bytes;
                    3'd2: pixel_pack_q <= {56'd0, pixel_bytes[23:16]};
                    3'd3: pixel_pack_q[31:8] <= pixel_bytes;
                    3'd4: pixel_pack_q[55:32] <= pixel_bytes;
                    3'd5: pixel_pack_q <= {48'd0, pixel_bytes[23:8]};
                    3'd6: pixel_pack_q[39:16] <= pixel_bytes;
                    default: pixel_pack_q <= 64'd0;
                endcase
                pixel_phase_q <= pixel_phase_q + 3'd1;
                pixel_count_q <= pixel_count_q + 1'b1;
                if (fifo_wr_valid && fifo_wr_ready)
                    pixel_beat_count_q <= pixel_beat_count_q + 1'b1;
                if (fifo_wr_valid && !fifo_wr_ready) begin
                    pixel_frame_bad_q <= 1'b1;
                    pixel_frame_overflow_q <= 1'b1;
                end

                if (pixel_count_q == FRAME_PIXELS - 1) begin
                    pixel_capture_active_q <= 1'b0;
                    pixel_done_bad_q <= pixel_frame_bad_q ||
                        (fifo_wr_valid && !fifo_wr_ready);
                    pixel_done_overflow_q <= pixel_frame_overflow_q ||
                        (fifo_wr_valid && !fifo_wr_ready);
                    pixel_done_beats_q <= pixel_beat_count_q +
                        ((fifo_wr_valid && fifo_wr_ready) ? 1'b1 : 1'b0);
                    pixel_done_toggle_q <= ~pixel_done_toggle_q;
                end
            end
        end
    end

    (* ASYNC_REG = "TRUE" *) reg [1:0] start_build_sync_q;
    (* ASYNC_REG = "TRUE" *) reg [1:0] done_build_sync_q;
    (* ASYNC_REG = "TRUE" *) reg [BEAT_COUNT_WIDTH-1:0] done_beats_meta_q;
    (* ASYNC_REG = "TRUE" *) reg [BEAT_COUNT_WIDTH-1:0] done_beats_sync_q;
    (* ASYNC_REG = "TRUE" *) reg [1:0] done_bad_build_sync_q;
    (* ASYNC_REG = "TRUE" *) reg [1:0] done_overflow_build_sync_q;
    (* ASYNC_REG = "TRUE" *) reg [1:0] overflow_build_sync_q;
    reg start_seen_q;
    reg done_seen_toggle_q;
    reg done_sample_pending_q;
    reg frame_done_seen_q;
    reg frame_bad_build_q;
    reg frame_overflow_build_q;
    reg [BEAT_COUNT_WIDTH-1:0] frame_target_beats_q;
    reg frame_axi_error_q;
    reg [BEAT_COUNT_WIDTH-1:0] beats_consumed_q;
    reg [BURST_COUNT_WIDTH-1:0] bursts_issued_q;
    reg [15:0] outstanding_q;
    reg [1:0] writer_state_q;
    reg [BURST_INDEX_WIDTH-1:0] writer_beat_q;
    reg [31:0] writer_address_q;

    wire writer_last_accept = writer_state_q == WRITER_W &&
        fifo_rd_valid && m_axi_wready &&
        writer_beat_q == BURST_BEATS - 1;
    wire response_accept = m_axi_bvalid && m_axi_bready;
    wire discard_pop = writer_state_q == WRITER_IDLE && frame_done_seen_q &&
        frame_bad_build_q && fifo_rd_valid &&
        beats_consumed_q < frame_target_beats_q;
    assign fifo_rd_ready = (writer_state_q == WRITER_W && m_axi_wready) ||
                           discard_pop;

    assign m_axi_awid = {AXI_ID_WIDTH{1'b0}};
    assign m_axi_awaddr = writer_address_q;
    assign m_axi_awlen = BURST_BEATS - 1;
    assign m_axi_awsize = 3'b011;
    assign m_axi_awburst = 2'b01;
    assign m_axi_awcache = 4'b0011;
    assign m_axi_awprot = 3'b000;
    assign m_axi_awqos = 4'b0000;
    assign m_axi_awvalid = writer_state_q == WRITER_AW;
    assign m_axi_wdata = fifo_rd_data;
    assign m_axi_wstrb = 8'hff;
    assign m_axi_wlast = writer_beat_q == BURST_BEATS - 1;
    assign m_axi_wvalid = writer_state_q == WRITER_W && fifo_rd_valid;
    assign m_axi_bready = capture_active_q || outstanding_q != 0;

    wire buffer_base_valid = buffer_base_q[7:0] == 8'd0 &&
        buffer_base_q >= ARENA_BASE &&
        {1'b0, buffer_base_q} + FRAME_BYTES <= {1'b0, ARENA_LIMIT};
    wire frame_finished = capture_active_q && frame_done_seen_q &&
        beats_consumed_q == frame_target_beats_q &&
        writer_state_q == WRITER_IDLE && outstanding_q == 0;

    reg aw_pending_q;
    reg [7:0] awaddr_q;
    reg w_pending_q;
    reg [31:0] wdata_q;
    reg [3:0] wstrb_q;
    wire write_fire = aw_pending_q && w_pending_q && !s_axi_bvalid;
    wire control_write = awaddr_q == 8'h0c;
    wire base_write = awaddr_q == 8'h14;
    wire write_address_valid = control_write || base_write;
    wire write_transport_valid = awaddr_q[1:0] == 2'b00;
    wire read_fire = s_axi_arvalid && s_axi_arready;

    assign s_axi_awready = !aw_pending_q && !s_axi_bvalid;
    assign s_axi_wready = !w_pending_q && !s_axi_bvalid;
    assign s_axi_arready = !s_axi_rvalid && !write_fire;

    always @(posedge build_clk or posedge build_reset) begin
        if (build_reset) begin
            capture_enable_q <= 1'b0;
            arm_pending_q <= 1'b0;
            capture_active_q <= 1'b0;
            completion_pending_q <= 1'b0;
            buffer_base_q <= ARENA_BASE;
            active_base_q <= 32'd0;
            completed_base_q <= 32'd0;
            completed_generation_q <= 32'd0;
            generation_q <= 32'd0;
            completed_count_q <= 32'd0;
            dropped_count_q <= 32'd0;
            overflow_count_q <= 32'd0;
            axi_error_count_q <= 32'd0;
            command_error_count_q <= 32'd0;
            capture_cycles_q <= 32'd0;
            last_capture_cycles_q <= 32'd0;
            last_dropped_q <= 1'b0;
            last_axi_error_q <= 1'b0;
            ack_toggle_q <= 1'b0;
            start_build_sync_q <= 2'd0;
            done_build_sync_q <= 2'd0;
            done_beats_meta_q <= {BEAT_COUNT_WIDTH{1'b0}};
            done_beats_sync_q <= {BEAT_COUNT_WIDTH{1'b0}};
            done_bad_build_sync_q <= 2'd0;
            done_overflow_build_sync_q <= 2'd0;
            overflow_build_sync_q <= 2'd0;
            start_seen_q <= 1'b0;
            done_seen_toggle_q <= 1'b0;
            done_sample_pending_q <= 1'b0;
            frame_done_seen_q <= 1'b0;
            frame_bad_build_q <= 1'b0;
            frame_overflow_build_q <= 1'b0;
            frame_target_beats_q <= {BEAT_COUNT_WIDTH{1'b0}};
            frame_axi_error_q <= 1'b0;
            beats_consumed_q <= {BEAT_COUNT_WIDTH{1'b0}};
            bursts_issued_q <= {BURST_COUNT_WIDTH{1'b0}};
            outstanding_q <= 16'd0;
            writer_state_q <= WRITER_IDLE;
            writer_beat_q <= {BURST_INDEX_WIDTH{1'b0}};
            writer_address_q <= 32'd0;
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
            start_build_sync_q <= {start_build_sync_q[0],
                                   pixel_start_toggle_q};
            done_build_sync_q <= {done_build_sync_q[0],
                                  pixel_done_toggle_q};
            done_beats_meta_q <= pixel_done_beats_q;
            done_beats_sync_q <= done_beats_meta_q;
            done_bad_build_sync_q <= {done_bad_build_sync_q[0],
                                      pixel_done_bad_q};
            done_overflow_build_sync_q <= {
                done_overflow_build_sync_q[0], pixel_done_overflow_q};
            overflow_build_sync_q <= {overflow_build_sync_q[0],
                                      fifo_overflow};

            if (capture_active_q)
                capture_cycles_q <= capture_cycles_q + 32'd1;

            if (start_build_sync_q[1] != start_seen_q) begin
                start_seen_q <= start_build_sync_q[1];
                if (arm_pending_q && !capture_active_q) begin
                    arm_pending_q <= 1'b0;
                    capture_active_q <= 1'b1;
                    active_base_q <= buffer_base_q;
                    capture_cycles_q <= 32'd0;
                    frame_done_seen_q <= 1'b0;
                    frame_bad_build_q <= 1'b0;
                    frame_overflow_build_q <= 1'b0;
                    frame_axi_error_q <= 1'b0;
                    frame_target_beats_q <= {BEAT_COUNT_WIDTH{1'b0}};
                    beats_consumed_q <= {BEAT_COUNT_WIDTH{1'b0}};
                    bursts_issued_q <= {BURST_COUNT_WIDTH{1'b0}};
                    writer_state_q <= WRITER_IDLE;
                    writer_address_q <= buffer_base_q;
                end
            end

            if (done_build_sync_q[1] != done_seen_toggle_q &&
                !done_sample_pending_q) begin
                done_seen_toggle_q <= done_build_sync_q[1];
                done_sample_pending_q <= 1'b1;
            end else if (done_sample_pending_q) begin
                // The payload is held until ack_toggle_q returns, and this
                // extra cycle keeps bus sampling behind the synchronized tag.
                done_sample_pending_q <= 1'b0;
                frame_done_seen_q <= 1'b1;
                frame_target_beats_q <= done_beats_sync_q;
                frame_bad_build_q <= done_bad_build_sync_q[1] ||
                    done_beats_sync_q != FRAME_BEATS;
                frame_overflow_build_q <=
                    done_overflow_build_sync_q[1];
            end

            if (writer_state_q == WRITER_IDLE && capture_active_q &&
                !frame_bad_build_q && bursts_issued_q < FRAME_BURSTS &&
                fifo_rd_level >= BURST_BEATS) begin
                writer_state_q <= WRITER_AW;
                writer_address_q <= active_base_q +
                    (bursts_issued_q * (BURST_BEATS * 8));
            end else if (writer_state_q == WRITER_AW && m_axi_awready) begin
                writer_state_q <= WRITER_W;
                writer_beat_q <= {BURST_INDEX_WIDTH{1'b0}};
            end else if (writer_state_q == WRITER_W && fifo_rd_valid &&
                         m_axi_wready) begin
                beats_consumed_q <= beats_consumed_q + 1'b1;
                if (writer_beat_q == BURST_BEATS - 1) begin
                    writer_state_q <= WRITER_IDLE;
                    writer_beat_q <= {BURST_INDEX_WIDTH{1'b0}};
                    bursts_issued_q <= bursts_issued_q + 1'b1;
                end else begin
                    writer_beat_q <= writer_beat_q + 1'b1;
                end
            end else if (discard_pop) begin
                beats_consumed_q <= beats_consumed_q + 1'b1;
            end

            case ({writer_last_accept, response_accept})
                2'b10: outstanding_q <= outstanding_q + 16'd1;
                2'b01: if (outstanding_q != 0)
                    outstanding_q <= outstanding_q - 16'd1;
                default: begin end
            endcase
            if (response_accept) begin
                if ((outstanding_q == 0 && !writer_last_accept) ||
                    m_axi_bid != {AXI_ID_WIDTH{1'b0}} ||
                    m_axi_bresp != 2'b00) begin
                    frame_axi_error_q <= 1'b1;
                    axi_error_count_q <= axi_error_count_q + 32'd1;
                end
            end

            if (frame_finished) begin
                generation_q <= generation_q + 32'd1;
                last_capture_cycles_q <= capture_cycles_q;
                last_axi_error_q <= frame_axi_error_q;
                capture_active_q <= 1'b0;
                frame_done_seen_q <= 1'b0;
                ack_toggle_q <= ~ack_toggle_q;
                if (frame_bad_build_q || frame_axi_error_q) begin
                    dropped_count_q <= dropped_count_q + 32'd1;
                    last_dropped_q <= 1'b1;
                    if (frame_overflow_build_q)
                        overflow_count_q <= overflow_count_q + 32'd1;
                end else begin
                    completed_base_q <= active_base_q;
                    completed_generation_q <= generation_q + 32'd1;
                    completed_count_q <= completed_count_q + 32'd1;
                    completion_pending_q <= 1'b1;
                    last_dropped_q <= 1'b0;
                end
            end

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
                s_axi_bresp <= write_address_valid && write_transport_valid ?
                    2'b00 : 2'b11;
                if (write_address_valid && write_transport_valid) begin
                    if (wstrb_q != 4'hf) begin
                        command_error_count_q <= command_error_count_q + 32'd1;
                    end else if (base_write) begin
                        if (arm_pending_q || capture_active_q)
                            command_error_count_q <=
                                command_error_count_q + 32'd1;
                        else
                            buffer_base_q <= wdata_q;
                    end else if (wdata_q[31:4] != 0) begin
                        command_error_count_q <= command_error_count_q + 32'd1;
                    end else begin
                        if (wdata_q[0] != capture_enable_q) begin
                            if (arm_pending_q || capture_active_q)
                                command_error_count_q <=
                                    command_error_count_q + 32'd1;
                            else
                                capture_enable_q <= wdata_q[0];
                        end
                        if (wdata_q[2])
                            completion_pending_q <= 1'b0;
                        if (wdata_q[1]) begin
                            if (!wdata_q[0] || arm_pending_q ||
                                capture_active_q ||
                                (completion_pending_q && !wdata_q[2]) ||
                                !buffer_base_valid)
                                command_error_count_q <=
                                    command_error_count_q + 32'd1;
                            else
                                arm_pending_q <= 1'b1;
                        end
                        if (wdata_q[3] && !capture_active_q) begin
                            completed_count_q <= 32'd0;
                            dropped_count_q <= 32'd0;
                            overflow_count_q <= 32'd0;
                            axi_error_count_q <= 32'd0;
                            command_error_count_q <= 32'd0;
                        end
                    end
                end
            end

            if (s_axi_rvalid && s_axi_rready)
                s_axi_rvalid <= 1'b0;
            if (read_fire) begin
                s_axi_rvalid <= 1'b1;
                s_axi_rresp <= 2'b00;
                case (s_axi_araddr[7:0])
                    8'h00: s_axi_rdata <= DEVICE_ID;
                    8'h04: s_axi_rdata <= VERSION;
                    8'h08: s_axi_rdata <= CAPABILITIES;
                    8'h0c: s_axi_rdata <= {31'd0, capture_enable_q};
                    8'h10: s_axi_rdata <= {22'd0,
                        command_error_count_q != 0, last_axi_error_q,
                        last_dropped_q, overflow_build_sync_q[1],
                        frame_done_seen_q,
                        writer_state_q != WRITER_IDLE, completion_pending_q,
                        capture_active_q, arm_pending_q, capture_enable_q};
                    8'h14: s_axi_rdata <= buffer_base_q;
                    8'h18: s_axi_rdata <= FRAME_BYTES;
                    8'h1c: s_axi_rdata <= completed_base_q;
                    8'h20: s_axi_rdata <= completed_generation_q;
                    8'h24: s_axi_rdata <= completed_count_q;
                    8'h28: s_axi_rdata <= dropped_count_q;
                    8'h2c: s_axi_rdata <= overflow_count_q;
                    8'h30: s_axi_rdata <= axi_error_count_q;
                    8'h34: s_axi_rdata <= command_error_count_q;
                    8'h38: s_axi_rdata <= last_capture_cycles_q;
                    default: begin
                        s_axi_rdata <= 32'd0;
                        s_axi_rresp <= 2'b11;
                    end
                endcase
            end
        end
    end

    wire unused = &{1'b0, s_axi_awprot, s_axi_arprot, fifo_wr_level,
                    fifo_underflow};

`ifndef SYNTHESIS
    initial begin
        if (FRAME_PIXELS < 8 || FRAME_PIXELS % 8 != 0)
            $fatal(1, "capture frame must contain a multiple of eight pixels");
        if (FRAME_BEATS % BURST_BEATS != 0)
            $fatal(1, "capture frame must contain complete AXI bursts");
        if (BURST_BEATS < 2 || BURST_BEATS > 256 ||
            (BURST_BEATS & (BURST_BEATS - 1)) != 0)
            $fatal(1, "capture burst length must be a power of two in 2..256");
        if ((BURST_BEATS * 8) > 4096)
            $fatal(1, "capture burst may not exceed one 4 KiB boundary");
        if ((1 << FIFO_ADDR_WIDTH) < BURST_BEATS)
            $fatal(1, "capture FIFO must hold one complete burst");
    end
`endif
endmodule

`default_nettype wire
