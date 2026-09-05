// CEA raster timing extracted from hdl-util/hdmi so parallel and serialized
// HDMI transports share one timing implementation.
`timescale 1ns/1ps

module video_timing #(
    parameter int VIDEO_ID_CODE = 1,
    parameter int BIT_WIDTH = VIDEO_ID_CODE < 4 ? 10 :
                              VIDEO_ID_CODE == 4 ? 11 : 12,
    parameter int BIT_HEIGHT = VIDEO_ID_CODE == 16 ? 11 : 10,
    parameter int START_X = 0,
    parameter int START_Y = 0
) (
    input  logic clk_pixel,
    input  logic reset,
    output logic [BIT_WIDTH-1:0] cx = START_X,
    output logic [BIT_HEIGHT-1:0] cy = START_Y,
    output logic [BIT_WIDTH-1:0] frame_width,
    output logic [BIT_HEIGHT-1:0] frame_height,
    output logic [BIT_WIDTH-1:0] screen_width,
    output logic [BIT_HEIGHT-1:0] screen_height,
    output logic hsync,
    output logic vsync,
    output logic video_data_period = 1'b0
);
    logic [BIT_WIDTH-1:0] hsync_pulse_start;
    logic [BIT_WIDTH-1:0] hsync_pulse_size;
    logic [BIT_HEIGHT-1:0] vsync_pulse_start;
    logic [BIT_HEIGHT-1:0] vsync_pulse_size;
    logic invert;

    generate
        case (VIDEO_ID_CODE)
            1: begin
                assign frame_width = 800;
                assign frame_height = 525;
                assign screen_width = 640;
                assign screen_height = 480;
                assign hsync_pulse_start = 16;
                assign hsync_pulse_size = 96;
                assign vsync_pulse_start = 10;
                assign vsync_pulse_size = 2;
                assign invert = 1;
            end
            2, 3: begin
                assign frame_width = 858;
                assign frame_height = 525;
                assign screen_width = 720;
                assign screen_height = 480;
                assign hsync_pulse_start = 16;
                assign hsync_pulse_size = 62;
                assign vsync_pulse_start = 9;
                assign vsync_pulse_size = 6;
                assign invert = 1;
            end
            4: begin
                assign frame_width = 1650;
                assign frame_height = 750;
                assign screen_width = 1280;
                assign screen_height = 720;
                assign hsync_pulse_start = 110;
                assign hsync_pulse_size = 40;
                assign vsync_pulse_start = 5;
                assign vsync_pulse_size = 5;
                assign invert = 0;
            end
            16, 34: begin
                assign frame_width = 2200;
                assign frame_height = 1125;
                assign screen_width = 1920;
                assign screen_height = 1080;
                assign hsync_pulse_start = 88;
                assign hsync_pulse_size = 44;
                assign vsync_pulse_start = 4;
                assign vsync_pulse_size = 5;
                assign invert = 0;
            end
            17, 18: begin
                assign frame_width = 864;
                assign frame_height = 625;
                assign screen_width = 720;
                assign screen_height = 576;
                assign hsync_pulse_start = 12;
                assign hsync_pulse_size = 64;
                assign vsync_pulse_start = 5;
                assign vsync_pulse_size = 5;
                assign invert = 1;
            end
            19: begin
                assign frame_width = 1980;
                assign frame_height = 750;
                assign screen_width = 1280;
                assign screen_height = 720;
                assign hsync_pulse_start = 440;
                assign hsync_pulse_size = 40;
                assign vsync_pulse_start = 5;
                assign vsync_pulse_size = 5;
                assign invert = 0;
            end
            95, 105, 97, 107: begin
                assign frame_width = 4400;
                assign frame_height = 2250;
                assign screen_width = 3840;
                assign screen_height = 2160;
                assign hsync_pulse_start = 176;
                assign hsync_pulse_size = 88;
                assign vsync_pulse_start = 8;
                assign vsync_pulse_size = 10;
                assign invert = 0;
            end
        endcase
    endgenerate

    always_comb begin
        hsync = invert ^
            (cx >= screen_width + hsync_pulse_start &&
             cx < screen_width + hsync_pulse_start + hsync_pulse_size);
        if (cy == screen_height + vsync_pulse_start - 1'b1)
            vsync = invert ^ (cx >= screen_width + hsync_pulse_start);
        else if (cy == screen_height + vsync_pulse_start +
                       vsync_pulse_size - 1'b1)
            vsync = invert ^ (cx < screen_width + hsync_pulse_start);
        else
            vsync = invert ^
                (cy >= screen_height + vsync_pulse_start &&
                 cy < screen_height + vsync_pulse_start +
                      vsync_pulse_size);
    end

    always_ff @(posedge clk_pixel) begin
        if (reset) begin
            cx <= BIT_WIDTH'(START_X);
            cy <= BIT_HEIGHT'(START_Y);
            video_data_period <= 1'b0;
        end else begin
            cx <= cx == frame_width - 1'b1 ? BIT_WIDTH'(0) : cx + 1'b1;
            cy <= cx == frame_width - 1'b1 ?
                (cy == frame_height - 1'b1 ? BIT_HEIGHT'(0) : cy + 1'b1) :
                cy;
            video_data_period <= cx < screen_width && cy < screen_height;
        end
    end
endmodule
