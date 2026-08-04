// Copyright (c) 2026 Astra68 contributors
//
// Bounded tile-surface validator. Address-span generation, address addition,
// and range comparison occupy separate 200 MHz stages.
`timescale 1ns/1ps
`default_nettype none

module astra_tile_config_validator (
    input  wire        clk,
    input  wire        reset,
    input  wire        start,
    input  wire        tile_16,
    input  wire        index_8,
    input  wire [3:0]  map_width_log2,
    input  wire [3:0]  map_height_log2,
    input  wire [31:0] map_base,
    input  wire [31:0] pattern_base,
    input  wire [16:0] tile_count,
    input  wire [31:0] arena_base,
    input  wire [31:0] arena_limit,
    output reg         busy,
    output reg         done,
    output reg         config_valid
);
    localparam [2:0] ST_IDLE = 3'd0;
    localparam [2:0] ST_ADD_BASES = 3'd1;
    localparam [2:0] ST_PIPE = 3'd2;
    localparam [2:0] ST_PIPE2 = 3'd3;
    localparam [2:0] ST_CHECK = 3'd4;

    reg [2:0] state;
    reg simple_shape_count_q;
    reg simple_alignment_q;
    reg simple_arena_range_q;
    reg simple_map_base_q;
    reg simple_pattern_base_q;
    reg simple_valid_q;
    reg [32:0] map_span_q;
    reg [32:0] pattern_span_q;
    reg [31:0] map_base_q;
    reg [31:0] pattern_base_q;
    reg [31:0] arena_limit_q;
    reg [32:0] map_end_q;
    reg [32:0] pattern_end_q;

    wire [5:0] map_shift =
        {2'd0, map_width_log2} + {2'd0, map_height_log2} + 6'd2;
    wire [3:0] pattern_shift =
        index_8 ? (tile_16 ? 4'd8 : 4'd6) :
                  (tile_16 ? 4'd7 : 4'd5);
    wire map_shape_valid = tile_16 ?
        (map_width_log2 <= 4'd8 && map_height_log2 <= 4'd8) :
        (map_width_log2 <= 4'd9 && map_height_log2 <= 4'd9);
    always @(posedge clk) begin
        if (reset) begin
            state <= ST_IDLE;
            simple_shape_count_q <= 1'b0;
            simple_alignment_q <= 1'b0;
            simple_arena_range_q <= 1'b0;
            simple_map_base_q <= 1'b0;
            simple_pattern_base_q <= 1'b0;
            simple_valid_q <= 1'b0;
            map_span_q <= 33'd0;
            pattern_span_q <= 33'd0;
            map_base_q <= 32'd0;
            pattern_base_q <= 32'd0;
            arena_limit_q <= 32'd0;
            map_end_q <= 33'd0;
            pattern_end_q <= 33'd0;
            busy <= 1'b0;
            done <= 1'b0;
            config_valid <= 1'b0;
        end else begin
            done <= 1'b0;
            case (state)
                ST_IDLE: begin
                    if (start) begin
                        simple_shape_count_q <=
                            map_shape_valid && tile_count != 17'd0;
                        simple_alignment_q <=
                            map_base[5:0] == 6'd0 &&
                            pattern_base[5:0] == 6'd0;
                        simple_arena_range_q <= arena_limit > arena_base;
                        simple_map_base_q <= map_base >= arena_base;
                        simple_pattern_base_q <= pattern_base >= arena_base;
                        map_span_q <= 33'd1 << map_shift;
                        pattern_span_q <=
                            {16'd0, tile_count} << pattern_shift;
                        map_base_q <= map_base;
                        pattern_base_q <= pattern_base;
                        arena_limit_q <= arena_limit;
                        busy <= 1'b1;
                        state <= ST_ADD_BASES;
                    end
                end

                ST_ADD_BASES: begin
                    simple_valid_q <=
                        simple_shape_count_q && simple_alignment_q &&
                        simple_arena_range_q && simple_map_base_q &&
                        simple_pattern_base_q;
                    map_end_q <= {1'b0, map_base_q} + map_span_q;
                    pattern_end_q <= {1'b0, pattern_base_q} +
                                     pattern_span_q;
                    state <= ST_PIPE;
                end

                ST_PIPE: state <= ST_PIPE2;

                ST_PIPE2: state <= ST_CHECK;

                ST_CHECK: begin
                    config_valid <= simple_valid_q &&
                        !map_end_q[32] && !pattern_end_q[32] &&
                        map_end_q <= {1'b0, arena_limit_q} &&
                        pattern_end_q <= {1'b0, arena_limit_q};
                    busy <= 1'b0;
                    done <= 1'b1;
                    state <= ST_IDLE;
                end

                default: begin
                    config_valid <= 1'b0;
                    busy <= 1'b0;
                    done <= 1'b1;
                    state <= ST_IDLE;
                end
            endcase
        end
    end
endmodule

`default_nettype wire
