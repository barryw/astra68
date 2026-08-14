// Directed contract tests for the Astra dual-bank copper.
`timescale 1ns/1ps
`default_nettype none

module tb_astra_copper;
    localparam [2:0] OP_END      = 3'd0;
    localparam [2:0] OP_MOVE     = 3'd1;
    localparam [2:0] OP_WAIT     = 3'd2;
    localparam [2:0] OP_SKIP     = 3'd3;
    localparam [2:0] OP_IRQ      = 3'd4;
    localparam [2:0] OP_JUMP     = 3'd5;
    localparam [2:0] OP_DISPATCH = 3'd6;

    reg clk = 1'b0;
    always #3 clk = ~clk;

    reg reset = 1'b1;
    reg program_write = 1'b0;
    reg program_read = 1'b0;
    reg [12:0] program_word_address = 13'd0;
    reg [31:0] program_write_data = 32'd0;
    reg [3:0] program_write_strobe = 4'd0;
    wire program_write_ready;
    wire [31:0] program_read_data;
    wire program_read_valid;
    reg validate_start = 1'b0;
    reg [11:0] validate_first = 12'd0;
    reg [12:0] validate_count = 13'd0;
wire [15:0] validate_move_target;
wire [31:0] validate_move_data;
    wire validate_move_allowed = validate_move_target == 16'h0100 ||
        validate_move_target == 16'h0104;
    wire [15:0] validate_dispatch_id;
    wire validate_dispatch_allowed = validate_dispatch_id == 16'h0055;
    wire validate_busy;
    wire validate_done;
    wire validate_valid;
    wire [7:0] validate_fault;
    wire [11:0] validate_fault_index;
    reg promote_request = 1'b0;
    reg frame_boundary = 1'b0;
    reg frame_start = 1'b0;
    wire promotion_pending;
    wire promoted;
    wire baseline_restore;
    wire active_bank;
    reg enable = 1'b1;
    reg [10:0] beam_x = 11'd0;
    reg [9:0] beam_y = 10'd0;
    wire move_valid;
    reg move_ready = 1'b1;
    wire [15:0] move_target;
    wire [31:0] move_data;
    wire [10:0] move_beam_x;
    wire [9:0] move_beam_y;
    reg runtime_move_permission = 1'b1;
    wire move_allowed = runtime_move_permission &&
        (move_target == 16'h0100 || move_target == 16'h0104);
    reg [1:0] move_timing_class = 2'd2;
    wire [1:0] move_class;
    wire dispatch_valid;
    reg dispatch_ready = 1'b1;
    wire [15:0] dispatch_id;
    wire dispatch_allowed = dispatch_id == 16'h0055;
    wire irq_event;
    reg irq_ready = 1'b1;
