`timescale 1ns/1ps
`default_nettype none

module tb_astra_scanline_replay;
    localparam integer OUTPUT_WIDTH = 4;
    localparam integer TOTAL_WIDTH = 6;
    reg pixel_clk = 1'b0;
    always #3 pixel_clk = ~pixel_clk;
    reg pixel_reset = 1'b1;
    reg [11:0] pixel_x = 12'd0;
    reg next_frame = 1'b0;
    reg line_source_active = 1'b0;
    reg [10:0] line_source_y = 11'd0;
    reg input_valid = 1'b0;
    reg [23:0] input_rgb = 24'd0;
    wire output_valid;
    wire [23:0] output_rgb;
    wire replaying;

    astra_scanline_replay #(
        .OUTPUT_WIDTH(OUTPUT_WIDTH),
        .TOTAL_WIDTH(TOTAL_WIDTH)
    ) dut (.*);

    task automatic prepare_line(
        input [10:0] source_y,
        input active,
        input new_frame
    );
        begin
            @(negedge pixel_clk);
            pixel_x = TOTAL_WIDTH - 1;
            line_source_y = source_y;
            line_source_active = active;
            next_frame = new_frame;
            input_valid = 1'b0;
            @(posedge pixel_clk);
            #1;
            next_frame = 1'b0;
        end
    endtask

    task automatic check_line(
        input [23:0] supplied_base,
        input [23:0] expected_base,
        input expected_replay
    );
        integer x;
        begin
            for (x = 0; x < OUTPUT_WIDTH; x = x + 1) begin
                @(negedge pixel_clk);
                pixel_x = x;
                input_valid = 1'b1;
                input_rgb = supplied_base + x;
                #1;
                if (!output_valid || replaying != expected_replay ||
                    output_rgb != expected_base + x)
                    $fatal(1,
                        "line pixel %0d rgb=%06x expected=%06x replay=%0d",
                        x, output_rgb, expected_base + x, replaying);
                @(posedge pixel_clk);
            end
        end
    endtask

    initial begin
        repeat (3) @(negedge pixel_clk);
        pixel_reset = 1'b0;

        prepare_line(11'd5, 1'b1, 1'b1);
        check_line(24'h100000, 24'h100000, 1'b0);
        prepare_line(11'd5, 1'b1, 1'b0);
        check_line(24'he00000, 24'h100000, 1'b1);
        prepare_line(11'd6, 1'b1, 1'b0);
        check_line(24'h200000, 24'h200000, 1'b0);
        prepare_line(11'd6, 1'b0, 1'b0);
        @(negedge pixel_clk);
        pixel_x = 0;
        input_valid = 1'b0;
        #1;
        if (output_valid || replaying)
            $fatal(1, "letterbox line was not blank");
        @(posedge pixel_clk);

        $display("ASTRA SCANLINE REPLAY PASS");
        $finish;
    end
endmodule

`default_nettype wire
