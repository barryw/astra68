`timescale 1ns/1ps
`default_nettype none

module tb_vesta_irq_timer;
    reg clk;
    reg rst = 1'b1;
    reg select = 1'b0;
    reg [7:0] reg_index = 8'd0;
    reg write_strobe = 1'b0;
    reg [31:0] write_data = 32'd0;
    reg [3:0] byte_enable = 4'd0;
    wire [31:0] read_data;
    reg [31:0] source_level = 32'd0;
    wire [2:0] cpu_ipln_n;
    reg iack_strobe = 1'b0;
    reg [2:0] iack_level = 3'd0;
    wire [7:0] iack_vector;
    wire iack_valid;

    localparam [7:0] IRQ_PENDING = 8'h00;
    localparam [7:0] IRQ_ENABLE = 8'h01;
    localparam [7:0] IRQ_SOFT = 8'h02;
    localparam [7:0] IRQ_ACK = 8'h03;
    localparam [7:0] IRQ_CURRENT = 8'h04;
    localparam [7:0] IRQ_CFG = 8'h20;
    localparam [7:0] TIMER0 = 8'h40;
    localparam [7:0] TIMER1 = 8'h44;

    integer test_source;

    initial clk = 1'b0;
    always #5 clk = ~clk;

    vesta_irq_timer dut (
        .clk(clk), .rst(rst), .select(select), .reg_index(reg_index),
        .write_strobe(write_strobe), .write_data(write_data),
        .byte_enable(byte_enable), .read_data(read_data),
        .source_level(source_level), .cpu_ipln_n(cpu_ipln_n),
        .iack_strobe(iack_strobe), .iack_level(iack_level),
        .iack_vector(iack_vector),
        .iack_valid(iack_valid)
    );

    task automatic write_reg(
        input [7:0] index,
        input [31:0] value,
        input [3:0] enables
    );
        begin
            @(negedge clk);
            select = 1'b1;
            reg_index = index;
            write_data = value;
            byte_enable = enables;
            write_strobe = 1'b1;
            @(posedge clk);
            #1;
            write_strobe = 1'b0;
            select = 1'b0;
            byte_enable = 4'd0;
        end
    endtask

    task automatic expect_reg(input [7:0] index, input [31:0] expected);
        begin
            select = 1'b1;
            reg_index = index;
            #1;
            if (read_data !== expected)
                $fatal(1, "reg %02x expected %08x got %08x",
                       index, expected, read_data);
            select = 1'b0;
        end
    endtask

    task automatic expect_ipl(input [2:0] expected);
        begin
            #1;
            if (cpu_ipln_n !== expected)
                $fatal(1, "IPLn expected %03b got %03b",
                       expected, cpu_ipln_n);
        end
    endtask

    task automatic begin_iack(
        input [2:0] level,
        input expected_valid,
        input [7:0] expected_vector
    );
        begin
            @(negedge clk);
            iack_level = level;
            iack_strobe = 1'b1;
            @(posedge clk);
            #1;
            if (iack_valid !== expected_valid ||
                iack_vector !== expected_vector)
                $fatal(1, "IACK level %0d expected valid=%0d vector=%02x got valid=%0d vector=%02x",
                       level, expected_valid, expected_vector,
                       iack_valid, iack_vector);
        end
    endtask

    task automatic expect_iack_held(
        input expected_valid,
        input [7:0] expected_vector
    );
        begin
            #1;
            if (iack_valid !== expected_valid ||
                iack_vector !== expected_vector)
                $fatal(1, "held IACK expected valid=%0d vector=%02x got valid=%0d vector=%02x",
                       expected_valid, expected_vector,
                       iack_valid, iack_vector);
        end
    endtask

    task automatic end_iack;
        begin
            @(negedge clk);
            iack_strobe = 1'b0;
            iack_level = 3'd0;
            @(posedge clk);
            #1;
        end
    endtask

    initial begin
        repeat (3) @(posedge clk);
        rst = 1'b0;
        @(posedge clk);
        #1;
        expect_reg(IRQ_PENDING, 32'd0);
        expect_ipl(3'b111);

        // Level interrupt, vector delivery, level-source ACK behavior, and
        // transaction-stable IACK selection.
        write_reg(IRQ_CFG + 3, 32'h00005503, 4'b1111);
        write_reg(IRQ_CFG + 6, 32'h00005603, 4'b1111);
        write_reg(IRQ_ENABLE, 32'h00000048, 4'b1111);
        source_level[3] = 1'b1;
        #1;
        expect_ipl(3'b111);
        @(posedge clk);
        #1;
        expect_ipl(3'b100);
        expect_reg(IRQ_CURRENT, 32'h80550303);
        write_reg(IRQ_ACK, 32'h00000008, 4'b1111);
        expect_reg(IRQ_PENDING, 32'h00000008);

        begin_iack(3'd3, 1'b1, 8'h55);
        source_level[3] = 1'b0;
        source_level[6] = 1'b1;
        repeat (2) @(posedge clk);
        expect_iack_held(1'b1, 8'h55);
        end_iack();
        begin_iack(3'd3, 1'b1, 8'h56);
        end_iack();

        // A source appearing after a spurious acknowledge starts belongs to
        // the next transaction; it cannot replace vector 24 in flight.
        source_level = 32'd0;
        @(posedge clk);
        begin_iack(3'd3, 1'b0, 8'd24);
        source_level[3] = 1'b1;
        repeat (2) @(posedge clk);
        expect_iack_held(1'b0, 8'd24);
        end_iack();
        begin_iack(3'd3, 1'b1, 8'h55);
        end_iack();
        source_level = 32'd0;
        @(posedge clk);
        #1;
        expect_ipl(3'b111);

        // A higher level wins; equal levels choose the lowest source number.
        write_reg(IRQ_CFG + 7, 32'h00008006, 4'b1111);
        write_reg(IRQ_CFG + 2, 32'h00006606, 4'b1111);
        write_reg(IRQ_ENABLE, 32'h0000008c, 4'b1111);
        source_level[7] = 1'b1;
        source_level[2] = 1'b1;
        @(posedge clk);
        #1;
        expect_reg(IRQ_CURRENT, 32'h80660206);
        begin_iack(3'd6, 1'b1, 8'h66);
        end_iack();
        source_level = 32'd0;
        write_reg(IRQ_ENABLE, 32'd0, 4'b1111);

        // Edge pending survives deassertion. ACK while the input remains high
        // clears the latched event without inventing another edge; only a new
        // low-to-high transition may relatch it. A simultaneous rise still
        // wins over ACK so an event cannot be lost.
        write_reg(IRQ_CFG + 4, 32'h00016002, 4'b1111);
        write_reg(IRQ_ENABLE, 32'h00000010, 4'b1111);
        source_level[4] = 1'b1;
        @(posedge clk);
        #1;
        expect_reg(IRQ_PENDING, 32'h00000010);
        write_reg(IRQ_ACK, 32'h00000010, 4'b1111);
        expect_reg(IRQ_PENDING, 32'd0);
        repeat (2) @(posedge clk);
        #1;
        expect_reg(IRQ_PENDING, 32'd0);

        @(negedge clk);
        source_level[4] = 1'b0;
        @(posedge clk);
        @(negedge clk);
        source_level[4] = 1'b1;
        @(posedge clk);
        #1;
        expect_reg(IRQ_PENDING, 32'h00000010);
        write_reg(IRQ_ACK, 32'h00000010, 4'b1111);
        @(negedge clk);
        source_level[4] = 1'b0;

        @(negedge clk);
        source_level[4] = 1'b1;
        select = 1'b1;
        reg_index = IRQ_ACK;
        write_data = 32'h00000010;
        byte_enable = 4'b1111;
        write_strobe = 1'b1;
        @(posedge clk);
        #1;
        source_level[4] = 1'b0;
        write_strobe = 1'b0;
        select = 1'b0;
        expect_reg(IRQ_PENDING, 32'h00000010);
        write_reg(IRQ_ACK, 32'h00000010, 4'b1111);

        // Software interrupts use the same priority/vector path and ACK clears
        // them. A high-byte-only write must not alter low source enables.
        write_reg(IRQ_CFG + 5, 32'h00007001, 4'b1111);
        write_reg(IRQ_ENABLE, 32'h00000020, 4'b1111);
        write_reg(IRQ_ENABLE, 32'hff000000, 4'b1000);
        expect_reg(IRQ_ENABLE, 32'hff000020);
        write_reg(IRQ_SOFT, 32'h00000020, 4'b1111);
        @(posedge clk);
        #1;
        expect_ipl(3'b110);
        write_reg(IRQ_ACK, 32'h00000020, 4'b1111);
        expect_reg(IRQ_SOFT, 32'd0);
        write_reg(IRQ_ENABLE, 32'd0, 4'b1111);

        // Periodic timer: LOAD clocks per expiration, sticky status, IRQ, and
        // reload. Timer source is level-sensitive until status is acknowledged.
        write_reg(IRQ_CFG + 0, 32'h00008104, 4'b1111);
        write_reg(IRQ_ENABLE, 32'h00000001, 4'b1111);
        write_reg(TIMER0 + 0, 32'd3, 4'b1111);
        write_reg(TIMER0 + 2, 32'h00000007, 4'b1111);
        expect_reg(TIMER0 + 1, 32'd3);
        repeat (2) @(posedge clk);
        #1;
        expect_reg(TIMER0 + 3, 32'd0);
        @(posedge clk);
        #1;
        expect_reg(TIMER0 + 3, 32'd1);
        expect_reg(TIMER0 + 1, 32'd3);
        @(posedge clk);
        #1;
        expect_ipl(3'b011);
        write_reg(TIMER0 + 3, 32'd1, 4'b1111);
        expect_reg(TIMER0 + 3, 32'd0);

        // One-shot timer with divide-by-four prescale. It stops after expiry.
        write_reg(IRQ_CFG + 1, 32'h00008205, 4'b1111);
        write_reg(IRQ_ENABLE, 32'h00000002, 4'b1111);
        write_reg(TIMER1 + 0, 32'd2, 4'b1111);
        write_reg(TIMER1 + 2, 32'h00000025, 4'b1111);
        repeat (3) @(posedge clk);
        #1;
        expect_reg(TIMER1 + 1, 32'd2);
        @(posedge clk);
        #1;
        expect_reg(TIMER1 + 1, 32'd1);
        repeat (4) @(posedge clk);
        #1;
        expect_reg(TIMER1 + 3, 32'd1);
        expect_reg(TIMER1 + 2, 32'h00000024);
        @(posedge clk);
        #1;
        expect_ipl(3'b010);

        // Restarting an already-running timer is atomic: a low-byte CTRL
        // write with ENABLE set reloads VALUE from the current LOAD and resets
        // the prescaler. This is the scheduler's deadline-reprogramming path.
        write_reg(TIMER1 + 3, 32'd1, 4'b1111);
        write_reg(TIMER1 + 0, 32'd8, 4'b1111);
        write_reg(TIMER1 + 2, 32'h00000005, 4'b1111);
        repeat (2) @(posedge clk);
        #1;
        expect_reg(TIMER1 + 1, 32'd6);
        write_reg(TIMER1 + 0, 32'd7, 4'b1111);
        write_reg(TIMER1 + 2, 32'h00000005, 4'b1111);
        expect_reg(TIMER1 + 1, 32'd7);

        // If the old deadline expires on the exact restart edge, the old
        // event remains sticky while the new interval starts from LOAD.
        write_reg(TIMER1 + 2, 32'd0, 4'b1111);
        write_reg(TIMER1 + 3, 32'd1, 4'b1111);
        write_reg(TIMER1 + 0, 32'd3, 4'b1111);
        write_reg(TIMER1 + 2, 32'h00000005, 4'b1111);
        repeat (2) @(posedge clk);
        #1;
        expect_reg(TIMER1 + 1, 32'd1);
        write_reg(TIMER1 + 2, 32'h00000005, 4'b1111);
        expect_reg(TIMER1 + 3, 32'd1);
        expect_reg(TIMER1 + 2, 32'h00000005);
        expect_reg(TIMER1 + 1, 32'd3);
        write_reg(TIMER1 + 3, 32'd1, 4'b1111);

        // CTRL has no defined bits outside the low byte. Such writes must not
        // reset an active prescaler or perturb the deadline.
        write_reg(TIMER1 + 2, 32'd0, 4'b1111);
        write_reg(TIMER1 + 0, 32'd2, 4'b1111);
        write_reg(TIMER1 + 2, 32'h00000025, 4'b1111);
        repeat (2) @(posedge clk);
        #1;
        expect_reg(TIMER1 + 1, 32'd2);
        write_reg(TIMER1 + 2, 32'hff000000, 4'b1000);
        expect_reg(TIMER1 + 1, 32'd2);
        @(posedge clk);
        #1;
        expect_reg(TIMER1 + 1, 32'd1);

        // LOAD=0 has the same bounded behavior as LOAD=1: expiration on the
        // first prescaled tick, never an underflow-length interval.
        write_reg(TIMER1 + 2, 32'd0, 4'b1111);
        write_reg(TIMER1 + 3, 32'd1, 4'b1111);
        write_reg(TIMER1 + 0, 32'd0, 4'b1111);
        write_reg(TIMER1 + 2, 32'h00000005, 4'b1111);
        @(posedge clk);
        #1;
        expect_reg(TIMER1 + 3, 32'd1);
        expect_reg(TIMER1 + 2, 32'h00000004);
        expect_reg(TIMER1 + 1, 32'd0);

        // Expiration is set-dominant over a simultaneous status clear.
        write_reg(IRQ_ENABLE, 32'd0, 4'b1111);
        write_reg(TIMER0 + 2, 32'd0, 4'b1111);
        write_reg(TIMER0 + 3, 32'd1, 4'b1111);
        write_reg(TIMER0 + 0, 32'd1, 4'b1111);
        write_reg(TIMER0 + 2, 32'h00000005, 4'b1111);
        write_reg(TIMER0 + 3, 32'd1, 4'b1111);
        expect_reg(TIMER0 + 3, 32'd1);

        // Exercise every leaf of the balanced lowest-source tree. This also
        // verifies dynamic configuration lookup for all 32 source indices.
        rst = 1'b1;
        repeat (2) @(posedge clk);
        rst = 1'b0;
        @(posedge clk);
        for (test_source = 0; test_source < 32;
             test_source = test_source + 1) begin
            write_reg(IRQ_CFG + test_source[7:0],
                      {16'd0, 8'(32'h90 + test_source), 5'd0, 3'd4},
                      4'b1111);
        end
        write_reg(IRQ_ENABLE, 32'hffffffff, 4'b1111);
        for (test_source = 0; test_source < 32;
             test_source = test_source + 1) begin
            source_level = 32'd1 << test_source;
            @(posedge clk);
            #1;
            expect_ipl(3'b011);
            expect_reg(IRQ_CURRENT,
                {1'b1, 7'd0, 8'(32'h90 + test_source), 3'd0,
                 test_source[4:0], 5'd0, 3'd4});
            begin_iack(3'd4, 1'b1, 8'(32'h90 + test_source));
            end_iack();
        end
        source_level = 32'h81010010;
        @(posedge clk);
        #1;
        expect_reg(IRQ_CURRENT, 32'h80940404);

        $display("PASS Vesta IRQ priority, vectors, edge races, software IRQs, and timers");
        $finish;
    end
endmodule

`default_nettype wire
