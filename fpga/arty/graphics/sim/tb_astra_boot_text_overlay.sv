`timescale 1ns/1ps
`default_nettype none

module tb_astra_boot_text_overlay;
    localparam integer COLS = 36;
    localparam integer ORIGIN_X = 264;
    localparam integer ORIGIN_Y = 496;

    reg build_clk = 1'b0;
    always #2.5 build_clk = ~build_clk;
    reg pixel_clk = 1'b0;
    always #6.734 pixel_clk = ~pixel_clk;

    reg build_reset = 1'b1;
    reg pixel_reset = 1'b1;
    reg shadow_enable = 1'b0;
    reg write_strobe = 1'b0;
    reg [7:0] write_index = 8'd0;
    reg [15:0] write_cell = 16'h0020;
    wire write_ready;
    reg commit_strobe = 1'b0;
    wire commit_ready;
    wire active_enable;
    wire [31:0] generation;
    reg pixel_frame_boundary = 1'b0;
    reg input_valid = 1'b1;
    reg [10:0] pixel_x = ORIGIN_X;
    reg [9:0] pixel_y = ORIGIN_Y;
    reg [23:0] input_rgb = 24'h123456;
    wire output_valid;
    wire [23:0] output_rgb;

    astra_boot_text_overlay #(
        .FONT_HEX("fpga/soc/post_fonts.hex")
    ) dut (
        .build_clk(build_clk),
        .build_reset(build_reset),
        .shadow_enable(shadow_enable),
        .write_strobe(write_strobe),
        .write_index(write_index),
        .write_cell(write_cell),
        .write_ready(write_ready),
        .commit_strobe(commit_strobe),
        .commit_ready(commit_ready),
        .active_enable(active_enable),
        .generation(generation),
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .pixel_frame_boundary(pixel_frame_boundary),
        .input_valid(input_valid),
        .pixel_x(pixel_x),
        .pixel_y(pixel_y),
        .input_rgb(input_rgb),
        .output_valid(output_valid),
        .output_rgb(output_rgb)
    );

    task automatic write_character(
        input [7:0] index,
        input [1:0] color,
        input [7:0] character
    );
        begin
            while (!write_ready)
                @(posedge build_clk);
            @(negedge build_clk);
            write_index = index;
            write_cell = {6'd0, color, character};
            write_strobe = 1'b1;
            @(posedge build_clk);
            @(negedge build_clk);
            write_strobe = 1'b0;
            while (!write_ready)
                @(posedge build_clk);
        end
    endtask

    task automatic request_commit(input enable);
        begin
            while (!commit_ready)
                @(posedge build_clk);
            @(negedge build_clk);
            shadow_enable = enable;
            commit_strobe = 1'b1;
            @(posedge build_clk);
            @(negedge build_clk);
            commit_strobe = 1'b0;
        end
    endtask

    task automatic pulse_frame_boundary;
        begin
            @(negedge pixel_clk);
            pixel_frame_boundary = 1'b1;
            @(posedge pixel_clk);
            @(negedge pixel_clk);
            pixel_frame_boundary = 1'b0;
        end
    endtask

    task automatic wait_generation(input [31:0] expected);
        integer timeout;
        begin
            timeout = 0;
            while (generation != expected) begin
                @(posedge build_clk);
                timeout = timeout + 1;
                if (timeout > 2000)
                    $fatal(1, "text generation %0d timed out", expected);
            end
        end
    endtask

    task automatic prepare_pixel(
        input [10:0] target_x,
        input [9:0] target_y
    );
        begin
            // The overlay prefetches two pixels ahead: one register captures
            // the cell lookup and one captures the font lookup.
            @(negedge pixel_clk);
            pixel_x = target_x - 11'd2;
            pixel_y = target_y;
            @(posedge pixel_clk);
            @(negedge pixel_clk);
            pixel_x = target_x - 11'd1;
            @(posedge pixel_clk);
            #1;
            pixel_x = target_x;
        end
    endtask

    initial begin
        repeat (5) @(posedge build_clk);
        build_reset = 1'b0;
        repeat (5) @(posedge pixel_clk);
        pixel_reset = 1'b0;
        repeat (5) @(posedge build_clk);

        if (!write_ready || !commit_ready || active_enable ||
            generation != 32'd0)
            $fatal(1, "bad reset state");
        if (!output_valid || output_rgb != input_rgb)
            $fatal(1, "disabled overlay did not pass through RGB");

        // Bank one receives two cyan glyphs while bank zero remains visible.
        write_character(8'd0, 2'd0, "A");
        write_character(8'd1, 2'd0, "C");
        request_commit(1'b1);
        repeat (8) @(posedge pixel_clk);
        if (active_enable || commit_ready)
            $fatal(1, "text became active before vertical blank");
        pulse_frame_boundary();
        wait_generation(32'd1);
        if (!active_enable || !commit_ready)
            $fatal(1, "first text generation did not complete");

        // CP437 A row zero is 0x30: glyph column two is lit, column zero is
        // clear. Each source pixel is doubled in both dimensions.
        prepare_pixel(ORIGIN_X + 4, ORIGIN_Y);
        #1;
        if (output_rgb != 24'h00e5e5)
            $fatal(1, "cyan A glyph was not rendered");
        prepare_pixel(ORIGIN_X, ORIGIN_Y);
        #1;
        if (output_rgb != input_rgb)
            $fatal(1, "transparent glyph background was not preserved");

        // The post-commit clone lets software replace only cell zero. C in
        // cell one must survive the second bank swap.
        write_character(8'd0, 2'd1, "B");
        prepare_pixel(ORIGIN_X, ORIGIN_Y);
        #1;
        if (output_rgb != input_rgb)
            $fatal(1, "shadow write leaked into the active bank");
        request_commit(1'b1);
        repeat (8) @(posedge pixel_clk);
        pulse_frame_boundary();
        wait_generation(32'd2);
        prepare_pixel(ORIGIN_X, ORIGIN_Y);
        #1;
        if (output_rgb != 24'hff9d00)
            $fatal(1, "amber B glyph was not promoted");
        prepare_pixel(ORIGIN_X + 16 + 4, ORIGIN_Y);
        #1;
        if (output_rgb != 24'h00e5e5)
            $fatal(1, "unchanged C glyph was not cloned forward");

        request_commit(1'b0);
        repeat (8) @(posedge pixel_clk);
        pulse_frame_boundary();
        wait_generation(32'd3);
        if (active_enable)
            $fatal(1, "disable generation remained active");
        prepare_pixel(ORIGIN_X, ORIGIN_Y);
        #1;
        if (output_rgb != input_rgb)
            $fatal(1, "disabled overlay did not pass through RGB");

        input_valid = 1'b0;
        #1;
        if (output_valid)
            $fatal(1, "overlay changed stream validity");

        $display("ASTRA BOOT TEXT OVERLAY PASS");
        $finish;
    end
endmodule

`default_nettype wire
