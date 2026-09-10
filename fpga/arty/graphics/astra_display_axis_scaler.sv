// Copyright (c) 2026 Astra68 contributors
//
// Divider-free nearest-neighbour coordinate scaling for one display axis.
// Samples must arrive in increasing physical-position order, with
// sequence_start asserted on position zero. Source extent must not exceed the
// viewport extent; cropping implements fill/overscan without downscaling.
`timescale 1ns/1ps
`default_nettype none

module astra_display_axis_scaler #(
    parameter integer WIDTH = 12
) (
    input  wire                 clk,
    input  wire                 reset,
    input  wire                 sample_valid,
    input  wire                 sequence_start,
    input  wire [WIDTH-1:0]     physical_position,
    input  wire [WIDTH-1:0]     viewport_origin,
    input  wire [WIDTH-1:0]     viewport_extent,
    input  wire [WIDTH-1:0]     source_origin,
    input  wire [WIDTH-1:0]     source_extent,
    output reg                  output_valid,
    output reg                  output_active,
    output reg  [WIDTH-1:0]     source_position
);
    reg [WIDTH-1:0] source_q;
    reg [WIDTH-1:0] phase_q;
    wire [WIDTH:0] viewport_end =
        {1'b0, viewport_origin} + {1'b0, viewport_extent};
    wire position_in_viewport = viewport_extent != 0 &&
        {1'b0, physical_position} >= {1'b0, viewport_origin} &&
        {1'b0, physical_position} < viewport_end;
    wire config_valid = source_extent != 0 &&
                        source_extent <= viewport_extent;
    wire [WIDTH:0] phase_sum =
        {1'b0, phase_q} + {1'b0, source_extent};

    always @(posedge clk) begin
        if (reset) begin
            source_q <= {WIDTH{1'b0}};
            phase_q <= {WIDTH{1'b0}};
            output_valid <= 1'b0;
            output_active <= 1'b0;
            source_position <= {WIDTH{1'b0}};
        end else begin
            output_valid <= sample_valid;
            if (sample_valid) begin
                output_active <= position_in_viewport && config_valid;

                if (sequence_start) begin
                    source_q <= source_origin;
                    phase_q <= {WIDTH{1'b0}};
                    source_position <= source_origin;
                    if (position_in_viewport && config_valid) begin
                        if ({1'b0, source_extent} >=
                            {1'b0, viewport_extent}) begin
                            source_q <= source_origin + 1'b1;
                            phase_q <= source_extent - viewport_extent;
                        end else begin
                            phase_q <= source_extent;
                        end
                    end
                end else if (physical_position == viewport_origin) begin
                    source_position <= source_origin;
                    if (config_valid) begin
                        if ({1'b0, source_extent} >=
                            {1'b0, viewport_extent}) begin
                            source_q <= source_origin + 1'b1;
                            phase_q <= source_extent - viewport_extent;
                        end else begin
                            source_q <= source_origin;
                            phase_q <= source_extent;
                        end
                    end
                end else if (position_in_viewport && config_valid) begin
                    source_position <= source_q;
                    if (phase_sum >= {1'b0, viewport_extent}) begin
                        source_q <= source_q + 1'b1;
                        phase_q <= phase_sum - viewport_extent;
                    end else begin
                        phase_q <= phase_sum[WIDTH-1:0];
                    end
                end else begin
                    source_position <= source_q;
                end
            end else begin
                output_active <= 1'b0;
            end
        end
    end
endmodule

`default_nettype wire
