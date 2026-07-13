// Focused CDC, pipelining, and byte-accurate error-reporting gate for the
// native 32-bit SDRAM POST engine.
`timescale 1ns/1ps

module tb_sdram32_bist_fault;
    localparam integer TEST_BYTES = 1024;
    localparam integer RESPONSE_LATENCY = 4;

    reg cpu_clk = 1'b0;
    reg mem_clk = 1'b0;
    reg rst = 1'b1;
    reg cpu_start = 1'b0;
    reg inject_error = 1'b0;

    always #11 cpu_clk = ~cpu_clk;
    always #5 mem_clk = ~mem_clk;

    wire cpu_busy;
    wire cpu_done;
    wire [2:0] cpu_phase;
    wire [24:0] cpu_progress;
    wire [31:0] cpu_errors;
    wire [24:0] cpu_first;
    wire [7:0] cpu_expected;
    wire [7:0] cpu_actual;

    wire mem_lock;
    wire mem_valid;
    wire mem_ready = 1'b1;
    wire mem_write;
    wire [24:0] mem_addr;
    wire [3:0] mem_be;
    wire [31:0] mem_wdata;
    reg mem_rsp_valid = 1'b0;
    reg [31:0] mem_rdata = 32'd0;

    sdram32_bist #(
        .MEM_BYTES(TEST_BYTES),
        .MAX_OUTSTANDING(16)
    ) dut (
        .cpu_clk(cpu_clk), .cpu_rst(rst), .cpu_start(cpu_start),
        .cpu_busy(cpu_busy), .cpu_done(cpu_done), .cpu_phase(cpu_phase),
        .cpu_progress(cpu_progress), .cpu_error_count(cpu_errors),
        .cpu_first_fail(cpu_first), .cpu_expected(cpu_expected),
        .cpu_actual(cpu_actual),
        .mem_clk(mem_clk), .mem_rst(rst), .mem_lock(mem_lock),
        .mem_valid(mem_valid), .mem_ready(mem_ready),
        .mem_write(mem_write), .mem_addr(mem_addr), .mem_be(mem_be),
        .mem_wdata(mem_wdata), .mem_rsp_valid(mem_rsp_valid),
        .mem_rdata(mem_rdata)
    );

    reg [31:0] memory [0:(TEST_BYTES / 4) - 1];
    reg [RESPONSE_LATENCY-1:0] response_valid_pipe = '0;
    reg [31:0] response_data_pipe [0:RESPONSE_LATENCY-1];
    integer index;

    always @(posedge mem_clk) begin
        if (rst) begin
            mem_rsp_valid <= 1'b0;
            mem_rdata <= 32'd0;
            response_valid_pipe <= '0;
            for (index = 0; index < RESPONSE_LATENCY; index = index + 1)
                response_data_pipe[index] <= 32'd0;
        end else begin
            mem_rsp_valid <= response_valid_pipe[RESPONSE_LATENCY-1];
            mem_rdata <= response_data_pipe[RESPONSE_LATENCY-1];

            for (index = RESPONSE_LATENCY-1; index > 0; index = index - 1) begin
                response_valid_pipe[index] <= response_valid_pipe[index-1];
                response_data_pipe[index] <= response_data_pipe[index-1];
            end
            response_valid_pipe[0] <= mem_valid && mem_ready;

            if (mem_valid && mem_ready) begin
                if (mem_be != 4'b1111)
                    $fatal(1, "BIST issued partial write mask %b", mem_be);
                if (mem_write) begin
                    memory[mem_addr[9:2]] <= mem_wdata;
                    response_data_pipe[0] <= 32'd0;
                end else begin
                    response_data_pipe[0] <= memory[mem_addr[9:2]] ^
                        ((inject_error && mem_addr == 25'h0000040) ?
                         32'h00000100 : 32'd0);
                end
            end
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
            if (timeout == 20000)
                $fatal(1, "native BIST timeout");
            repeat (4) @(posedge cpu_clk);
        end
    endtask

    initial begin
        repeat (5) @(posedge mem_clk);
        rst = 1'b0;

        start_test();
        wait_done();
        if (cpu_errors != 0 || cpu_phase != 3'd3)
            $fatal(1, "clean native BIST failed errors=%0d phase=%0d",
                   cpu_errors, cpu_phase);

        inject_error = 1'b1;
        start_test();
        wait_done();
        if (cpu_errors != 2 || cpu_first != 25'h0000042)
            $fatal(1, "native fault report mismatch errors=%0d first=%x",
                   cpu_errors, cpu_first);
        if ((cpu_expected ^ cpu_actual) != 8'h01)
            $fatal(1, "native fault data mismatch expected=%02x actual=%02x",
                   cpu_expected, cpu_actual);

        $display("SDRAM32 BIST FAULT PASS clean+two byte-accurate injected errors");
        $finish;
    end
endmodule
