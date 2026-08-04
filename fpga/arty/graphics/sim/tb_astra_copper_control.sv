`timescale 1ns/1ps
`default_nettype none

module tb_astra_copper_control;
    localparam [2:0] OP_END = 3'd0;
    localparam [2:0] OP_MOVE = 3'd1;
    localparam [2:0] OP_IRQ = 3'd4;
    localparam [2:0] OP_DISPATCH = 3'd6;

    reg clk = 0;
    reg reset = 1;
    always #3 clk = ~clk;
    reg frame_boundary = 0;
    reg frame_start = 0;
    reg [10:0] beam_x = 0;
    reg [9:0] beam_y = 0;

    wire move_valid;
    reg move_ready = 1;
    wire [15:0] move_target;
    wire [31:0] move_data;
    wire [10:0] move_beam_x;
    wire [9:0] move_beam_y;
    wire move_allowed = move_target == 16'h0018;
    wire [1:0] move_timing_class = 2'd0;
    wire [1:0] move_class;
    wire [15:0] validate_move_target;
    wire [31:0] validate_move_data;
    wire validate_move_allowed = validate_move_target == 16'h0018;
    wire dispatch_valid;
    reg dispatch_ready = 0;
    wire [15:0] dispatch_id;
    wire [10:0] dispatch_submission_producer;
    reg dispatch_allowed = 1;
    wire [15:0] validate_dispatch_id;
    reg validate_dispatch_allowed = 1;
    wire irq_event;
    reg irq_ready = 1;
    wire irq_delivered = irq_event && irq_ready;
    wire [15:0] irq_sources;
    wire [10:0] irq_beam_x;
    wire [9:0] irq_beam_y;
    wire interrupt;
    wire baseline_restore;
    wire enabled;
    wire running;
    wire waiting;
    wire faulted;
    integer dispatch_handshakes = 0;

    always @(posedge clk) begin
        if (!reset && dispatch_valid && dispatch_ready)
            dispatch_handshakes <= dispatch_handshakes + 1;
    end

    reg [31:0] s_axi_awaddr = 0;
    reg [2:0] s_axi_awprot = 0;
    reg s_axi_awvalid = 0;
    wire s_axi_awready;
    reg [31:0] s_axi_wdata = 0;
    reg [3:0] s_axi_wstrb = 0;
    reg s_axi_wvalid = 0;
    wire s_axi_wready;
    wire [1:0] s_axi_bresp;
    wire s_axi_bvalid;
    reg s_axi_bready = 0;
    reg [31:0] s_axi_araddr = 0;
    reg [2:0] s_axi_arprot = 0;
    reg s_axi_arvalid = 0;
    wire s_axi_arready;
    wire [31:0] s_axi_rdata;
    wire [1:0] s_axi_rresp;
    wire s_axi_rvalid;
    reg s_axi_rready = 0;

    astra_copper_control dut (.*);

    function automatic [31:0] ins0(input [2:0] op, input [15:0] arg);
        ins0 = {op, 13'd0, arg};
    endfunction

    task automatic axi_write(input [31:0] address, input [31:0] data);
        begin
            @(negedge clk);
            s_axi_awaddr = address;
            s_axi_awvalid = 1;
            while (!s_axi_awready) @(negedge clk);
            @(negedge clk);
            s_axi_awvalid = 0;
            s_axi_wdata = data;
            s_axi_wstrb = 4'hf;
            s_axi_wvalid = 1;
            while (!s_axi_wready) @(negedge clk);
            @(negedge clk);
            s_axi_wvalid = 0;
            while (!s_axi_bvalid) @(negedge clk);
            if (s_axi_bresp != 0)
                $fatal(1, "AXI write failed address=%08x response=%b",
                       address, s_axi_bresp);
            s_axi_bready = 1;
            @(negedge clk);
            s_axi_bready = 0;
        end
    endtask

    task automatic axi_write_rejected(
        input [31:0] address,
        input [31:0] data
    );
        begin
            @(negedge clk);
            s_axi_awaddr = address;
            s_axi_awvalid = 1;
            while (!s_axi_awready) @(negedge clk);
            @(negedge clk);
            s_axi_awvalid = 0;
            s_axi_wdata = data;
            s_axi_wstrb = 4'hf;
            s_axi_wvalid = 1;
            while (!s_axi_wready) @(negedge clk);
            @(negedge clk);
            s_axi_wvalid = 0;
            while (!s_axi_bvalid) @(negedge clk);
            if (s_axi_bresp == 0)
                $fatal(1, "AXI write unexpectedly accepted address=%08x",
                       address);
            s_axi_bready = 1;
            @(negedge clk);
            s_axi_bready = 0;
        end
    endtask

    task automatic axi_read(input [31:0] address, output [31:0] data);
        begin
            @(negedge clk);
            s_axi_araddr = address;
            s_axi_arvalid = 1;
            while (!s_axi_arready) @(negedge clk);
            @(negedge clk);
            s_axi_arvalid = 0;
            while (!s_axi_rvalid) @(negedge clk);
            if (s_axi_rresp != 0)
                $fatal(1, "AXI read failed address=%08x response=%b",
                       address, s_axi_rresp);
            data = s_axi_rdata;
            s_axi_rready = 1;
            @(negedge clk);
            s_axi_rready = 0;
        end
    endtask

    task automatic write_instruction(
        input [11:0] index,
        input [31:0] word0,
        input [31:0] word1
    );
        begin
            axi_write(32'h8000 + index * 8, word0);
            axi_write(32'h8004 + index * 8, word1);
        end
    endtask

    reg [31:0] value;
    integer timeout;
    initial begin
        fork
            begin
                repeat (10000) @(posedge clk);
                $fatal(1, "copper control global timeout");
            end
        join_none
        repeat (5) @(posedge clk);
        reset = 0;
        axi_read(32'h4000, value);
        if (value != 32'h434f5052)
            $fatal(1, "copper ID mismatch %08x", value);

        axi_write(32'h4030, 32'd3);
        axi_write(32'h4034, 32'h80000002);
        axi_read(32'h4030, value);
        if (value != 32'd3)
            $fatal(1, "dispatch selector readback mismatch %08x", value);
        axi_read(32'h4034, value);
        if (value != 32'h80000002)
            $fatal(1, "dispatch endpoint readback mismatch %08x", value);

        write_instruction(0, ins0(OP_DISPATCH, 16'h0004), 32'd0);
        write_instruction(1, ins0(OP_END, 16'd0), 32'd0);
        axi_write(32'h4010, (32'd2 << 16));
        axi_write(32'h4014, 32'd1);
        timeout = 0;
        value = 0;
        while (value[15:8] == 8'd0 && timeout < 100) begin
            axi_read(32'h4018, value);
            timeout = timeout + 1;
        end
        if (value[15:8] != 8'd4 || value[2])
            $fatal(1, "unregistered dispatch ID validated status=%08x",
                   value);

        write_instruction(0, ins0(OP_MOVE, 16'h0018), 32'h00123456);
        write_instruction(1, ins0(OP_DISPATCH, 16'h0003), 32'd0);
        write_instruction(2, ins0(OP_IRQ, 16'h0042), 32'd0);
        write_instruction(3, ins0(OP_END, 16'd0), 32'd0);
        axi_read(32'h8000, value);
        if (value != ins0(OP_MOVE, 16'h0018))
            $fatal(1, "inactive-bank readback mismatch %08x", value);

        axi_write(32'h4010, (32'd4 << 16));
        axi_write(32'h4014, 32'd1);
        timeout = 0;
        value = 0;
        while (!value[4] && timeout < 100) begin
            axi_read(32'h400c, value);
            timeout = timeout + 1;
        end
        if (!value[4])
            $fatal(1, "validated bank did not become valid status=%08x", value);

        axi_write(32'h4008, 32'd3);
        axi_write_rejected(32'h4030, 32'd4);
        axi_write_rejected(32'h4034, 32'h80000003);
        @(negedge clk);
        frame_boundary = 1;
        frame_start = 1;
        @(posedge clk);
        #1;
        if (!baseline_restore)
            $fatal(1, "baseline restore missing");
        @(negedge clk);
        frame_boundary = 0;
        frame_start = 0;

        timeout = 0;
        while (!move_valid && timeout < 100) begin
            @(negedge clk);
            timeout = timeout + 1;
        end
        if (!move_valid || move_target != 16'h0018 ||
            move_data != 32'h00123456 || move_class != 0)
            $fatal(1, "MOVE mismatch valid=%b target=%04x data=%08x class=%0d",
                   move_valid, move_target, move_data, move_class);
        timeout = 0;
        while (!dispatch_valid && timeout < 100) begin
            @(negedge clk);
            timeout = timeout + 1;
        end
        if (!dispatch_valid || dispatch_id != 16'h0003 ||
            dispatch_submission_producer != 11'd2)
            $fatal(1, "DISPATCH mismatch valid=%b id=%04x producer=%0d",
                   dispatch_valid, dispatch_id,
                   dispatch_submission_producer);
        repeat (3) begin
            @(negedge clk);
            if (!dispatch_valid || dispatch_id != 16'h0003 ||
                dispatch_submission_producer != 11'd2)
                $fatal(1, "staged DISPATCH changed under backpressure");
        end
        dispatch_ready = 1;
        @(negedge clk);
        dispatch_ready = 0;
        timeout = 0;
        while (!irq_event && timeout < 100) begin
            @(negedge clk);
            timeout = timeout + 1;
        end
        if (!irq_event || irq_sources != 16'h0042)
            $fatal(1, "IRQ mismatch event=%b sources=%04x",
                   irq_event, irq_sources);
        axi_read(32'h4028, value);
        if (!value[0] || !interrupt)
            $fatal(1, "IRQ was not retained status=%08x", value);
        axi_write(32'h4028, 32'd1);
        if (interrupt)
            $fatal(1, "IRQ clear failed");

        timeout = 0;
        while (running && timeout < 100) begin
            @(negedge clk);
            timeout = timeout + 1;
        end
        if (running || dispatch_handshakes != 1)
            $fatal(1, "DISPATCH was not completed exactly once count=%0d",
                   dispatch_handshakes);

        // A runtime renderer rejection is returned to the core as a
        // registered failed completion.  It must fault without publishing
        // the producer endpoint or hanging in EXEC_DISPATCH.
        dispatch_allowed = 0;
        @(negedge clk);
        frame_boundary = 1;
        frame_start = 1;
        @(negedge clk);
        frame_boundary = 0;
        frame_start = 0;
        timeout = 0;
        while (!faulted && timeout < 100) begin
            @(negedge clk);
            timeout = timeout + 1;
        end
        if (!faulted || dispatch_handshakes != 1)
            $fatal(1, "rejected DISPATCH did not fault exactly once");
        axi_read(32'h401c, value);
        if (!value[0] || value[15:8] != 8'd4)
            $fatal(1, "rejected DISPATCH fault mismatch status=%08x",
                   value);
        $display("ASTRA COPPER CONTROL PASS");
        $finish;
    end
endmodule

`default_nettype wire
