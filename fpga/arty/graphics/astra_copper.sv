// Copyright (c) 2026 Astra68 contributors
//
// Dual-bank raster command processor. Software can access only the inactive
// bank. A bounded validator must accept that bank before a frame-boundary
// promotion can make it executable.
`timescale 1ns/1ps
`default_nettype none

module astra_copper #(
    parameter integer TOTAL_WIDTH = 1650,
    parameter integer TOTAL_HEIGHT = 750,
    parameter integer MAX_INSTRUCTIONS_PER_FRAME = 16384
) (
    input  wire        clk,
    input  wire        reset,

    input  wire        program_write,
    input  wire        program_read,
    input  wire [12:0] program_word_address,
    input  wire [31:0] program_write_data,
    input  wire [3:0]  program_write_strobe,
    output wire        program_write_ready,
    output reg  [31:0] program_read_data,
    output reg         program_read_valid,

    input  wire        validate_start,
    input  wire [11:0] validate_first,
    input  wire [12:0] validate_count,
    output wire [15:0] validate_move_target,
    output wire [31:0] validate_move_data,
    input  wire        validate_move_allowed,
    output wire [15:0] validate_dispatch_id,
    input  wire        validate_dispatch_allowed,
    output reg         validate_busy,
    output reg         validate_done,
    output reg         validate_valid,
    output reg  [7:0]  validate_fault,
    output reg  [11:0] validate_fault_index,

    input  wire        promote_request,
    input  wire        frame_boundary,
    input  wire        frame_start,
    output reg         promotion_pending,
    output reg         promoted,
    output reg         baseline_restore,
    output reg         active_bank,

    input  wire        enable,
    input  wire [10:0] beam_x,
    input  wire [9:0]  beam_y,

    output reg         move_valid,
    input  wire        move_ready,
    output wire [15:0] move_target,
    output wire [31:0] move_data,
    output wire [10:0] move_beam_x,
    output wire [9:0]  move_beam_y,
    input  wire        move_allowed,
    input  wire [1:0]  move_timing_class,
    output reg  [1:0]  move_class,

    output reg         dispatch_valid,
    input  wire        dispatch_ready,
    output wire [15:0] dispatch_id,
    input  wire        dispatch_allowed,

    output reg         irq_event,
    input  wire        irq_ready,
    output reg  [15:0] irq_sources,
    output wire [10:0] irq_beam_x,
    output wire [9:0]  irq_beam_y,
    output wire        running,
    output wire        waiting,
    input  wire        fault_clear,
    output reg         fault,
    output reg  [7:0]  fault_code,
    output reg  [11:0] fault_pc,
    output reg  [11:0] pc,
    output reg  [31:0] instructions_retired
);
    localparam [2:0] OP_END      = 3'd0;
    localparam [2:0] OP_MOVE     = 3'd1;
    localparam [2:0] OP_WAIT     = 3'd2;
    localparam [2:0] OP_SKIP     = 3'd3;
    localparam [2:0] OP_IRQ      = 3'd4;
    localparam [2:0] OP_JUMP     = 3'd5;
    localparam [2:0] OP_DISPATCH = 3'd6;

    localparam [7:0] FAULT_NONE          = 8'd0;
    localparam [7:0] FAULT_BAD_RANGE     = 8'd1;
    localparam [7:0] FAULT_BAD_OPCODE    = 8'd2;
    localparam [7:0] FAULT_BAD_ENCODING  = 8'd3;
    localparam [7:0] FAULT_BAD_TARGET    = 8'd4;
    localparam [7:0] FAULT_MISSING_END   = 8'd5;
    localparam [7:0] FAULT_NOT_VALIDATED = 8'd6;
    localparam [7:0] FAULT_RUNAWAY       = 8'd7;

    localparam [3:0] EXEC_STOPPED  = 4'd0;
    localparam [3:0] EXEC_FETCH    = 4'd1;
    localparam [3:0] EXEC_CAPTURE  = 4'd2;
    localparam [3:0] EXEC_EVAL     = 4'd3;
    localparam [3:0] EXEC_MOVE     = 4'd4;
    localparam [3:0] EXEC_DISPATCH = 4'd5;
    localparam [3:0] EXEC_IRQ      = 4'd6;
    localparam [3:0] EXEC_RETIRE   = 4'd7;
