`timescale 1ns/1ps

module tb_uart_rx_fifo;
    reg clk = 1'b0;
    reg rst = 1'b1;
    reg [7:0] push_data = 8'd0;
    reg push = 1'b0;
    reg pop = 1'b0;
    reg clear_overrun = 1'b0;
    wire [7:0] data;
    wire empty;
    wire full;
    wire overrun;
    wire [7:0] level;

    uart_rx_fifo #(.DEPTH(4)) dut (
        .clk(clk), .rst(rst), .push_data(push_data), .push(push),
        .pop(pop), .clear_overrun(clear_overrun), .data(data),
        .empty(empty), .full(full), .overrun(overrun), .level(level)
    );

    always #5 clk = ~clk;

    task enqueue(input [7:0] value);
        begin
            @(negedge clk);
            push_data = value;
            push = 1'b1;
            @(negedge clk);
            push = 1'b0;
        end
    endtask

    task dequeue(input [7:0] expected);
        begin
            @(negedge clk);
            if (empty || data !== expected) begin
                $display("FAIL dequeue got=%02x expected=%02x empty=%0d", data, expected, empty);
                $fatal;
            end
            pop = 1'b1;
            @(negedge clk);
            pop = 1'b0;
        end
    endtask

    initial begin
        repeat (3) @(posedge clk);
        rst = 1'b0;

        enqueue(8'h11);
        enqueue(8'h22);
        enqueue(8'h33);
        enqueue(8'h44);
        if (!full || level !== 8'd4 || data !== 8'h11) begin
            $display("FAIL full state full=%0d level=%0d data=%02x", full, level, data);
            $fatal;
        end

        enqueue(8'hff);
        if (!overrun || level !== 8'd4 || data !== 8'h11) begin
            $display("FAIL overrun state overrun=%0d level=%0d data=%02x", overrun, level, data);
            $fatal;
        end

        @(negedge clk);
        if (data !== 8'h11) $fatal;
        pop = 1'b1;
        push = 1'b1;
        push_data = 8'h55;
        @(negedge clk);
        pop = 1'b0;
        push = 1'b0;
        if (!full || level !== 8'd4 || data !== 8'h22) begin
            $display("FAIL simultaneous pop/push full=%0d level=%0d data=%02x", full, level, data);
            $fatal;
        end

        dequeue(8'h22);
        dequeue(8'h33);
        dequeue(8'h44);
        dequeue(8'h55);
        if (!empty || level !== 8'd0) begin
            $display("FAIL empty state empty=%0d level=%0d", empty, level);
            $fatal;
        end

        @(negedge clk);
        clear_overrun = 1'b1;
        @(negedge clk);
        clear_overrun = 1'b0;
        if (overrun) begin
            $display("FAIL overrun did not clear");
            $fatal;
        end

        $display("PASS uart_rx_fifo");
        $finish;
    end
endmodule
