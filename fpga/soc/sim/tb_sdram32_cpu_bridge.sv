// CDC, line-fill, bypass, and invalidation gate for the native SDRAM bridge.
`timescale 1ns/1ps

module tb_sdram32_cpu_bridge;
    reg cpu_clk = 1'b0;
    reg mem_clk = 1'b0;
    reg cpu_rst = 1'b1;
    reg mem_rst = 1'b1;
    always #40 cpu_clk = ~cpu_clk;
    always #8.333 mem_clk = ~mem_clk;

    reg cpu_start = 1'b0;
    reg [24:0] cpu_addr = 25'd0;
    reg cpu_write = 1'b0;
    reg [3:0] cpu_be = 4'd0;
    reg [31:0] cpu_wdata = 32'd0;
    reg cpu_lock = 1'b0;
    reg cpu_cacheable = 1'b0;
    reg cpu_instruction = 1'b0;
    reg cpu_postable = 1'b0;
    reg cpu_cache_flush = 1'b0;
    wire cpu_busy, cpu_done;
    wire [31:0] cpu_rdata;
    wire [31:0] cpu_line_hits, cpu_line_misses;
    wire [31:0] cpu_posted_writes;
    wire cpu_fill_valid;
    wire [24:0] cpu_fill_addr;
    wire [127:0] cpu_fill_data;
    wire cpu_fill_instruction;

    wire mem_valid;
    reg mem_ready = 1'b0;
    wire mem_write;
    wire [24:0] mem_addr;
    wire [3:0] mem_be;
    wire [31:0] mem_wdata;
    wire mem_lock;
    wire mem_cache_quiescent;
    reg mem_rsp_valid = 1'b0;
    reg [31:0] mem_rdata = 32'd0;

    sdram32_cpu_bridge dut (
        .cpu_clk(cpu_clk), .cpu_rst(cpu_rst), .cpu_start(cpu_start),
        .cpu_addr(cpu_addr), .cpu_write(cpu_write), .cpu_be(cpu_be),
        .cpu_wdata(cpu_wdata), .cpu_lock(cpu_lock),
        .cpu_cacheable(cpu_cacheable), .cpu_instruction(cpu_instruction),
        .cpu_postable(cpu_postable),
        .cpu_cache_flush(cpu_cache_flush),
        .cpu_busy(cpu_busy), .cpu_done(cpu_done),
        .cpu_rdata(cpu_rdata), .cpu_line_hits(cpu_line_hits),
        .cpu_line_misses(cpu_line_misses),
        .cpu_posted_writes(cpu_posted_writes),
        .cpu_fill_valid(cpu_fill_valid), .cpu_fill_addr(cpu_fill_addr),
        .cpu_fill_data(cpu_fill_data),
        .cpu_fill_instruction(cpu_fill_instruction),
        .mem_clk(mem_clk), .mem_rst(mem_rst), .mem_valid(mem_valid),
        .mem_ready(mem_ready), .mem_write(mem_write), .mem_addr(mem_addr),
        .mem_be(mem_be), .mem_wdata(mem_wdata),
        .mem_lock(mem_lock),
        .mem_cache_quiescent(mem_cache_quiescent),
        .mem_rsp_valid(mem_rsp_valid), .mem_rdata(mem_rdata)
    );

    integer request_count = 0;
    reg [24:0] request_addr_log [0:31];
    integer response_delay = -1;
    integer response_cycles = 3;
    reg [31:0] next_read_data = 32'd0;
    integer fill_count = 0;
    always @(posedge cpu_clk) begin
        if (!cpu_rst && cpu_fill_valid) begin
            fill_count <= fill_count + 1;
            if (cpu_fill_addr[3:0] != 4'd0)
                $fatal(1, "cache fill address is not line aligned: %x",
                       cpu_fill_addr);
            if (cpu_fill_instruction)
                $fatal(1, "data test unexpectedly produced instruction fill");
            if (^cpu_fill_data === 1'bx)
                $fatal(1, "cache fill data contains unknown bits");
        end
    end
    always @(posedge mem_clk) begin
        mem_ready <= 1'b0;
        mem_rsp_valid <= 1'b0;
        if (!mem_rst && mem_valid && response_delay < 0) begin
            mem_ready <= 1'b1;
            response_delay <= response_cycles;
            request_addr_log[request_count] <= mem_addr;
            request_count <= request_count + 1;
            next_read_data <= 32'ha5000000 | request_count;
        end
        if (response_delay == 0) begin
            mem_rdata <= next_read_data;
            mem_rsp_valid <= 1'b1;
            response_delay <= -1;
        end else if (response_delay > 0) begin
            response_delay <= response_delay - 1;
        end
    end

    task automatic issue(
        input [24:0] address,
        input write_cycle,
        input [3:0] enables,
        input [31:0] data,
        input cacheable,
        input postable,
        input integer expected_request,
        input [31:0] expected_read_data
    );
        begin
            @(negedge cpu_clk);
            cpu_addr = address;
            cpu_write = write_cycle;
            cpu_be = enables;
            cpu_wdata = data;
            cpu_cacheable = cacheable;
            cpu_postable = postable;
            cpu_start = 1'b1;
            @(negedge cpu_clk);
            cpu_start = 1'b0;
            wait (cpu_done);
            if (request_count != expected_request)
                $fatal(1, "expected %0d native requests, got %0d",
                       expected_request, request_count);
            if (!write_cycle && cpu_rdata != expected_read_data)
                $fatal(1, "read response mismatch expected=%08x actual=%08x",
                       expected_read_data, cpu_rdata);
            @(posedge cpu_clk);
        end
    endtask

    task automatic issue_posted_write(
        input [24:0] address,
        input [3:0] enables,
        input [31:0] data,
        input integer expected_request
    );
        begin
            @(negedge cpu_clk);
            cpu_addr = address;
            cpu_write = 1'b1;
            cpu_be = enables;
            cpu_wdata = data;
            cpu_cacheable = 1'b0;
            cpu_postable = 1'b1;
            cpu_start = 1'b1;
            @(negedge cpu_clk);
            cpu_start = 1'b0;
            wait (cpu_done);
            if (!cpu_busy)
                $fatal(1, "posted write did not remain externally pending");
            wait (!cpu_busy);
            if (request_count != expected_request)
                $fatal(1, "posted write request count expected=%0d actual=%0d",
                       expected_request, request_count);
            cpu_postable = 1'b0;
            @(posedge cpu_clk);
        end
    endtask

    initial begin
        repeat (4) @(posedge mem_clk);
        cpu_rst = 1'b0;
        mem_rst = 1'b0;

        cpu_lock = 1'b1;
        issue(25'h0000100, 1'b1, 4'b1000, 32'haa000000,
              1'b1, 1'b1, 1, 32'd0);
        cpu_lock = 1'b0;
        wait (!mem_lock);
        issue(25'h0000102, 1'b1, 4'b0011, 32'h0000beef,
              1'b0, 1'b0, 2, 32'd0);
        issue(25'h0000100, 1'b1, 4'b1111, 32'h12345678,
              1'b0, 1'b0, 3, 32'd0);

        // Cache-inhibited reads always issue one native request.
        issue(25'h0000101, 1'b0, 4'b0100, 32'd0,
              1'b0, 1'b0, 4, 32'ha5000003);
        issue(25'h0000101, 1'b0, 4'b0100, 32'd0,
              1'b0, 1'b0, 5, 32'ha5000004);

        // One cacheable miss fetches the aligned four-word line. The next two
        // reads hit without crossing into the memory clock domain.
        issue(25'h0000108, 1'b0, 4'b1111, 32'd0,
              1'b1, 1'b0, 9, 32'ha5000007);
        issue(25'h000010c, 1'b0, 4'b1111, 32'd0,
              1'b1, 1'b0, 9, 32'ha5000008);
        issue(25'h0000100, 1'b0, 4'b1111, 32'd0,
              1'b1, 1'b0, 9, 32'ha5000005);
        if (request_addr_log[5] != 25'h0000100 ||
            request_addr_log[6] != 25'h0000104 ||
            request_addr_log[7] != 25'h0000108 ||
            request_addr_log[8] != 25'h000010c)
            $fatal(1, "line fill addresses are not aligned/sequential");

        // An overlapping write invalidates the line after completing normally.
        issue(25'h0000108, 1'b1, 4'b1111, 32'hfeedface,
              1'b0, 1'b0, 10, 32'd0);
        issue(25'h000010c, 1'b0, 4'b1111, 32'd0,
              1'b1, 1'b0, 14, 32'ha500000d);

        // DMA/BIST flushes invalidate all lines, and locked reads bypass them.
        @(negedge cpu_clk);
        cpu_cache_flush = 1'b1;
        @(negedge cpu_clk);
        cpu_cache_flush = 1'b0;
        issue(25'h000010c, 1'b0, 4'b1111, 32'd0,
              1'b1, 1'b0, 18, 32'ha5000011);
        cpu_lock = 1'b1;
        issue(25'h000010c, 1'b0, 4'b1111, 32'd0,
              1'b1, 1'b0, 19, 32'ha5000012);
        cpu_lock = 1'b0;

        issue_posted_write(25'h0000200, 4'b1111, 32'h89abcdef, 20);

        // A DMA fence may arrive while a posted write is still outstanding.
        // Quiescent must stay low until that write completes, and the CPU must
        // not launch another SDRAM cycle while the fence is asserted.
        response_cycles = 20;
        @(negedge cpu_clk);
        cpu_addr = 25'h0000204;
        cpu_write = 1'b1;
        cpu_be = 4'b1111;
        cpu_wdata = 32'h76543210;
        cpu_cacheable = 1'b0;
        cpu_postable = 1'b1;
        cpu_start = 1'b1;
        @(negedge cpu_clk);
        cpu_start = 1'b0;
        wait (cpu_done);
        if (!cpu_busy || mem_cache_quiescent)
            $fatal(1, "posted write was not pending before DMA fence");
        cpu_cache_flush = 1'b1;
        wait (mem_cache_quiescent);
        if (!cpu_busy || request_count != 21)
            $fatal(1, "DMA fence acknowledged before posted write drained");

        @(negedge cpu_clk);
        cpu_addr = 25'h0000208;
        cpu_write = 1'b0;
        cpu_be = 4'b1111;
        cpu_postable = 1'b0;
        cpu_start = 1'b1;
        repeat (3) @(negedge cpu_clk);
        if (request_count != 21)
            $fatal(1, "CPU request crossed active DMA fence count=%0d",
                   request_count);
        cpu_cache_flush = 1'b0;
        @(negedge cpu_clk);
        cpu_start = 1'b0;
        wait (cpu_done);
        if (request_count != 22)
            $fatal(1, "fenced CPU request did not resume after release");
        response_cycles = 3;

        // A PMMU walk holds RMC across several uncached descriptor requests
        // and the idle CPU-clock gaps between them. The synchronized memory-
        // domain lock must neither arrive late nor pulse low between requests.
        cpu_lock = 1'b1;
        wait (mem_lock);
        issue(25'h0000300, 1'b0, 4'b1111, 32'd0,
              1'b0, 1'b0, 23, 32'ha5000016);
        repeat (4) @(posedge cpu_clk);
        if (!mem_lock)
            $fatal(1, "PMMU lock dropped in first descriptor gap");
        issue(25'h0000304, 1'b0, 4'b1111, 32'd0,
              1'b0, 1'b0, 24, 32'ha5000017);
        repeat (3) @(posedge cpu_clk);
        if (!mem_lock)
            $fatal(1, "PMMU lock dropped in second descriptor gap");
        issue(25'h0000308, 1'b0, 4'b1111, 32'd0,
              1'b0, 1'b0, 25, 32'ha5000018);
        cpu_lock = 1'b0;
        wait (!mem_lock);

        if (cpu_line_hits != 2 || cpu_line_misses != 3)
            $fatal(1, "line counters mismatch hits=%0d misses=%0d",
                   cpu_line_hits, cpu_line_misses);
        if (cpu_posted_writes != 2)
            $fatal(1, "posted write counter mismatch %0d", cpu_posted_writes);
        if (fill_count != 3)
            $fatal(1, "cache fill sideband count mismatch %0d", fill_count);

        $display("SDRAM32 CPU BRIDGE PASS line=%0d/%0d posted=%0d native=%0d",
                 cpu_line_hits, cpu_line_misses, cpu_posted_writes,
                 request_count);
        $finish;
    end
endmodule
