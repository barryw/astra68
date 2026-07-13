// Pin-level functional and throughput gate for the 32-bit SDRAM subsystem.
`timescale 1ns/1ps

module astra_sdram_model (
    input  wire        sdram_clk,
    input  wire        cke,
    input  wire        cs,
    input  wire        ras,
    input  wire        cas,
    input  wire        we,
    input  wire [12:0] addr,
    input  wire [1:0]  ba,
    input  wire [1:0]  dqm,
    input  wire [15:0] dq_in,
    output reg  [15:0] dq_out,
    output reg         dq_oe
);
    localparam [3:0] CMD_ACTIVE    = 4'b0011;
    localparam [3:0] CMD_READ      = 4'b0101;
    localparam [3:0] CMD_WRITE     = 4'b0100;
    localparam [3:0] CMD_PRECHARGE = 4'b0010;
    localparam [3:0] CMD_REFRESH   = 4'b0001;
    localparam [3:0] CMD_LOAD_MODE = 4'b0000;

    wire [3:0] command = {cs, ras, cas, we};
    reg [15:0] memory [0:16777215];
    reg [12:0] open_row [0:3];
    reg [3:0] row_open;
    reg configured;
    reg [2:0] cas_latency;
    reg [3:0] burst_length;

    localparam integer READ_PIPE_DEPTH = 8;
    reg [READ_PIPE_DEPTH-1:0] read_valid_pipe;
    reg [23:0] read_key_pipe [0:READ_PIPE_DEPTH-1];
    wire read_pending = |read_valid_pipe;
    reg write_pending;
    reg [1:0] write_bank;
    reg [8:0] write_col;
    reg [3:0] write_left;

    function automatic [23:0] word_key(
        input [1:0] bank,
        input [12:0] row,
        input [8:0] col
    );
        word_key = {col[8], bank, row, col[7:0]};
    endfunction

    task automatic write_word(
        input [1:0] bank,
        input [8:0] col,
        input [15:0] value,
        input [1:0] mask
    );
        reg [23:0] key;
        reg [15:0] current;
        begin
            key = word_key(bank, open_row[bank], col);
            current = memory[key];
            if (!mask[0]) current[7:0] = value[7:0];
            if (!mask[1]) current[15:8] = value[15:8];
            memory[key] = current;
        end
    endtask

    task automatic queue_read(input integer slot, input [23:0] key);
        begin
            // Static array indices support compiled simulation while
            // preserving nonblocking update ordering.
            case (slot)
                0: begin read_valid_pipe[0] <= 1'b1; read_key_pipe[0] <= key; end
                1: begin read_valid_pipe[1] <= 1'b1; read_key_pipe[1] <= key; end
                2: begin read_valid_pipe[2] <= 1'b1; read_key_pipe[2] <= key; end
                3: begin read_valid_pipe[3] <= 1'b1; read_key_pipe[3] <= key; end
                4: begin read_valid_pipe[4] <= 1'b1; read_key_pipe[4] <= key; end
                5: begin read_valid_pipe[5] <= 1'b1; read_key_pipe[5] <= key; end
                6: begin read_valid_pipe[6] <= 1'b1; read_key_pipe[6] <= key; end
                7: begin read_valid_pipe[7] <= 1'b1; read_key_pipe[7] <= key; end
                default: $fatal(1, "SDRAM model read pipeline overflow");
            endcase
        end
    endtask

    integer bank_index;
    integer pipe_index;
    initial begin
        dq_out = 16'd0;
        dq_oe = 1'b0;
        row_open = 4'd0;
        configured = 1'b0;
        cas_latency = 3'd2;
        burst_length = 4'd2;
        read_valid_pipe = {READ_PIPE_DEPTH{1'b0}};
        write_pending = 1'b0;
        write_left = 4'd0;
        for (bank_index = 0; bank_index < 4; bank_index = bank_index + 1)
            open_row[bank_index] = 13'd0;
        for (pipe_index = 0; pipe_index < READ_PIPE_DEPTH;
             pipe_index = pipe_index + 1)
            read_key_pipe[pipe_index] = 24'd0;
    end

    always @(posedge sdram_clk) begin
        dq_oe <= 1'b0;
        // Burst continuations happen on NOP cycles after the initiating command.
        if (write_pending) begin
            write_word(write_bank, write_col, dq_in, dqm);
            write_col <= write_col + 9'd1;
            if (write_left == 4'd1) begin
                write_pending <= 1'b0;
                write_left <= 4'd0;
            end else begin
                write_left <= write_left - 4'd1;
            end
        end

        if (read_valid_pipe[0]) begin
            dq_out <= memory[read_key_pipe[0]];
            dq_oe <= 1'b1;
        end
        for (pipe_index = 0; pipe_index < READ_PIPE_DEPTH - 1;
             pipe_index = pipe_index + 1) begin
            read_valid_pipe[pipe_index] <= read_valid_pipe[pipe_index + 1];
            read_key_pipe[pipe_index] <= read_key_pipe[pipe_index + 1];
        end
        read_valid_pipe[READ_PIPE_DEPTH - 1] <= 1'b0;

        if (cke) begin
            case (command)
                CMD_LOAD_MODE: begin
                    configured <= 1'b1;
                    cas_latency <= addr[6:4];
                    case (addr[2:0])
                        3'b000: burst_length <= 4'd1;
                        3'b001: burst_length <= 4'd2;
                        3'b010: burst_length <= 4'd4;
                        3'b011: burst_length <= 4'd8;
                        default: burst_length <= 4'd1;
                    endcase
                end
                CMD_ACTIVE: begin
                    open_row[ba] <= addr;
                    row_open[ba] <= 1'b1;
                end
                CMD_PRECHARGE: begin
                    if (addr[10]) row_open <= 4'd0;
                    else row_open[ba] <= 1'b0;
                end
                CMD_REFRESH: begin
                    if (row_open != 4'd0)
                        $fatal(1, "refresh issued with an open SDRAM row");
                end
                CMD_WRITE: begin
                    if (!configured || !row_open[ba])
                        $fatal(1, "write issued before configuration/activate");
                    write_word(ba, addr[8:0], dq_in, dqm);
                    if (burst_length > 1) begin
                        write_pending <= 1'b1;
                        write_bank <= ba;
                        write_col <= addr[8:0] + 9'd1;
                        write_left <= burst_length - 4'd1;
                    end
                end
                CMD_READ: begin
                    if (!configured || !row_open[ba])
                        $fatal(1, "read issued before configuration/activate");
                    // Capture each burst beat into a fixed CAS pipeline. This
                    // permits a new READ when the previous burst is still on
                    // DQ, as the real SDRAM does. The old single pending-read
                    // record both drove CL=2 one edge early and discarded a
                    // continuation when pipelined READ commands overlapped.
                    queue_read(cas_latency - 1,
                               word_key(ba, open_row[ba], addr[8:0]));
                    if (burst_length > 1)
                        queue_read(cas_latency,
                                   word_key(ba, open_row[ba], addr[8:0] + 1));
                    if (burst_length > 2) begin
                        queue_read(cas_latency + 1,
                                   word_key(ba, open_row[ba], addr[8:0] + 2));
                        queue_read(cas_latency + 2,
                                   word_key(ba, open_row[ba], addr[8:0] + 3));
                    end
                    if (burst_length > 4) begin
                        queue_read(cas_latency + 3,
                                   word_key(ba, open_row[ba], addr[8:0] + 4));
                        queue_read(cas_latency + 4,
                                   word_key(ba, open_row[ba], addr[8:0] + 5));
                        queue_read(cas_latency + 5,
                                   word_key(ba, open_row[ba], addr[8:0] + 6));
                        queue_read(cas_latency + 6,
                                   word_key(ba, open_row[ba], addr[8:0] + 7));
                    end
                end
                default: ;
            endcase
        end
    end
endmodule

module tb_sdram32_controller;
    reg clk = 1'b0;
    reg rst = 1'b1;
    always #6.666 clk = ~clk; // 75 MHz

    reg cpu_valid = 1'b0;
    wire cpu_ready;
    reg cpu_write = 1'b0;
    reg [24:0] cpu_addr = 25'd0;
    reg [3:0] cpu_be = 4'd0;
    reg [31:0] cpu_wdata = 32'd0;
    reg cpu_lock = 1'b0;
    wire cpu_rsp_valid;
    wire [31:0] cpu_rdata;

    reg dma_lock = 1'b0;
    reg dma_valid = 1'b0;
    wire dma_ready;
    reg dma_write = 1'b0;
    reg [24:0] dma_addr = 25'd0;
    reg [3:0] dma_be = 4'd0;
    reg [31:0] dma_wdata = 32'd0;
    wire dma_rsp_valid;
    wire [31:0] dma_rdata;

    wire [15:0] sd_din;
    wire [15:0] sd_dout;
    wire sd_doe;
    wire sd_clk, sd_cke, sd_cs, sd_ras, sd_cas, sd_we;
    wire [1:0] sd_dqm, sd_ba;
    wire [12:0] sd_addr;

    sdram32_controller dut (
        .clk(clk), .rst(rst),
        .cpu_valid(cpu_valid), .cpu_ready(cpu_ready),
        .cpu_write(cpu_write), .cpu_addr(cpu_addr), .cpu_be(cpu_be),
        .cpu_wdata(cpu_wdata), .cpu_lock(cpu_lock),
        .cpu_rsp_valid(cpu_rsp_valid),
        .cpu_rdata(cpu_rdata),
        .dma_lock(dma_lock), .dma_valid(dma_valid), .dma_ready(dma_ready),
        .dma_write(dma_write), .dma_addr(dma_addr), .dma_be(dma_be),
        .dma_wdata(dma_wdata), .dma_rsp_valid(dma_rsp_valid),
        .dma_rdata(dma_rdata),
        .sdram_data_in(sd_din), .sdram_data_out(sd_dout),
        .sdram_data_oe(sd_doe), .sdram_clk(sd_clk), .sdram_cke(sd_cke),
        .sdram_cs(sd_cs), .sdram_ras(sd_ras), .sdram_cas(sd_cas),
        .sdram_we(sd_we), .sdram_dqm(sd_dqm), .sdram_addr(sd_addr),
        .sdram_ba(sd_ba)
    );

    astra_sdram_model memory (
        .sdram_clk(sd_clk), .cke(sd_cke), .cs(sd_cs), .ras(sd_ras),
        .cas(sd_cas), .we(sd_we), .addr(sd_addr), .ba(sd_ba),
        .dqm(sd_dqm), .dq_in(sd_dout), .dq_out(sd_din)
    );

`ifdef ASTRA_SDRAM_TRACE
    always @(posedge clk) begin
        if (dut.core.state_q == 4'd4 || |dut.core.rd_q || dut.core.ack_q)
            $display("C t=%0t state=%0d rd=%b din=%04x s0=%04x s=%04x buf=%04x ack=%b r=%08x",
                     $time, dut.core.state_q, dut.core.rd_q, sd_din,
                     dut.core.sample_data0_q, dut.core.sample_data_q,
                     dut.core.data_buffer_q, dut.core.ack_q, dut.core_rdata);
    end
    always @(posedge sd_clk) begin
        if (memory.read_pending || {sd_cs, sd_ras, sd_cas, sd_we} == 4'b0101)
            $display("S t=%0t cmd=%b pipe=%b dq=%04x",
                     $time, {sd_cs, sd_ras, sd_cas, sd_we},
                     memory.read_valid_pipe, memory.dq_out);
    end
`endif

    task automatic cpu_write32(
        input [24:0] address,
        input [3:0] enables,
        input [31:0] data
    );
        begin
            @(negedge clk);
            cpu_addr = address;
            cpu_be = enables;
            cpu_wdata = data;
            cpu_write = 1'b1;
            cpu_valid = 1'b1;
            @(posedge clk);
            while (!cpu_ready) @(posedge clk);
            @(negedge clk);
            cpu_valid = 1'b0;
            while (!cpu_rsp_valid) @(negedge clk);
        end
    endtask

    task automatic cpu_read32(input [24:0] address, output [31:0] data);
        begin
            @(negedge clk);
            cpu_addr = address;
            cpu_be = 4'b1111;
            cpu_write = 1'b0;
            cpu_valid = 1'b1;
            @(posedge clk);
            while (!cpu_ready) @(posedge clk);
            @(negedge clk);
            cpu_valid = 1'b0;
            while (!cpu_rsp_valid) @(negedge clk);
            data = cpu_rdata;
        end
    endtask

    function automatic [31:0] burst_pattern(input integer index);
        burst_pattern = 32'h13579bdf ^ (index * 32'h01020408);
    endfunction

    function automatic [31:0] merge_masked(
        input [31:0] original,
        input [31:0] replacement,
        input [3:0] enables
    );
        integer lane;
        begin
            merge_masked = original;
            for (lane = 0; lane < 4; lane = lane + 1) begin
                if (enables[lane])
                    merge_masked[lane * 8 +: 8] = replacement[lane * 8 +: 8];
            end
        end
    endfunction

    localparam integer BURST_WORDS = 4096;
    reg burst_active = 1'b0;
    reg burst_is_write = 1'b0;
    integer burst_issued = 0;
    integer burst_retired = 0;
    integer burst_cycles = 0;
    integer burst_errors = 0;
    reg rmc_test_active = 1'b0;

    always @(posedge clk) begin
        if (dut.owner == 2'd2 && (cpu_ready || cpu_rsp_valid))
            $fatal(1, "CPU transaction leaked into an owned DMA burst");
        if (rmc_test_active && dma_ready)
            $fatal(1, "DMA transaction leaked into a locked CPU sequence");
        if (burst_active) begin
            burst_cycles <= burst_cycles + 1;
            if (dma_valid && dma_ready) begin
                burst_issued <= burst_issued + 1;
                if (burst_issued + 1 == BURST_WORDS) begin
                    dma_valid <= 1'b0;
                end else begin
                    dma_addr <= dma_addr + 25'd4;
                    dma_wdata <= burst_pattern(burst_issued + 1);
                end
            end
            if (dma_rsp_valid) begin
                if (!burst_is_write &&
                    dma_rdata != burst_pattern(burst_retired)) begin
                    burst_errors <= burst_errors + 1;
                    $display("burst mismatch index=%0d expected=%08x actual=%08x",
                             burst_retired, burst_pattern(burst_retired), dma_rdata);
                end
                burst_retired <= burst_retired + 1;
                if (burst_retired + 1 == BURST_WORDS) begin
                    burst_active <= 1'b0;
                    dma_lock <= 1'b0;
                end
            end
        end
    end

    task automatic run_burst(input write_mode, output integer elapsed_cycles);
        begin
            @(negedge clk);
            burst_is_write = write_mode;
            burst_issued = 0;
            burst_retired = 0;
            burst_cycles = 0;
            burst_errors = 0;
            dma_addr = 25'h0010000;
            dma_be = 4'b1111;
            dma_wdata = burst_pattern(0);
            dma_write = write_mode;
            dma_lock = 1'b1;
            dma_valid = 1'b1;
            burst_active = 1'b1;
            while (burst_active) @(negedge clk);
            elapsed_cycles = burst_cycles;
            repeat (3) @(negedge clk);
        end
    endtask

    integer write_cycles;
    integer read_cycles;
    integer contention_cycles;
    real write_mbps;
    real read_mbps;
    reg [31:0] got;
    reg [31:0] expected;
    integer mask_index;

    initial begin
        repeat (5) @(posedge clk);
        rst = 1'b0;
        // The controller performs its JEDEC 100 us initialization internally.
        repeat (8000) @(posedge clk);

        cpu_write32(25'h0000000, 4'b1111, 32'h11223344);
        cpu_read32(25'h0000000, got);
        if (got != 32'h11223344)
            $fatal(1, "full write/read mismatch: %08x", got);

        cpu_write32(25'h0000000, 4'b1000, 32'haa000000);
        cpu_write32(25'h0000000, 4'b0100, 32'h00bb0000);
        cpu_write32(25'h0000000, 4'b0010, 32'h0000cc00);
        cpu_write32(25'h0000000, 4'b0001, 32'h000000dd);
        cpu_read32(25'h0000000, got);
        if (got != 32'haabbccdd)
            $fatal(1, "byte lane mismatch: %08x", got);

        for (mask_index = 1; mask_index < 16; mask_index = mask_index + 1) begin
            cpu_write32(25'h0000040, 4'b1111, 32'h10203040);
            cpu_write32(25'h0000040, mask_index[3:0], 32'ha1b2c3d4);
            cpu_read32(25'h0000040, got);
            expected = merge_masked(32'h10203040, 32'ha1b2c3d4,
                                    mask_index[3:0]);
            if (got != expected)
                $fatal(1, "mask %b mismatch: expected=%08x actual=%08x",
                       mask_index[3:0], expected, got);
        end

        cpu_write32(25'h0400000, 4'b1100, 32'h5a6b0000);
        cpu_read32(25'h0400000, got);
        if (got[31:16] != 16'h5a6b)
            $fatal(1, "bank address mismatch: %08x", got);
        cpu_write32(25'h1fffffc, 4'b0011, 32'h0000c3d4);
        cpu_read32(25'h1fffffc, got);
        if (got[15:0] != 16'hc3d4)
            $fatal(1, "top address mismatch: %08x", got);

        run_burst(1'b1, write_cycles);
        if (burst_errors != 0) $fatal(1, "write burst errors=%0d", burst_errors);
        run_burst(1'b0, read_cycles);
        if (burst_errors != 0) $fatal(1, "read burst errors=%0d", burst_errors);

        fork
            begin
                run_burst(1'b1, contention_cycles);
            end
            begin
                wait (burst_issued >= 128);
                cpu_write32(25'h0008000, 4'b1111, 32'hfacecafe);
            end
        join
        cpu_read32(25'h0008000, got);
        if (got != 32'hfacecafe)
            $fatal(1, "deferred CPU request mismatch: %08x", got);

        cpu_lock = 1'b1;
        rmc_test_active = 1'b1;
        cpu_write32(25'h0009000, 4'b1111, 32'h11223344);
        fork
            begin
                run_burst(1'b1, contention_cycles);
            end
            begin
                wait (dma_valid);
                repeat (8) @(posedge clk);
                cpu_write32(25'h0009004, 4'b1111, 32'h55667788);
                @(negedge clk);
                cpu_lock = 1'b0;
                rmc_test_active = 1'b0;
            end
        join
        cpu_read32(25'h0009000, got);
        if (got != 32'h11223344)
            $fatal(1, "locked CPU readback mismatch: %08x", got);
        cpu_read32(25'h0009004, got);
        if (got != 32'h55667788)
            $fatal(1, "locked CPU tail readback mismatch: %08x", got);

        write_mbps = (BURST_WORDS * 4.0 * 75.0) / write_cycles;
        read_mbps = (BURST_WORDS * 4.0 * 75.0) / read_cycles;
        $display("SDRAM32 PASS write=%0.2f MB/s (%0d cycles) read=%0.2f MB/s (%0d cycles)",
                 write_mbps, write_cycles, read_mbps, read_cycles);
        if (write_mbps < 120.0 || read_mbps < 120.0)
            $fatal(1, "bandwidth target missed");
        $finish;
    end
endmodule
