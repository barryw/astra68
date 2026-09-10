`timescale 1ns/1ps

module tb_video_timing_1080p;
    reg clk = 1'b0;
    reg reset = 1'b1;
    wire [11:0] cx;
    wire [10:0] cy;
    wire [11:0] frame_width;
    wire [10:0] frame_height;
    wire [11:0] screen_width;
    wire [10:0] screen_height;
    wire hsync;
    wire vsync;
    wire video_data_period;
    integer x;
    integer y;
    integer active_pixels = 0;

    always #5 clk = ~clk;

    video_timing #(.VIDEO_ID_CODE(16)) dut (
        .clk_pixel(clk),
        .reset(reset),
        .cx(cx),
        .cy(cy),
        .frame_width(frame_width),
        .frame_height(frame_height),
        .screen_width(screen_width),
        .screen_height(screen_height),
        .hsync(hsync),
        .vsync(vsync),
        .video_data_period(video_data_period)
    );

    initial begin
        repeat (2) @(posedge clk);
        @(negedge clk);
        reset = 1'b0;
        for (y = 0; y < 1125; y = y + 1) begin
            for (x = 0; x < 2200; x = x + 1) begin
                @(negedge clk);
                if (cx !== ((x == 2199) ? 0 : x + 1) ||
                    cy !== ((x == 2199) ? ((y == 1124) ? 0 : y + 1) : y))
                    $fatal(1, "counter mismatch at %0d,%0d: %0d,%0d",
                           x, y, cx, cy);
                if (video_data_period !== (x < 1920 && y < 1080))
                    $fatal(1, "active-video mismatch at %0d,%0d", x, y);
                if (hsync !== (cx >= 2008 && cx < 2052))
                    $fatal(1, "hsync mismatch at %0d,%0d", cx, cy);
                if (video_data_period)
                    active_pixels = active_pixels + 1;
            end
        end
        if (frame_width != 2200 || frame_height != 1125 ||
            screen_width != 1920 || screen_height != 1080)
            $fatal(1, "1080p geometry mismatch");
        if (active_pixels != 1920 * 1080)
            $fatal(1, "active pixel count mismatch: %0d", active_pixels);
        if (cx != 0 || cy != 0)
            $fatal(1, "frame did not wrap");
        $display("VIDEO TIMING 1080P60 PASS");
        $finish;
    end
endmodule