localparam [3:0] EXEC_SELECT   = 4'd8;
localparam [3:0] EXEC_RANGE    = 4'd9;
localparam [3:0] EXEC_WAIT     = 4'd10;
localparam [3:0] EXEC_SKIP     = 4'd11;
localparam [3:0] EXEC_MOVE_COMMIT = 4'd12;

    localparam [1:0] RETIRE_BEAM_NONE    = 2'd0;
    localparam [1:0] RETIRE_BEAM_TARGET  = 2'd1;
    localparam [1:0] RETIRE_BEAM_ADVANCE = 2'd2;

    localparam [1:0] RETIRE_PC_PLUS_ONE = 2'd0;
    localparam [1:0] RETIRE_PC_PLUS_TWO = 2'd1;
    localparam [1:0] RETIRE_PC_TARGET   = 2'd2;

    localparam [3:0] VALID_IDLE        = 4'd0;
    localparam [3:0] VALID_FETCH       = 4'd1;
    localparam [3:0] VALID_CAPTURE     = 4'd2;
    localparam [3:0] VALID_PERMIT      = 4'd3;
    localparam [3:0] VALID_CHECK       = 4'd4;
    localparam [3:0] VALID_APPLY       = 4'd5;
    localparam [3:0] VALID_CAPTURE_PIPE = 4'd6;
    localparam [3:0] VALID_DECIDE      = 4'd7;
    localparam [3:0] VALID_RANGE       = 4'd8;
    localparam [3:0] VALID_PERMIT_PIPE = 4'd9;

    localparam [1:0] VALID_ACTION_FAIL    = 2'd0;
    localparam [1:0] VALID_ACTION_ADVANCE = 2'd1;
    localparam [1:0] VALID_ACTION_FINISH  = 2'd2;

    function automatic beam_reached(
        input [9:0] target_y,
        input [10:0] target_x,
        input [9:0] current_y,
        input [10:0] current_x
    );
        begin
            beam_reached = current_y > target_y ||
                (current_y == target_y && current_x >= target_x);
        end
    endfunction

    // Split each word into byte-wide memories. This expresses byte writes as
    // independent RAM write enables, which both Vivado and Quartus infer as
    // block RAM without changing the old-data read-during-write contract.
    (* ram_style = "block" *) reg [7:0] bank0_w0 [0:3][0:4095];
    (* ram_style = "block" *) reg [7:0] bank0_w1 [0:3][0:4095];
    (* ram_style = "block" *) reg [7:0] bank1_w0 [0:3][0:4095];
    (* ram_style = "block" *) reg [7:0] bank1_w1 [0:3][0:4095];

    wire edit_bank = ~active_bank;
    assign program_write_ready = !validate_busy;
    wire [11:0] program_index = program_word_address[12:1];
    wire program_half = program_word_address[0];
    reg [31:0] program_bank0_w0_q;
    reg [31:0] program_bank0_w1_q;
    reg [31:0] program_bank1_w0_q;
    reg [31:0] program_bank1_w1_q;
    reg [31:0] program_bank0_capture_q;
    reg [31:0] program_bank1_capture_q;
    reg program_read_pending_q;
    reg program_read_select_pending_q;
    reg program_read_bank_q;
    reg program_read_half_q;
    reg program_read_output_bank_q;
    integer write_lane;
    reg [31:0] bank0_exec_w0_q;
    reg [31:0] bank0_exec_w1_q;
    reg [31:0] bank1_exec_w0_q;
    reg [31:0] bank1_exec_w1_q;
    reg [31:0] bank0_capture_w0_q;
    reg [31:0] bank0_capture_w1_q;
    reg [31:0] bank1_capture_w0_q;
    reg [31:0] bank1_capture_w1_q;

    reg [3:0] validate_state;
    reg validate_bank_q;
    reg [11:0] validate_first_q;
    reg [11:0] validate_index_q;
    reg [12:0] validate_remaining_q;
    reg [12:0] validate_end_q;
    reg [31:0] validate_w0_q;
    reg [31:0] validate_w1_q;
    reg validate_move_allowed_q;
    reg validate_dispatch_allowed_q;
    reg [2:0] validate_opcode_q;
    reg validate_encoding_ok_q;
    reg validate_target_ok_q;
    reg validate_range_ok_q;
    reg validate_last_q;
    reg [1:0] validate_action_q;
    reg [7:0] validate_action_fault_q;
    reg bank0_valid_q;
    reg bank1_valid_q;
    reg [11:0] bank0_first_q;
    reg [11:0] bank1_first_q;
    reg [12:0] bank0_end_q;
    reg [12:0] bank1_end_q;

    wire [11:0] validate_index = validate_index_q;
    wire [2:0] validate_opcode = validate_w0_q[31:29];
    wire validate_reserved_zero = validate_w0_q[28:16] == 13'd0;
    wire [11:0] validate_jump_target = validate_w0_q[11:0];
    wire validate_jump_in_list =
        {1'b0, validate_jump_target} >= {1'b0, validate_first_q} &&
        {1'b0, validate_jump_target} < validate_end_q;
    wire validate_skip_in_list = validate_remaining_q > 13'd2;
    wire validate_wait_in_range = validate_w0_q[9:0] < TOTAL_HEIGHT &&
        validate_w1_q[10:0] < TOTAL_WIDTH &&
        validate_w0_q[28:10] == 19'd0 &&
        validate_w1_q[31:11] == 21'd0;
    assign validate_move_target = validate_w0_q[15:0];
    assign validate_move_data = validate_w1_q;
    assign validate_dispatch_id = validate_w0_q[15:0];

    reg [3:0] exec_state;
    reg [31:0] execute_w0_q;
    reg [31:0] execute_w1_q;
    reg [11:0] active_first_q;
    reg [12:0] active_end_q;
    reg [14:0] frame_instruction_count_q;
    reg execute_move_allowed_q;
    reg [10:0] execution_beam_x_q;
    reg [9:0] execution_beam_y_q;
    reg [1:0] retire_pc_action_q;
    reg [1:0] retire_beam_action_q;
    reg [10:0] retire_beam_x_q;
    reg [9:0] retire_beam_y_q;
    reg execute_wait_reached_q;
    reg pc_in_active_list_q;
    wire pc_in_active_list = {1'b0, pc} >= {1'b0, active_first_q} &&
        {1'b0, pc} < active_end_q;
    assign move_target = execute_w0_q[15:0];
    assign move_data = execute_w1_q;
    assign move_beam_x = execution_beam_x_q;
    assign move_beam_y = execution_beam_y_q;
    assign dispatch_id = execute_w0_q[15:0];
    assign irq_beam_x = execution_beam_x_q;
    assign irq_beam_y = execution_beam_y_q;
    wire execute_wait_reached_now = beam_reached(
        execute_w0_q[9:0], execute_w1_q[10:0], beam_y, beam_x);
    wire execute_wait_reached = execute_wait_reached_q;
    wire edit_bank_valid = edit_bank ? bank1_valid_q : bank0_valid_q;
    wire promotion_now = promotion_pending ||
        (promote_request && edit_bank_valid &&
         !(program_write && program_write_ready));
    wire frame_bank = promotion_now ? edit_bank : active_bank;
    wire frame_bank_valid = frame_bank ? bank1_valid_q : bank0_valid_q;
    wire [11:0] frame_first = frame_bank ? bank1_first_q : bank0_first_q;
    wire [12:0] frame_end = frame_bank ? bank1_end_q : bank0_end_q;

    assign running = exec_state != EXEC_STOPPED;
    // The line scheduler needs the current result before it may release a
    // prepared line. Execution consumes the registered copy so the wide beam
    // comparator does not feed the execution-state mux.
    assign waiting = exec_state == EXEC_WAIT && !execute_wait_reached_now;

    task automatic validation_fail(input [7:0] code);
        begin
            validate_fault <= code;
            validate_fault_index <= validate_index;
            validate_busy <= 1'b0;
            validate_done <= 1'b1;
            validate_valid <= 1'b0;
            validate_state <= VALID_IDLE;
            if (!validate_bank_q)
                bank0_valid_q <= 1'b0;
            else
                bank1_valid_q <= 1'b0;
        end
    endtask

    task automatic validation_advance;
        begin
            validate_index_q <= validate_index_q + 12'd1;
            validate_remaining_q <= validate_remaining_q - 13'd1;
            validate_state <= VALID_FETCH;
        end
    endtask

    task automatic validation_finish;
        begin
            validate_busy <= 1'b0;
            validate_done <= 1'b1;
            validate_valid <= 1'b1;
            validate_state <= VALID_IDLE;
            if (!validate_bank_q) begin
                bank0_valid_q <= 1'b1;
                bank0_first_q <= validate_first_q;
                bank0_end_q <= validate_end_q;
            end else begin
                bank1_valid_q <= 1'b1;
                bank1_first_q <= validate_first_q;
                bank1_end_q <= validate_end_q;
            end
        end
    endtask

    task automatic execution_fail(input [7:0] code);
        begin
            fault <= 1'b1;
            fault_code <= code;
            fault_pc <= pc;
            exec_state <= EXEC_STOPPED;
            move_valid <= 1'b0;
            dispatch_valid <= 1'b0;
            irq_event <= 1'b0;
        end
    endtask

    task automatic advance_execution_beam;
        begin
            if (execution_beam_x_q == TOTAL_WIDTH - 1) begin
                execution_beam_x_q <= 11'd0;
                if (execution_beam_y_q == TOTAL_HEIGHT - 1)
                    execution_beam_y_q <= 10'd0;
                else
                    execution_beam_y_q <= execution_beam_y_q + 10'd1;
            end else begin
                execution_beam_x_q <= execution_beam_x_q + 11'd1;
            end
        end
    endtask

    always @(posedge clk) begin
        if (reset) begin
            program_read_data <= 32'd0;
            program_read_valid <= 1'b0;
            program_read_pending_q <= 1'b0;
            program_read_select_pending_q <= 1'b0;
            program_read_bank_q <= 1'b0;
            program_read_half_q <= 1'b0;
            program_read_output_bank_q <= 1'b0;
            program_bank0_capture_q <= 32'd0;
            program_bank1_capture_q <= 32'd0;
        end else begin
            program_read_valid <= program_read_select_pending_q;
            program_read_select_pending_q <= program_read_pending_q;
            program_read_output_bank_q <= program_read_bank_q;
            program_bank0_capture_q <= program_read_half_q ?
                program_bank0_w1_q : program_bank0_w0_q;
            program_bank1_capture_q <= program_read_half_q ?
                program_bank1_w1_q : program_bank1_w0_q;
            program_read_data <= program_read_output_bank_q ?
                program_bank1_capture_q : program_bank0_capture_q;
            program_read_pending_q <= program_read;
            if (program_read) begin
                program_read_bank_q <= edit_bank;
                program_read_half_q <= program_half;
                if (!program_half) begin
                    program_bank0_w0_q <= {
                        bank0_w0[3][program_index],
                        bank0_w0[2][program_index],
                        bank0_w0[1][program_index],
                        bank0_w0[0][program_index]
                    };
                    program_bank1_w0_q <= {
                        bank1_w0[3][program_index],
                        bank1_w0[2][program_index],
                        bank1_w0[1][program_index],
                        bank1_w0[0][program_index]
                    };
                end else begin
                    program_bank0_w1_q <= {
                        bank0_w1[3][program_index],
                        bank0_w1[2][program_index],
                        bank0_w1[1][program_index],
                        bank0_w1[0][program_index]
                    };
                    program_bank1_w1_q <= {
                        bank1_w1[3][program_index],
                        bank1_w1[2][program_index],
                        bank1_w1[1][program_index],
                        bank1_w1[0][program_index]
                    };
                end
            end
        end

        if (!reset && program_write && program_write_ready) begin
            if (!edit_bank) begin
                for (write_lane = 0; write_lane < 4;
                     write_lane = write_lane + 1)
                    if (program_write_strobe[write_lane]) begin
                        if (!program_half)
                            bank0_w0[write_lane][program_index] <=
                                program_write_data[write_lane * 8 +: 8];
                        else
                            bank0_w1[write_lane][program_index] <=
                                program_write_data[write_lane * 8 +: 8];
                    end
            end else begin
                for (write_lane = 0; write_lane < 4;
                     write_lane = write_lane + 1)
                    if (program_write_strobe[write_lane]) begin
                        if (!program_half)
                            bank1_w0[write_lane][program_index] <=
                                program_write_data[write_lane * 8 +: 8];
                        else
                            bank1_w1[write_lane][program_index] <=
                                program_write_data[write_lane * 8 +: 8];
                    end
            end
        end
    end

    always @(posedge clk) begin
        bank0_exec_w0_q <= {
            bank0_w0[3][active_bank ? validate_index : pc],
            bank0_w0[2][active_bank ? validate_index : pc],
            bank0_w0[1][active_bank ? validate_index : pc],
            bank0_w0[0][active_bank ? validate_index : pc]
        };
        bank0_exec_w1_q <= {
            bank0_w1[3][active_bank ? validate_index : pc],
            bank0_w1[2][active_bank ? validate_index : pc],
            bank0_w1[1][active_bank ? validate_index : pc],
            bank0_w1[0][active_bank ? validate_index : pc]
        };
        bank1_exec_w0_q <= {
            bank1_w0[3][active_bank ? pc : validate_index],
            bank1_w0[2][active_bank ? pc : validate_index],
            bank1_w0[1][active_bank ? pc : validate_index],
            bank1_w0[0][active_bank ? pc : validate_index]
        };
        bank1_exec_w1_q <= {
            bank1_w1[3][active_bank ? pc : validate_index],
            bank1_w1[2][active_bank ? pc : validate_index],
            bank1_w1[1][active_bank ? pc : validate_index],
            bank1_w1[0][active_bank ? pc : validate_index]
        };
    end

    always @(posedge clk) begin
        validate_done <= 1'b0;
        if (reset) begin
            validate_state <= VALID_IDLE;
            validate_bank_q <= 1'b1;
            validate_first_q <= 12'd0;
            validate_index_q <= 12'd0;
            validate_remaining_q <= 13'd0;
            validate_end_q <= 13'd0;
            validate_w0_q <= 32'd0;
            validate_w1_q <= 32'd0;
            validate_move_allowed_q <= 1'b0;
            validate_dispatch_allowed_q <= 1'b0;
            validate_opcode_q <= 3'd0;
            validate_encoding_ok_q <= 1'b0;
            validate_target_ok_q <= 1'b0;
            validate_range_ok_q <= 1'b0;
            validate_last_q <= 1'b0;
            validate_action_q <= VALID_ACTION_FAIL;
            validate_action_fault_q <= FAULT_NONE;
            validate_busy <= 1'b0;
            validate_done <= 1'b0;
            validate_valid <= 1'b0;
            validate_fault <= FAULT_NONE;
            validate_fault_index <= 12'd0;
            bank0_valid_q <= 1'b0;
            bank1_valid_q <= 1'b0;
            bank0_first_q <= 12'd0;
            bank1_first_q <= 12'd0;
            bank0_end_q <= 13'd0;
            bank1_end_q <= 13'd0;
        end else begin
            if (program_write && program_write_ready) begin
                if (!edit_bank)
                    bank0_valid_q <= 1'b0;
                else
                    bank1_valid_q <= 1'b0;
            end
            if (validate_start && !validate_busy) begin
                validate_bank_q <= edit_bank;
                validate_first_q <= validate_first;
                validate_index_q <= validate_first;
                validate_remaining_q <= validate_count;
                validate_end_q <= {1'b0, validate_first} + validate_count;
                validate_valid <= 1'b0;
                validate_fault <= FAULT_NONE;
                validate_fault_index <= validate_first;
                validate_busy <= 1'b1;
                validate_state <= VALID_RANGE;
            end else if (validate_busy) begin
                case (validate_state)
                    VALID_RANGE: begin
                        if (validate_remaining_q != 13'd0 &&
                            validate_remaining_q <= 13'd4096 &&
                            validate_end_q <= 13'd4096)
                            validate_state <= VALID_FETCH;
                        else
                            validation_fail(FAULT_BAD_RANGE);
                    end
                    VALID_FETCH: validate_state <= VALID_CAPTURE_PIPE;
                    VALID_CAPTURE_PIPE:
                        validate_state <= VALID_CAPTURE;
                    VALID_CAPTURE: begin
                        validate_w0_q <= validate_bank_q ?
                            bank1_capture_w0_q : bank0_capture_w0_q;
                        validate_w1_q <= validate_bank_q ?
                            bank1_capture_w1_q : bank0_capture_w1_q;
                        validate_state <= VALID_PERMIT_PIPE;
                    end
                    VALID_PERMIT_PIPE:
                        validate_state <= VALID_PERMIT;
                    VALID_PERMIT: begin
                        validate_move_allowed_q <= validate_move_allowed;
                        validate_dispatch_allowed_q <=
                            validate_dispatch_allowed;
                        validate_state <= VALID_CHECK;
                    end
                    VALID_CHECK: begin
                        validate_opcode_q <= validate_opcode;
                        validate_encoding_ok_q <= 1'b1;
                        validate_target_ok_q <= 1'b1;
                        validate_range_ok_q <= 1'b1;
                        validate_last_q <= validate_remaining_q == 13'd1;
                        validate_state <= VALID_DECIDE;
                        case (validate_opcode)
                            OP_END: validate_encoding_ok_q <=
                                validate_w0_q[28:0] == 29'd0 &&
                                validate_w1_q == 32'd0;
                            OP_MOVE: begin
                                validate_encoding_ok_q <=
                                    validate_reserved_zero &&
                                    validate_w0_q[1:0] == 2'b00;
                                validate_target_ok_q <=
                                    validate_move_allowed_q;
                            end
                            OP_WAIT: validate_range_ok_q <=
                                validate_wait_in_range;
                            OP_SKIP: validate_range_ok_q <=
                                validate_wait_in_range &&
                                validate_skip_in_list;
                            OP_IRQ: validate_encoding_ok_q <=
                                validate_reserved_zero &&
                                validate_w1_q == 32'd0;
                            OP_JUMP: validate_target_ok_q <=
                                validate_w0_q[28:12] == 17'd0 &&
                                validate_w1_q == 32'd0 &&
                                validate_jump_in_list;
                            OP_DISPATCH: begin
                                validate_encoding_ok_q <=
                                    validate_reserved_zero &&
                                    validate_w1_q == 32'd0;
                                validate_target_ok_q <=
                                    validate_dispatch_allowed_q;
                            end
                            default: validate_encoding_ok_q <= 1'b0;
                        endcase
                    end
                    VALID_DECIDE: begin
                        validate_action_q <= VALID_ACTION_FAIL;
                        validate_action_fault_q <= FAULT_BAD_OPCODE;
                        validate_state <= VALID_APPLY;
                        case (validate_opcode_q)
                            OP_END: begin
                                if (!validate_encoding_ok_q ||
                                    !validate_last_q)
                                    validate_action_fault_q <=
                                        FAULT_BAD_ENCODING;
                                else begin
                                    validate_action_q <=
                                        VALID_ACTION_FINISH;
                                    validate_action_fault_q <= FAULT_NONE;
                                end
                            end
                            OP_MOVE: begin
                                if (!validate_encoding_ok_q)
                                    validate_action_fault_q <=
                                        FAULT_BAD_ENCODING;
                                else if (!validate_target_ok_q)
                                    validate_action_fault_q <=
                                        FAULT_BAD_TARGET;
                                else if (validate_last_q)
                                    validate_action_fault_q <=
                                        FAULT_MISSING_END;
                                else begin
                                    validate_action_q <=
                                        VALID_ACTION_ADVANCE;
                                    validate_action_fault_q <= FAULT_NONE;
                                end
                            end
                            OP_WAIT, OP_SKIP: begin
                                if (!validate_range_ok_q)
                                    validate_action_fault_q <=
                                        FAULT_BAD_RANGE;
                                else if (validate_last_q)
                                    validate_action_fault_q <=
                                        FAULT_MISSING_END;
                                else begin
                                    validate_action_q <=
                                        VALID_ACTION_ADVANCE;
                                    validate_action_fault_q <= FAULT_NONE;
                                end
                            end
                            OP_IRQ: begin
                                if (!validate_encoding_ok_q)
                                    validate_action_fault_q <=
                                        FAULT_BAD_ENCODING;
                                else if (validate_last_q)
                                    validate_action_fault_q <=
                                        FAULT_MISSING_END;
                                else begin
                                    validate_action_q <=
                                        VALID_ACTION_ADVANCE;
                                    validate_action_fault_q <= FAULT_NONE;
                                end
                            end
                            OP_JUMP: begin
                                if (!validate_target_ok_q)
                                    validate_action_fault_q <=
                                        FAULT_BAD_TARGET;
                                else if (validate_last_q)
                                    validate_action_fault_q <=
                                        FAULT_MISSING_END;
                                else begin
                                    validate_action_q <=
                                        VALID_ACTION_ADVANCE;
                                    validate_action_fault_q <= FAULT_NONE;
                                end
                            end
                            OP_DISPATCH: begin
                                if (!validate_encoding_ok_q)
                                    validate_action_fault_q <=
                                        FAULT_BAD_ENCODING;
                                else if (!validate_target_ok_q)
                                    validate_action_fault_q <=
                                        FAULT_BAD_TARGET;
                                else if (validate_last_q)
                                    validate_action_fault_q <=
                                        FAULT_MISSING_END;
                                else begin
                                    validate_action_q <=
                                        VALID_ACTION_ADVANCE;
                                    validate_action_fault_q <= FAULT_NONE;
                                end
                            end
                            default: begin end
                        endcase
                    end
                    VALID_APPLY: begin
                        case (validate_action_q)
                            VALID_ACTION_ADVANCE: validation_advance();
                            VALID_ACTION_FINISH: validation_finish();
                            default:
                                validation_fail(validate_action_fault_q);
                        endcase
                    end
                    default: validation_fail(FAULT_BAD_ENCODING);
                endcase
            end
        end
    end

    always @(posedge clk) begin
        promoted <= 1'b0;
        baseline_restore <= 1'b0;
        if (reset) begin
            active_bank <= 1'b0;
            promotion_pending <= 1'b0;
            active_first_q <= 12'd0;
            active_end_q <= 13'd0;
            pc <= 12'd0;
            exec_state <= EXEC_STOPPED;
            execute_w0_q <= 32'd0;
            execute_w1_q <= 32'd0;
            bank0_capture_w0_q <= 32'd0;
            bank0_capture_w1_q <= 32'd0;
            bank1_capture_w0_q <= 32'd0;
            bank1_capture_w1_q <= 32'd0;
            move_valid <= 1'b0;
            move_class <= 2'd0;
            dispatch_valid <= 1'b0;
            irq_sources <= 16'd0;
            irq_event <= 1'b0;
            fault <= 1'b0;
            fault_code <= FAULT_NONE;
            fault_pc <= 12'd0;
            frame_instruction_count_q <= 15'd0;
            execute_move_allowed_q <= 1'b0;
            execution_beam_x_q <= 11'd0;
            execution_beam_y_q <= 10'd0;
            retire_pc_action_q <= RETIRE_PC_PLUS_ONE;
            retire_beam_action_q <= RETIRE_BEAM_NONE;
            retire_beam_x_q <= 11'd0;
            retire_beam_y_q <= 10'd0;
            execute_wait_reached_q <= 1'b0;
            pc_in_active_list_q <= 1'b0;
            instructions_retired <= 32'd0;
        end else begin
            // Keep the shared BRAM-output boundary warm for both concurrent
            // consumers. Execution and validation select banks only after
            // this register stage, so neither mux sits on BRAM clock-to-out.
            bank0_capture_w0_q <= bank0_exec_w0_q;
            bank0_capture_w1_q <= bank0_exec_w1_q;
            bank1_capture_w0_q <= bank1_exec_w0_q;
            bank1_capture_w1_q <= bank1_exec_w1_q;
            // The target is meaningful only when RETIRE_BEAM_TARGET is
            // selected. Loading it every cycle avoids a beam comparator on
            // the clock-enable path of all 21 target flops.
            retire_beam_x_q <= execute_w1_q[10:0];
            retire_beam_y_q <= execute_w0_q[9:0];
            execute_wait_reached_q <= execute_wait_reached_now;
            if (fault_clear) begin
                fault <= 1'b0;
                fault_code <= FAULT_NONE;
                fault_pc <= 12'd0;
            end

            if (promote_request) begin
                if (edit_bank_valid &&
                    !(program_write && program_write_ready))
                    promotion_pending <= 1'b1;
                else begin
                    fault <= 1'b1;
                    fault_code <= FAULT_NOT_VALIDATED;
                    fault_pc <= pc;
                end
            end

            if (program_write && program_write_ready)
                promotion_pending <= 1'b0;

            if (frame_boundary) begin
                baseline_restore <= 1'b1;
                exec_state <= EXEC_STOPPED;
                move_valid <= 1'b0;
                dispatch_valid <= 1'b0;
                irq_event <= 1'b0;
                frame_instruction_count_q <= 15'd0;
                execution_beam_x_q <= 11'd0;
                execution_beam_y_q <= 10'd0;
                if (promotion_now) begin
                    active_bank <= edit_bank;
                    if (edit_bank) begin
                        active_first_q <= bank1_first_q;
                        active_end_q <= bank1_end_q;
                    end else begin
                        active_first_q <= bank0_first_q;
                        active_end_q <= bank0_end_q;
                    end
                    promotion_pending <= 1'b0;
                    promoted <= 1'b1;
                end

                if (enable && frame_start && frame_bank_valid) begin
                    pc <= frame_first;
                    active_end_q <= frame_end;
                    exec_state <= EXEC_FETCH;
                end
            end else if (!enable) begin
                exec_state <= EXEC_STOPPED;
                move_valid <= 1'b0;
                dispatch_valid <= 1'b0;
                irq_event <= 1'b0;
            end else if (frame_start) begin
                if ((active_bank && bank1_valid_q) ||
                    (!active_bank && bank0_valid_q)) begin
                    pc <= active_first_q;
                    exec_state <= EXEC_FETCH;
                    move_valid <= 1'b0;
                    dispatch_valid <= 1'b0;
                    irq_event <= 1'b0;
                    frame_instruction_count_q <= 15'd0;
                end
            end else begin
                case (exec_state)
                    EXEC_STOPPED: begin end
                    EXEC_FETCH: begin
                        pc_in_active_list_q <= pc_in_active_list;
                        exec_state <= EXEC_RANGE;
                    end
                    EXEC_RANGE: begin
                        if (!pc_in_active_list_q)
                            execution_fail(FAULT_BAD_RANGE);
                        else
                            exec_state <= EXEC_CAPTURE;
                    end
                    EXEC_CAPTURE: begin
                        exec_state <= EXEC_SELECT;
                    end
                    EXEC_SELECT: begin
                        execute_w0_q <= active_bank ? bank1_capture_w0_q :
                            bank0_capture_w0_q;
                        execute_w1_q <= active_bank ? bank1_capture_w1_q :
                            bank0_capture_w1_q;
                        exec_state <= EXEC_EVAL;
                    end
                    EXEC_EVAL: begin
                        if (frame_instruction_count_q >=
                            MAX_INSTRUCTIONS_PER_FRAME)
                            execution_fail(FAULT_RUNAWAY);
                        else begin
                            case (execute_w0_q[31:29])
                                OP_END: begin
                                    exec_state <= EXEC_STOPPED;
                                    instructions_retired <=
                                        instructions_retired + 32'd1;
                                end
                                OP_MOVE: begin
                                    // Static payload checks happened before
                                    // promotion. Runtime permission retains
                                    // only baseline-dependent guards.
                                    execute_move_allowed_q <= move_allowed;
                                    move_class <= move_timing_class;
                                    retire_pc_action_q <= RETIRE_PC_PLUS_ONE;
                                    retire_beam_action_q <=
                                        move_timing_class == 2'd0 ?
                                        RETIRE_BEAM_ADVANCE : RETIRE_BEAM_NONE;
                                    exec_state <= EXEC_MOVE;
                                end
                                OP_WAIT: begin
                                    exec_state <= EXEC_WAIT;
                                end
                                OP_SKIP: begin
                                    exec_state <= EXEC_SKIP;
                                end
                                OP_IRQ: begin
                                    irq_sources <= execute_w0_q[15:0];
                                    irq_event <= 1'b1;
                                    exec_state <= EXEC_IRQ;
                                end
                                OP_JUMP: begin
                                    // Validation guarantees an in-list
                                    // destination before bank promotion.
                                    retire_pc_action_q <= RETIRE_PC_TARGET;
                                    retire_beam_action_q <= RETIRE_BEAM_NONE;
                                    exec_state <= EXEC_RETIRE;
                                end
                                OP_DISPATCH: begin
                                    dispatch_valid <= 1'b1;
                                    exec_state <= EXEC_DISPATCH;
                                end
                                default: execution_fail(FAULT_BAD_OPCODE);
                            endcase
                        end
                    end
                    EXEC_WAIT: begin
                        if (execute_wait_reached) begin
                            pc <= pc + 12'd1;
                            execution_beam_x_q <= retire_beam_x_q;
                            execution_beam_y_q <= retire_beam_y_q;
                            frame_instruction_count_q <=
                                frame_instruction_count_q + 15'd1;
                            instructions_retired <=
                                instructions_retired + 32'd1;
                            exec_state <= EXEC_FETCH;
                        end
                    end
                    EXEC_SKIP: begin
                        // Validation guarantees that a taken skip remains
                        // inside the active list.
                        pc <= pc + (execute_wait_reached ? 12'd2 : 12'd1);
                        if (execute_wait_reached) begin
                            execution_beam_x_q <= retire_beam_x_q;
                            execution_beam_y_q <= retire_beam_y_q;
                        end
                        frame_instruction_count_q <=
                            frame_instruction_count_q + 15'd1;
                        instructions_retired <= instructions_retired + 32'd1;
                        exec_state <= EXEC_FETCH;
                    end
                    EXEC_MOVE: begin
                        if (!move_valid) begin
                            if (!execute_move_allowed_q)
                                execution_fail(FAULT_BAD_TARGET);
                            else
                                move_valid <= 1'b1;
                        end
                        else if (move_ready) begin
                            move_valid <= 1'b0;
                            exec_state <= move_class == 2'd1 ?
                                EXEC_MOVE_COMMIT : EXEC_RETIRE;
                        end
                    end
                    EXEC_MOVE_COMMIT: exec_state <= EXEC_RETIRE;
                    EXEC_IRQ: if (irq_event && irq_ready) begin
                        irq_event <= 1'b0;
                        retire_pc_action_q <= RETIRE_PC_PLUS_ONE;
                        retire_beam_action_q <= RETIRE_BEAM_ADVANCE;
                        exec_state <= EXEC_RETIRE;
                    end
                    EXEC_DISPATCH: if (dispatch_valid && dispatch_ready) begin
                        dispatch_valid <= 1'b0;
                        if (!dispatch_allowed)
                            execution_fail(FAULT_BAD_TARGET);
                        else begin
                            retire_pc_action_q <= RETIRE_PC_PLUS_ONE;
                            retire_beam_action_q <= RETIRE_BEAM_NONE;
                            exec_state <= EXEC_RETIRE;
                        end
                    end
                    EXEC_RETIRE: begin
                        case (retire_pc_action_q)
                            RETIRE_PC_PLUS_TWO: pc <= pc + 12'd2;
                            RETIRE_PC_TARGET: pc <= execute_w0_q[11:0];
                            default: pc <= pc + 12'd1;
                        endcase
                        case (retire_beam_action_q)
                            RETIRE_BEAM_TARGET: begin
                                execution_beam_x_q <= retire_beam_x_q;
                                execution_beam_y_q <= retire_beam_y_q;
                            end
                            RETIRE_BEAM_ADVANCE: advance_execution_beam();
                            default: begin end
                        endcase
                        frame_instruction_count_q <=
                            frame_instruction_count_q + 15'd1;
                        instructions_retired <= instructions_retired + 32'd1;
                        exec_state <= EXEC_FETCH;
                    end
                    default: execution_fail(FAULT_BAD_ENCODING);
                endcase
            end
        end
    end
endmodule

`default_nettype wire