wire [15:0] irq_sources;
wire [10:0] irq_beam_x;
wire [9:0] irq_beam_y;
    wire running;
    wire waiting;
    reg fault_clear = 1'b0;
    wire fault;
    wire [7:0] fault_code;
    wire [11:0] fault_pc;
    wire [11:0] pc;
    wire [31:0] instructions_retired;

    integer move_count = 0;
    integer dispatch_count = 0;
    integer irq_count = 0;
    reg [15:0] last_irq = 16'd0;
    reg [10:0] last_irq_x = 11'd0;
    reg [9:0] last_irq_y = 10'd0;

    astra_copper #(
        .TOTAL_WIDTH(1650),
        .TOTAL_HEIGHT(750),
        .MAX_INSTRUCTIONS_PER_FRAME(8)
    ) dut (
        .clk(clk),
        .reset(reset),
        .program_write(program_write),
        .program_read(program_read),
        .program_word_address(program_word_address),
        .program_write_data(program_write_data),
        .program_write_strobe(program_write_strobe),
        .program_write_ready(program_write_ready),
        .program_read_data(program_read_data),
        .program_read_valid(program_read_valid),
        .validate_start(validate_start),
        .validate_first(validate_first),
        .validate_count(validate_count),
.validate_move_target(validate_move_target),
.validate_move_data(validate_move_data),
        .validate_move_allowed(validate_move_allowed),
        .validate_dispatch_id(validate_dispatch_id),
        .validate_dispatch_allowed(validate_dispatch_allowed),
        .validate_busy(validate_busy),
        .validate_done(validate_done),
        .validate_valid(validate_valid),
        .validate_fault(validate_fault),
        .validate_fault_index(validate_fault_index),
        .promote_request(promote_request),
        .frame_boundary(frame_boundary),
        .frame_start(frame_start),
        .promotion_pending(promotion_pending),
        .promoted(promoted),
        .baseline_restore(baseline_restore),
        .active_bank(active_bank),
        .enable(enable),
        .beam_x(beam_x),
        .beam_y(beam_y),
        .move_valid(move_valid),
        .move_ready(move_ready),
        .move_target(move_target),
        .move_data(move_data),
        .move_beam_x(move_beam_x),
        .move_beam_y(move_beam_y),
        .move_allowed(move_allowed),
        .move_timing_class(move_timing_class),
        .move_class(move_class),
        .dispatch_valid(dispatch_valid),
        .dispatch_ready(dispatch_ready),
        .dispatch_id(dispatch_id),
        .dispatch_allowed(dispatch_allowed),
        .irq_event(irq_event),
        .irq_ready(irq_ready),
        .irq_sources(irq_sources),
        .irq_beam_x(irq_beam_x),
        .irq_beam_y(irq_beam_y),
        .running(running),
        .waiting(waiting),
        .fault_clear(fault_clear),
        .fault(fault),
        .fault_code(fault_code),
        .fault_pc(fault_pc),
        .pc(pc),
        .instructions_retired(instructions_retired)
    );

    always @(posedge clk) begin
        if (!reset && move_valid && move_ready)
            move_count <= move_count + 1;
        if (!reset && dispatch_valid && dispatch_ready)
            dispatch_count <= dispatch_count + 1;
        if (!reset && irq_event && irq_ready) begin
            irq_count <= irq_count + 1;
            last_irq <= irq_sources;
            last_irq_x <= irq_beam_x;
            last_irq_y <= irq_beam_y;
        end
    end

    function automatic [31:0] ins0(input [2:0] op, input [15:0] arg);
        ins0 = {op, 13'd0, arg};
    endfunction

    function automatic [31:0] beam0(input [2:0] op, input [9:0] y);
        beam0 = {op, 19'd0, y};
    endfunction

    task automatic write_word(
        input [12:0] address,
        input [31:0] value,
        input [3:0] strobes
    );
        begin
            @(negedge clk);
            while (!program_write_ready)
                @(negedge clk);
            program_word_address = address;
            program_write_data = value;
            program_write_strobe = strobes;
            program_write = 1'b1;
            @(negedge clk);
            program_write = 1'b0;
        end
    endtask

    task automatic write_instruction(
        input [11:0] index,
        input [31:0] word0,
        input [31:0] word1
    );
        begin
            write_word({index, 1'b0}, word0, 4'hf);
            write_word({index, 1'b1}, word1, 4'hf);
        end
    endtask

    task automatic expect_read(
        input [12:0] address,
        input [31:0] expected
    );
        integer timeout;
        begin
            @(negedge clk);
            program_word_address = address;
            program_read = 1'b1;
            @(negedge clk);
            program_read = 1'b0;
            timeout = 0;
            while (!program_read_valid && timeout < 8) begin
                @(negedge clk);
                timeout = timeout + 1;
            end
            if (!program_read_valid || program_read_data !== expected)
                $fatal(1, "program read mismatch address=%0d got=%08x valid=%b expected=%08x",
                    address, program_read_data, program_read_valid, expected);
        end
    endtask

    task automatic validate_list(
        input [11:0] first,
        input [12:0] count,
        input expected_valid,
        input [7:0] expected_fault
    );
        integer timeout;
        begin
            @(negedge clk);
            validate_first = first;
            validate_count = count;
            validate_start = 1'b1;
            @(negedge clk);
            validate_start = 1'b0;
            timeout = 0;
            while (!validate_done && timeout < 100000) begin
                @(negedge clk);
                timeout = timeout + 1;
            end
            if (!validate_done)
                $fatal(1, "validator timeout first=%0d count=%0d", first, count);
            if (validate_valid !== expected_valid ||
                validate_fault !== expected_fault)
                $fatal(1, "validator mismatch first=%0d count=%0d valid=%b fault=%0d index=%0d",
                    first, count, validate_valid, validate_fault,
                    validate_fault_index);
        end
    endtask

    task automatic pulse_frame(input request_promotion);
        begin
            @(negedge clk);
            promote_request = request_promotion;
            frame_boundary = 1'b1;
            frame_start = 1'b1;
            @(posedge clk);
            #1;
            if (!baseline_restore)
                $fatal(1, "baseline restore missing at frame boundary");
            if (request_promotion && !promoted)
                $fatal(1, "validated bank was not promoted at boundary");
            @(negedge clk);
            promote_request = 1'b0;
            frame_boundary = 1'b0;
            frame_start = 1'b0;
        end
    endtask

    task automatic wait_for(input integer kind);
        integer timeout;
        begin
            timeout = 0;
            while (timeout < 1000 &&
                   !((kind == 0 && move_valid) ||
                     (kind == 1 && waiting) ||
                     (kind == 2 && dispatch_valid) ||
                     (kind == 3 && !running) ||
                     (kind == 4 && fault) ||
                     (kind == 5 && irq_event))) begin
                @(negedge clk);
                timeout = timeout + 1;
            end
            if (timeout == 1000)
                $fatal(1, "execution timeout kind=%0d pc=%0d state=%0d fault=%b code=%0d w0=%08x w1=%08x active=%b",
                    kind, pc, dut.exec_state, fault, fault_code,
                    dut.execute_w0_q, dut.execute_w1_q, active_bank);
        end
    endtask

    initial begin
        repeat (5) @(negedge clk);
        reset = 1'b0;

        // An unvalidated bank cannot be requested for promotion.
        @(negedge clk);
        promote_request = 1'b1;
        @(negedge clk);
        promote_request = 1'b0;
        if (!fault || fault_code != 8'd6)
            $fatal(1, "unvalidated promotion was not rejected");
        fault_clear = 1'b1;
        @(negedge clk);
        fault_clear = 1'b0;
        if (fault)
            $fatal(1, "sticky fault did not clear");

        // Software sees only inactive bank 1. Verify byte strobes and read timing.
        write_word(13'd14, 32'h11223344, 4'hf);
        write_word(13'd14, 32'haa00cc00, 4'b1010);
        expect_read(13'd14, 32'haa22cc44);

        validate_list(12'd0, 13'd0, 1'b0, 8'd1);
        write_instruction(12'd0, {3'd7, 29'd0}, 32'd0);
        validate_list(12'd0, 13'd1, 1'b0, 8'd2);
        write_instruction(12'd0, ins0(OP_MOVE, 16'h0200), 32'h1);
        write_instruction(12'd1, ins0(OP_END, 16'd0), 32'd0);
        validate_list(12'd0, 13'd2, 1'b0, 8'd4);
        write_instruction(12'd0, ins0(OP_IRQ, 16'h1), 32'd0);
        validate_list(12'd0, 13'd1, 1'b0, 8'd5);
        write_instruction(12'd0, ins0(OP_DISPATCH, 16'h9999), 32'd0);
        write_instruction(12'd1, ins0(OP_END, 16'd0), 32'd0);
        validate_list(12'd0, 13'd2, 1'b0, 8'd4);
        write_instruction(12'd0, beam0(OP_WAIT, 10'd750), 32'd0);
        write_instruction(12'd1, ins0(OP_END, 16'd0), 32'd0);
        validate_list(12'd0, 13'd2, 1'b0, 8'd1);

        // Valid bank 1: MOVE, WAIT, true SKIP, dispatch, IRQ, END.
        write_instruction(12'd10, ins0(OP_MOVE, 16'h0100), 32'hcafef00d);
        write_instruction(12'd11, beam0(OP_WAIT, 10'd2), 32'd5);
        write_instruction(12'd12, beam0(OP_SKIP, 10'd2), 32'd5);
        write_instruction(12'd13, ins0(OP_IRQ, 16'hdead), 32'd0);
        write_instruction(12'd14, ins0(OP_DISPATCH, 16'h0055), 32'd0);
        write_instruction(12'd15, ins0(OP_IRQ, 16'h1234), 32'd0);
        write_instruction(12'd16, ins0(OP_END, 16'd0), 32'd0);
        validate_list(12'd10, 13'd7, 1'b1, 8'd0);

        move_ready = 1'b0;
        dispatch_ready = 1'b0;
        pulse_frame(1'b1);
        if (!active_bank || pc != 12'd10)
            $fatal(1, "simultaneous promotion/start selected wrong list");
        wait_for(0);
        repeat (3) begin
            @(negedge clk);
            if (!move_valid || move_target != 16'h0100 ||
                move_data != 32'hcafef00d || move_class != 2'd2 ||
                move_beam_x != 11'd0 || move_beam_y != 10'd0)
                $fatal(1, "MOVE changed while backpressured");
        end
        move_ready = 1'b1;
        wait_for(1);
        beam_y = 10'd2;
        beam_x = 11'd5;
        wait_for(2);
        if (dispatch_id != 16'h0055)
            $fatal(1, "dispatch ID mismatch");
        repeat (2) @(negedge clk);
        if (!dispatch_valid)
            $fatal(1, "dispatch did not hold under backpressure");
        irq_ready = 1'b0;
        dispatch_ready = 1'b1;
        wait_for(5);
        repeat (3) begin
            @(negedge clk);
            if (!irq_event || irq_sources != 16'h1234 ||
                irq_beam_x != 11'd5 || irq_beam_y != 10'd2)
                $fatal(1, "IRQ changed while backpressured");
        end
        if (irq_count != 0)
            $fatal(1, "backpressured IRQ retired early");
        irq_ready = 1'b1;
        wait_for(3);
        if (move_count != 1 || dispatch_count != 1 || irq_count != 1 ||
            last_irq != 16'h1234 || last_irq_x != 11'd5 ||
            last_irq_y != 10'd2)
            $fatal(1, "list effects mismatch move=%0d dispatch=%0d irq=%0d source=%04x",
                move_count, dispatch_count, irq_count, last_irq);

        // Active bank remained immutable: software now edits and reads bank 0.
        write_instruction(12'd20, ins0(OP_JUMP, 16'd22), 32'd0);
        write_instruction(12'd21, ins0(OP_IRQ, 16'hbad0), 32'd0);
        write_instruction(12'd22, ins0(OP_IRQ, 16'h600d), 32'd0);
        write_instruction(12'd23, ins0(OP_END, 16'd0), 32'd0);
        expect_read(13'd40, ins0(OP_JUMP, 16'd22));
        validate_list(12'd20, 13'd4, 1'b1, 8'd0);
        beam_x = 11'd0;
        beam_y = 10'd0;
        pulse_frame(1'b1);
        wait_for(3);
        if (active_bank || irq_count != 2 || last_irq != 16'h600d)
            $fatal(1, "JUMP or second-bank promotion mismatch");

        // A self-loop is structurally valid but bounded at runtime.
        write_instruction(12'd30, ins0(OP_JUMP, 16'd30), 32'd0);
        write_instruction(12'd31, ins0(OP_END, 16'd0), 32'd0);
        validate_list(12'd30, 13'd2, 1'b1, 8'd0);
        pulse_frame(1'b1);
        wait_for(4);
        if (fault_code != 8'd7 || fault_pc != 12'd30)
            $fatal(1, "runaway fault mismatch code=%0d pc=%0d",
                fault_code, fault_pc);
        pulse_frame(1'b0);
        if (!fault)
            $fatal(1, "execution fault was not sticky across frames");
        fault_clear = 1'b1;
        @(negedge clk);
        fault_clear = 1'b0;
        if (fault)
            $fatal(1, "execution fault clear failed");

        // A baseline-dependent permission may change after list validation.
        write_instruction(12'd40, ins0(OP_MOVE, 16'h0100), 32'h12345678);
        write_instruction(12'd41, ins0(OP_END, 16'd0), 32'd0);
        validate_list(12'd40, 13'd2, 1'b1, 8'd0);
        runtime_move_permission = 1'b0;
        pulse_frame(1'b1);
        wait_for(4);
        if (fault_code != 8'd4 || fault_pc != 12'd40)
            $fatal(1, "runtime MOVE permission fault mismatch code=%0d pc=%0d",
                fault_code, fault_pc);

        $display("ASTRA COPPER PASS retired=%0d moves=%0d dispatch=%0d irq=%0d",
            instructions_retired, move_count, dispatch_count, irq_count);
        $finish;
    end
endmodule

`default_nettype wire
