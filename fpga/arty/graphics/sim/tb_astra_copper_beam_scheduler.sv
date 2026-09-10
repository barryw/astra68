`timescale 1ns/1ps
`default_nettype none

module tb_astra_copper_beam_scheduler;
    reg clk = 1'b0;
    always #3 clk = ~clk;
    reg reset = 1'b1;
    reg frame_start = 1'b0;
    reg baseline_ready = 1'b1;
    reg copper_enabled = 1'b1;
    reg copper_running = 1'b1;
    reg copper_waiting = 1'b0;
    reg [10:0] source_width = 11'd1920;
    reg [10:0] source_height = 11'd1080;
    reg [10:0] last_visible_source_y = 11'd1079;
    reg line_prepare_valid = 1'b0;
    reg [10:0] line_prepare_y = 11'd0;
    wire line_prepare_ready;
    wire [11:0] beam_x;
    wire [10:0] beam_y;

    astra_copper_beam_scheduler #(
        .OUTPUT_HEIGHT(1080),
        .TOTAL_WIDTH(2200),
        .TOTAL_HEIGHT(1125)
    ) dut (.*);

    task automatic request_line(input [10:0] line);
        begin
            @(negedge clk);
            line_prepare_y = line;
            line_prepare_valid = 1'b1;
            @(negedge clk);
            if (line_prepare_ready)
                $fatal(1, "line %0d acknowledged before copper settled", line);
            copper_waiting = 1'b1;
            @(negedge clk);
            if (!line_prepare_ready)
                $fatal(1, "line %0d did not acknowledge at future WAIT", line);
            @(posedge clk);
            @(negedge clk);
            line_prepare_valid = 1'b0;
            copper_waiting = 1'b0;
        end
    endtask

    initial begin
        repeat (4) @(negedge clk);
        reset = 1'b0;
        frame_start = 1'b1;
        @(negedge clk);
        frame_start = 1'b0;

        baseline_ready = 1'b0;
        line_prepare_y = 10'd0;
        line_prepare_valid = 1'b1;
        copper_waiting = 1'b1;
        repeat (2) @(negedge clk);
        if (line_prepare_ready)
            $fatal(1, "baseline restoration did not block line zero");
        baseline_ready = 1'b1;
        #1;
        if (!line_prepare_ready || beam_x != 0 || beam_y != 0)
            $fatal(1, "line zero virtual beam mismatch");
        @(posedge clk);
        @(negedge clk);
        line_prepare_valid = 1'b0;
        copper_waiting = 1'b0;

        request_line(11'd1);
        if (beam_x != 12'd1919 || beam_y != 11'd0)
            $fatal(1, "line one did not advance through line zero");

        request_line(11'd1079);
        if (beam_x != 12'd1919 || beam_y != 11'd1078)
            $fatal(1, "last line preparation coordinate mismatch");
        @(negedge clk);
        if (beam_x != 12'd1919 || beam_y != 11'd1079)
            $fatal(1, "vertical blank was not finalized");

        copper_enabled = 1'b0;
        line_prepare_y = 11'd8;
        line_prepare_valid = 1'b1;
        @(negedge clk);
        #1;
        if (!line_prepare_ready)
            $fatal(1, "disabled copper blocked line preparation");

        $display("ASTRA COPPER BEAM SCHEDULER PASS");
        $finish;
    end
endmodule

`default_nettype wire
