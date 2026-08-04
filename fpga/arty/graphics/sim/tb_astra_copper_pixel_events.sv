`timescale 1ns/1ps
`default_nettype none

module tb_astra_copper_pixel_events;
    reg build_clk = 1'b0;
    reg pixel_clk = 1'b0;
    always #3 build_clk = ~build_clk;
    always #7 pixel_clk = ~pixel_clk;
    reg build_reset = 1'b1;
    reg pixel_reset = 1'b1;
    reg enqueue_frame = 1'b0;
    reg enqueue_irq = 1'b0;
    reg [9:0] enqueue_y = 10'd0;
    reg [10:0] enqueue_x = 11'd0;
    reg [15:0] enqueue_target = 16'd0;
    reg [31:0] enqueue_data = 32'd0;
    reg enqueue_valid = 1'b0;
    wire enqueue_ready;
    wire [3:0] enqueue_level;
    reg pixel_frame = 1'b0;
    reg source_valid = 1'b0;
    reg [9:0] source_y = 10'd0;
    reg [10:0] source_x = 11'd0;
    wire event_irq;
    wire [9:0] event_y;
    wire [10:0] event_x;
    wire [15:0] event_target;
    wire [31:0] event_data;
    wire event_valid;
    reg event_ready = 1'b0;
    wire [3:0] event_level;
    wire overflow;
    wire stale_event;
    wire late_event;

    astra_copper_pixel_events #(.ADDR_WIDTH(3)) dut (.*);

    task automatic enqueue(
        input frame,
        input irq,
        input [9:0] y,
        input [10:0] x,
        input [15:0] target,
        input [31:0] data
    );
        begin
            @(negedge build_clk);
            while (!enqueue_ready) @(negedge build_clk);
            enqueue_frame = frame;
            enqueue_irq = irq;
            enqueue_y = y;
            enqueue_x = x;
            enqueue_target = target;
            enqueue_data = data;
            enqueue_valid = 1'b1;
            @(negedge build_clk);
            enqueue_valid = 1'b0;
        end
    endtask

    task automatic expect_event(
        input irq,
        input [9:0] y,
        input [10:0] x,
        input [15:0] target,
        input [31:0] data
    );
        integer timeout;
        begin
            timeout = 0;
            while (!event_valid && timeout < 100) begin
                @(negedge pixel_clk);
                timeout = timeout + 1;
            end
            if (!event_valid || event_irq != irq || event_y != y ||
                event_x != x || event_target != target || event_data != data)
                $fatal(1, "pixel event mismatch valid=%b irq=%b y=%0d x=%0d target=%04x data=%08x",
                       event_valid, event_irq, event_y, event_x,
                       event_target, event_data);
            event_ready = 1'b1;
            @(posedge pixel_clk);
            #1;
            event_ready = 1'b0;
            @(negedge pixel_clk);
        end
    endtask

    initial begin
        repeat (5) @(negedge build_clk);
        build_reset = 1'b0;
        pixel_reset = 1'b0;

        enqueue(1'b0, 1'b0, 10'd2, 11'd5, 16'h0018, 32'h00112233);
        enqueue(1'b0, 1'b1, 10'd2, 11'd6, 16'd0, 32'h000000a5);
        repeat (5) @(negedge pixel_clk);
        source_valid = 1'b1;
        source_y = 10'd2;
        source_x = 11'd4;
        repeat (2) @(negedge pixel_clk);
        if (event_valid)
            $fatal(1, "event delivered before its source coordinate");
        source_x = 11'd5;
        expect_event(1'b0, 10'd2, 11'd5, 16'h0018, 32'h00112233);
        source_x = 11'd6;
        expect_event(1'b1, 10'd2, 11'd6, 16'd0, 32'h000000a5);

        // Vertical-blank effects apply before the following active line zero.
        pixel_frame = 1'b1;
        enqueue(1'b1, 1'b0, 10'd720, 11'd0,
                16'h0018, 32'h00445566);
        source_y = 10'd0;
        source_x = 11'd0;
        expect_event(1'b0, 10'd720, 11'd0,
                     16'h0018, 32'h00445566);

        // A record from the prior generation is discarded and diagnosed.
        enqueue(1'b0, 1'b0, 10'd0, 11'd0, 16'h0018, 32'h00ffffff);
        repeat (8) @(negedge pixel_clk);
        if (!stale_event)
            $fatal(1, "stale frame record was not diagnosed");
        if (overflow || late_event)
            $fatal(1, "unexpected queue fault overflow=%b late=%b",
                   overflow, late_event);

        $display("ASTRA COPPER PIXEL EVENTS PASS");
        $finish;
    end
endmodule

`default_nettype wire
