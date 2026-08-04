// Copyright (c) 2026 Astra68 contributors
//
// Converts one tile-layer scanline into bounded map spans. The AXI fetcher
// consumes these records; this block never issues memory transactions itself.
`timescale 1ns/1ps
`default_nettype none

module astra_tile_span_walker #(
    parameter [11:0] OUTPUT_WIDTH = 12'd1280
) (
    input  wire               clk,
    input  wire               rst,

    input  wire               start,
    input  wire [10:0]        line_y,
    input  wire signed [31:0] scroll_x,
    input  wire signed [31:0] scroll_y,
    input  wire               tile_16,
    input  wire [3:0]         map_width_log2,
    input  wire [3:0]         map_height_log2,
    input  wire               wrap_x,
    input  wire               wrap_y,

    output reg                busy,
    output reg                done,
    output reg                config_error,

    output wire               span_valid,
    input  wire               span_ready,
    output wire               span_first,
    output wire               span_last,
    output wire               span_mapped,
    output wire [7:0]         span_slot,
    output wire [10:0]        span_screen_x,
    output wire [4:0]         span_pixels,
    output wire [3:0]         span_tile_x,
    output wire [3:0]         span_tile_y,
    output wire [8:0]         span_map_x,
    output wire [8:0]         span_map_y,
    output wire [17:0]        span_map_index,
    output wire [19:0]        span_map_byte_offset
);
    reg        tile_16_q;
    reg [3:0]  map_width_log2_q;
    reg [3:0]  map_height_log2_q;
    reg        wrap_x_q;
    reg        wrap_y_q;
    reg signed [32:0] raw_tile_x_q;
    reg signed [32:0] raw_tile_y_q;
    reg [8:0]  map_y_q;
    reg [17:0] map_row_q;
    reg [3:0]  tile_x_q;
    reg [3:0]  tile_y_q;
    reg [4:0]  span_advance_q;
    reg [10:0] screen_x_q;
    reg [7:0]  slot_q;
    reg        start_pending_q;

    wire config_valid = tile_16 ?
        (map_width_log2 <= 4'd8 && map_height_log2 <= 4'd8) :
        (map_width_log2 <= 4'd9 && map_height_log2 <= 4'd9);

    wire [4:0] tile_size = tile_16_q ? 5'd16 : 5'd8;
    wire [9:0] map_width = 10'd1 << map_width_log2_q;
    wire [9:0] map_height = 10'd1 << map_height_log2_q;
    wire [8:0] map_width_mask = map_width[8:0] - 9'd1;

    wire [3:0] start_tile_shift = tile_16 ? 4'd4 : 4'd3;
    wire signed [32:0] start_scroll_x =
        $signed({scroll_x[31], scroll_x});
    wire signed [32:0] start_world_y =
        $signed({scroll_y[31], scroll_y}) +
        $signed({22'd0, line_y});
    wire signed [32:0] start_raw_tile_x =
        start_scroll_x >>> start_tile_shift;
    wire signed [32:0] start_raw_tile_y =
        start_world_y >>> start_tile_shift;
    wire [9:0] start_map_height = 10'd1 << map_height_log2;
    wire [8:0] start_map_height_mask = start_map_height[8:0] - 9'd1;
    wire [8:0] start_map_y =
        start_raw_tile_y[8:0] & start_map_height_mask;
    wire [3:0] start_tile_x = tile_16 ? scroll_x[3:0] :
                                              {1'b0, scroll_x[2:0]};
    wire [3:0] start_tile_y = tile_16 ? start_world_y[3:0] :
                                              {1'b0, start_world_y[2:0]};
    wire signed [32:0] map_width_ext = $signed({23'd0, map_width});
    wire signed [32:0] map_height_ext = $signed({23'd0, map_height});

    wire x_inside = raw_tile_x_q >= 33'sd0 &&
                    raw_tile_x_q < map_width_ext;
    wire y_inside = raw_tile_y_q >= 33'sd0 &&
                    raw_tile_y_q < map_height_ext;
    wire [8:0] map_x = raw_tile_x_q[8:0] & map_width_mask;
    wire [17:0] map_index = map_row_q + {9'd0, map_x};

    wire [11:0] screen_x_12 = {1'b0, screen_x_q};
    wire [11:0] pixels_remaining = OUTPUT_WIDTH - screen_x_12;
    wire remaining_fits =
        pixels_remaining <= {7'd0, span_advance_q};
    wire [4:0] span_length =
        remaining_fits ?
        pixels_remaining[4:0] : span_advance_q;
    wire [10:0] next_screen_x =
        screen_x_q + {6'd0, span_advance_q};

    assign span_valid = busy && !start_pending_q;
    assign span_first = screen_x_q == 11'd0;
    assign span_last = remaining_fits;
    assign span_mapped = (wrap_x_q || x_inside) &&
                         (wrap_y_q || y_inside);
    assign span_slot = slot_q;
    assign span_screen_x = screen_x_q;
    assign span_pixels = span_length;
    assign span_tile_x = tile_x_q;
    assign span_tile_y = tile_y_q;
    assign span_map_x = map_x;
    assign span_map_y = map_y_q;
    assign span_map_index = map_index;
    assign span_map_byte_offset = {map_index, 2'b00};

    always @(posedge clk) begin
        if (rst) begin
            busy <= 1'b0;
            done <= 1'b0;
            config_error <= 1'b0;
            tile_16_q <= 1'b0;
            map_width_log2_q <= 4'd0;
            map_height_log2_q <= 4'd0;
            wrap_x_q <= 1'b0;
            wrap_y_q <= 1'b0;
            raw_tile_x_q <= 33'sd0;
            raw_tile_y_q <= 33'sd0;
            map_y_q <= 9'd0;
            map_row_q <= 18'd0;
            tile_x_q <= 4'd0;
            tile_y_q <= 4'd0;
            span_advance_q <= 5'd0;
            screen_x_q <= 11'd0;
            slot_q <= 8'd0;
            start_pending_q <= 1'b0;
        end else begin
            done <= 1'b0;
            config_error <= 1'b0;

            if (start && !busy) begin
                if (!config_valid) begin
                    done <= 1'b1;
                    config_error <= 1'b1;
                end else begin
                    tile_16_q <= tile_16;
                    map_width_log2_q <= map_width_log2;
                    map_height_log2_q <= map_height_log2;
                    wrap_x_q <= wrap_x;
                    wrap_y_q <= wrap_y;
                    raw_tile_x_q <= start_raw_tile_x;
                    raw_tile_y_q <= start_raw_tile_y;
                    map_y_q <= start_map_y;
                    tile_x_q <= start_tile_x;
                    tile_y_q <= start_tile_y;
                    span_advance_q <=
                        (tile_16 ? 5'd16 : 5'd8) -
                        {1'b0, start_tile_x};
                    screen_x_q <= 11'd0;
                    slot_q <= 8'd0;
                    start_pending_q <= 1'b1;
                    busy <= 1'b1;
                end
            end else if (start_pending_q) begin
                // Split scroll/line arithmetic from the variable-width row
                // shift; no span is exposed until both stages are complete.
                map_row_q <= {9'd0, map_y_q} << map_width_log2_q;
                start_pending_q <= 1'b0;
            end else if (span_valid && span_ready) begin
                // Completion never feeds the X/slot clock enables. This keeps
                // tile-edge math out of their control path and is harmless
                // after the final record because busy is cleared concurrently.
                screen_x_q <= next_screen_x;
                slot_q <= slot_q + 8'd1;
                // These next-span values are also harmless after the final
                // record. Updating them unconditionally prevents span_last's
                // carry chain from becoming their clock-enable path.
                raw_tile_x_q <= raw_tile_x_q + 33'sd1;
                tile_x_q <= 4'd0;
                span_advance_q <= tile_size;
                if (span_last) begin
                    busy <= 1'b0;
                    done <= 1'b1;
                end
            end
        end
    end
endmodule

`default_nettype wire
