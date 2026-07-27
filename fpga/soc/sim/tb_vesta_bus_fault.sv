`timescale 1ns/1ps
`default_nettype none

module tb_vesta_bus_fault;
    reg clk = 1'b0;
    reg rst = 1'b1;
    reg select = 1'b0;
    reg [2:0] reg_index = 3'd0;
    reg write_strobe = 1'b0;
    reg [31:0] write_data = 32'd0;
    reg [3:0] byte_enable = 4'd0;
    wire [31:0] read_data;
    reg fault_strobe = 1'b0;
    reg [31:0] fault_status = 32'd0;
    reg [31:0] fault_address = 32'd0;
    reg [31:0] fault_target = 32'd0;
    reg [63:0] fault_cycles = 64'd0;
    wire fault_valid;
    reg cycle_active = 1'b0;
    reg cycle_complete = 1'b0;
    wire timeout_active;

    localparam [2:0] REG_STATUS = 3'd0;
    localparam [2:0] REG_ADDRESS = 3'd1;
    localparam [2:0] REG_TARGET = 3'd2;
    localparam [2:0] REG_CYCLES_LO = 3'd3;
    localparam [2:0] REG_CYCLES_HI = 3'd4;
    localparam [2:0] REG_LOST = 3'd5;
    localparam [2:0] REG_TIMEOUT = 3'd6;
    localparam [2:0] REG_ACK = 3'd7;

    always #5 clk = ~clk;

    vesta_bus_fault #(.TIMEOUT_CYCLES(4)) dut (
        .clk(clk), .rst(rst), .select(select), .reg_index(reg_index),
        .write_strobe(write_strobe), .write_data(write_data),
        .byte_enable(byte_enable), .read_data(read_data),
        .fault_strobe(fault_strobe), .fault_status(fault_status),
        .fault_address(fault_address), .fault_target(fault_target),
        .fault_cycles(fault_cycles), .fault_valid(fault_valid),
        .cycle_active(cycle_active), .cycle_complete(cycle_complete),
        .timeout_active(timeout_active)
    );

    task automatic expect_reg(input [2:0] index, input [31:0] expected);
        begin
            select = 1'b1;
            reg_index = index;
            #1;
            if (read_data !== expected)
                $fatal(1, "reg %0d expected %08x got %08x",
                       index, expected, read_data);
            select = 1'b0;
        end
    endtask

    task automatic inject_fault(
        input [31:0] status,
        input [31:0] address,
        input [31:0] target,
        input [63:0] cycles
    );
        begin
            @(negedge clk);
            fault_status = status;
            fault_address = address;
            fault_target = target;
            fault_cycles = cycles;
            fault_strobe = 1'b1;
            @(posedge clk);
            #1;
            fault_strobe = 1'b0;
        end
    endtask

    task automatic acknowledge(input [3:0] enables, input [31:0] value);
        begin
            @(negedge clk);
            select = 1'b1;
            reg_index = REG_ACK;
            write_data = value;
            byte_enable = enables;
            write_strobe = 1'b1;
            @(posedge clk);
            #1;
            select = 1'b0;
            byte_enable = 4'd0;
            write_strobe = 1'b0;
        end
    endtask

    initial begin
        repeat (3) @(posedge clk);
        rst = 1'b0;
        @(posedge clk);
        #1;
        if (fault_valid !== 1'b0)
            $fatal(1, "fault record valid after reset");
        expect_reg(REG_STATUS, 32'd0);
        expect_reg(REG_LOST, 32'd0);
        expect_reg(REG_TIMEOUT, 32'd4);

        // The fourth uncompleted active clock expires the cycle. The result
        // remains asserted until AS-equivalent cycle_active is released.
        @(negedge clk);
        cycle_active = 1'b1;
        repeat (3) begin
            @(posedge clk);
            #1;
            if (timeout_active !== 1'b0)
                $fatal(1, "bus timeout asserted before deadline");
        end
        @(posedge clk);
        #1;
        if (timeout_active !== 1'b1)
            $fatal(1, "bus timeout did not assert at deadline");
        cycle_complete = 1'b1;
        @(posedge clk);
        #1;
        if (timeout_active !== 1'b1)
            $fatal(1, "late completion cleared active timeout");
        @(negedge clk);
        cycle_active = 1'b0;
        cycle_complete = 1'b0;
        @(posedge clk);
        #1;
        if (timeout_active !== 1'b0)
            $fatal(1, "bus timeout did not clear after cycle release");

        // Completion on the final permitted edge wins and suppresses the
        // watchdog for the rest of that active cycle.
        @(negedge clk);
        cycle_active = 1'b1;
        repeat (3) @(posedge clk);
        @(negedge clk);
        cycle_complete = 1'b1;
        @(posedge clk);
        #1;
        if (timeout_active !== 1'b0)
            $fatal(1, "completion at deadline lost to timeout");
        @(negedge clk);
        cycle_complete = 1'b0;
        repeat (6) @(posedge clk);
        #1;
        if (timeout_active !== 1'b0)
            $fatal(1, "completed cycle restarted watchdog before release");
        @(negedge clk);
        cycle_active = 1'b0;
        @(posedge clk);

        // The recorder supplies VALID and preserves one coherent snapshot.
        inject_fault(32'h00000532, 32'hdeadbeef, 32'd3,
                     64'h0123456789abcdef);
        if (fault_valid !== 1'b1)
            $fatal(1, "first fault did not become valid");
        expect_reg(REG_STATUS, 32'h00000533);
        expect_reg(REG_ADDRESS, 32'hdeadbeef);
        expect_reg(REG_TARGET, 32'd3);
        expect_reg(REG_CYCLES_LO, 32'h89abcdef);
        expect_reg(REG_CYCLES_HI, 32'h01234567);

        // Later faults cannot overwrite the first; LOST is saturating.
        inject_fault(32'h00000104, 32'h11111111, 32'd7, 64'd9);
        inject_fault(32'h00000104, 32'h22222222, 32'd8, 64'd10);
        expect_reg(REG_ADDRESS, 32'hdeadbeef);
        expect_reg(REG_TARGET, 32'd3);
        expect_reg(REG_LOST, 32'd2);
        @(negedge clk);
        dut.lost_record = 32'hffffffff;
        inject_fault(32'h00000104, 32'h33333333, 32'd9, 64'd11);
        expect_reg(REG_LOST, 32'hffffffff);

        // ACK is low-byte RW1C. Wrong lanes and a zero bit are inert.
        acknowledge(4'b1000, 32'd1);
        expect_reg(REG_STATUS, 32'h00000533);
        acknowledge(4'b0001, 32'd0);
        expect_reg(REG_STATUS, 32'h00000533);
        acknowledge(4'b0001, 32'd1);
        expect_reg(REG_STATUS, 32'h00000532);
        expect_reg(REG_LOST, 32'd0);

        // If an ACK and a new fault meet, the new fault wins atomically.
        @(negedge clk);
        select = 1'b1;
        reg_index = REG_ACK;
        write_data = 32'd1;
        byte_enable = 4'b0001;
        write_strobe = 1'b1;
        fault_status = 32'h00000308;
        fault_address = 32'hfff40020;
        fault_target = 32'd3;
        fault_cycles = 64'hfeedface00000042;
        fault_strobe = 1'b1;
        @(posedge clk);
        #1;
        select = 1'b0;
        write_strobe = 1'b0;
        fault_strobe = 1'b0;
        byte_enable = 4'd0;
        expect_reg(REG_STATUS, 32'h00000309);
        expect_reg(REG_ADDRESS, 32'hfff40020);
        expect_reg(REG_CYCLES_LO, 32'h00000042);
        expect_reg(REG_CYCLES_HI, 32'hfeedface);
        expect_reg(REG_LOST, 32'd0);

        $display("PASS Vesta sticky physical bus-fault record");
        $finish;
    end
endmodule

`default_nettype wire
