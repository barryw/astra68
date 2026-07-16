// Directed instruction, beam-wait, restart, and fault tests for Astraea copper.
`timescale 1ns/1ps

module tb_astraea_copper;
    localparam [31:0] OP_END  = 32'h00000000;
    localparam [31:0] OP_MOVE = 32'h20000000;
    localparam [31:0] OP_WAIT = 32'h40000000;
    localparam [31:0] OP_SKIP = 32'h60000000;
    localparam [31:0] OP_IRQ  = 32'h80000000;
    localparam [31:0] OP_JUMP = 32'ha0000000;

    reg clk = 1'b0;
    always #40 clk = ~clk;
    reg rst = 1'b1;

    reg cpu_write_stb = 1'b0;
    reg [15:0] cpu_addr = 16'd0;
    reg [3:0] cpu_be = 4'd0;
    reg [31:0] cpu_wdata = 32'd0;
    wire [31:0] cpu_rdata;

    reg [9:0] beam_x = 10'd0;
    reg [9:0] beam_y = 10'd0;
    wire move_stb;
    wire [17:0] move_addr;
    wire [31:0] move_data;
    wire irq_event;
    wire [3:0] irq_sources;
    wire running;
    wire waiting;
    wire fault;

    astraea_copper dut (
        .clk(clk), .rst(rst),
        .cpu_write_stb(cpu_write_stb), .cpu_addr(cpu_addr),
        .cpu_be(cpu_be), .cpu_wdata(cpu_wdata), .cpu_rdata(cpu_rdata),
        .beam_x_async(beam_x), .beam_y_async(beam_y),
        .move_stb(move_stb), .move_addr(move_addr), .move_data(move_data),
        .irq_event(irq_event), .irq_sources(irq_sources),
        .running(running), .waiting(waiting), .fault(fault)
    );

    task automatic write32(input [15:0] address, input [31:0] value);
        begin
            @(negedge clk);
            cpu_addr = address;
            cpu_be = 4'b1100;
            cpu_wdata = value;
            cpu_write_stb = 1'b1;
            @(negedge clk);
            cpu_addr = address + 16'd2;
            cpu_be = 4'b0011;
            @(negedge clk);
            cpu_write_stb = 1'b0;
            cpu_be = 4'd0;
            cpu_wdata = 32'd0;
        end
    endtask

    task automatic write_masked(
        input [15:0] address,
        input [3:0] enables,
        input [31:0] value
    );
        begin
            @(negedge clk);
            cpu_addr = address;
            cpu_be = enables;
            cpu_wdata = value;
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
            #1 value = cpu_rdata;
        end
    endtask

    task automatic write_insn(
        input [10:0] index,
        input [31:0] word0,
        input [31:0] word1
    );
        reg [15:0] address;
        begin
            address = 16'h4000 + {index, 3'b000};
            write_masked(address, 4'b1100, word0);
            write_masked(address + 16'd2, 4'b0011, word0);
            write_masked(address + 16'd4, 4'b1100, word1);
            write_masked(address + 16'd6, 4'b0011, word1);
        end
    endtask

    task automatic wait_stopped;
        integer timeout;
        begin
            timeout = 0;
            while (running) begin
                @(posedge clk);
                timeout = timeout + 1;
                if (timeout > 200)
                    $fatal(1, "copper stop timeout pc=%0d", dut.pc);
            end
        end
    endtask

    integer move_count = 0;
    reg [17:0] move_addr_log [0:15];
    reg [31:0] move_data_log [0:15];
    integer irq_count = 0;
    reg [3:0] last_irq_sources = 4'd0;

    always @(posedge clk) begin
        if (move_stb) begin
            move_addr_log[move_count] <= move_addr;
            move_data_log[move_count] <= move_data;
            move_count <= move_count + 1;
        end
        if (irq_event) begin
            irq_count <= irq_count + 1;
            last_irq_sources <= irq_sources;
        end
    end

    reg [31:0] value;
    integer first_restart_moves;

    initial begin
        repeat (5) @(posedge clk);
        rst = 1'b0;

        // Byte enables merge exactly; this also models the two portions of a
        // physical 68030 longword store.
        write32(16'h4000, 32'h11223344);
        write_masked(16'h4000, 4'b0101, 32'haabbccdd);
        read32(16'h4000, value);
        if (value !== 32'h11bb33dd)
            $fatal(1, "copper RAM byte-enable mismatch %08x", value);

        write_insn(0, OP_WAIT | 32'd5, 32'd20);
        write_insn(1, OP_MOVE | 32'h00020030, 32'h00112233);
        write_insn(2, OP_SKIP | 32'd5, 32'd30);
        write_insn(3, OP_MOVE | 32'h00020034, 32'h00445566);
        write_insn(4, OP_SKIP | 32'd5, 32'd10);
        write_insn(5, OP_MOVE | 32'h00020038, 32'h00badbad);
        write_insn(6, OP_IRQ | 32'h5, 32'd0);
        write_insn(7, OP_JUMP | 32'd9, 32'd0);
        write_insn(8, OP_MOVE | 32'h0002003c, 32'h00bad008);
        write_insn(9, OP_END, 32'd0);

        write32(16'h0084, 32'd0);
        write32(16'h0080, 32'd1);
        write32(16'h008c, 32'd1);
        repeat (8) @(posedge clk);
        if (!running || !waiting || move_count != 0)
            $fatal(1, "WAIT did not hold running=%b waiting=%b moves=%0d",
                   running, waiting, move_count);

        beam_y = 10'd5;
        beam_x = 10'd19;
        repeat (6) @(posedge clk);
        if (!waiting || move_count != 0)
            $fatal(1, "WAIT released before target");
        beam_x = 10'd20;
        wait_stopped();
        repeat (3) @(posedge clk);

        if (fault || move_count != 2)
            $fatal(1, "program completion mismatch fault=%b moves=%0d",
                   fault, move_count);
        if (move_addr_log[0] !== 18'h20030 ||
            move_data_log[0] !== 32'h00112233)
            $fatal(1, "first MOVE mismatch addr=%05x data=%08x",
                   move_addr_log[0], move_data_log[0]);
        if (move_addr_log[1] !== 18'h20034 ||
            move_data_log[1] !== 32'h00445566)
            $fatal(1, "SKIP false MOVE mismatch addr=%05x data=%08x",
                   move_addr_log[1], move_data_log[1]);
        if (irq_count != 1 || last_irq_sources != 4'h5)
            $fatal(1, "IRQ instruction mismatch count=%0d sources=%x",
                   irq_count, last_irq_sources);
        read32(16'h0088, value);
        if (value[18:16] != 3'b000 || value[10:0] != 11'd9)
            $fatal(1, "stopped status mismatch %08x", value);

        // Vblank restart arms the selected list at the next active-frame
        // boundary. A visible-line WAIT must not collapse while y is in
        // vertical blanking.
        write_insn(16, OP_WAIT | 32'd5, 32'd0);
        write_insn(17, OP_MOVE | 32'h00020030, 32'h00abcdef);
        write_insn(18, OP_END, 32'd0);
        beam_x = 10'd0;
        beam_y = 10'd0;
        repeat (4) @(posedge clk);
        write32(16'h0084, 32'd16);
        write32(16'h0080, 32'd3);
        repeat (6) @(posedge clk);
        if (running)
            $fatal(1, "VBL_RESTART ran before vblank");
        first_restart_moves = move_count;
        beam_y = 10'd480;
        repeat (16) @(posedge clk);
        if (move_count != first_restart_moves || running)
            $fatal(1, "copper ran on vblank entry moves=%0d running=%b",
                   move_count, running);
        beam_y = 10'd0;
        repeat (12) @(posedge clk);
        if (!running || !waiting || move_count != first_restart_moves)
            $fatal(1, "frame-start WAIT mismatch moves=%0d running=%b waiting=%b",
                   move_count, running, waiting);
        beam_y = 10'd5;
        wait_stopped();
        if (move_count != first_restart_moves + 1)
            $fatal(1, "first frame restart MOVE missing moves=%0d", move_count);
        beam_y = 10'd480;
        repeat (16) @(posedge clk);
        if (move_count != first_restart_moves + 1 || running)
            $fatal(1, "second vblank entry executed moves=%0d running=%b",
                   move_count, running);
        beam_y = 10'd0;
        repeat (12) @(posedge clk);
        beam_y = 10'd5;
        wait_stopped();
        if (move_count != first_restart_moves + 2)
            $fatal(1, "second frame restart MOVE missing moves=%0d", move_count);

        // Misaligned MOVE encodings halt and set the sticky fault bit.
        write_insn(20, OP_MOVE | 32'h00020031, 32'hffffffff);
        write_insn(21, OP_END, 32'd0);
        write32(16'h0080, 32'd1);
        write32(16'h0084, 32'd20);
        write32(16'h008c, 32'd1);
        repeat (12) @(posedge clk);
        if (running || !fault || move_count != first_restart_moves + 2)
            $fatal(1, "invalid MOVE fault mismatch running=%b fault=%b moves=%0d",
                   running, fault, move_count);

        $display("ASTRAEA COPPER PASS moves=%0d irqs=%0d", move_count, irq_count);
        $finish;
    end
endmodule
