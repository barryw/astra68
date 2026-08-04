// Copyright (c) 2026 Astra68 contributors
//
// Exact aliased geometry producer. The command processor validates the
// destination surface and the shared pixel writer owns all AXI writes.
`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module astra_render_geometry (
    input  wire                         clk,
    input  wire                         reset,
    input  wire                         start,
    input  wire                         abort,
    input  wire [15:0]                  opcode,
    input  wire [15:0]                  command_flags,
    input  wire signed [15:0]           clip_left,
    input  wire signed [15:0]           clip_top,
    input  wire signed [15:0]           clip_right,
    input  wire signed [15:0]           clip_bottom,
    input  wire signed [15:0]           p0_x,
    input  wire signed [15:0]           p0_y,
    input  wire signed [15:0]           p1_x,
    input  wire signed [15:0]           p1_y,
    input  wire [15:0]                  radius_x,
    input  wire [15:0]                  radius_y,
    input  wire signed [15:0]           pattern_origin_x,
    input  wire signed [15:0]           pattern_origin_y,
    input  wire [63:0]                  pattern,
    input  wire [31:0]                  foreground,
    input  wire [31:0]                  background,
    input  wire [31:0]                  arena_base,
    input  wire [31:0]                  destination_data_offset,
    input  wire [31:0]                  destination_pitch,
    input  wire [7:0]                   destination_format,
    input  wire [2:0]                   destination_bytes_per_pixel,
    output reg                          busy,
    output reg                          done,
    output reg  [15:0]                  status,
    output reg  [31:0]                  fault_detail,
    output reg  [31:0]                  completed_pixels,
    output reg                          writer_start,
    output reg                          writer_abort,
    output reg                          writer_flush,
    input  wire                         writer_flush_ready,
    input  wire                         writer_done,
    input  wire                         writer_aborted,
    input  wire                         writer_error,
    input  wire [31:0]                  writer_fault_detail,
    output wire                         pixel_valid,
    input  wire                         pixel_ready,
    output wire [31:0]                  pixel_address,
    output wire [7:0]                   pixel_format,
    output wire [31:0]                  pixel_value
);
    localparam [23:0] ST_IDLE          = 24'b1 << 0;
    localparam [23:0] ST_DISPATCH      = 24'b1 << 1;
    localparam [23:0] ST_LINE_EMIT     = 24'b1 << 2;
    localparam [23:0] ST_LINE_STEP     = 24'b1 << 3;
    localparam [23:0] ST_RECT_NEXT     = 24'b1 << 4;
    localparam [23:0] ST_SCAN_EMIT     = 24'b1 << 5;
    localparam [23:0] ST_SCAN_NEXT     = 24'b1 << 6;
    localparam [23:0] ST_CIRCLE_EMIT   = 24'b1 << 7;
    localparam [23:0] ST_CIRCLE_NEXT   = 24'b1 << 8;
    localparam [23:0] ST_CIRCLE_STEP   = 24'b1 << 9;
    localparam [23:0] ST_ELLIPSE_ROW   = 24'b1 << 10;
    localparam [23:0] ST_FLUSH         = 24'b1 << 11;
    localparam [23:0] ST_WAIT_WRITER   = 24'b1 << 12;
    localparam [23:0] ST_ABORT_WRITER  = 24'b1 << 13;
    localparam [23:0] ST_CIRCLE_ADJUST = 24'b1 << 14;
    localparam [23:0] ST_LINE_SETUP    = 24'b1 << 15;
    localparam [23:0] ST_LINE_ERROR    = 24'b1 << 16;
    localparam [23:0] ST_SCAN_ROW      = 24'b1 << 17;
    localparam [23:0] ST_PATTERN_EMIT  = 24'b1 << 18;
    localparam [23:0] ST_CIRCLE_ERROR  = 24'b1 << 19;
    localparam [23:0] ST_CIRCLE_DECIDE = 24'b1 << 20;
    localparam [23:0] ST_SCAN_DECIDE   = 24'b1 << 21;
    localparam [23:0] ST_RECT_SETUP    = 24'b1 << 22;
    localparam [23:0] ST_SCAN_ROW_DECIDE = 24'b1 << 23;

    localparam [10:0] EL_MUL_WAIT       = 11'b1 << 0;
    localparam [10:0] EL_MUL_DONE       = 11'b1 << 1;
    localparam [10:0] EL_ROW            = 11'b1 << 2;
    localparam [10:0] EL_SEARCH         = 11'b1 << 3;
    localparam [10:0] EL_DECIDE         = 11'b1 << 4;
    localparam [10:0] EL_SUM            = 11'b1 << 5;
    localparam [10:0] EL_COMPARE        = 11'b1 << 6;
    localparam [10:0] EL_COMPARE_DECIDE = 11'b1 << 7;
    localparam [10:0] EL_EMIT           = 11'b1 << 8;
    localparam [10:0] EL_NEXT           = 11'b1 << 9;
    localparam [10:0] EL_NEXT_DECIDE    = 11'b1 << 10;

    localparam [1:0] SCAN_SOLID   = 2'd0;
    localparam [1:0] SCAN_PATTERN = 2'd1;
    localparam [1:0] SCAN_CIRCLE  = 2'd2;
    localparam [1:0] SCAN_ELLIPSE = 2'd3;

    (* fsm_encoding = "one_hot", extract_enable = "no" *) reg [23:0] state;
    (* fsm_encoding = "one_hot" *) reg [10:0] ellipse_state;
    reg [1:0] scan_kind;
    reg filled_q;
    reg pattern_opaque_q;
    reg signed [16:0] x_q, y_q, end_x_q, end_y_q;
    reg signed [16:0] line_x1_q, line_y1_q;
    reg signed [16:0] line_setup_x0_q, line_setup_y0_q;
    reg signed [16:0] line_setup_x1_q, line_setup_y1_q;
    reg signed [17:0] line_dx_q, line_dy_q, line_error_q;
    reg signed [1:0] line_step_x_q, line_step_y_q;
    (* keep = "true" *) reg line_finished_q;
    reg line_advance_x_q, line_advance_y_q, line_apply_q;
    reg [1:0] rect_edge_q;
    reg signed [16:0] rect_x0_q, rect_y0_q, rect_x1_q, rect_y1_q;
    reg signed [16:0] circle_x_q, circle_y_q;
    reg signed [19:0] circle_error_q;
    reg circle_decrement_q;
    reg circle_coordinate_valid_q;
    reg signed [16:0] circle_emit_x0_q, circle_emit_x1_q, circle_emit_y_q;
    reg ellipse_second_pixel_q;
    reg ellipse_last_row_q;
    reg [2:0] circle_slot_q;
    reg signed [16:0] ellipse_dy_q;
    reg [15:0] ellipse_low_q;
    reg [15:0] ellipse_high_q, ellipse_mid_q;
    reg [15:0] ellipse_extent_q;
    reg [31:0] ellipse_rx_square_q, ellipse_ry_square_q;
    reg [63:0] ellipse_rhs_q, ellipse_row_term_q;
    reg [63:0] ellipse_mid_term_q, ellipse_lhs_q;
    reg ellipse_converged_q, ellipse_within_q;
    reg [63:0] multiply_accumulator_q, multiply_multiplicand_q;
    reg [31:0] multiply_multiplier_q;
    reg [5:0] multiply_count_q;
    reg [2:0] multiply_return_q;
    reg multiply_start_q, multiply_busy_q, multiply_done_q;
    reg abort_active_q;
    reg [31:0] multiply_operand_a_q, multiply_operand_b_q;
    reg [63:0] multiply_product_q;
    reg emit_valid_q;
    reg emit_classified_q;
    reg emit_in_clip_q;
    reg pattern_selected_q;
    reg scan_continue_q;
    reg scan_row_continue_q;
    reg signed [16:0] emit_x_q, emit_y_q;
    (* extract_enable = "no" *) reg [31:0] emit_color_q;
    (* keep = "true" *) reg coordinate_valid_q;
    (* keep = "true" *) reg [15:0] coordinate_x_q, coordinate_y_q;
    (* keep = "true" *) reg [7:0] coordinate_format_q;
    (* keep = "true" *) reg [31:0] coordinate_value_q;
    reg operand_valid_q;
    reg [15:0] operand_x_q, operand_y_q;
    reg [7:0] operand_format_q;
    reg [31:0] operand_value_q;
    reg stage0_valid_q;
    reg [31:0] stage0_row_low_product_q;
    reg [31:0] stage0_row_high_product_q;
    reg [31:0] stage0_column_offset_q;
    reg [7:0] stage0_format_q;
    reg [31:0] stage0_value_q;
    reg sum_valid_q;
    reg [31:0] sum_row_offset_q;
    reg [31:0] sum_column_offset_q;
    reg [7:0] sum_format_q;
    reg [31:0] sum_value_q;
    reg offset_valid_q;
    reg [31:0] offset_q;
    reg [7:0] offset_format_q;
    reg [31:0] offset_value_q;
    reg stage1_valid_q;
    reg [31:0] stage1_address_q;
    reg [7:0] stage1_format_q;
    reg [31:0] stage1_value_q;
    reg [15:0] opcode_q;
    reg op_line_q, op_rect_q, op_circle_q, op_ellipse_q, op_pattern_fill_q;
    reg signed [15:0] clip_left_q, clip_top_q, clip_right_q, clip_bottom_q;
    reg signed [15:0] p0_x_q, p0_y_q, p1_x_q, p1_y_q;
    reg [15:0] radius_x_q, radius_y_q;
    reg signed [15:0] pattern_origin_x_q, pattern_origin_y_q;
    reg [63:0] pattern_q;
    reg [31:0] foreground_q, background_q;
    reg [31:0] arena_base_q, destination_data_offset_q;
    reg [31:0] destination_pitch_q;
    reg [7:0] destination_format_q;
    reg [2:0] destination_bytes_per_pixel_q;

    wire emit_in_clip_now = emit_x_q >= $signed({clip_left_q[15], clip_left_q}) &&
        emit_x_q < $signed({clip_right_q[15], clip_right_q}) &&
        emit_y_q >= $signed({clip_top_q[15], clip_top_q}) &&
        emit_y_q < $signed({clip_bottom_q[15], clip_bottom_q});
    wire stage1_ready = !stage1_valid_q || pixel_ready;
    wire offset_ready = !offset_valid_q || stage1_ready;
    wire sum_ready = !sum_valid_q || offset_ready;
    wire stage0_ready = !stage0_valid_q || sum_ready;
    wire operand_ready = !operand_valid_q || stage0_ready;
    wire coordinate_ready = !coordinate_valid_q || operand_ready;
    wire emit_accept = emit_valid_q && emit_classified_q &&
        (!emit_in_clip_q || coordinate_ready);
    assign pixel_valid = stage1_valid_q;
    assign pixel_address = stage1_address_q;
    assign pixel_format = stage1_format_q;
    assign pixel_value = stage1_value_q;

    wire signed [18:0] line_e2 =
        $signed({line_error_q[17], line_error_q}) <<< 1;
    wire circle_decrement_x = circle_error_q >=
        $signed({{3{circle_x_q[16]}}, circle_x_q});

    wire [15:0] ellipse_abs_dy = ellipse_dy_q[16] ?
        $unsigned(-ellipse_dy_q) : $unsigned(ellipse_dy_q);
    wire [63:0] multiply_engine_final = multiply_accumulator_q +
        (multiply_multiplier_q[0] ? multiply_multiplicand_q : 64'd0);
    wire [2:0] pattern_row = y_q[2:0] - pattern_origin_y_q[2:0];
    wire [2:0] pattern_column = x_q[2:0] - pattern_origin_x_q[2:0];
    wire [5:0] pattern_index = {pattern_row, pattern_column};
    wire pattern_selected = pattern_q[6'd63 - pattern_index];

    task automatic queue_pixel;
        input signed [16:0] px;
        input signed [16:0] py;
        begin
            emit_x_q <= px;
            emit_y_q <= py;
            emit_valid_q <= 1'b1;
            emit_classified_q <= 1'b0;
        end
    endtask

    task automatic setup_line;
        input signed [16:0] ax;
        input signed [16:0] ay;
        input signed [16:0] bx;
        input signed [16:0] by;
        begin
            line_setup_x0_q <= ax;
            line_setup_y0_q <= ay;
            line_setup_x1_q <= bx;
            line_setup_y1_q <= by;
            line_apply_q <= 1'b0;
            state <= ST_LINE_SETUP;
        end
    endtask

    task automatic setup_scan;
        input signed [16:0] sx0;
        input signed [16:0] sx1;
        input signed [16:0] sy;
        input [1:0] kind;
        begin
            x_q <= sx0;
            end_x_q <= sx1;
            y_q <= sy;
            scan_kind <= kind;
            state <= ST_SCAN_EMIT;
        end
    endtask

    task automatic start_multiply;
        input [31:0] a;
        input [31:0] b;
        input [2:0] return_code;
        begin
            multiply_operand_a_q <= a;
            multiply_operand_b_q <= b;
            multiply_start_q <= 1'b1;
            multiply_return_q <= return_code;
        end
    endtask

    wire abort_request = abort && busy && !abort_active_q;
    wire completed_pixel_accept = emit_accept && emit_in_clip_q;

    always @(posedge clk) begin
        if (reset)
            completed_pixels <= 32'd0;
        else if (start)
            completed_pixels <= 32'd0;
        else if (completed_pixel_accept)
            completed_pixels <= completed_pixels + 32'd1;
    end

    always @(posedge clk) begin
        if (reset || abort_request) begin
            coordinate_valid_q <= 1'b0;
            operand_valid_q <= 1'b0;
            stage0_valid_q <= 1'b0;
            sum_valid_q <= 1'b0;
            offset_valid_q <= 1'b0;
            stage1_valid_q <= 1'b0;
        end else begin
            if (stage1_ready) begin
                stage1_valid_q <= offset_valid_q;
                if (offset_valid_q) begin
                    stage1_address_q <= arena_base_q +
                        destination_data_offset_q + offset_q;
                    stage1_format_q <= offset_format_q;
                    stage1_value_q <= offset_value_q;
                end
            end

            if (offset_ready) begin
                offset_valid_q <= sum_valid_q;
                if (sum_valid_q) begin
                    offset_q <= sum_row_offset_q + sum_column_offset_q;
                    offset_format_q <= sum_format_q;
                    offset_value_q <= sum_value_q;
                end
            end

            if (sum_ready) begin
                sum_valid_q <= stage0_valid_q;
                if (stage0_valid_q) begin
                    sum_row_offset_q <= stage0_row_low_product_q +
                        {stage0_row_high_product_q[15:0], 16'd0};
                    sum_column_offset_q <= stage0_column_offset_q;
                    sum_format_q <= stage0_format_q;
                    sum_value_q <= stage0_value_q;
                end
            end

            if (stage0_ready) begin
                stage0_valid_q <= operand_valid_q;
                if (operand_valid_q) begin
                    stage0_row_low_product_q <= operand_y_q *
                        destination_pitch_q[15:0];
                    stage0_row_high_product_q <= operand_y_q *
                        destination_pitch_q[31:16];
                    stage0_column_offset_q <= operand_x_q *
                        destination_bytes_per_pixel_q;
                    stage0_format_q <= operand_format_q;
                    stage0_value_q <= operand_value_q;
                end
            end

            if (operand_ready) begin
                operand_valid_q <= coordinate_valid_q;
                if (coordinate_valid_q) begin
                    operand_x_q <= coordinate_x_q;
                    operand_y_q <= coordinate_y_q;
                    operand_format_q <= coordinate_format_q;
                    operand_value_q <= coordinate_value_q;
                end
            end

            if (coordinate_ready) begin
                coordinate_valid_q <= emit_valid_q && emit_classified_q &&
                    emit_in_clip_q;
                if (emit_valid_q && emit_classified_q && emit_in_clip_q) begin
                    coordinate_x_q <= emit_x_q[15:0];
                    coordinate_y_q <= emit_y_q[15:0];
                    coordinate_format_q <= destination_format_q;
                    coordinate_value_q <= emit_color_q;
                end
            end
        end
    end

    always @(posedge clk) begin
        if (reset) begin
            multiply_busy_q <= 1'b0;
            multiply_done_q <= 1'b0;
            multiply_accumulator_q <= 64'd0;
            multiply_multiplicand_q <= 64'd0;
            multiply_multiplier_q <= 32'd0;
            multiply_count_q <= 6'd0;
            multiply_product_q <= 64'd0;
        end else begin
            multiply_done_q <= 1'b0;
            if (multiply_start_q) begin
                multiply_busy_q <= 1'b1;
                multiply_accumulator_q <= 64'd0;
                multiply_multiplicand_q <= {32'd0, multiply_operand_a_q};
                multiply_multiplier_q <= multiply_operand_b_q;
                multiply_count_q <= 6'd0;
            end else if (multiply_busy_q) begin
                if (multiply_multiplier_q[0])
                    multiply_accumulator_q <= multiply_accumulator_q +
                        multiply_multiplicand_q;
                multiply_multiplicand_q <= multiply_multiplicand_q << 1;
                multiply_multiplier_q <= multiply_multiplier_q >> 1;
                if (multiply_count_q == 6'd31) begin
                    multiply_product_q <= multiply_engine_final;
                    multiply_busy_q <= 1'b0;
                    multiply_done_q <= 1'b1;
                end else begin
                    multiply_count_q <= multiply_count_q + 6'd1;
                end
            end
        end
    end

    always @(posedge clk) begin
        writer_start <= 1'b0;
        writer_abort <= 1'b0;
        writer_flush <= 1'b0;
        multiply_start_q <= 1'b0;
        done <= 1'b0;

        if (reset) begin
            state <= ST_IDLE;
            ellipse_state <= EL_MUL_WAIT;
            busy <= 1'b0;
            status <= `ASTRA_RENDER_STATUS_OK;
            fault_detail <= 32'd0;
            emit_valid_q <= 1'b0;
            emit_classified_q <= 1'b0;
            emit_in_clip_q <= 1'b0;
            abort_active_q <= 1'b0;
            multiply_start_q <= 1'b0;
            filled_q <= 1'b0;
            pattern_opaque_q <= 1'b0;
            scan_continue_q <= 1'b0;
            circle_decrement_q <= 1'b0;
            circle_coordinate_valid_q <= 1'b0;
            ellipse_second_pixel_q <= 1'b0;
            ellipse_last_row_q <= 1'b0;
            op_line_q <= 1'b0;
            op_rect_q <= 1'b0;
            op_circle_q <= 1'b0;
            op_ellipse_q <= 1'b0;
            op_pattern_fill_q <= 1'b0;
        end else begin
            if (!emit_valid_q)
                emit_color_q <= foreground_q;

            if (emit_valid_q && !emit_classified_q) begin
                emit_in_clip_q <= emit_in_clip_now;
                emit_classified_q <= 1'b1;
            end

            if (emit_accept) begin
                emit_valid_q <= 1'b0;
                emit_classified_q <= 1'b0;
            end

            if (abort_request) begin
                emit_valid_q <= 1'b0;
                emit_classified_q <= 1'b0;
                abort_active_q <= 1'b1;
                writer_abort <= 1'b1;
                state <= ST_ABORT_WRITER;
            end else case (1'b1)
                state[0]: if (start) begin
                    busy <= 1'b1;
                    status <= `ASTRA_RENDER_STATUS_OK;
                    fault_detail <= 32'd0;
                    opcode_q <= opcode;
                    op_line_q <= opcode == `ASTRA_RENDER_OP_LINE;
                    op_rect_q <= opcode == `ASTRA_RENDER_OP_RECT;
                    op_circle_q <= opcode == `ASTRA_RENDER_OP_CIRCLE;
                    op_ellipse_q <= opcode == `ASTRA_RENDER_OP_ELLIPSE;
                    op_pattern_fill_q <= opcode == `ASTRA_RENDER_OP_PATTERN_FILL;
                    clip_left_q <= clip_left;
                    clip_top_q <= clip_top;
                    clip_right_q <= clip_right;
                    clip_bottom_q <= clip_bottom;
                    p0_x_q <= p0_x;
                    p0_y_q <= p0_y;
                    p1_x_q <= p1_x;
                    p1_y_q <= p1_y;
                    radius_x_q <= radius_x;
                    radius_y_q <= radius_y;
                    pattern_origin_x_q <= pattern_origin_x;
                    pattern_origin_y_q <= pattern_origin_y;
                    pattern_q <= pattern;
                    foreground_q <= foreground;
                    background_q <= background;
                    arena_base_q <= arena_base;
                    destination_data_offset_q <= destination_data_offset;
                    destination_pitch_q <= destination_pitch;
                    destination_format_q <= destination_format;
                    destination_bytes_per_pixel_q <= destination_bytes_per_pixel;
                    filled_q <= command_flags[0];
                    pattern_opaque_q <= command_flags[1];
                    writer_start <= 1'b1;
                    state <= ST_DISPATCH;
                end

                state[1]: begin
                    case (1'b1)
                        op_line_q:
                            setup_line(p0_x_q, p0_y_q, p1_x_q, p1_y_q);
                        op_rect_q: begin
                            rect_x0_q <= p0_x_q < p1_x_q ? p0_x_q : p1_x_q;
                            rect_x1_q <= p0_x_q > p1_x_q ? p0_x_q : p1_x_q;
                            rect_y0_q <= p0_y_q < p1_y_q ? p0_y_q : p1_y_q;
                            rect_y1_q <= p0_y_q > p1_y_q ? p0_y_q : p1_y_q;
                            rect_edge_q <= 2'd0;
                            state <= ST_RECT_SETUP;
                        end
                        op_pattern_fill_q: begin
                            rect_x0_q <= p0_x_q < p1_x_q ? p0_x_q : p1_x_q;
                            rect_x1_q <= p0_x_q > p1_x_q ? p0_x_q : p1_x_q;
                            rect_y0_q <= p0_y_q < p1_y_q ? p0_y_q : p1_y_q;
                            rect_y1_q <= p0_y_q > p1_y_q ? p0_y_q : p1_y_q;
                            state <= ST_RECT_SETUP;
                        end
                        op_circle_q: begin
                            circle_x_q <= radius_x_q;
                            circle_y_q <= 17'sd0;
                            circle_error_q <= 20'sd1 - $signed({4'd0, radius_x_q});
                            circle_slot_q <= 3'd0;
                            circle_coordinate_valid_q <= 1'b0;
                            state <= ST_CIRCLE_EMIT;
                        end
                        op_ellipse_q: begin
                            ellipse_dy_q <= -$signed({1'b0, radius_y_q});
                            start_multiply({16'd0, radius_x_q},
                                           {16'd0, radius_x_q}, 3'd0);
                            ellipse_state <= EL_MUL_WAIT;
                            state <= ST_ELLIPSE_ROW;
                        end
                        default: begin
                            status <= `ASTRA_RENDER_STATUS_UNSUPPORTED;
                            fault_detail <= {16'd0, opcode_q};
                            writer_abort <= 1'b1;
                            state <= ST_ABORT_WRITER;
                        end
                    endcase
                end

                state[2]: if (!emit_valid_q) begin
                    line_finished_q <= x_q == line_x1_q && y_q == line_y1_q;
                    queue_pixel(x_q, y_q);
                    state <= ST_LINE_STEP;
                end
                state[15]: begin
                    if (line_apply_q) begin
                        if (line_advance_x_q) begin
                            line_error_q <= line_error_q + line_dy_q;
                            x_q <= x_q + line_step_x_q;
                        end
                        if (line_advance_y_q) begin
                            line_error_q <= line_error_q + line_dx_q;
                            y_q <= y_q + line_step_y_q;
                        end
                        if (line_advance_x_q && line_advance_y_q)
                            line_error_q <= line_error_q + line_dy_q + line_dx_q;
                        state <= ST_LINE_EMIT;
                    end else begin
                        x_q <= line_setup_x0_q;
                        y_q <= line_setup_y0_q;
                        line_x1_q <= line_setup_x1_q;
                        line_y1_q <= line_setup_y1_q;
                        line_dx_q <= line_setup_x0_q >= line_setup_x1_q ?
                            line_setup_x0_q - line_setup_x1_q :
                            line_setup_x1_q - line_setup_x0_q;
                        line_dy_q <= -(line_setup_y0_q >= line_setup_y1_q ?
                            line_setup_y0_q - line_setup_y1_q :
                            line_setup_y1_q - line_setup_y0_q);
                        line_step_x_q <= line_setup_x0_q < line_setup_x1_q ?
                            2'sd1 : -2'sd1;
                        line_step_y_q <= line_setup_y0_q < line_setup_y1_q ?
                            2'sd1 : -2'sd1;
                        state <= ST_LINE_ERROR;
                    end
                end
                state[16]: begin
                    line_error_q <= line_dx_q + line_dy_q;
                    state <= ST_LINE_EMIT;
                end
                state[3]: if (!emit_valid_q) begin
                    if (line_finished_q) begin
                        if (op_rect_q)
                            state <= ST_RECT_NEXT;
                        else
                            state <= ST_FLUSH;
                    end else begin
                        line_advance_x_q <= line_e2 >= line_dy_q;
                        line_advance_y_q <= line_e2 <= line_dx_q;
                        line_apply_q <= 1'b1;
                        state <= ST_LINE_SETUP;
                    end
                end

                state[4]: begin
                    rect_edge_q <= rect_edge_q + 2'd1;
                    case (rect_edge_q)
                        2'd0: setup_line(rect_x1_q, rect_y0_q,
                                         rect_x1_q, rect_y1_q);
                        2'd1: setup_line(rect_x1_q, rect_y1_q,
                                         rect_x0_q, rect_y1_q);
                        2'd2: setup_line(rect_x0_q, rect_y1_q,
                                         rect_x0_q, rect_y0_q);
                        default: state <= ST_FLUSH;
                    endcase
                end

                state[22]: begin
                    if (op_pattern_fill_q) begin
                        end_y_q <= rect_y1_q;
                        setup_scan(rect_x0_q, rect_x1_q, rect_y0_q,
                                   SCAN_PATTERN);
                    end else if (filled_q) begin
                        end_y_q <= rect_y1_q;
                        setup_scan(rect_x0_q, rect_x1_q, rect_y0_q,
                                   SCAN_SOLID);
                    end else begin
                        setup_line(rect_x0_q, rect_y0_q,
                                   rect_x1_q, rect_y0_q);
                    end
                end

                state[5]: if (!emit_valid_q) begin
                    if (scan_kind == SCAN_PATTERN) begin
                        pattern_selected_q <= pattern_selected;
                        state <= ST_PATTERN_EMIT;
                    end else begin
                        queue_pixel(x_q, y_q);
                        state <= ST_SCAN_NEXT;
                    end
                end
                state[18]: if (!emit_valid_q) begin
                    if (pattern_selected_q)
                        queue_pixel(x_q, y_q);
                    else if (pattern_opaque_q) begin
                        emit_color_q <= background_q;
                        queue_pixel(x_q, y_q);
                    end
                    else
                        state <= ST_SCAN_NEXT;
                    if (pattern_selected_q || pattern_opaque_q)
                        state <= ST_SCAN_NEXT;
                end
                state[6]: if (!emit_valid_q) begin
                    scan_continue_q <= x_q < end_x_q;
                    state <= ST_SCAN_DECIDE;
                end
                state[21]: begin
                    if (scan_continue_q) begin
                        x_q <= x_q + 17'sd1;
                        state <= ST_SCAN_EMIT;
                    end else begin
                        state <= ST_SCAN_ROW;
                    end
                end
                state[17]: begin
                    scan_row_continue_q <=
                        (scan_kind == SCAN_SOLID ||
                         scan_kind == SCAN_PATTERN) && y_q < end_y_q;
                    state <= ST_SCAN_ROW_DECIDE;
                end
                state[23]: begin
                    if (scan_row_continue_q) begin
                        y_q <= y_q + 17'sd1;
                        x_q <= rect_x0_q;
                        end_x_q <= rect_x1_q;
                        state <= ST_SCAN_EMIT;
                    end else if (scan_kind == SCAN_CIRCLE) begin
                        state <= ST_CIRCLE_NEXT;
                    end else if (scan_kind == SCAN_ELLIPSE) begin
                        state <= ST_ELLIPSE_ROW;
                    end else begin
                        state <= ST_FLUSH;
                    end
                end

                state[7]: if (!emit_valid_q) begin
                    if (!circle_coordinate_valid_q) begin
                        if (filled_q) begin
                            case (circle_slot_q[1:0])
                                2'd0: begin
                                    circle_emit_x0_q <= p0_x_q - circle_x_q;
                                    circle_emit_x1_q <= p0_x_q + circle_x_q;
                                    circle_emit_y_q <= p0_y_q + circle_y_q;
                                end
                                2'd1: begin
                                    circle_emit_x0_q <= p0_x_q - circle_x_q;
                                    circle_emit_x1_q <= p0_x_q + circle_x_q;
                                    circle_emit_y_q <= p0_y_q - circle_y_q;
                                end
                                2'd2: begin
                                    circle_emit_x0_q <= p0_x_q - circle_y_q;
                                    circle_emit_x1_q <= p0_x_q + circle_y_q;
                                    circle_emit_y_q <= p0_y_q + circle_x_q;
                                end
                                default: begin
                                    circle_emit_x0_q <= p0_x_q - circle_y_q;
                                    circle_emit_x1_q <= p0_x_q + circle_y_q;
                                    circle_emit_y_q <= p0_y_q - circle_x_q;
                                end
                            endcase
                        end else begin
                            case (circle_slot_q)
                                3'd0: begin circle_emit_x0_q <= p0_x_q + circle_x_q; circle_emit_y_q <= p0_y_q + circle_y_q; end
                                3'd1: begin circle_emit_x0_q <= p0_x_q + circle_y_q; circle_emit_y_q <= p0_y_q + circle_x_q; end
                                3'd2: begin circle_emit_x0_q <= p0_x_q - circle_y_q; circle_emit_y_q <= p0_y_q + circle_x_q; end
                                3'd3: begin circle_emit_x0_q <= p0_x_q - circle_x_q; circle_emit_y_q <= p0_y_q + circle_y_q; end
                                3'd4: begin circle_emit_x0_q <= p0_x_q - circle_x_q; circle_emit_y_q <= p0_y_q - circle_y_q; end
                                3'd5: begin circle_emit_x0_q <= p0_x_q - circle_y_q; circle_emit_y_q <= p0_y_q - circle_x_q; end
                                3'd6: begin circle_emit_x0_q <= p0_x_q + circle_y_q; circle_emit_y_q <= p0_y_q - circle_x_q; end
                                default: begin circle_emit_x0_q <= p0_x_q + circle_x_q; circle_emit_y_q <= p0_y_q - circle_y_q; end
                            endcase
                        end
                        circle_coordinate_valid_q <= 1'b1;
                    end else if (filled_q) begin
                        setup_scan(circle_emit_x0_q, circle_emit_x1_q,
                                   circle_emit_y_q, SCAN_CIRCLE);
                        circle_coordinate_valid_q <= 1'b0;
                    end else begin
                        queue_pixel(circle_emit_x0_q, circle_emit_y_q);
                        circle_coordinate_valid_q <= 1'b0;
                        state <= ST_CIRCLE_NEXT;
                    end
                end
                state[8]: if (!emit_valid_q) begin
                    if (circle_slot_q == (filled_q ? 3'd3 : 3'd7)) begin
                        circle_slot_q <= 3'd0;
                        state <= ST_CIRCLE_STEP;
                    end else begin
                        circle_slot_q <= circle_slot_q + 3'd1;
                        state <= ST_CIRCLE_EMIT;
                    end
                end
                state[9]: begin
                    if (circle_x_q < circle_y_q) begin
                        state <= ST_FLUSH;
                    end else begin
                        circle_y_q <= circle_y_q + 17'sd1;
                        state <= ST_CIRCLE_ERROR;
                    end
                end
                state[19]: begin
                    circle_error_q <= circle_error_q + 20'sd1 +
                        (circle_y_q <<< 1);
                    state <= ST_CIRCLE_DECIDE;
                end
                state[20]: begin
                    circle_decrement_q <= circle_decrement_x;
                    state <= ST_CIRCLE_ADJUST;
                end
                state[14]: begin
                    if (circle_decrement_q) begin
                        circle_x_q <= circle_x_q - 17'sd1;
                        circle_error_q <= circle_error_q +
                            20'sd1 - (circle_x_q <<< 1);
                    end
                    state <= ST_CIRCLE_EMIT;
                end

                state[10]: case (1'b1)
                    ellipse_state[0]: if (multiply_done_q)
                        ellipse_state <= EL_MUL_DONE;
                    ellipse_state[1]: begin
                        case (multiply_return_q)
                            3'd0: begin
                                ellipse_rx_square_q <= multiply_product_q[31:0];
                                start_multiply({16'd0, radius_y_q},
                                               {16'd0, radius_y_q}, 3'd1);
                                ellipse_state <= EL_MUL_WAIT;
                            end
                            3'd1: begin
                                ellipse_ry_square_q <= multiply_product_q[31:0];
                                start_multiply(ellipse_rx_square_q,
                                               multiply_product_q[31:0], 3'd2);
                                ellipse_state <= EL_MUL_WAIT;
                            end
                            3'd2: begin
                                ellipse_rhs_q <= multiply_product_q;
                                ellipse_state <= EL_ROW;
                            end
                            3'd3: begin
                                start_multiply(multiply_product_q[31:0],
                                               ellipse_rx_square_q, 3'd4);
                                ellipse_state <= EL_MUL_WAIT;
                            end
                            3'd4: begin
                                ellipse_row_term_q <= multiply_product_q;
                                ellipse_state <= EL_SEARCH;
                            end
                            3'd5: begin
                                start_multiply(multiply_product_q[31:0],
                                               ellipse_ry_square_q, 3'd6);
                                ellipse_state <= EL_MUL_WAIT;
                            end
                            default: begin
                                ellipse_mid_term_q <= multiply_product_q;
                                ellipse_state <= EL_SUM;
                            end
                        endcase
                    end
                    ellipse_state[2]: begin
                        ellipse_low_q <= 16'd0;
                        ellipse_high_q <= radius_x_q;
                        start_multiply({16'd0, ellipse_abs_dy},
                                       {16'd0, ellipse_abs_dy}, 3'd3);
                        ellipse_state <= EL_MUL_WAIT;
                    end
                    ellipse_state[3]: begin
                        ellipse_mid_q <= ellipse_low_q +
                            ((ellipse_high_q - ellipse_low_q + 16'd1) >> 1);
                        ellipse_state <= EL_DECIDE;
                    end
                    ellipse_state[4]: begin
                        start_multiply({16'd0, ellipse_mid_q},
                                       {16'd0, ellipse_mid_q}, 3'd5);
                        ellipse_state <= EL_MUL_WAIT;
                    end
                    ellipse_state[5]: begin
                        ellipse_lhs_q <= ellipse_mid_term_q + ellipse_row_term_q;
                        ellipse_state <= EL_COMPARE;
                    end
                    ellipse_state[6]: begin
                        ellipse_converged_q <= ellipse_low_q == ellipse_high_q;
                        ellipse_within_q <= ellipse_lhs_q <= ellipse_rhs_q;
                        ellipse_state <= EL_COMPARE_DECIDE;
                    end
                    ellipse_state[7]: begin
                        if (ellipse_converged_q) begin
                            ellipse_extent_q <= ellipse_low_q;
                            ellipse_state <= EL_EMIT;
                        end else if (ellipse_within_q) begin
                            ellipse_low_q <= ellipse_mid_q;
                            ellipse_state <= EL_SEARCH;
                        end else begin
                            ellipse_high_q <= ellipse_mid_q - 16'd1;
                            ellipse_state <= EL_SEARCH;
                        end
                    end
                    ellipse_state[8]: begin
                        ellipse_state <= EL_NEXT;
                        if (filled_q)
                            setup_scan(p0_x_q - $signed({1'b0, ellipse_extent_q}),
                                       p0_x_q + $signed({1'b0, ellipse_extent_q}),
                                       p0_y_q + ellipse_dy_q, SCAN_ELLIPSE);
                        else begin
                            queue_pixel(p0_x_q - $signed({1'b0, ellipse_extent_q}),
                                        p0_y_q + ellipse_dy_q);
                            end_x_q <= p0_x_q + $signed({1'b0, ellipse_extent_q});
                        end
                    end
                    ellipse_state[9]: if (!emit_valid_q) begin
                        ellipse_second_pixel_q <= !filled_q && emit_x_q != end_x_q;
                        ellipse_last_row_q <=
                            ellipse_dy_q == $signed({1'b0, radius_y_q});
                        ellipse_state <= EL_NEXT_DECIDE;
                    end
                    ellipse_state[10]: begin
                        if (ellipse_second_pixel_q) begin
                            queue_pixel(end_x_q, p0_y_q + ellipse_dy_q);
                            emit_x_q <= end_x_q;
                            ellipse_state <= EL_NEXT;
                        end else if (ellipse_last_row_q) begin
                            state <= ST_FLUSH;
                        end else begin
                            ellipse_dy_q <= ellipse_dy_q + 17'sd1;
                            ellipse_state <= EL_ROW;
                        end
                    end
                    default: begin
                        status <= `ASTRA_RENDER_STATUS_RESET;
                        fault_detail <= 32'h454c4c49;
                        writer_abort <= 1'b1;
                        state <= ST_ABORT_WRITER;
                    end
                endcase

                state[11]: if (!coordinate_valid_q && !operand_valid_q &&
                    !stage0_valid_q &&
                    !sum_valid_q && !offset_valid_q && !stage1_valid_q &&
                    writer_flush_ready) begin
                    writer_flush <= 1'b1;
                    state <= ST_WAIT_WRITER;
                end
                state[12]: begin
                    if (writer_done || writer_aborted || writer_error) begin
                        if (writer_error) begin
                            status <= `ASTRA_RENDER_STATUS_AXI_WRITE;
                            fault_detail <= writer_fault_detail;
                        end else if (writer_aborted && status == `ASTRA_RENDER_STATUS_OK) begin
                            status <= `ASTRA_RENDER_STATUS_RESET;
                        end
                        busy <= 1'b0;
                        abort_active_q <= 1'b0;
                        done <= 1'b1;
                        state <= ST_IDLE;
                    end
                end
                state[13]: begin
                    if (writer_aborted || writer_done || writer_error) begin
                        busy <= 1'b0;
                        abort_active_q <= 1'b0;
                        done <= 1'b1;
                        state <= ST_IDLE;
                    end
                end
                default: begin
                    status <= `ASTRA_RENDER_STATUS_RESET;
                    fault_detail <= 32'h47454f4d;
                    writer_abort <= 1'b1;
                    state <= ST_ABORT_WRITER;
                end
            endcase
        end
    end
endmodule

`default_nettype wire
