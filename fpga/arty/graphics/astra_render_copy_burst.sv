// Copyright (c) 2026 Astra68 contributors
//
// Bounded 64-bit AXI mover for validated same-format copies and solid fills,
// and (pixel mode) format-converting and source-over blended BLITs.
// A planner splits each row into page-bounded chunks and reads ahead into up
// to SLOTS chunk buffers while earlier chunks are written, so throughput does
// not depend on memory latency. A chunk is written only after all of its
// source beats have arrived, and chunks are written in planning order; the
// blitter walks overlapping copies in the direction where no chunk's writes
// reach a later chunk's source, so reading ahead keeps memmove semantics.
// Source bytes are realigned onto the destination lanes inside the mover;
// AXI accesses remain aligned, full-width INCR bursts.
//
// Pixel mode streams one pixel per clock: each chunk's source (and, when
// blending, destination) beats are read ahead into slots, every source pixel
// is expanded to ARGB, composited source-over as the blitter does (straight
// alpha, a' = m(a, opacity), out = m(src, a') + m(dst, 255 - a')), packed to
// the destination format and merged into full-width W beats. Unblended
// conversion is the same datapath with a' = 255 and a zero destination, which
// is exact because m(x, 255) = x. A chunk whose source pixels are all opaque
// at opacity 255 does not depend on the destination, so its destination read
// is skipped.
`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module astra_render_copy_burst #(
    parameter integer AXI_ID_WIDTH = 6,
    parameter [AXI_ID_WIDTH-1:0] READ_ID = {AXI_ID_WIDTH{1'b0}},
    parameter [AXI_ID_WIDTH-1:0] WRITE_ID = {AXI_ID_WIDTH{1'b0}},
    // Write bursts in flight before the next chunk waits for a response.
    parameter integer MAX_POSTED_WRITES = 16,
    // POSTED: write responses belong to the command processor; the mover
    // finishes once every burst is issued.
    parameter bit POSTED = 1'b0
) (
    input wire clk, input wire reset, input wire start, input wire abort,
    input wire reverse,
    // Fill mode: no reads; every destination beat is fill_beat.
    input wire fill, input wire [63:0] fill_beat,
    // Blend: read the destination and write it back composited under a
    // straight-alpha ARGB color (source-over, opacity 255), three pipeline
    // stages after realignment, one beat per clock.
    input wire blend, input wire [31:0] blend_argb, input wire [7:0] blend_format,
    // Pixel mode: source_format -> blend_format (RGB565, XRGB8888 or
    // ARGB8888, forward only), composited under opacity when pixel_blend.
    input wire pixel, input wire pixel_blend, input wire [7:0] opacity,
    input wire [7:0] source_format,
    input wire [31:0] source_address,
    input wire [31:0] destination_address, input wire [31:0] source_pitch,
    input wire [31:0] destination_pitch, input wire [17:0] row_bytes,
    input wire [15:0] row_count, output reg busy, output reg done,
    output reg aborted, output reg read_error, output reg write_error,
    output reg [31:0] fault_detail, output reg [31:0] bytes_copied,
    output wire [AXI_ID_WIDTH-1:0] m_axi_arid,
    output wire [31:0] m_axi_araddr, output wire [7:0] m_axi_arlen,
    output wire [2:0] m_axi_arsize, output wire [1:0] m_axi_arburst,
    output wire [3:0] m_axi_arcache, output wire [2:0] m_axi_arprot,
    output wire [3:0] m_axi_arqos, output wire m_axi_arvalid,
    input wire m_axi_arready, input wire [AXI_ID_WIDTH-1:0] m_axi_rid,
    input wire [63:0] m_axi_rdata, input wire [1:0] m_axi_rresp,
    input wire m_axi_rlast, input wire m_axi_rvalid,
    output wire m_axi_rready, output wire [AXI_ID_WIDTH-1:0] m_axi_awid,
    output wire [31:0] m_axi_awaddr, output wire [7:0] m_axi_awlen,
    output wire [2:0] m_axi_awsize, output wire [1:0] m_axi_awburst,
    output wire [3:0] m_axi_awcache, output wire [2:0] m_axi_awprot,
    output wire [3:0] m_axi_awqos, output wire m_axi_awvalid,
    input wire m_axi_awready, output wire [63:0] m_axi_wdata,
    output wire [7:0] m_axi_wstrb, output wire m_axi_wlast,
    output wire m_axi_wvalid, input wire m_axi_wready,
    input wire [AXI_ID_WIDTH-1:0] m_axi_bid,
    input wire [1:0] m_axi_bresp, input wire m_axi_bvalid,
    output wire m_axi_bready
);
    // Chunk slots of 32 beats each; a chunk is at most 248 bytes plus lane
    // offset, so it never needs more than 32 beats on either side.
    localparam integer SLOTS = 8;

    localparam [3:0] P_IDLE = 4'd0, P_PLAN = 4'd1, P_SOURCE_LIMIT = 4'd2,
        P_DESTINATION_LIMIT = 4'd3, P_CHUNK = 4'd4, P_START = 4'd5,
        P_ADDRESS = 4'd6, P_PUSH = 4'd7, P_DONE = 4'd8;
    localparam [1:0] W_IDLE = 2'd0, W_AW = 2'd1, W_DATA = 2'd2;

    function automatic [7:0] low_strobes(input [2:0] lanes);
        begin
            low_strobes = lanes == 3'd0 ? 8'hff :
                (9'h001 << lanes) - 9'h001;
        end
    endfunction

    function automatic [12:0] page_bytes(
        input [31:0] address, input reverse_direction
    );
        begin
            if (reverse_direction)
                page_bytes = address[11:0] == 12'd0 ?
                    13'd4096 : {1'b0, address[11:0]};
            else
                page_bytes = 13'd4096 - {1'b0, address[11:0]};
        end
    endfunction

    function automatic [8:0] chunk_capacity(
        input [2:0] source_lane, input [2:0] destination_lane
    );
        begin
            chunk_capacity = 9'd248 - {6'd0,
                source_lane > destination_lane ?
                    source_lane : destination_lane};
        end
    endfunction

    // ---- command state ---------------------------------------------------
    reg reverse_q, fill_q, abort_pending, stop_q;
    reg blend_q;
    reg pixel_q, pixel_blend_q;
    reg [7:0] opacity_q, source_format_q;
    reg [7:0] blend_format_q;
    reg [31:0] blend_argb_q;
    reg [63:0] fill_beat_q;
    reg [17:0] row_bytes_q;
    reg [31:0] source_pitch_q, destination_pitch_q;

    // ---- planner -----------------------------------------------------------
    reg [3:0] plan_state;
    reg [15:0] rows_remaining_q;
    reg [31:0] source_row_address_q, destination_row_address_q,
        planning_source_address_q, planning_destination_address_q;
    reg [17:0] byte_cursor_q, bytes_remaining_q, chunk_start_q,
        remaining_limit_q;
    reg [12:0] source_limit_q, source_remaining_limit_q, destination_limit_q;
    reg [7:0] chunk_bytes_q;
    reg [8:0] forward_chunk_limit_q;
    reg chunk_finishes_row_q;
    reg [31:0] chunk_source_address_q;
    reg [28:0] chunk_destination_beat_q;
    reg [2:0] chunk_source_lane_q, chunk_destination_lane_q;
    reg [5:0] read_beats_q, write_beats_q;

    // Pixel mode walks destination bytes; the source advances by the
    // source/destination bytes-per-pixel ratio (2, 1 or 1/2).
    localparam [7:0] FORMAT_RGB565 = `ASTRA_RENDER_FORMAT_RGB565;
    localparam [7:0] FORMAT_XRGB8888 = `ASTRA_RENDER_FORMAT_XRGB8888;
    localparam [7:0] FORMAT_ARGB8888 = `ASTRA_RENDER_FORMAT_ARGB8888;
    wire source_wide = source_format_q != FORMAT_RGB565;
    wire destination_wide = blend_format_q != FORMAT_RGB565;
    wire source_up = pixel_q && source_wide && !destination_wide;
    wire source_down = pixel_q && !source_wide && destination_wide;
    function automatic [18:0] source_span(input [17:0] bytes,
                                          input up, input down);
        begin
            source_span = up ? {bytes, 1'b0} :
                down ? {2'd0, bytes[17:1]} : {1'b0, bytes};
        end
    endfunction
    wire [31:0] cursor_source_address = source_row_address_q +
        {13'd0, source_span(byte_cursor_q, source_up, source_down)};
    wire [31:0] cursor_destination_address = destination_row_address_q +
        {14'd0, byte_cursor_q};
    wire [31:0] chunk_source_logical = source_row_address_q +
        {13'd0, source_span(chunk_start_q, source_up, source_down)};
    wire [8:0] chunk_source_bytes = source_up ? {chunk_bytes_q, 1'b0} :
        source_down ? {2'd0, chunk_bytes_q[7:1]} : {1'b0, chunk_bytes_q};
    // Pixel chunks keep both sides within 31 beats and whole pixels.
    wire [8:0] chunk_limit = !pixel_q ? forward_chunk_limit_q :
        source_up ? 9'd120 : 9'd240;
    wire [12:0] source_page = page_bytes(planning_source_address_q,
                                         reverse_q);
    wire [31:0] chunk_destination_logical = destination_row_address_q +
        {14'd0, chunk_start_q};
    wire [2:0] next_chunk_source_lane = chunk_source_logical[2:0] +
        chunk_bytes_q[2:0];
    wire [2:0] next_chunk_destination_lane =
        chunk_destination_logical[2:0] + chunk_bytes_q[2:0];

    // ---- chunk queue (planning order == slot order) ------------------------
    (* ram_style = "distributed", ramstyle = "MLAB" *)
    reg [28:0] queue_destination_beat [0:SLOTS-1];
    // {bytes[7:3], finishes_row, source_lane, destination_lane, bytes[2:0],
    //  write_beats}
    (* ram_style = "distributed", ramstyle = "MLAB" *)
    reg [20:0] queue_shape [0:SLOTS-1];
    reg [5:0] slot_read_beats_q [0:SLOTS-1];
    reg [2:0] queue_head_q, queue_tail_q;
    reg [3:0] queue_count_q;
    reg [SLOTS-1:0] slot_complete_q, slot_error_q;
    wire [20:0] push_shape = {chunk_bytes_q[7:3], chunk_finishes_row_q,
        chunk_source_lane_q, chunk_destination_lane_q, chunk_bytes_q[2:0],
        write_beats_q};
    // Pixel blending: each chunk's destination beats, read after its source
    // unless every source pixel is opaque at opacity 255 (skipped).
    (* ram_style = "distributed", ramstyle = "MLAB" *)
    reg [28:0] queue_read_beat [0:SLOTS-1];
    reg [5:0] slot_write_beats_q [0:SLOTS-1];
    reg [SLOTS-1:0] slot_dest_q, slot_skip_q, slot_opaque_q,
        slot_skip_first_q, slot_skip_last_q;
    reg [2:0] chunk_source_end_lane_q;
    reg [2:0] d_slot_q;
    reg [3:0] d_count_q;

    // ---- read channel ------------------------------------------------------
    reg ar_valid_q;
    reg [31:0] ar_address_q;
    reg [5:0] ar_beats_q;
    reg [4:0] reads_outstanding_q;
    reg ar_kind_q;
    reg [2:0] ar_slot_q;
    // Outstanding bursts in issue order: {destination, slot}.
    (* ram_style = "distributed", ramstyle = "MLAB" *)
    reg [3:0] rq_q [0:15];
    reg [3:0] rq_head_q, rq_tail_q;
    wire [3:0] rq_head = rq_q[rq_head_q];
    wire r_kind = rq_head[3];
    wire [2:0] r_slot_q = rq_head[2:0];
    reg [5:0] r_beat_q;
    reg r_draining_q, read_error_seen;

    (* ramstyle = "M20K", ram_style = "block" *)
    reg [63:0] data [0:SLOTS*32-1];
    (* ramstyle = "M20K", ram_style = "block" *)
    reg [63:0] destination_data [0:SLOTS*32-1];

    wire r_accept = m_axi_rvalid && m_axi_rready;
    wire read_bad = m_axi_rid != READ_ID || m_axi_rresp != 2'b00;
    wire read_expected_last = r_beat_q + 6'd1 == (r_kind ?
        slot_write_beats_q[r_slot_q] : slot_read_beats_q[r_slot_q]);

    // ---- write side ----------------------------------------------------------
    reg [1:0] write_state;
    reg [28:0] w_destination_beat_q;
    reg [5:0] w_write_beats_q, w_read_beats_q;
    reg [2:0] w_shift_q;
    reg w_prefix_q, w_finishes_row_q;
    reg [7:0] w_first_strobes_q, w_last_strobes_q;
    reg [2:0] w_slot_q;
    reg [6:0] w_issue_q;      // stream index j, 0..write_beats
    reg [5:0] w_out_q;        // output beat index k
    reg [4:0] writes_outstanding_q;
    reg [1:0] w_open_q;       // pixel mode: AW accepted, WLAST not yet
    reg [7:0] w_s_off_q, w_d_off_q, w_pixels_q, w_px_count_q;
    reg w_skip_q;
    // Stream pipeline: A (RAM address), B (RAM data), C (the W beat).
    reg a_valid_q, a_zero_q, a_first_q;
    reg [7:0] a_address_q;
    reg b_valid_q, b_zero_q, b_first_q;
    reg [63:0] b_data_q;
    reg [63:0] previous_q;
    reg c_valid_q, c_last_q;
    reg [63:0] c_data_q;
    reg [7:0] c_strobes_q;

    wire w_accept = m_axi_wvalid && m_axi_wready;
    // Without blending the W beat leaves from stage C, with it from F, in
    // pixel mode from G.
    reg g_valid_q, g_last_q;
    reg [63:0] g_data_q;
    reg [7:0] g_strobes_q;
    wire advance;
    wire b_accept = m_axi_bvalid && m_axi_bready;
    wire b_bad = m_axi_bid != WRITE_ID || m_axi_bresp != 2'b00;
    wire w_last_accept = w_accept && m_axi_wlast;
    wire writes_drained = writes_outstanding_q == 5'd0;
    wire [63:0] b_value = b_zero_q ? 64'd0 : b_data_q;
    wire [127:0] realignment_pair = {b_value, previous_q};
    wire [63:0] realigned = realignment_pair >> {w_shift_q, 3'b000};

    // ---- source-over blend stages D (products), E (sums), F (pack) --------
    // m(x, y) = round(x * y / 255), exactly the blitter's divide.
    function automatic [7:0] fold255(input [15:0] product);
        reg [16:0] adjusted, folded;
        begin
            adjusted = {1'b0, product} + 17'd128;
            folded = adjusted + {9'd0, adjusted[16:8]};
            fold255 = folded[15:8];
        end
    endfunction
    // Per command: a' = m(a, 255), K = (a', m(r, a'), m(g, a'), m(b, a')).
    wire [7:0] blend_alpha = fold255(blend_argb_q[31:24] * 8'd255);
    reg [7:0] blend_inverse_q;
    reg [7:0] blend_k_q [0:3];
    always @(posedge clk) begin
        blend_inverse_q <= 8'd255 - blend_alpha;
        blend_k_q[0] <= blend_alpha;
        blend_k_q[1] <= fold255(blend_argb_q[23:16] * blend_alpha);
        blend_k_q[2] <= fold255(blend_argb_q[15:8] * blend_alpha);
        blend_k_q[3] <= fold255(blend_argb_q[7:0] * blend_alpha);
    end
    // Twelve channel slots per beat: RGB565 pixel p uses slots 3p..3p+2
    // (R, G, B, expanded by bit replication as the blitter does); 32-bit
    // pixel q uses slots 4q..4q+3 (A, R, G, B). Memory is big-endian.
    reg [7:0] blend_channel [0:11];
    reg [1:0] blend_slot_kind [0:11];
    integer slot;
    always @* begin
        for (slot = 0; slot < 12; slot = slot + 1) begin
            blend_channel[slot] = 8'd0;
            blend_slot_kind[slot] = 2'd0;
        end
        if (blend_format_q == FORMAT_RGB565) begin
            for (slot = 0; slot < 4; slot = slot + 1) begin : rgb565_unpack
                reg [15:0] v;
                v = {c_data_q[slot * 16 +: 8], c_data_q[slot * 16 + 8 +: 8]};
                blend_channel[slot * 3] = {v[15:11], v[15:13]};
                blend_channel[slot * 3 + 1] = {v[10:5], v[10:9]};
                blend_channel[slot * 3 + 2] = {v[4:0], v[4:2]};
                blend_slot_kind[slot * 3] = 2'd1;
                blend_slot_kind[slot * 3 + 1] = 2'd2;
                blend_slot_kind[slot * 3 + 2] = 2'd3;
            end
        end else begin
            for (slot = 0; slot < 8; slot = slot + 1) begin
                blend_channel[slot] = slot % 4 == 0 &&
                    blend_format_q == FORMAT_XRGB8888 ?
                        8'hff : c_data_q[slot * 8 +: 8];
                blend_slot_kind[slot] = slot % 4;
            end
        end
    end
    reg d_valid_q, d_last_q, e_valid_q, e_last_q, f_valid_q, f_last_q;
    reg [7:0] d_strobes_q, e_strobes_q, f_strobes_q;
    assign advance = pixel_q ? !g_valid_q || m_axi_wready :
        blend_q ? !f_valid_q || m_axi_wready :
        !c_valid_q || m_axi_wready;
    (* multstyle = "dsp" *) reg [15:0] d_product_q [0:11];
    reg [7:0] e_channel_q [0:11];
    reg [63:0] f_data_q;
    reg [8:0] blend_sum;
    reg [63:0] blend_packed;
    always @* begin
        blend_packed = 64'd0;
        if (blend_format_q == FORMAT_RGB565) begin
            for (slot = 0; slot < 4; slot = slot + 1) begin : rgb565_pack
                reg [15:0] v;
                v = {e_channel_q[slot * 3][7:3], e_channel_q[slot * 3 + 1][7:2],
                     e_channel_q[slot * 3 + 2][7:3]};
                blend_packed[slot * 16 +: 16] = {v[7:0], v[15:8]};
            end
        end else begin
            for (slot = 0; slot < 8; slot = slot + 1)
                blend_packed[slot * 8 +: 8] = slot % 4 == 0 &&
                    blend_format_q == FORMAT_XRGB8888 ?
                        8'hff : e_channel_q[slot];
        end
    end

    // ---- pixel pipeline: A (RAM addresses), B (RAM data), X (expand),
    // P1 (a * opacity), P2 (a'), P3 (products), P4 (divides), P5 (sums),
    // G (pack into the W beat) -----------------------------------------
    function automatic [31:0] expand_argb(input [7:0] format,
                                          input [31:0] value);
        begin
            if (format == FORMAT_RGB565)
                expand_argb = {8'hff, value[15:11], value[15:13],
                               value[10:5], value[10:9],
                               value[4:0], value[4:2]};
            else if (format == FORMAT_XRGB8888)
                expand_argb = {8'hff, value[23:0]};
            else
                expand_argb = value;
        end
    endfunction
    // Big-endian pixel at a lane of a beat (never straddles: surfaces are
    // aligned to their pixel size).
    function automatic [31:0] beat_pixel(input [63:0] beat, input [2:0] lane,
                                         input wide);
        reg [63:0] w;
        begin
            w = beat >> {lane[2:1], 4'b0000};
            beat_pixel = wide ? {w[7:0], w[15:8], w[23:16], w[31:24]} :
                {16'd0, w[7:0], w[15:8]};
        end
    endfunction
    function automatic [7:0] sat255(input [8:0] v);
        begin
            sat255 = v[8] ? 8'hff : v[7:0];
        end
    endfunction
    reg [9:0] ad_address_q;
    reg a_last_q, a_end_q, a_skip_q;
    reg [2:0] a_slane_q, a_dlane_q;
    reg [63:0] bd_data_q;
    reg pb_last_q, pb_end_q, pb_skip_q;
    reg [2:0] pb_slane_q, pb_dlane_q;
    // Sidebands per stage: {valid, last, end, destination lane}.
    reg [5:0] x_side_q, p1_side_q, p2_side_q, p3_side_q, p4_side_q, p5_side_q;
    reg [31:0] x_source_q, x_destination_q;
    (* multstyle = "dsp" *) reg [15:0] p1_alpha_product_q;
    reg [23:0] p1_rgb_q, p2_rgb_q;
    reg [31:0] p1_destination_q, p2_destination_q;
    reg [7:0] p2_alpha_q, p3_alpha_q, p4_alpha_q;
    (* multstyle = "dsp" *) reg [15:0] p3_product_q [0:6];
    reg [7:0] p4_fold_q [0:6];
    reg [31:0] p5_argb_q;
    reg g_fresh_q;            // G holds no pixel of the next beat yet
    wire [31:0] b_source_pixel = beat_pixel(b_data_q, pb_slane_q, source_wide);
    wire [31:0] b_destination_pixel = beat_pixel(bd_data_q, pb_dlane_q,
                                                 destination_wide);
    wire [31:0] b_source_argb = expand_argb(source_format_q, b_source_pixel);
    wire [7:0] effective_opacity = pixel_blend_q ? opacity_q : 8'hff;
    wire [7:0] p2_inverse = 8'hff - p2_alpha_q;
    // Pack P5's ARGB into the destination format at its lane.
    wire [31:0] p5_value = blend_format_q == FORMAT_RGB565 ?
        {16'd0, p5_argb_q[23:19], p5_argb_q[15:10], p5_argb_q[7:3]} :
        blend_format_q == FORMAT_XRGB8888 ? {8'hff, p5_argb_q[23:0]} :
        p5_argb_q;
    wire [63:0] p5_lanes = (destination_wide ?
        {32'd0, p5_value[7:0], p5_value[15:8], p5_value[23:16],
         p5_value[31:24]} :
        {48'd0, p5_value[7:0], p5_value[15:8]}) << {p5_side_q[2:1], 4'b0000};
    wire [7:0] p5_strobes = (destination_wide ? 8'h0f : 8'h03) <<
        {p5_side_q[2:1], 1'b0};
    wire [7:0] pixel_step_source = source_wide ? 8'd4 : 8'd2;
    wire [7:0] pixel_step_destination = destination_wide ? 8'd4 : 8'd2;
    wire pixel_issue = pixel_q && advance && write_state == W_DATA;
    wire pixel_issue_last = pixel_issue && w_px_count_q + 8'd1 == w_pixels_q;
    // The last pixel's RAM reads happen this cycle; the slot is free after.
    wire pixel_release = pixel_q && advance && a_valid_q && a_last_q;

    // Head chunk fields, decoded as the writer takes it.
    wire [20:0] head_shape = queue_shape[queue_head_q];
    wire [5:0] head_write_beats = head_shape[5:0];
    wire [2:0] head_bytes_lane = head_shape[8:6];
    wire [2:0] head_destination_lane = head_shape[11:9];
    wire [2:0] head_source_lane = head_shape[14:12];
    wire [7:0] head_chunk_bytes = {head_shape[20:16], head_shape[8:6]};
    wire head_ready = queue_count_q != 4'd0 &&
        (fill_q || slot_complete_q[queue_head_q]) &&
        (!pixel_blend_q || slot_dest_q[queue_head_q]);
    wire head_failed = head_ready && slot_error_q[queue_head_q];
    wire plan_stop = stop_q || abort_pending || abort;
    // Destination reads, in chunk order, once the chunk's source is in.
    wire dest_decide = pixel_blend_q && d_count_q != 4'd0 &&
        slot_complete_q[d_slot_q];
    wire dest_needed = !(opacity_q == 8'hff && slot_opaque_q[d_slot_q]);
    wire push_chunk = plan_state == P_PUSH && !plan_stop &&
        queue_count_q != SLOTS && (fill_q || !ar_valid_q);
    // The planner's source read goes first; a chunk's destination read may
    // follow any later source read, the read queue records the order.
    wire ar_free = !ar_valid_q && !push_chunk;
    wire dest_issue = dest_decide && dest_needed && !plan_stop && ar_free;
    wire dest_decided = dest_decide && (!dest_needed || plan_stop || ar_free);
    // Nothing is in flight and the writer has reached a stopping point.
    wire all_idle = write_state == W_IDLE && !a_valid_q && !b_valid_q &&
        !c_valid_q && !d_valid_q && !e_valid_q && !f_valid_q &&
        !x_side_q[5] && !p1_side_q[5] && !p2_side_q[5] && !p3_side_q[5] &&
        !p4_side_q[5] && !p5_side_q[5] && !g_valid_q &&
        !ar_valid_q && reads_outstanding_q == 5'd0 &&
        writes_drained && (queue_count_q == 4'd0 || write_error ||
                           abort_pending || head_failed);

    assign m_axi_arid = READ_ID;
    assign m_axi_araddr = ar_address_q;
    assign m_axi_arlen = {2'd0, ar_beats_q} - 8'd1;
    assign m_axi_arsize = 3'b011;
    assign m_axi_arburst = 2'b01;
    assign m_axi_arcache = 4'b0011;
    assign m_axi_arprot = 3'b000;
    assign m_axi_arqos = 4'b0000;
    assign m_axi_arvalid = ar_valid_q;
    assign m_axi_rready = busy && reads_outstanding_q != 5'd0;
    assign m_axi_awid = WRITE_ID;
    assign m_axi_awaddr = {w_destination_beat_q, 3'b000};
    assign m_axi_awlen = {2'd0, w_write_beats_q} - 8'd1;
    assign m_axi_awsize = 3'b011;
    assign m_axi_awburst = 2'b01;
    assign m_axi_awcache = 4'b0011;
    assign m_axi_awprot = 3'b000;
    assign m_axi_awqos = 4'b0000;
    assign m_axi_awvalid = write_state == W_AW;
    assign m_axi_wdata = pixel_q ? g_data_q : blend_q ? f_data_q : c_data_q;
    assign m_axi_wstrb = pixel_q ? g_strobes_q :
        blend_q ? f_strobes_q : c_strobes_q;
    assign m_axi_wlast = pixel_q ? g_last_q : blend_q ? f_last_q : c_last_q;
    assign m_axi_wvalid = pixel_q ? g_valid_q :
        blend_q ? f_valid_q : c_valid_q;
    // Responses are counted, not waited for per chunk; every finish drains
    // them so no response can reach the next write owner.
    assign m_axi_bready = !POSTED && busy;

    // Chunk data: R beats in, stream reads out (one-cycle registered read).
    integer lane_slot;
    always @(posedge clk) begin
        if (r_accept && !r_draining_q && !r_kind)
            data[{r_slot_q, r_beat_q[4:0]}] <= m_axi_rdata;
        if (r_accept && !r_draining_q && r_kind)
            destination_data[{r_slot_q, r_beat_q[4:0]}] <= m_axi_rdata;
        if (advance)
            b_data_q <= data[a_address_q];
        if (advance)
            bd_data_q <= destination_data[ad_address_q];
        if (advance) begin
            pb_last_q <= a_last_q;
            pb_end_q <= a_end_q;
            pb_skip_q <= a_skip_q;
            pb_slane_q <= a_slane_q;
            pb_dlane_q <= a_dlane_q;
            x_source_q <= pixel_blend_q ? b_source_argb :
                {8'hff, b_source_argb[23:0]};
            x_destination_q <= pb_skip_q ? 32'd0 :
                expand_argb(blend_format_q, b_destination_pixel);
            p1_alpha_product_q <= x_source_q[31:24] * effective_opacity;
            p1_rgb_q <= x_source_q[23:0];
            p1_destination_q <= x_destination_q;
            p2_alpha_q <= fold255(p1_alpha_product_q);
            p2_rgb_q <= p1_rgb_q;
            p2_destination_q <= p1_destination_q;
            p3_alpha_q <= p2_alpha_q;
            p3_product_q[0] <= p2_rgb_q[23:16] * p2_alpha_q;
            p3_product_q[1] <= p2_rgb_q[15:8] * p2_alpha_q;
            p3_product_q[2] <= p2_rgb_q[7:0] * p2_alpha_q;
            p3_product_q[3] <= p2_destination_q[31:24] * p2_inverse;
            p3_product_q[4] <= p2_destination_q[23:16] * p2_inverse;
            p3_product_q[5] <= p2_destination_q[15:8] * p2_inverse;
            p3_product_q[6] <= p2_destination_q[7:0] * p2_inverse;
            p4_alpha_q <= p3_alpha_q;
            for (lane_slot = 0; lane_slot < 7; lane_slot = lane_slot + 1)
                p4_fold_q[lane_slot] <= fold255(p3_product_q[lane_slot]);
            p5_argb_q <= {
                sat255({1'b0, p4_alpha_q} + {1'b0, p4_fold_q[3]}),
                sat255({1'b0, p4_fold_q[0]} + {1'b0, p4_fold_q[4]}),
                sat255({1'b0, p4_fold_q[1]} + {1'b0, p4_fold_q[5]}),
                sat255({1'b0, p4_fold_q[2]} + {1'b0, p4_fold_q[6]})};
            // G is built in place; the W beat holds while it waits.
            if (p5_side_q[5]) begin
                g_data_q <= (g_fresh_q ? 64'd0 : g_data_q) | p5_lanes;
                g_strobes_q <= (g_fresh_q ? 8'd0 : g_strobes_q) | p5_strobes;
                g_last_q <= p5_side_q[4];
            end
        end
        if (advance) begin
            for (slot = 0; slot < 12; slot = slot + 1)
                d_product_q[slot] <= blend_channel[slot] * blend_inverse_q;
            for (slot = 0; slot < 12; slot = slot + 1) begin
                blend_sum = {1'b0, blend_k_q[blend_slot_kind[slot]]} +
                    {1'b0, fold255(d_product_q[slot])};
                e_channel_q[slot] <= blend_sum[8] ? 8'hff : blend_sum[7:0];
            end
            f_data_q <= blend_packed;
        end
    end

    integer slot_index;
    always @(posedge clk) begin
        if (reset) begin
            plan_state <= P_IDLE;
            write_state <= W_IDLE;
            reverse_q <= 1'b0;
            fill_q <= 1'b0;
            fill_beat_q <= 64'd0;
            abort_pending <= 1'b0;
            stop_q <= 1'b0;
            row_bytes_q <= 18'd0;
            source_pitch_q <= 32'd0;
            destination_pitch_q <= 32'd0;
            rows_remaining_q <= 16'd0;
            source_row_address_q <= 32'd0;
            destination_row_address_q <= 32'd0;
            planning_source_address_q <= 32'd0;
            planning_destination_address_q <= 32'd0;
            byte_cursor_q <= 18'd0;
            bytes_remaining_q <= 18'd0;
            chunk_start_q <= 18'd0;
            remaining_limit_q <= 18'd0;
            source_limit_q <= 13'd0;
            source_remaining_limit_q <= 13'd0;
            destination_limit_q <= 13'd0;
            chunk_bytes_q <= 8'd0;
            forward_chunk_limit_q <= 9'd0;
            chunk_finishes_row_q <= 1'b0;
            chunk_source_address_q <= 32'd0;
            chunk_destination_beat_q <= 29'd0;
            chunk_source_lane_q <= 3'd0;
            chunk_destination_lane_q <= 3'd0;
            read_beats_q <= 6'd0;
            write_beats_q <= 6'd0;
            queue_head_q <= 3'd0;
            queue_tail_q <= 3'd0;
            queue_count_q <= 4'd0;
            slot_complete_q <= {SLOTS{1'b0}};
            slot_error_q <= {SLOTS{1'b0}};
            for (slot_index = 0; slot_index < SLOTS;
                 slot_index = slot_index + 1) begin
                slot_read_beats_q[slot_index] <= 6'd0;
                slot_write_beats_q[slot_index] <= 6'd0;
            end
            ar_valid_q <= 1'b0;
            ar_address_q <= 32'd0;
            ar_beats_q <= 6'd0;
            reads_outstanding_q <= 5'd0;
            ar_kind_q <= 1'b0;
            ar_slot_q <= 3'd0;
            rq_head_q <= 4'd0;
            rq_tail_q <= 4'd0;
            r_beat_q <= 6'd0;
            pixel_q <= 1'b0;
            pixel_blend_q <= 1'b0;
            opacity_q <= 8'd0;
            source_format_q <= 8'd0;
            slot_dest_q <= {SLOTS{1'b0}};
            slot_skip_q <= {SLOTS{1'b0}};
            slot_opaque_q <= {SLOTS{1'b0}};
            slot_skip_first_q <= {SLOTS{1'b0}};
            slot_skip_last_q <= {SLOTS{1'b0}};
            chunk_source_end_lane_q <= 3'd0;
            d_slot_q <= 3'd0;
            d_count_q <= 4'd0;
            w_open_q <= 2'd0;
            w_s_off_q <= 8'd0;
            w_d_off_q <= 8'd0;
            w_pixels_q <= 8'd0;
            w_px_count_q <= 8'd0;
            w_skip_q <= 1'b0;
            a_last_q <= 1'b0;
            a_end_q <= 1'b0;
            a_skip_q <= 1'b0;
            a_slane_q <= 3'd0;
            a_dlane_q <= 3'd0;
            ad_address_q <= 10'd0;
            x_side_q <= 6'd0;
            p1_side_q <= 6'd0;
            p2_side_q <= 6'd0;
            p3_side_q <= 6'd0;
            p4_side_q <= 6'd0;
            p5_side_q <= 6'd0;
            g_valid_q <= 1'b0;
            g_fresh_q <= 1'b1;
            r_draining_q <= 1'b0;
            read_error_seen <= 1'b0;
            w_destination_beat_q <= 29'd0;
            w_write_beats_q <= 6'd0;
            w_read_beats_q <= 6'd0;
            w_shift_q <= 3'd0;
            w_prefix_q <= 1'b0;
            w_finishes_row_q <= 1'b0;
            w_first_strobes_q <= 8'hff;
            w_last_strobes_q <= 8'hff;
            w_slot_q <= 3'd0;
            w_issue_q <= 7'd0;
            w_out_q <= 6'd0;
            writes_outstanding_q <= 5'd0;
            a_valid_q <= 1'b0;
            a_zero_q <= 1'b0;
            a_first_q <= 1'b0;
            a_address_q <= 8'd0;
            b_valid_q <= 1'b0;
            b_zero_q <= 1'b0;
            b_first_q <= 1'b0;
            previous_q <= 64'd0;
            c_valid_q <= 1'b0;
            c_last_q <= 1'b0;
            c_data_q <= 64'd0;
            c_strobes_q <= 8'hff;
            d_valid_q <= 1'b0;
            d_last_q <= 1'b0;
            d_strobes_q <= 8'hff;
            e_valid_q <= 1'b0;
            e_last_q <= 1'b0;
            e_strobes_q <= 8'hff;
            f_valid_q <= 1'b0;
            f_last_q <= 1'b0;
            f_strobes_q <= 8'hff;
            blend_q <= 1'b0;
            blend_format_q <= 8'd0;
            blend_argb_q <= 32'd0;
            busy <= 1'b0;
            done <= 1'b0;
            aborted <= 1'b0;
            read_error <= 1'b0;
            write_error <= 1'b0;
            fault_detail <= 32'd0;
            bytes_copied <= 32'd0;
        end else begin
            done <= 1'b0;
            if (abort && busy)
                abort_pending <= 1'b1;

            // ---- write responses --------------------------------------------
            case ({w_last_accept, b_accept})
                2'b10: if (!POSTED)
                    writes_outstanding_q <= writes_outstanding_q + 5'd1;
                2'b01: writes_outstanding_q <= writes_outstanding_q - 5'd1;
                default: begin end
            endcase
            if (b_accept && b_bad) begin
                write_error <= 1'b1;
                stop_q <= 1'b1;
                if (!write_error && !read_error)
                    fault_detail <= {16'h0003,
                        {{(8-AXI_ID_WIDTH){1'b0}}, m_axi_bid},
                        6'd0, m_axi_bresp};
            end

            case ({m_axi_awvalid && m_axi_awready, w_last_accept})
                2'b10: w_open_q <= w_open_q + 2'd1;
                2'b01: w_open_q <= w_open_q - 2'd1;
                default: begin end
            endcase

            // ---- read address -------------------------------------------------
            if (ar_valid_q && m_axi_arready) begin
                ar_valid_q <= 1'b0;
                rq_q[rq_tail_q] <= {ar_kind_q, ar_slot_q};
                rq_tail_q <= rq_tail_q + 4'd1;
            end
            if (r_accept && m_axi_rlast)
                rq_head_q <= rq_head_q + 4'd1;
            case ({ar_valid_q && m_axi_arready, r_accept && m_axi_rlast})
                2'b10: reads_outstanding_q <= reads_outstanding_q + 5'd1;
                2'b01: reads_outstanding_q <= reads_outstanding_q - 5'd1;
                default: begin end
            endcase

            // ---- destination reads (pixel blending), in chunk order ---------
            if (dest_issue) begin
                ar_valid_q <= 1'b1;
                ar_address_q <= {queue_read_beat[d_slot_q], 3'b000};
                ar_beats_q <= slot_write_beats_q[d_slot_q];
                ar_kind_q <= 1'b1;
                ar_slot_q <= d_slot_q;
                slot_skip_q[d_slot_q] <= 1'b0;
            end else if (dest_decided) begin
                // Opaque at opacity 255, or stopping (then failed).
                slot_dest_q[d_slot_q] <= 1'b1;
                slot_skip_q[d_slot_q] <= 1'b1;
                if (dest_needed)
                    slot_error_q[d_slot_q] <= 1'b1;
            end
            if (dest_decided)
                d_slot_q <= d_slot_q + 3'd1;
            case ({push_chunk && pixel_blend_q, dest_decided})
                2'b10: d_count_q <= d_count_q + 4'd1;
                2'b01: d_count_q <= d_count_q - 4'd1;
                default: begin end
            endcase

            // ---- read data: beats land in their chunk's slot ------------------
            if (r_accept) begin
                if (read_bad) begin
                    read_error <= 1'b1;
                    stop_q <= 1'b1;
                    slot_error_q[r_slot_q] <= 1'b1;
                    if (!read_error_seen)
                        fault_detail <= {16'h0002,
                            {{(8-AXI_ID_WIDTH){1'b0}}, m_axi_rid},
                            5'd0, m_axi_rlast, m_axi_rresp};
                    read_error_seen <= 1'b1;
                end
                if (m_axi_rlast) begin
                    if (!r_draining_q && !read_expected_last) begin
                        read_error <= 1'b1;
                        stop_q <= 1'b1;
                        slot_error_q[r_slot_q] <= 1'b1;
                        fault_detail <= 32'h00020001;
                    end
                    if (r_kind)
                        slot_dest_q[r_slot_q] <= 1'b1;
                    else
                        slot_complete_q[r_slot_q] <= 1'b1;
                    r_beat_q <= 6'd0;
                    r_draining_q <= 1'b0;
                end else if (!r_draining_q && read_expected_last) begin
                    // Missing RLAST: take beats until it arrives.
                    read_error <= 1'b1;
                    stop_q <= 1'b1;
                    slot_error_q[r_slot_q] <= 1'b1;
                    fault_detail <= 32'h00020002;
                    r_draining_q <= 1'b1;
                end else if (!r_draining_q) begin
                    r_beat_q <= r_beat_q + 6'd1;
                end
                // A translucent source pixel inside the chunk makes the
                // chunk depend on its destination.
                if (!r_kind && source_format_q == FORMAT_ARGB8888 &&
                    ((m_axi_rdata[7:0] != 8'hff &&
                      !(r_beat_q == 6'd0 && slot_skip_first_q[r_slot_q])) ||
                     (m_axi_rdata[39:32] != 8'hff &&
                      !(read_expected_last && slot_skip_last_q[r_slot_q]))))
                    slot_opaque_q[r_slot_q] <= 1'b0;
            end

            // ---- planner ------------------------------------------------------------
            case (plan_state)
                P_IDLE: if (start) begin
                    reverse_q <= reverse;
                    fill_q <= fill;
                    blend_q <= blend;
                    pixel_q <= pixel;
                    pixel_blend_q <= pixel && pixel_blend;
                    opacity_q <= opacity;
                    source_format_q <= source_format;
                    d_slot_q <= 3'd0;
                    d_count_q <= 4'd0;
                    slot_dest_q <= {SLOTS{1'b0}};
                    g_fresh_q <= 1'b1;
                    blend_format_q <= blend_format;
                    blend_argb_q <= blend_argb;
                    fill_beat_q <= fill_beat;
                    abort_pending <= 1'b0;
                    stop_q <= 1'b0;
                    read_error_seen <= 1'b0;
                    row_bytes_q <= row_bytes;
                    rows_remaining_q <= row_count;
                    source_pitch_q <= source_pitch;
                    destination_pitch_q <= destination_pitch;
                    source_row_address_q <= source_address;
                    destination_row_address_q <= destination_address;
                    byte_cursor_q <= reverse ? row_bytes : 18'd0;
                    bytes_remaining_q <= row_bytes;
                    forward_chunk_limit_q <= chunk_capacity(
                        source_address[2:0], destination_address[2:0]);
                    queue_head_q <= 3'd0;
                    queue_tail_q <= 3'd0;
                    slot_complete_q <= {SLOTS{1'b0}};
                    slot_error_q <= {SLOTS{1'b0}};
                    r_beat_q <= 6'd0;
                    r_draining_q <= 1'b0;
                    busy <= 1'b1;
                    aborted <= 1'b0;
                    read_error <= 1'b0;
                    write_error <= 1'b0;
                    fault_detail <= 32'd0;
                    bytes_copied <= 32'd0;
                    if (row_bytes == 18'd0 || row_count == 16'd0 ||
                        source_pitch[2:0] != 3'd0 ||
                        destination_pitch[2:0] != 3'd0) begin
                        read_error <= 1'b1;
                        fault_detail <= 32'h00010000;
                        stop_q <= 1'b1;
                        plan_state <= P_DONE;
                    end else plan_state <= P_PLAN;
                end

                P_PLAN: if (plan_stop) begin
                    plan_state <= P_DONE;
                end else begin
                    planning_source_address_q <= cursor_source_address;
                    planning_destination_address_q <=
                        cursor_destination_address;
                    if (reverse_q)
                        remaining_limit_q <= bytes_remaining_q > 18'd240 ?
                            18'd240 : bytes_remaining_q;
                    else
                        remaining_limit_q <= bytes_remaining_q >
                            {9'd0, chunk_limit} ?
                                {9'd0, chunk_limit} :
                                bytes_remaining_q;
                    plan_state <= P_SOURCE_LIMIT;
                end

                P_SOURCE_LIMIT: begin
                    // In destination bytes: the source page scaled.
                    source_limit_q <= source_up ?
                        {1'b0, source_page[12:1]} :
                        source_down ? (source_page[12:11] != 2'd0 ?
                            13'd4095 : {source_page[11:0], 1'b0}) :
                        source_page;
                    plan_state <= P_DESTINATION_LIMIT;
                end

                P_DESTINATION_LIMIT: begin
                    source_remaining_limit_q <=
                        source_limit_q < remaining_limit_q ?
                            source_limit_q : remaining_limit_q[12:0];
                    destination_limit_q <= page_bytes(
                        planning_destination_address_q, reverse_q);
                    plan_state <= P_CHUNK;
                end

                P_CHUNK: begin
                    chunk_bytes_q <=
                        destination_limit_q < source_remaining_limit_q ?
                            destination_limit_q[7:0] :
                            source_remaining_limit_q[7:0];
                    plan_state <= P_START;
                end

                P_START: begin
                    chunk_start_q <= reverse_q ?
                        byte_cursor_q - {10'd0, chunk_bytes_q} : byte_cursor_q;
                    chunk_finishes_row_q <=
                        bytes_remaining_q == {10'd0, chunk_bytes_q};
                    plan_state <= P_ADDRESS;
                end

                P_ADDRESS: begin
                    chunk_source_address_q <=
                        {chunk_source_logical[31:3], 3'b000};
                    chunk_destination_beat_q <=
                        chunk_destination_logical[31:3];
                    chunk_source_lane_q <= chunk_source_logical[2:0];
                    chunk_destination_lane_q <=
                        chunk_destination_logical[2:0];
                    read_beats_q <=
                        ({15'd0, chunk_source_logical[2:0]} +
                         {9'd0, chunk_source_bytes} + 18'd7) >> 3;
                    chunk_source_end_lane_q <= chunk_source_logical[2:0] +
                        chunk_source_bytes[2:0];
                    write_beats_q <=
                        ({15'd0, chunk_destination_logical[2:0]} +
                         {10'd0, chunk_bytes_q} + 18'd7) >> 3;
                    plan_state <= P_PUSH;
                end

                // Queue the chunk and request its source once a slot and the
                // read address channel are free.
                P_PUSH: if (plan_stop) begin
                    plan_state <= P_DONE;
                end else if (push_chunk) begin
                    queue_destination_beat[queue_tail_q] <=
                        chunk_destination_beat_q;
                    queue_shape[queue_tail_q] <= push_shape;
                    queue_read_beat[queue_tail_q] <=
                        chunk_destination_beat_q;
                    slot_read_beats_q[queue_tail_q] <= read_beats_q;
                    slot_write_beats_q[queue_tail_q] <= write_beats_q;
                    slot_opaque_q[queue_tail_q] <= 1'b1;
                    slot_dest_q[queue_tail_q] <= 1'b0;
                    slot_skip_first_q[queue_tail_q] <=
                        chunk_source_lane_q[2];
                    slot_skip_last_q[queue_tail_q] <=
                        chunk_source_end_lane_q == 3'd4;
                    queue_tail_q <= queue_tail_q + 3'd1;
                    if (!fill_q) begin
                        ar_valid_q <= 1'b1;
                        ar_address_q <= chunk_source_address_q;
                        ar_beats_q <= read_beats_q;
                        ar_kind_q <= 1'b0;
                        ar_slot_q <= queue_tail_q;
                    end
                    if (chunk_finishes_row_q) begin
                        if (rows_remaining_q == 16'd1)
                            plan_state <= P_DONE;
                        else begin
                            rows_remaining_q <= rows_remaining_q - 16'd1;
                            source_row_address_q <= reverse_q ?
                                source_row_address_q - source_pitch_q :
                                source_row_address_q + source_pitch_q;
                            destination_row_address_q <= reverse_q ?
                                destination_row_address_q -
                                    destination_pitch_q :
                                destination_row_address_q +
                                    destination_pitch_q;
                            byte_cursor_q <= reverse_q ? row_bytes_q : 18'd0;
                            bytes_remaining_q <= row_bytes_q;
                            forward_chunk_limit_q <= chunk_capacity(
                                source_row_address_q[2:0],
                                destination_row_address_q[2:0]);
                            plan_state <= P_PLAN;
                        end
                    end else begin
                        bytes_remaining_q <= bytes_remaining_q -
                            {10'd0, chunk_bytes_q};
                        byte_cursor_q <= reverse_q ?
                            byte_cursor_q - {10'd0, chunk_bytes_q} :
                            byte_cursor_q + {10'd0, chunk_bytes_q};
                        forward_chunk_limit_q <= chunk_capacity(
                            next_chunk_source_lane,
                            next_chunk_destination_lane);
                        plan_state <= P_PLAN;
                    end
                end

                // Every issued read and write has been answered.
                P_DONE: if (busy && all_idle) begin
                    busy <= 1'b0;
                    done <= 1'b1;
                    aborted <= abort_pending;
                    plan_state <= P_IDLE;
                end

                default: plan_state <= P_DONE;
            endcase

            // ---- writer: one chunk at a time, in planning order ---------------
            case (write_state)
                W_IDLE: if (head_ready && !head_failed && !write_error &&
                             !abort_pending && !abort &&
                             writes_outstanding_q < MAX_POSTED_WRITES &&
                             (!pixel_q || w_open_q != 2'd2)) begin
                    w_s_off_q <= {5'd0, head_source_lane};
                    w_d_off_q <= {5'd0, head_destination_lane};
                    w_pixels_q <= destination_wide ?
                        {2'd0, head_chunk_bytes[7:2]} :
                        {1'b0, head_chunk_bytes[7:1]};
                    w_px_count_q <= 8'd0;
                    w_skip_q <= !pixel_blend_q || slot_skip_q[queue_head_q];
                    w_destination_beat_q <=
                        queue_destination_beat[queue_head_q];
                    w_write_beats_q <= head_write_beats;
                    w_read_beats_q <= slot_read_beats_q[queue_head_q];
                    w_shift_q <= head_source_lane - head_destination_lane;
                    w_prefix_q <= head_source_lane < head_destination_lane;
                    w_finishes_row_q <= head_shape[15];
                    w_first_strobes_q <= 8'hff << head_destination_lane;
                    w_last_strobes_q <= low_strobes(
                        head_destination_lane + head_bytes_lane);
                    w_slot_q <= queue_head_q;
                    w_issue_q <= 7'd0;
                    w_out_q <= 6'd0;
                    write_state <= W_AW;
                end

                // AWVALID is held until its handshake, even across an abort.
                W_AW: if (m_axi_awready) write_state <= W_DATA;

                // Pixel mode takes the next chunk once this one is issued;
                // its slot is released when the last pixel is read.
                W_DATA: if (pixel_q ? pixel_issue_last : w_last_accept) begin
                    if (!pixel_q)
                        slot_complete_q[w_slot_q] <= 1'b0;
                    queue_head_q <= queue_head_q + 3'd1;
                    if (w_finishes_row_q)
                        bytes_copied <= bytes_copied + {14'd0, row_bytes_q};
                    write_state <= W_IDLE;
                end

                default: write_state <= W_IDLE;
            endcase

            if (pixel_release) begin
                slot_complete_q[a_address_q[7:5]] <= 1'b0;
                slot_dest_q[a_address_q[7:5]] <= 1'b0;
            end
            case ({push_chunk, pixel_q ? pixel_release :
                   write_state == W_DATA && w_last_accept})
                2'b10: queue_count_q <= queue_count_q + 4'd1;
                2'b01: queue_count_q <= queue_count_q - 4'd1;
                default: begin end
            endcase
            if (plan_state == P_IDLE && start)
                queue_count_q <= 4'd0;

            // ---- W stream -------------------------------------------------------
            // Stream index j runs 0..write_beats over v, the chunk's source
            // beats prefixed with one zero beat when the source lane is below
            // the destination lane. Beat k = j - 1 is {v[j], v[j-1]} shifted
            // down by the lane difference.
            if (advance) begin
                a_valid_q <= 1'b0;
                if (pixel_issue) begin
                    a_valid_q <= 1'b1;
                    a_first_q <= 1'b0;
                    a_zero_q <= 1'b0;
                    a_address_q <= {w_slot_q, w_s_off_q[7:3]};
                    ad_address_q <= {w_slot_q, w_d_off_q[7:3]};
                    a_slane_q <= w_s_off_q[2:0];
                    a_dlane_q <= w_d_off_q[2:0];
                    a_last_q <= pixel_issue_last;
                    a_end_q <= pixel_issue_last || w_d_off_q[2:0] ==
                        (destination_wide ? 3'd4 : 3'd6);
                    a_skip_q <= w_skip_q;
                    w_s_off_q <= w_s_off_q + pixel_step_source;
                    w_d_off_q <= w_d_off_q + pixel_step_destination;
                    w_px_count_q <= w_px_count_q + 8'd1;
                end else if (!pixel_q && write_state == W_DATA &&
                    w_issue_q <= {1'b0, w_write_beats_q}) begin
                    a_valid_q <= 1'b1;
                    a_first_q <= w_issue_q == 7'd0;
                    a_address_q <= {w_slot_q, w_issue_q[4:0] -
                                    {4'd0, w_prefix_q}};
                    a_zero_q <= fill_q ||
                        (w_prefix_q && w_issue_q == 7'd0) ||
                        w_issue_q - {6'd0, w_prefix_q} >=
                            {1'b0, w_read_beats_q};
                    w_issue_q <= w_issue_q + 7'd1;
                end
                b_valid_q <= a_valid_q;
                b_zero_q <= a_zero_q;
                b_first_q <= a_first_q;
                if (b_valid_q)
                    previous_q <= b_value;
                c_valid_q <= b_valid_q && !b_first_q && !pixel_q;
                if (b_valid_q && !b_first_q && !pixel_q) begin
                    c_data_q <= fill_q ? fill_beat_q : realigned;
                    c_last_q <= w_out_q + 6'd1 == w_write_beats_q;
                    c_strobes_q <= w_write_beats_q == 6'd1 ?
                        w_first_strobes_q & w_last_strobes_q :
                        w_out_q == 6'd0 ? w_first_strobes_q :
                        w_out_q + 6'd1 == w_write_beats_q ?
                            w_last_strobes_q : 8'hff;
                    w_out_q <= w_out_q + 6'd1;
                end
                d_valid_q <= blend_q && c_valid_q;
                d_last_q <= c_last_q;
                d_strobes_q <= c_strobes_q;
                e_valid_q <= d_valid_q;
                e_last_q <= d_last_q;
                e_strobes_q <= d_strobes_q;
                f_valid_q <= e_valid_q;
                f_last_q <= e_last_q;
                f_strobes_q <= e_strobes_q;
                // Pixel stages.
                x_side_q <= {pixel_q && b_valid_q, pb_last_q, pb_end_q,
                             pb_dlane_q};
                p1_side_q <= x_side_q;
                p2_side_q <= p1_side_q;
                p3_side_q <= p2_side_q;
                p4_side_q <= p3_side_q;
                p5_side_q <= p4_side_q;
                g_valid_q <= p5_side_q[5] && p5_side_q[3];
                if (p5_side_q[5])
                    g_fresh_q <= p5_side_q[3];
            end
        end
    end
endmodule

`default_nettype wire
