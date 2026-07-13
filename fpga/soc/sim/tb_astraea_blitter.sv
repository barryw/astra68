// Functional and throughput gate for Astraea COPY/FILL over the native SDRAM port.
`timescale 1ns/1ps

module tb_astraea_blitter;
    reg cpu_clk = 1'b0;
    reg mem_clk = 1'b0;
    reg rst = 1'b1;
    always #40 cpu_clk = ~cpu_clk;
    always #6.666 mem_clk = ~mem_clk;

    reg cpu_write_stb = 1'b0;
    reg [4:0] cpu_reg = 5'd0;
    reg [3:0] cpu_be = 4'd0;
    reg [31:0] cpu_wdata = 32'd0;
    wire [31:0] blit_rdata;
    wire blit_busy, blit_done, blit_irq, cache_flush;

    wire dma_lock, dma_valid, dma_ready, dma_write;
    wire [24:0] dma_addr;
    wire [3:0] dma_be;
    wire [31:0] dma_wdata;
    wire dma_rsp_valid;
    wire [31:0] dma_rdata;

    reg cpu_mem_valid = 1'b0;
    wire cpu_mem_ready;
    reg cpu_mem_write = 1'b0;
    reg [24:0] cpu_mem_addr = 25'd0;
    reg [3:0] cpu_mem_be = 4'd0;
    reg [31:0] cpu_mem_wdata = 32'd0;
    wire cpu_mem_rsp_valid;
    wire [31:0] cpu_mem_rdata;

    astraea_blitter blitter (
        .cpu_clk(cpu_clk), .cpu_rst(rst),
        .cpu_write_stb(cpu_write_stb), .cpu_reg(cpu_reg),
        .cpu_be(cpu_be), .cpu_wdata(cpu_wdata), .cpu_rdata(blit_rdata),
        .cpu_busy(blit_busy), .cpu_done(blit_done), .cpu_irq(blit_irq),
        .cache_flush(cache_flush),
        .mem_clk(mem_clk), .mem_rst(rst), .mem_lock(dma_lock),
        .mem_valid(dma_valid), .mem_ready(dma_ready),
        .mem_write(dma_write), .mem_addr(dma_addr), .mem_be(dma_be),
        .mem_wdata(dma_wdata), .mem_rsp_valid(dma_rsp_valid),
        .mem_rdata(dma_rdata)
    );

    wire [15:0] sd_din, sd_dout;
    wire sd_doe, sd_clk, sd_cke, sd_cs, sd_ras, sd_cas, sd_we;
    wire [1:0] sd_dqm, sd_ba;
    wire [12:0] sd_addr;

    sdram32_controller controller (
        .clk(mem_clk), .rst(rst),
        .cpu_valid(cpu_mem_valid), .cpu_ready(cpu_mem_ready),
        .cpu_write(cpu_mem_write), .cpu_addr(cpu_mem_addr),
        .cpu_be(cpu_mem_be), .cpu_wdata(cpu_mem_wdata), .cpu_lock(1'b0),
        .cpu_rsp_valid(cpu_mem_rsp_valid), .cpu_rdata(cpu_mem_rdata),
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

    task automatic reg_write(input [4:0] address, input [31:0] value);
        begin
            @(negedge cpu_clk);
            cpu_reg = address;
            cpu_be = 4'b1111;
            cpu_wdata = value;
            cpu_write_stb = 1'b1;
            @(negedge cpu_clk);
            cpu_write_stb = 1'b0;
        end
    endtask

    task automatic native_write(
        input [24:0] address,
        input [3:0] enables,
        input [31:0] value
    );
        begin
            @(negedge mem_clk);
            cpu_mem_addr = {address[24:2], 2'b00};
            cpu_mem_write = 1'b1;
            cpu_mem_be = enables;
            cpu_mem_wdata = value;
            cpu_mem_valid = 1'b1;
            @(posedge mem_clk);
            while (!cpu_mem_ready) @(posedge mem_clk);
            @(negedge mem_clk);
            cpu_mem_valid = 1'b0;
            while (!cpu_mem_rsp_valid) @(negedge mem_clk);
        end
    endtask

    task automatic native_read(input [24:0] address, output [31:0] value);
        begin
            @(negedge mem_clk);
            cpu_mem_addr = {address[24:2], 2'b00};
            cpu_mem_write = 1'b0;
            cpu_mem_be = 4'b1111;
            cpu_mem_wdata = 32'd0;
            cpu_mem_valid = 1'b1;
            @(posedge mem_clk);
            while (!cpu_mem_ready) @(posedge mem_clk);
            @(negedge mem_clk);
            cpu_mem_valid = 1'b0;
            while (!cpu_mem_rsp_valid) @(negedge mem_clk);
            value = cpu_mem_rdata;
        end
    endtask

    function automatic [3:0] byte_be(input [1:0] lane);
        byte_be = 4'b1000 >> lane;
    endfunction

    function automatic [31:0] byte_data(input [1:0] lane, input [7:0] value);
        begin
            case (lane)
                2'd0: byte_data = {value, 24'd0};
                2'd1: byte_data = {8'd0, value, 16'd0};
                2'd2: byte_data = {16'd0, value, 8'd0};
                default: byte_data = {24'd0, value};
            endcase
        end
    endfunction

    function automatic [7:0] read_lane(input [1:0] lane, input [31:0] value);
        begin
            case (lane)
                2'd0: read_lane = value[31:24];
                2'd1: read_lane = value[23:16];
                2'd2: read_lane = value[15:8];
                default: read_lane = value[7:0];
            endcase
        end
    endfunction

    task automatic write_byte(input [24:0] address, input [7:0] value);
        native_write(address, byte_be(address[1:0]),
                     byte_data(address[1:0], value));
    endtask

    task automatic read_byte(input [24:0] address, output [7:0] value);
        reg [31:0] word;
        begin
            native_read(address, word);
            value = read_lane(address[1:0], word);
        end
    endtask

    task automatic start_blit(
        input [24:0] src,
        input [24:0] dst,
        input [15:0] src_pitch,
        input [15:0] dst_pitch,
        input [15:0] width,
        input [15:0] height,
        input [31:0] operation,
        input [31:0] color,
        output integer elapsed
    );
        begin
            reg_write(5'h10, {7'd0, src});
            reg_write(5'h11, {7'd0, dst});
            reg_write(5'h13, {16'd0, src_pitch});
            reg_write(5'h14, {16'd0, dst_pitch});
            reg_write(5'h16, {height, width});
            reg_write(5'h17, operation);
            reg_write(5'h18, color);
            reg_write(5'h1a, 32'd1);
            wait (blit_busy);
            elapsed = 0;
            while (blit_busy) begin
                @(posedge mem_clk);
                elapsed = elapsed + 1;
                if (elapsed > 1000000)
                    $fatal(1, "blitter timeout");
            end
            wait (blit_done);
            @(negedge cpu_clk);
            cpu_reg = 5'h1b;
            @(posedge cpu_clk);
            if (blit_rdata[15:8] != 8'd0)
                $fatal(1, "blitter error status=%08x", blit_rdata);
        end
    endtask

    integer i;
    integer elapsed_copy;
    integer elapsed_fill;
    integer elapsed_misc;
    reg [31:0] word;
    reg [7:0] byte_value;
    real copy_mbps;
    real fill_mbps;

    initial begin
        repeat (8) @(posedge mem_clk);
        rst = 1'b0;
        repeat (8000) @(posedge mem_clk);

        if (blit_rdata !== 32'h41535452)
            $fatal(1, "Astraea ID mismatch %08x", blit_rdata);

        // Aligned 32-bit copy, large enough to exercise several chunks.
        for (i = 0; i < 256; i = i + 1) begin
            native_write(25'h0001000 + i * 4, 4'b1111,
                         32'h60000000 ^ (i * 32'h01010101));
            native_write(25'h0002000 + i * 4, 4'b1111, 32'd0);
        end
        for (i = 0; i < 4; i = i + 1) begin
            native_read(25'h0001000 + i * 4, word);
            if (word !== (32'h60000000 ^ (i * 32'h01010101)))
                $fatal(1, "source initialization mismatch i=%0d got=%08x", i, word);
        end
        start_blit(25'h0001000, 25'h0002000, 16'd1024, 16'd1024,
                   16'd256, 16'd1, 32'h00000020, 32'd0, elapsed_copy);
        for (i = 0; i < 256; i = i + 1) begin
            native_read(25'h0002000 + i * 4, word);
            if (word !== (32'h60000000 ^ (i * 32'h01010101)))
                $fatal(1, "aligned copy mismatch i=%0d got=%08x", i, word);
        end

        // Aligned fill should issue a full native word per request.
        start_blit(25'd0, 25'h0003000, 16'd0, 16'd2048,
                   16'd512, 16'd1, 32'h00000021, 32'h5aa5c33c,
                   elapsed_fill);
        for (i = 0; i < 512; i = i + 1) begin
            native_read(25'h0003000 + i * 4, word);
            if (word !== 32'h5aa5c33c)
                $fatal(1, "fill mismatch i=%0d got=%08x", i, word);
        end

        // Unaligned byte copy with untouched guard bytes.
        for (i = 0; i < 44; i = i + 1) begin
            write_byte(25'h0005000 + i, 8'hee);
            write_byte(25'h0004800 + i, 8'h30 + i);
        end
        start_blit(25'h0004803, 25'h0005001, 16'd37, 16'd37,
                   16'd37, 16'd1, 32'h00000000, 32'd0, elapsed_misc);
        read_byte(25'h0005000, byte_value);
        if (byte_value !== 8'hee) $fatal(1, "low guard overwritten");
        for (i = 0; i < 37; i = i + 1) begin
            read_byte(25'h0005001 + i, byte_value);
            if (byte_value !== (8'h33 + i))
                $fatal(1, "unaligned copy mismatch i=%0d got=%02x", i, byte_value);
        end
        read_byte(25'h0005026, byte_value);
        if (byte_value !== 8'hee) $fatal(1, "high guard overwritten");

        // Reverse traversal provides memmove semantics for overlapping ranges.
        for (i = 0; i < 64; i = i + 1)
            write_byte(25'h0006000 + i, i[7:0]);
        start_blit(25'h0006000, 25'h0006004, 16'd60, 16'd60,
                   16'd60, 16'd1, 32'h00000100, 32'd0, elapsed_misc);
        for (i = 0; i < 60; i = i + 1) begin
            read_byte(25'h0006004 + i, byte_value);
            if (byte_value !== i[7:0])
                $fatal(1, "reverse overlap mismatch i=%0d got=%02x", i, byte_value);
        end

        // Two-dimensional unaligned copy, traversed bottom-to-top.
        for (i = 0; i < 27; i = i + 1)
            write_byte(25'h0007000 + i, 8'h80 + i);
        for (i = 0; i < 35; i = i + 1)
            write_byte(25'h0007100 + i, 8'hee);
        start_blit(25'h0007001, 25'h0007102, 16'd9, 16'd11,
                   16'd5, 16'd3, 32'h00000200, 32'd0, elapsed_misc);
        for (i = 0; i < 3; i = i + 1) begin
            integer column;
            for (column = 0; column < 5; column = column + 1) begin
                read_byte(25'h0007102 + i * 11 + column, byte_value);
                if (byte_value !== (8'h81 + i * 9 + column))
                    $fatal(1, "2D copy mismatch row=%0d col=%0d got=%02x",
                           i, column, byte_value);
            end
        end

        // Reserved operations fail explicitly without taking DMA ownership.
        reg_write(5'h16, {16'd1, 16'd4});
        reg_write(5'h17, 32'd2);
        reg_write(5'h1a, 32'd1);
        @(negedge cpu_clk);
        cpu_reg = 5'h1b;
        wait (blit_rdata[1]);
        if (blit_rdata[15:8] !== 8'd1 || blit_busy || dma_lock)
            $fatal(1, "unsupported operation status mismatch %08x", blit_rdata);

        copy_mbps = (1024.0 * 75.0) / elapsed_copy;
        fill_mbps = (2048.0 * 75.0) / elapsed_fill;
        $display("ASTRAEA BLITTER PASS copy=%0.2f MB/s fill=%0.2f MB/s copy_cycles=%0d fill_cycles=%0d",
                 copy_mbps, fill_mbps, elapsed_copy, elapsed_fill);
        if (copy_mbps < 45.0 || fill_mbps < 100.0)
            $fatal(1, "blitter bandwidth target missed");
        $finish;
    end
endmodule
