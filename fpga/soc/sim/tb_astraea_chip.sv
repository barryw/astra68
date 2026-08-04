// Integration gate for Astraea copper routing, global IRQs, and blitter MMIO.
`timescale 1ns/1ps

module tb_astraea_chip;
    localparam [31:0] OP_END  = 32'h00000000;
    localparam [31:0] OP_MOVE = 32'h20000000;
    localparam [31:0] OP_IRQ  = 32'h80000000;

    reg clk = 1'b0;
    reg mem_clk = 1'b0;
    always #40 clk = ~clk;
    always #8.333 mem_clk = ~mem_clk;
    reg rst = 1'b1;

    reg cpu_write_stb = 1'b0;
    reg [15:0] cpu_addr = 16'd0;
    reg [3:0] cpu_be = 4'd0;
    reg [31:0] cpu_wdata = 32'd0;
    wire [31:0] cpu_rdata;
    wire cpu_busy;
    wire cpu_done;
    wire cpu_irq;
    wire cache_flush;
    wire cop_move_stb;
    wire [17:0] cop_move_addr;
    wire [31:0] cop_move_data;
    wire mem_lock;
    wire mem_valid;
    reg mem_hold = 1'b0;
    wire mem_ready;
    wire mem_write;
    wire [24:0] mem_addr;
    wire [3:0] mem_be;
    wire [31:0] mem_wdata;
    reg mem_rsp_valid = 1'b0;
    reg [31:0] mem_rdata = 32'd0;

    astraea_chip dut (
        .cpu_clk(clk), .cpu_rst(rst), .cpu_write_stb(cpu_write_stb),
        .cpu_addr(cpu_addr), .cpu_be(cpu_be), .cpu_wdata(cpu_wdata),
        .cpu_rdata(cpu_rdata), .cpu_busy(cpu_busy), .cpu_done(cpu_done),
        .cpu_irq(cpu_irq), .cache_flush(cache_flush),
        .beam_x(10'd0), .beam_y(10'd0),
        .cop_move_stb(cop_move_stb), .cop_move_addr(cop_move_addr),
        .cop_move_data(cop_move_data),
        .front_guard_valid(1'b0), .front_guard_start(25'd0),
        .front_guard_end(26'd0), .pending_guard_valid(1'b0),
        .pending_guard_start(25'd0), .pending_guard_end(26'd0),
        .mem_clk(mem_clk), .mem_rst(rst), .mem_lock(mem_lock),
        .mem_valid(mem_valid), .mem_ready(mem_ready),
        .mem_write(mem_write), .mem_addr(mem_addr), .mem_be(mem_be),
        .mem_wdata(mem_wdata), .mem_rsp_valid(mem_rsp_valid),
        .mem_rdata(mem_rdata)
    );

    reg [7:0] memory [0:1023];
    reg response_pending = 1'b0;
    reg [31:0] response_data = 32'd0;
    assign mem_ready = !mem_hold && !response_pending;
    always @(posedge mem_clk) begin
        mem_rsp_valid <= 1'b0;
        if (rst) begin
            response_pending <= 1'b0;
            mem_rdata <= 32'd0;
        end else begin
            if (response_pending) begin
                mem_rsp_valid <= 1'b1;
                mem_rdata <= response_data;
                response_pending <= 1'b0;
            end
            if (mem_valid && mem_ready) begin
                if (mem_addr + 25'd3 >= 1024)
                    $fatal(1, "integrated memory address %08x", mem_addr);
                response_data <= {memory[mem_addr], memory[mem_addr + 1],
                                  memory[mem_addr + 2], memory[mem_addr + 3]};
                if (mem_write) begin
                    if (mem_be[3]) memory[mem_addr] <= mem_wdata[31:24];
                    if (mem_be[2]) memory[mem_addr + 1] <= mem_wdata[23:16];
                    if (mem_be[1]) memory[mem_addr + 2] <= mem_wdata[15:8];
                    if (mem_be[0]) memory[mem_addr + 3] <= mem_wdata[7:0];
                end
                response_pending <= 1'b1;
            end
        end
    end

    task automatic write32(input [15:0] address, input [31:0] value);
        begin
            @(negedge clk);
            cpu_addr = address;
            cpu_be = 4'b1100;
            cpu_wdata = {value[31:16], 16'd0};
            cpu_write_stb = 1'b1;
            @(negedge clk);
            cpu_write_stb = 1'b0;
            @(negedge clk);
            cpu_addr = address + 16'd2;
            cpu_be = 4'b0011;
            cpu_wdata = {16'd0, value[15:0]};
            cpu_write_stb = 1'b1;
            @(negedge clk);
            cpu_write_stb = 1'b0;
            cpu_be = 4'd0;
            cpu_wdata = 32'd0;
        end
    endtask

    task automatic read32(input [15:0] address, output [31:0] value);
        begin
            @(negedge clk);
            cpu_addr = address;
            repeat (2) @(posedge clk);
            #1 value[31:16] = cpu_rdata[31:16];
            @(negedge clk);
            cpu_addr = address + 16'd2;
            repeat (2) @(posedge clk);
            #1 value[15:0] = cpu_rdata[15:0];
        end
    endtask

    task automatic write_insn(
        input [10:0] index,
        input [31:0] word0,
        input [31:0] word1
    );
        reg [15:0] address;
        begin
            address = 16'h4000 + {2'd0, index, 3'b000};
            write32(address, word0);
            write32(address + 16'd4, word1);
        end
    endtask

    integer external_moves = 0;
    reg [17:0] last_move_addr = 18'd0;
    reg [31:0] last_move_data = 32'd0;
    always @(posedge clk) begin
        if (cop_move_stb) begin
            external_moves <= external_moves + 1;
            last_move_addr <= cop_move_addr;
            last_move_data <= cop_move_data;
        end
    end

    reg [31:0] value;
    integer timeout;
    integer init_index;

    initial begin
        for (init_index = 0; init_index < 1024; init_index = init_index + 1)
            memory[init_index] = 8'h00;
        repeat (6) @(posedge clk);
        rst = 1'b0;

        read32(16'h0000, value);
        if (value !== 32'h41535452)
            $fatal(1, "Astraea ID mismatch %08x", value);
        read32(16'h0004, value);
        if (value !== 32'h00040000)
            $fatal(1, "Astraea version mismatch %08x", value);
        read32(16'h0018, value);
        if (value !== 32'h000000ff)
            $fatal(1, "Astraea capabilities mismatch %08x", value);

        write_insn(0, OP_MOVE | 32'h00010060, 32'h89abcdef);
        write_insn(1, OP_MOVE | 32'h00010120, 32'h00005aa5);
        write_insn(2, OP_MOVE | 32'h00020030, 32'h00123456);
        write_insn(3, OP_IRQ | 32'h9, 32'd0);
        write_insn(4, OP_END, 32'd0);
        write32(16'h0010, 32'h00000002);
        write32(16'h0084, 32'd0);
        write32(16'h0080, 32'd1);
        write32(16'h008c, 32'd1);

        timeout = 0;
        while (dut.copper_running) begin
            @(posedge clk);
            timeout = timeout + 1;
            if (timeout > 100)
                $fatal(1, "integrated copper timeout");
        end
        repeat (3) @(posedge clk);

        read32(16'h0060, value);
        if (value !== 32'h89abcdef)
            $fatal(1, "copper-to-blitter MOVE mismatch %08x", value);
        read32(16'h0120, value);
        if (value !== 32'h00005aa5)
            $fatal(1, "copper-to-draw MOVE mismatch %08x", value);
        if (external_moves != 1 || last_move_addr !== 18'h20030 ||
            last_move_data !== 32'h00123456)
            $fatal(1, "external MOVE mismatch count=%0d addr=%05x data=%08x",
                   external_moves, last_move_addr, last_move_data);
        read32(16'h0014, value);
        if (!value[1] || !cpu_irq)
            $fatal(1, "copper global IRQ mismatch stat=%08x irq=%b",
                   value, cpu_irq);
        read32(16'h0090, value);
        if (value[3:0] != 4'h9)
            $fatal(1, "copper IRQ source mismatch %08x", value);
        write32(16'h0014, 32'h00000002);
        repeat (2) @(posedge clk);
        read32(16'h0014, value);
        if (value[1] || cpu_irq)
            $fatal(1, "copper IRQ clear mismatch stat=%08x irq=%b",
                   value, cpu_irq);
        if (cpu_busy || cpu_done || cache_flush)
            $fatal(1, "idle blitter status mismatch");

        // The kernel qualification path deliberately launches a zero-sized
        // blit. It must complete without memory traffic and raise BLIT_DONE.
        write32(16'h0010, 32'd0);
        write32(16'h0014, 32'h00000001);
        write32(16'h0058, 32'd0);
        write32(16'h0010, 32'h00000001);
        write32(16'h0068, 32'h00000003);
        timeout = 0;
        while (!cpu_irq && timeout < 100) begin
            @(posedge clk);
            timeout = timeout + 1;
        end
        read32(16'h0014, value);
        if (timeout >= 100 || !value[0] || !cpu_irq)
            $fatal(1,
                   "zero-sized blit IRQ mismatch timeout=%0d stat=%08x irq=%b",
                   timeout, value, cpu_irq);
        write32(16'h0010, 32'd0);
        write32(16'h0014, 32'h00000001);
        repeat (2) @(posedge clk);
        if (cpu_irq)
            $fatal(1, "zero-sized blit IRQ did not clear");

        // A fully clipped draw command exercises draw MMIO, completion, fence,
        // and global IRQ plumbing without requiring the memory test model.
        write32(16'h0100, 32'd0);
        write32(16'h0104, 32'd16);
        write32(16'h0108, 32'd0);
        write32(16'h010c, 32'h00000000);
        write32(16'h0110, 32'h00040004);
        write32(16'h0114, 32'hffffffff);
        write32(16'h0118, 32'hffffffff);
        write32(16'h014c, 32'd0);
        write32(16'h0158, 32'h13579bdf);
        write32(16'h0010, 32'h00000008);
        write32(16'h0150, 32'd1);
        timeout = 0;
        while (!cpu_busy && timeout < 20) begin
            @(posedge clk);
            timeout = timeout + 1;
        end
        while (cpu_busy && timeout < 100) begin
            @(posedge clk);
            timeout = timeout + 1;
        end
        if (timeout >= 100)
            $fatal(1, "integrated draw timeout");
        repeat (3) @(posedge clk);
        read32(16'h0158, value);
        if (value !== 32'h13579bdf)
            $fatal(1, "integrated draw fence mismatch %08x", value);
        read32(16'h0014, value);
        if (!value[3] || !cpu_irq)
            $fatal(1, "draw global IRQ mismatch stat=%08x irq=%b",
                   value, cpu_irq);
        write32(16'h0014, 32'h00000008);
        repeat (2) @(posedge clk);
        if (cpu_irq)
            $fatal(1, "draw global IRQ did not clear");

        // Launch both DMA clients while memory is held. The registered local
        // owner must retire the blitter stream before routing draw responses.
        write32(16'h0044, 32'h00000100);
        write32(16'h0050, 32'd4);
        write32(16'h0058, 32'h00010004);
        write32(16'h005c, 32'd1);
        write32(16'h0060, 32'h000000aa);
        write32(16'h0100, 32'h00000200);
        write32(16'h0104, 32'd16);
        write32(16'h0108, 32'd0);
        write32(16'h010c, 32'h00000000);
        write32(16'h0110, 32'h00010004);
        write32(16'h0114, 32'h00000000);
        write32(16'h0118, 32'h00000003);
        write32(16'h0120, 32'h00000055);
        write32(16'h014c, 32'd0);
        mem_hold = 1'b1;
        write32(16'h0068, 32'd1);
        write32(16'h0150, 32'd1);
        timeout = 0;
        while ((!dut.blitter_mem_lock || !dut.draw_mem_lock) &&
               timeout < 128) begin
            @(posedge mem_clk);
            timeout = timeout + 1;
        end
        if (!dut.blitter_mem_lock || !dut.draw_mem_lock)
            $fatal(1, "concurrent engines did not both request ownership");
        mem_hold = 1'b0;
        timeout = 0;
        while (cpu_busy && timeout < 1000) begin
            @(posedge clk);
            timeout = timeout + 1;
        end
        if (timeout >= 1000)
            $fatal(1, "integrated memory owner timeout");
        repeat (4) @(posedge mem_clk);
        if ({memory[16'h100], memory[16'h101], memory[16'h102],
             memory[16'h103]} !== 32'haaaaaaaa)
            $fatal(1, "integrated blitter data mismatch %02x%02x%02x%02x",
                   memory[16'h100], memory[16'h101], memory[16'h102],
                   memory[16'h103]);
        if ({memory[16'h200], memory[16'h201], memory[16'h202],
             memory[16'h203]} !== 32'h55555555)
            $fatal(1, "integrated draw data mismatch %02x%02x%02x%02x",
                   memory[16'h200], memory[16'h201], memory[16'h202],
                   memory[16'h203]);
        if (mem_lock)
            $fatal(1, "integrated memory owner remained locked");

        $display("ASTRAEA CHIP PASS external_moves=%0d", external_moves);
        $finish;
    end
endmodule
