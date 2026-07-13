// Focused state/CDC/error-reporting gate for the SDRAM POST engine.
`timescale 1ns/1ps

module tb_sdram_bist;
    reg cpu_clk = 1'b0;
    reg sd_clk = 1'b0;
    reg cpu_rst = 1'b1;
    reg sd_rst = 1'b1;
    reg cpu_start = 1'b0;
    wire cpu_busy;
    wire cpu_done;
    wire [2:0] cpu_phase;
    wire [24:0] cpu_progress;
    wire [31:0] cpu_errors;
    wire [24:0] cpu_first;
    wire [7:0] cpu_expected;
    wire [7:0] cpu_actual;
    wire [24:0] sd_addr;
    wire [7:0] sd_din;
    wire sd_we;
    wire sd_oe;
    reg [7:0] sd_dout = 8'd0;
    reg sd_done = 1'b0;

    always #11 cpu_clk = ~cpu_clk;
    always #5 sd_clk = ~sd_clk;

    sdram_bist #(.MEM_BYTES(256)) dut (
        .cpu_clk(cpu_clk), .cpu_rst(cpu_rst), .cpu_start(cpu_start),
        .cpu_busy(cpu_busy), .cpu_done(cpu_done), .cpu_phase(cpu_phase),
        .cpu_progress(cpu_progress), .cpu_error_count(cpu_errors),
        .cpu_first_fail(cpu_first), .cpu_expected(cpu_expected),
        .cpu_actual(cpu_actual), .sdram_clk(sd_clk), .sdram_rst(sd_rst),
        .sdram_addr(sd_addr), .sdram_din(sd_din), .sdram_we(sd_we),
        .sdram_oe(sd_oe), .sdram_dout(sd_dout), .sdram_done(sd_done)
    );

    reg [7:0] memory [0:255];
    reg request_armed = 1'b1;
    reg inject_error = 1'b0;
    always @(posedge sd_clk) begin
        sd_done <= 1'b0;
        if (!sd_we && !sd_oe) request_armed <= 1'b1;
        if (request_armed && sd_we) begin
            memory[sd_addr[7:0]] <= sd_din;
            sd_done <= 1'b1;
            request_armed <= 1'b0;
        end else if (request_armed && sd_oe) begin
            sd_dout <= memory[sd_addr[7:0]] ^
                       ((inject_error && sd_addr[7:0] == 8'h42) ? 8'h01 : 8'h00);
            sd_done <= 1'b1;
            request_armed <= 1'b0;
        end
    end

    task automatic start_test;
        begin
            @(negedge cpu_clk);
            cpu_start = 1'b1;
            @(negedge cpu_clk);
            cpu_start = 1'b0;
        end
    endtask

    task automatic wait_done;
        integer timeout;
        reg saw_busy;
        begin
            timeout = 0;
            saw_busy = 1'b0;
            while (!(saw_busy && cpu_done && !cpu_busy) && timeout < 20000) begin
                @(posedge cpu_clk);
                if (cpu_busy) saw_busy = 1'b1;
                timeout = timeout + 1;
            end
            if (timeout == 20000) $fatal(1, "BIST timeout");
            repeat (3) @(posedge cpu_clk);
        end
    endtask

    initial begin
        repeat (4) @(posedge sd_clk);
        sd_rst = 1'b0;
        cpu_rst = 1'b0;

        start_test();
        wait_done();
        if (cpu_errors != 0 || cpu_phase != 3)
            $fatal(1, "clean BIST failed errors=%0d phase=%0d", cpu_errors, cpu_phase);

        inject_error = 1'b1;
        start_test();
        wait_done();
        if (cpu_errors != 2 || cpu_first != 25'h42)
            $fatal(1, "fault report mismatch errors=%0d first=%x", cpu_errors, cpu_first);
        if ((cpu_expected ^ cpu_actual) != 8'h01)
            $fatal(1, "fault data mismatch expected=%02x actual=%02x", cpu_expected, cpu_actual);

        $display("SDRAM BIST PASS clean+injected-error");
        $finish;
    end
endmodule
