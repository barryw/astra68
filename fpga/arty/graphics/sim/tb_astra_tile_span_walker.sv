`timescale 1ns/1ps
`default_nettype none

module tb_astra_tile_span_walker;
    localparam integer OUTPUT_WIDTH = 1280;

    reg clk = 1'b0;
    always #2.5 clk = ~clk;

    reg rst = 1'b1;
    reg start = 1'b0;
    reg [10:0] line_y = 11'd0;
    reg signed [31:0] scroll_x = 32'sd0;
    reg signed [31:0] scroll_y = 32'sd0;
    reg tile_16 = 1'b0;
    reg [3:0] map_width_log2 = 4'd0;
    reg [3:0] map_height_log2 = 4'd0;
    reg wrap_x = 1'b0;
    reg wrap_y = 1'b0;
    wire busy;
    wire done;
    wire config_error;
    wire span_valid;
    reg span_ready = 1'b0;
    wire span_first;
    wire span_last;
    wire span_mapped;
    wire [7:0] span_slot;
    wire [10:0] span_screen_x;
    wire [4:0] span_pixels;
    wire [3:0] span_tile_x;
    wire [3:0] span_tile_y;
    wire [8:0] span_map_x;
    wire [8:0] span_map_y;
    wire [17:0] span_map_index;
    wire [19:0] span_map_byte_offset;

    astra_tile_span_walker #(
        .OUTPUT_WIDTH(OUTPUT_WIDTH)
    ) dut (
        .clk(clk),
        .rst(rst),
        .start(start),
        .line_y(line_y),
        .scroll_x(scroll_x),
        .scroll_y(scroll_y),
        .tile_16(tile_16),
        .map_width_log2(map_width_log2),
        .map_height_log2(map_height_log2),
        .wrap_x(wrap_x),
        .wrap_y(wrap_y),
        .busy(busy),
        .done(done),
        .config_error(config_error),
        .span_valid(span_valid),
        .span_ready(span_ready),
        .span_first(span_first),
        .span_last(span_last),
        .span_mapped(span_mapped),
        .span_slot(span_slot),
        .span_screen_x(span_screen_x),
        .span_pixels(span_pixels),
        .span_tile_x(span_tile_x),
        .span_tile_y(span_tile_y),
        .span_map_x(span_map_x),
        .span_map_y(span_map_y),
        .span_map_index(span_map_index),
        .span_map_byte_offset(span_map_byte_offset)
    );

    task automatic check_span(
        input integer accepted,
        input integer expected_scroll_x,
        input integer expected_scroll_y,
        input integer expected_line_y,
        input integer expected_tile_size,
        input integer expected_log_w,
        input integer expected_log_h,
        input integer expected_wrap_x,
        input integer expected_wrap_y
    );
        longint signed world_x;
        longint signed world_y;
        longint signed raw_x;
        longint signed raw_y;
        integer expected_x;
        integer expected_tile_x;
        integer expected_tile_y;
        integer expected_pixels;
        integer expected_map_x;
        integer expected_map_y;
        integer expected_index;
        integer expected_mapped;
        integer width;
        integer height;
        begin
            expected_x = span_screen_x;
            width = 1 << expected_log_w;
            height = 1 << expected_log_h;
            world_x = $signed(expected_scroll_x) + expected_x;
            world_y = $signed(expected_scroll_y) + expected_line_y;
            raw_x = world_x >>> (expected_tile_size == 16 ? 4 : 3);
            raw_y = world_y >>> (expected_tile_size == 16 ? 4 : 3);
            expected_tile_x = world_x & (expected_tile_size - 1);
            expected_tile_y = world_y & (expected_tile_size - 1);
            expected_pixels = expected_tile_size - expected_tile_x;
            if (expected_pixels > OUTPUT_WIDTH - expected_x)
                expected_pixels = OUTPUT_WIDTH - expected_x;
            expected_map_x = raw_x & (width - 1);
            expected_map_y = raw_y & (height - 1);
            expected_index = (expected_map_y << expected_log_w) +
                             expected_map_x;
            expected_mapped = (expected_wrap_x ||
                (raw_x >= 0 && raw_x < width)) &&
                (expected_wrap_y || (raw_y >= 0 && raw_y < height));

            if (span_slot !== accepted[7:0])
                $fatal(1, "slot %0d expected %0d", span_slot, accepted);
            if (span_first !== (accepted == 0))
                $fatal(1, "first mismatch at slot %0d", accepted);
            if (span_tile_x !== expected_tile_x[3:0] ||
                span_tile_y !== expected_tile_y[3:0])
                $fatal(1, "tile coordinate mismatch at slot %0d", accepted);
            if (span_pixels !== expected_pixels[4:0])
                $fatal(1, "span length %0d expected %0d at slot %0d",
                       span_pixels, expected_pixels, accepted);
            if (span_map_x !== expected_map_x[8:0] ||
                span_map_y !== expected_map_y[8:0])
                $fatal(1, "map coordinate mismatch at slot %0d", accepted);
            if (span_map_index !== expected_index[17:0] ||
                span_map_byte_offset !== (expected_index << 2))
                $fatal(1, "map address mismatch at slot %0d", accepted);
            if (span_mapped !== expected_mapped[0])
                $fatal(1, "mapped mismatch at slot %0d", accepted);
            if (span_last !== (expected_x + expected_pixels == OUTPUT_WIDTH))
                $fatal(1, "last mismatch at slot %0d", accepted);
        end
    endtask

    task automatic run_case(
        input integer case_id,
        input integer case_tile_size,
        input integer case_log_w,
        input integer case_log_h,
        input integer case_scroll_x,
        input integer case_scroll_y,
        input integer case_line_y,
        input integer case_wrap_x,
        input integer case_wrap_y,
        input integer apply_backpressure,
        input integer expected_spans
    );
        integer accepted;
        integer cycles;
        begin
            @(negedge clk);
            tile_16 = case_tile_size == 16;
            map_width_log2 = case_log_w[3:0];
            map_height_log2 = case_log_h[3:0];
            scroll_x = case_scroll_x;
            scroll_y = case_scroll_y;
            line_y = case_line_y[10:0];
            wrap_x = case_wrap_x[0];
            wrap_y = case_wrap_y[0];
            span_ready = 1'b0;
            start = 1'b1;
            @(negedge clk);
            start = 1'b0;

            accepted = 0;
            cycles = 0;
            while (!done) begin
                span_ready = !apply_backpressure || (cycles % 4 != 1);
                @(posedge clk);
                if (span_valid && span_ready) begin
                    check_span(accepted, case_scroll_x, case_scroll_y,
                               case_line_y, case_tile_size, case_log_w,
                               case_log_h, case_wrap_x, case_wrap_y);
                    accepted = accepted + 1;
                end
                #1;
                cycles = cycles + 1;
                if (cycles > 2000)
                    $fatal(1, "case %0d timed out", case_id);
            end
            span_ready = 1'b0;
            if (config_error)
                $fatal(1, "case %0d reported config error", case_id);
            if (accepted != expected_spans)
                $fatal(1, "case %0d spans %0d expected %0d",
                       case_id, accepted, expected_spans);
            $display("case %0d pass: %0d spans in %0d cycles",
                     case_id, accepted, cycles);
        end
    endtask

    task automatic run_invalid_case;
        begin
            @(negedge clk);
            tile_16 = 1'b1;
            map_width_log2 = 4'd9;
            map_height_log2 = 4'd8;
            start = 1'b1;
            @(posedge clk);
            #1;
            if (!done || !config_error || busy || span_valid)
                $fatal(1, "invalid tile16 map was not rejected");
            @(negedge clk);
            start = 1'b0;
            $display("invalid configuration pass");
        end
    endtask

    initial begin
        repeat (4) @(posedge clk);
        rst = 1'b0;

        run_case(1, 8, 5, 4, 0, 0, 0, 1, 1, 0, 160);
        run_case(2, 8, 5, 4, 3, 1, 2, 1, 1, 1, 161);
        run_case(3, 8, 5, 4, -5, 0, 5, 0, 0, 0, 161);
        run_case(4, 8, 5, 4, -1, -1, 0, 0, 1, 0, 161);
        run_case(5, 16, 8, 8, 15, 31, 7, 1, 1, 1, 81);
        run_case(6, 8, 9, 9, 32'h7fffffff, 32'h80000000,
                 719, 1, 1, 0, 161);
        run_invalid_case();

        $display("ASTRA TILE SPAN WALKER PASS");
        $finish;
    end
endmodule

`default_nettype wire
