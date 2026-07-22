// Functional and throughput gate for Astraea COPY/FILL over the native SDRAM port.
`timescale 1ns/1ps

module tb_astraea_blitter;
    reg cpu_clk = 1'b0;
    reg mem_clk = 1'b0;
    reg rst = 1'b1;
    always #40 cpu_clk = ~cpu_clk;
    always #8.333 mem_clk = ~mem_clk;

    reg cpu_write_stb = 1'b0;
    reg [4:0] cpu_reg = 5'd0;
    reg [3:0] cpu_be = 4'd0;
    reg [31:0] cpu_wdata = 32'd0;
    wire [31:0] blit_rdata;
    wire blit_busy, blit_done, blit_irq, cache_flush;
    wire [31:0] completed_fence;
    reg front_guard_valid = 1'b0;
    reg [24:0] front_guard_start = 25'd0;
    reg [25:0] front_guard_end = 26'd0;
    reg pending_guard_valid = 1'b0;
    reg [24:0] pending_guard_start = 25'd0;
    reg [25:0] pending_guard_end = 26'd0;

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
        .completed_fence(completed_fence),
        .cache_flush(cache_flush),
        .front_guard_valid(front_guard_valid),
        .front_guard_start(front_guard_start),
        .front_guard_end(front_guard_end),
        .pending_guard_valid(pending_guard_valid),
        .pending_guard_start(pending_guard_start),
        .pending_guard_end(pending_guard_end),
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
        .video_lock(1'b0), .video_valid(1'b0), .video_ready(),
        .video_write(1'b0), .video_addr(25'd0), .video_be(4'd0),
        .video_wdata(32'd0), .video_rsp_valid(), .video_rdata(),
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

    task automatic write_element(
        input [24:0] address,
        input integer elem_size,
        input [31:0] value
    );
        integer byte_index;
        integer byte_count;
        reg [7:0] element_byte;
        begin
            byte_count = 1 << elem_size;
            for (byte_index = 0; byte_index < byte_count;
                 byte_index = byte_index + 1) begin
                case (elem_size)
                    0: element_byte = value[7:0];
                    1: element_byte = byte_index == 0 ?
                                      value[15:8] : value[7:0];
                    default: element_byte =
                        value[31 - byte_index * 8 -: 8];
                endcase
                write_byte(address + byte_index, element_byte);
            end
        end
    endtask

    task automatic read_element(
        input [24:0] address,
        input integer elem_size,
        output [31:0] value
    );
        integer byte_index;
        integer byte_count;
        reg [7:0] element_byte;
        begin
            value = 32'd0;
            byte_count = 1 << elem_size;
            for (byte_index = 0; byte_index < byte_count;
                 byte_index = byte_index + 1) begin
                read_byte(address + byte_index, element_byte);
                case (elem_size)
                    0: value[7:0] = element_byte;
                    1: if (byte_index == 0) value[15:8] = element_byte;
                       else value[7:0] = element_byte;
                    default: value[31 - byte_index * 8 -: 8] = element_byte;
                endcase
            end
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

    task automatic expect_invalid(input [255:0] label);
        begin
            reg_write(5'h1a, 32'd1);
            @(negedge cpu_clk);
            cpu_reg = 5'h1b;
            wait (!blit_rdata[1]);
            wait (blit_rdata[1]);
            if (blit_rdata[15:8] !== 8'd1 || blit_busy || dma_lock)
                $fatal(1, "%0s status mismatch %08x", label, blit_rdata);
        end
    endtask

    task automatic expect_protected(input [255:0] label);
        begin
            reg_write(5'h1a, 32'd1);
            @(negedge cpu_clk);
            cpu_reg = 5'h1b;
            wait (!blit_rdata[1]);
            wait (blit_rdata[1]);
            if (blit_rdata[15:8] !== 8'd5 || blit_busy || dma_lock)
                $fatal(1, "%0s status mismatch %08x", label, blit_rdata);
        end
    endtask

    integer i;
    integer elapsed_copy;
    integer elapsed_fill;
    integer elapsed_misc;
    integer elem_size;
    integer elem_bytes;
    integer row;
    integer column;
    reg [31:0] word;
    reg [31:0] source_value;
    reg [31:0] expected_value;
    reg [31:0] key_value;
    reg [31:0] preserved_value;
    reg [7:0] byte_value;
    reg [7:0] mask_pattern;
    reg mask_selected;
    reg [24:0] test_src;
    reg [24:0] test_dst;
    reg [24:0] test_mask;
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

        // Color-key copy compares complete elements even when source and
        // destination are unaligned.
        for (elem_size = 0; elem_size < 3; elem_size = elem_size + 1) begin
            elem_bytes = 1 << elem_size;
            test_src = 25'h0008001 + elem_size * 25'h0000200;
            test_dst = 25'h0008802 + elem_size * 25'h0000200;
            case (elem_size)
                0: begin key_value = 32'h0000005a;
                         preserved_value = 32'h000000aa; end
                1: begin key_value = 32'h0000beef;
                         preserved_value = 32'h0000aaaa; end
                default: begin key_value = 32'hdeadbeef;
                               preserved_value = 32'haaaaaaaa; end
            endcase
            for (i = 0; i < 7; i = i + 1) begin
                case (elem_size)
                    0: source_value = 32'h00000010 + i;
                    1: source_value = 32'h00001200 + i;
                    default: source_value = 32'h12340000 + i;
                endcase
                if (i == 1 || i == 4)
                    source_value = key_value;
                write_element(test_src + i * elem_bytes, elem_size,
                              source_value);
                write_element(test_dst + i * elem_bytes, elem_size,
                              preserved_value);
            end
            reg_write(5'h19, key_value);
            start_blit(test_src, test_dst, 16'd64, 16'd64,
                       16'd7, 16'd1,
                       32'd2 | (elem_size << 4) |
                       (elem_size == 1 ? 32'h00000100 : 32'd0),
                       32'd0, elapsed_misc);
            for (i = 0; i < 7; i = i + 1) begin
                case (elem_size)
                    0: source_value = 32'h00000010 + i;
                    1: source_value = 32'h00001200 + i;
                    default: source_value = 32'h12340000 + i;
                endcase
                expected_value = (i == 1 || i == 4) ?
                                 preserved_value : source_value;
                read_element(test_dst + i * elem_bytes, elem_size, word);
                if (word !== expected_value)
                    $fatal(1, "copy-key mismatch size=%0d i=%0d got=%08x expected=%08x",
                           elem_size, i, word, expected_value);
            end
        end

        // Mask bits are MSB-first and indexed by element, independent of
        // element size. Reverse-Y verifies mask pitch follows row traversal.
        for (elem_size = 0; elem_size < 3; elem_size = elem_size + 1) begin
            elem_bytes = 1 << elem_size;
            test_src = 25'h0010001 + elem_size * 25'h0004000;
            test_dst = test_src + 25'h0001001;
            test_mask = test_src + 25'h0002002;
            case (elem_size)
                0: preserved_value = 32'h000000a5;
                1: preserved_value = 32'h0000a5a5;
                default: preserved_value = 32'ha5a5a5a5;
            endcase
            for (row = 0; row < 2; row = row + 1) begin
                for (column = 0; column < 10; column = column + 1) begin
                    case (elem_size)
                        0: source_value = 32'h00000040 + row * 16 + column;
                        1: source_value = 32'h00004000 + row * 16 + column;
                        default: source_value = 32'h40000000 + row * 16 + column;
                    endcase
                    write_element(test_src + row * (10 * elem_bytes + 3) +
                                  column * elem_bytes, elem_size, source_value);
                    write_element(test_dst + row * (10 * elem_bytes + 5) +
                                  column * elem_bytes, elem_size,
                                  preserved_value);
                end
                write_byte(test_mask + row * 3,
                           row == 0 ? 8'hb2 : 8'h4d);
                write_byte(test_mask + row * 3 + 1,
                           row == 0 ? 8'h80 : 8'h40);
            end
            reg_write(5'h12, {7'd0, test_mask});
            reg_write(5'h15, 32'd3);
            start_blit(test_src, test_dst,
                       10 * elem_bytes + 3, 10 * elem_bytes + 5,
                       16'd10, 16'd2,
                       32'd3 | (elem_size << 4) | 32'h00000200 |
                       (elem_size == 2 ? 32'h00000100 : 32'd0),
                       32'd0, elapsed_misc);
            for (row = 0; row < 2; row = row + 1) begin
                for (column = 0; column < 10; column = column + 1) begin
                    mask_pattern = column < 8 ?
                        (row == 0 ? 8'hb2 : 8'h4d) :
                        (row == 0 ? 8'h80 : 8'h40);
                    mask_selected = mask_pattern[7 - (column & 7)];
                    case (elem_size)
                        0: source_value = 32'h00000040 + row * 16 + column;
                        1: source_value = 32'h00004000 + row * 16 + column;
                        default: source_value = 32'h40000000 + row * 16 + column;
                    endcase
                    expected_value = mask_selected ? source_value :
                                                        preserved_value;
                    read_element(test_dst + row * (10 * elem_bytes + 5) +
                                 column * elem_bytes, elem_size, word);
                    if (word !== expected_value)
                        $fatal(1, "copy-mask mismatch size=%0d row=%0d col=%0d got=%08x expected=%08x",
                               elem_size, row, column, word, expected_value);
                end
            end
        end

        // Reserved operations fail explicitly without taking DMA ownership.
        reg_write(5'h16, {16'd1, 16'd4});
        reg_write(5'h17, 32'd4);
        reg_write(5'h1a, 32'd1);
        @(negedge cpu_clk);
        cpu_reg = 5'h1b;
        wait (blit_rdata[1]);
        if (blit_rdata[15:8] !== 8'd1 || blit_busy || dma_lock)
            $fatal(1, "unsupported operation status mismatch %08x", blit_rdata);

        // The last physical byte is legal. This is the equality boundary for
        // the end-exclusive 32 MiB range check used by every blitter mode.
        reg_write(5'h1c, 32'h12345678);
        start_blit(25'd0, 25'h1ffffff, 16'd0, 16'd1,
                   16'd1, 16'd1, 32'd1, 32'h000000a7, elapsed_misc);
        if (completed_fence !== 32'h12345678)
            $fatal(1, "blitter completion fence mismatch got=%08x",
                   completed_fence);
        read_byte(25'h1ffffff, byte_value);
        if (byte_value !== 8'ha7)
            $fatal(1, "last-byte boundary mismatch got=%02x", byte_value);

        // A command that would cross into either protected presentation range
        // is rejected before its first destination write.
        write_byte(25'h0002ffc, 8'h3c);
        front_guard_valid = 1'b1;
        front_guard_start = 25'h0003000;
        front_guard_end = 26'h0004000;
        reg_write(5'h11, 32'h00002ffc);
        reg_write(5'h14, 32'd8);
        reg_write(5'h16, {16'd1, 16'd8});
        reg_write(5'h17, 32'd1);
        reg_write(5'h18, 32'h000000a5);
        expect_protected("front framebuffer guard");
        read_byte(25'h0002ffc, byte_value);
        if (byte_value !== 8'h3c)
            $fatal(1, "front guard allowed a partial write");
        front_guard_valid = 1'b0;

        pending_guard_valid = 1'b1;
        pending_guard_start = 25'h0005000;
        pending_guard_end = 26'h0005100;
        reg_write(5'h11, 32'h00005000);
        reg_write(5'h14, 32'd4);
        reg_write(5'h16, {16'd1, 16'd4});
        expect_protected("pending framebuffer guard");
        pending_guard_valid = 1'b0;

        // Every source range is validated in the full 32-bit configuration
        // space before fields are narrowed to the native 25-bit SDRAM bus.
        reg_write(5'h16, {16'd1, 16'd4});
        reg_write(5'h17, 32'd1); // 8-bit fill
        reg_write(5'h11, 32'h80001000);
        reg_write(5'h14, 32'd4);
        expect_invalid("high destination address");

        reg_write(5'h11, 32'h00001000);
        reg_write(5'h14, 32'h00010004);
        expect_invalid("high destination pitch");

        reg_write(5'h11, 32'h01fffffc);
        reg_write(5'h14, 32'd8);
        reg_write(5'h16, {16'd1, 16'd8});
        expect_invalid("destination range overflow");

        reg_write(5'h17, 32'd0); // 8-bit copy
        reg_write(5'h10, 32'h01fffffc);
        reg_write(5'h11, 32'h00002000);
        reg_write(5'h13, 32'd8);
        reg_write(5'h14, 32'd8);
        expect_invalid("source range overflow");

        reg_write(5'h17, 32'd3); // 8-bit masked copy
        reg_write(5'h10, 32'h00001000);
        reg_write(5'h12, 32'h01ffffff);
        reg_write(5'h15, 32'd2);
        reg_write(5'h16, {16'd1, 16'd16});
        expect_invalid("mask range overflow");

        copy_mbps = (1024.0 * 60.0) / elapsed_copy;
        fill_mbps = (2048.0 * 60.0) / elapsed_fill;
        $display("ASTRAEA BLITTER PASS copy=%0.2f MB/s fill=%0.2f MB/s copy_cycles=%0d fill_cycles=%0d",
                 copy_mbps, fill_mbps, elapsed_copy, elapsed_fill);
        if (copy_mbps < 38.0 || fill_mbps < 88.0)
            $fatal(1, "blitter bandwidth target missed");
        $finish;
    end
endmodule
