`timescale 1ns/1ps

module hdmi_mode_control #(
    parameter int BIT_WIDTH = 11,
    parameter int BIT_HEIGHT = 10
) (
    input logic clk_pixel,
    input logic reset,
    input logic hdmi_output_enable,
    input logic [BIT_WIDTH-1:0] cx,
    input logic [BIT_HEIGHT-1:0] cy,
    input logic [BIT_HEIGHT-1:0] screen_height,
    output logic hdmi_output_active
);
    always_ff @(posedge clk_pixel)
    begin
        if (reset)
            hdmi_output_active <= 1'b0;
        else if (cx == 0 && cy == screen_height)
            hdmi_output_active <= hdmi_output_enable;
    end
endmodule
