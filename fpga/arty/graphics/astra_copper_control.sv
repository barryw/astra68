// Copyright (c) 2026 Astra68 contributors
//
// AXI4-Lite software boundary for the dual-bank copper. The upper half of the
// 64 KiB graphics aperture is the inactive program bank; the active bank is
// never addressable by software.
`timescale 1ns/1ps
`default_nettype none

module astra_copper_control #(
    parameter integer TOTAL_WIDTH = 1650,
    parameter integer TOTAL_HEIGHT = 750
) (
    input  wire        clk,
    input  wire        reset,
    input  wire        frame_boundary,
    input  wire        frame_start,
    input  wire [10:0] beam_x,
    input  wire [9:0]  beam_y,

    output wire        move_valid,
    input  wire        move_ready,
    output wire [15:0] move_target,
    output wire [31:0] move_data,
    output wire [10:0] move_beam_x,
    output wire [9:0]  move_beam_y,
    input  wire        move_allowed,
    input  wire [1:0]  move_timing_class,
    output wire [1:0]  move_class,
    output wire [15:0] validate_move_target,
    output wire [31:0] validate_move_data,
    input  wire        validate_move_allowed,

    output wire        dispatch_valid,
    input  wire        dispatch_ready,
    output wire [15:0] dispatch_id,
    output wire [10:0] dispatch_submission_producer,
    input  wire        dispatch_allowed,
    output wire [15:0] validate_dispatch_id,
    input  wire        validate_dispatch_allowed,

    output wire        irq_event,
    input  wire        irq_ready,
    input  wire        irq_delivered,
    output wire [15:0] irq_sources,
    output wire [10:0] irq_beam_x,
    output wire [9:0]  irq_beam_y,
    output wire        interrupt,
    output wire        baseline_restore,
    output wire        enabled,
    output wire        running,
    output wire        waiting,
    output wire        faulted,

    input  wire [31:0] s_axi_awaddr,
    input  wire [2:0]  s_axi_awprot,
    input  wire        s_axi_awvalid,
    output wire        s_axi_awready,
    input  wire [31:0] s_axi_wdata,
    input  wire [3:0]  s_axi_wstrb,
    input  wire        s_axi_wvalid,
    output wire        s_axi_wready,
    output reg  [1:0]  s_axi_bresp,
    output reg         s_axi_bvalid,
    input  wire        s_axi_bready,
    input  wire [31:0] s_axi_araddr,
    input  wire [2:0]  s_axi_arprot,
    input  wire        s_axi_arvalid,
    output wire        s_axi_arready,
    output reg  [31:0] s_axi_rdata,
    output reg  [1:0]  s_axi_rresp,
    output reg         s_axi_rvalid,
    input  wire        s_axi_rready
);
    localparam [31:0] DEVICE_ID = 32'h434f5052; // "COPR"
    localparam [31:0] VERSION = 32'h00010001;

    reg enable_q;
    reg [11:0] validate_first_q;
    reg [12:0] validate_count_q;
    reg validate_start_q;
    reg promote_request_q;
    reg fault_clear_q;
    reg irq_pending_q;
    reg waiting_status_q;
    reg [3:0] dispatch_selector_q;
    reg [10:0] dispatch_producer_q [0:15];
    reg [15:0] dispatch_valid_q;
    reg dispatch_write_pending_q;
    reg [3:0] dispatch_write_selector_q;
    reg [10:0] dispatch_write_producer_q;
    reg dispatch_write_valid_q;
    integer dispatch_reset_index;

    wire validate_busy;
    wire validate_done;
    wire validate_valid;
    wire [7:0] validate_fault;
    wire [11:0] validate_fault_index;
    wire promotion_pending;
    wire promoted;
    wire active_bank;
    wire fault;
    wire [7:0] fault_code;
    wire [11:0] fault_pc;
    wire [11:0] pc;
    wire [31:0] instructions_retired;
    wire core_dispatch_valid;
    wire [15:0] core_dispatch_id;
    wire core_dispatch_ready;
    wire core_dispatch_allowed;
    wire validate_dispatch_id_in_range =
        validate_dispatch_id[15:4] == 12'd0;
    wire selected_validate_dispatch_valid =
        validate_dispatch_id_in_range &&
        dispatch_valid_q[validate_dispatch_id[3:0]];

    // Dispatch table lookup and renderer ring validation used to form one
    // combinational round trip through the copper core.  Snapshot the table
    // entry and return a registered completion so neither path is timing
    // critical and the request remains stable under renderer backpressure.
    reg dispatch_pending_q;
    reg dispatch_completion_q;
    reg dispatch_completion_allowed_q;
    reg [15:0] dispatch_id_q;
    reg [10:0] dispatch_submission_producer_q;

    reg program_write_q;
    reg program_read_q;
    reg [12:0] program_word_address_q;
    reg [31:0] program_write_data_q;
    reg [3:0] program_write_strobe_q;
    wire program_write_ready;
    wire [31:0] program_read_data;
    wire program_read_valid;

    assign interrupt = irq_pending_q || fault;
    assign enabled = enable_q;
    assign faulted = fault;
    assign dispatch_valid = dispatch_pending_q;
    assign dispatch_id = dispatch_id_q;
    assign dispatch_submission_producer =
        dispatch_submission_producer_q;
    assign core_dispatch_ready = dispatch_completion_q;
    assign core_dispatch_allowed = dispatch_completion_allowed_q;

    astra_copper #(
        .TOTAL_WIDTH(TOTAL_WIDTH),
        .TOTAL_HEIGHT(TOTAL_HEIGHT)
    ) copper_i (
        .clk(clk),
        .reset(reset),
        .program_write(program_write_q),
        .program_read(program_read_q),
        .program_word_address(program_word_address_q),
        .program_write_data(program_write_data_q),
        .program_write_strobe(program_write_strobe_q),
        .program_write_ready(program_write_ready),
        .program_read_data(program_read_data),
        .program_read_valid(program_read_valid),
        .validate_start(validate_start_q),
        .validate_first(validate_first_q),
        .validate_count(validate_count_q),
        .validate_move_target(validate_move_target),
        .validate_move_data(validate_move_data),
        .validate_move_allowed(validate_move_allowed),
        .validate_dispatch_id(validate_dispatch_id),
        .validate_dispatch_allowed(validate_dispatch_allowed &&
            selected_validate_dispatch_valid),
        .validate_busy(validate_busy),
        .validate_done(validate_done),
        .validate_valid(validate_valid),
        .validate_fault(validate_fault),
        .validate_fault_index(validate_fault_index),
        .promote_request(promote_request_q),
        .frame_boundary(frame_boundary),
        .frame_start(frame_start),
        .promotion_pending(promotion_pending),
        .promoted(promoted),
        .baseline_restore(baseline_restore),
        .active_bank(active_bank),
        .enable(enable_q),
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
        .dispatch_valid(core_dispatch_valid),
        .dispatch_ready(core_dispatch_ready),
        .dispatch_id(core_dispatch_id),
        .dispatch_allowed(core_dispatch_allowed),
        .irq_event(irq_event),
        .irq_ready(irq_ready),
        .irq_sources(irq_sources),
        .irq_beam_x(irq_beam_x),
        .irq_beam_y(irq_beam_y),
        .running(running),
        .waiting(waiting),
        .fault_clear(fault_clear_q),
        .fault(fault),
        .fault_code(fault_code),
        .fault_pc(fault_pc),
        .pc(pc),
        .instructions_retired(instructions_retired)
    );

    reg aw_pending_q;
    reg [15:0] awaddr_q;
    reg w_pending_q;
    reg [31:0] wdata_q;
    reg [3:0] wstrb_q;
    wire write_fire = aw_pending_q && w_pending_q && !s_axi_bvalid;
    assign s_axi_awready = !aw_pending_q && !s_axi_bvalid;
    assign s_axi_wready = !w_pending_q && !s_axi_bvalid;

    reg read_program_pending_q;
    assign s_axi_arready = !read_program_pending_q && !s_axi_rvalid;
    wire unused_protection = &{1'b0, s_axi_awprot, s_axi_arprot};

    always @(posedge clk) begin
        validate_start_q <= 1'b0;
        promote_request_q <= 1'b0;
        fault_clear_q <= 1'b0;
        program_write_q <= 1'b0;
        program_read_q <= 1'b0;

        if (reset) begin
            enable_q <= 1'b0;
            validate_first_q <= 12'd0;
            validate_count_q <= 13'd0;
            irq_pending_q <= 1'b0;
            waiting_status_q <= 1'b0;
            dispatch_selector_q <= 4'd0;
            dispatch_valid_q <= 16'd0;
            dispatch_write_pending_q <= 1'b0;
            dispatch_write_selector_q <= 4'd0;
            dispatch_write_producer_q <= 11'd0;
            dispatch_write_valid_q <= 1'b0;
            dispatch_pending_q <= 1'b0;
            dispatch_completion_q <= 1'b0;
            dispatch_completion_allowed_q <= 1'b0;
            dispatch_id_q <= 16'd0;
            dispatch_submission_producer_q <= 11'd0;
            for (dispatch_reset_index = 0; dispatch_reset_index < 16;
                 dispatch_reset_index = dispatch_reset_index + 1)
                dispatch_producer_q[dispatch_reset_index] <= 11'd0;
            aw_pending_q <= 1'b0;
            awaddr_q <= 16'd0;
            w_pending_q <= 1'b0;
            wdata_q <= 32'd0;
            wstrb_q <= 4'd0;
            s_axi_bresp <= 2'b00;
            s_axi_bvalid <= 1'b0;
            read_program_pending_q <= 1'b0;
            program_word_address_q <= 13'd0;
            program_write_data_q <= 32'd0;
            program_write_strobe_q <= 4'd0;
            s_axi_rdata <= 32'd0;
            s_axi_rresp <= 2'b00;
            s_axi_rvalid <= 1'b0;
        end else begin
            waiting_status_q <= waiting;
            if (dispatch_write_pending_q) begin
                dispatch_producer_q[dispatch_write_selector_q] <=
                    dispatch_write_producer_q;
                dispatch_valid_q[dispatch_write_selector_q] <=
                    dispatch_write_valid_q;
                dispatch_write_pending_q <= 1'b0;
            end
            if (dispatch_pending_q) begin
                if (!core_dispatch_valid) begin
                    dispatch_pending_q <= 1'b0;
                end else if (dispatch_ready) begin
                    dispatch_pending_q <= 1'b0;
                    dispatch_completion_q <= 1'b1;
                    dispatch_completion_allowed_q <= dispatch_allowed;
                end
            end else if (dispatch_completion_q) begin
                dispatch_completion_q <= 1'b0;
            end else if (core_dispatch_valid) begin
                dispatch_pending_q <= 1'b1;
                dispatch_id_q <= core_dispatch_id;
                dispatch_submission_producer_q <=
                    dispatch_producer_q[core_dispatch_id[3:0]];
            end

            if (irq_delivered)
                irq_pending_q <= 1'b1;

            if (s_axi_awvalid && s_axi_awready) begin
                aw_pending_q <= 1'b1;
                awaddr_q <= s_axi_awaddr[15:0];
            end
            if (s_axi_wvalid && s_axi_wready) begin
                w_pending_q <= 1'b1;
                wdata_q <= s_axi_wdata;
                wstrb_q <= s_axi_wstrb;
            end
            if (s_axi_bvalid && s_axi_bready)
                s_axi_bvalid <= 1'b0;

            if (write_fire) begin
                aw_pending_q <= 1'b0;
                w_pending_q <= 1'b0;
                s_axi_bvalid <= 1'b1;
                s_axi_bresp <= 2'b00;
                if (awaddr_q[1:0] != 2'b00) begin
                    s_axi_bresp <= 2'b11;
                end else if (awaddr_q[15]) begin
                    if (!program_write_ready) begin
                        s_axi_bresp <= 2'b10;
                    end else begin
                        program_word_address_q <= awaddr_q[14:2];
                        program_write_data_q <= wdata_q;
                        program_write_strobe_q <= wstrb_q;
                        program_write_q <= 1'b1;
                    end
                end else begin
                    case (awaddr_q)
                        16'h4008: begin
                            if (wstrb_q != 4'hf ||
                                (wdata_q & 32'hfffffff8) != 32'd0) begin
                                s_axi_bresp <= 2'b10;
                            end else begin
                                enable_q <= wdata_q[0];
                                promote_request_q <= wdata_q[1];
                                fault_clear_q <= wdata_q[2];
                            end
                        end
                        16'h4010: begin
                            if (wstrb_q != 4'hf ||
                                wdata_q[15:12] != 4'd0 ||
                                wdata_q[31:29] != 3'd0) begin
                                s_axi_bresp <= 2'b10;
                            end else begin
                                validate_first_q <= wdata_q[11:0];
                                validate_count_q <= wdata_q[28:16];
                            end
                        end
                        16'h4014: begin
                            if (wstrb_q != 4'hf || wdata_q != 32'd1 ||
                                validate_busy)
                                s_axi_bresp <= 2'b10;
                            else
                                validate_start_q <= 1'b1;
                        end
                        16'h4028: begin
                            if (wstrb_q != 4'hf ||
                                (wdata_q & 32'hfffffffe) != 32'd0)
                                s_axi_bresp <= 2'b10;
                            else if (wdata_q[0])
                                irq_pending_q <= 1'b0;
                        end
                        16'h4030: begin
                            if (wstrb_q != 4'hf ||
                                wdata_q[31:4] != 28'd0 || enable_q)
                                s_axi_bresp <= 2'b10;
                            else
                                dispatch_selector_q <= wdata_q[3:0];
                        end
                        16'h4034: begin
                            if (wstrb_q != 4'hf || enable_q ||
                                wdata_q[30:11] != 20'd0)
                                s_axi_bresp <= 2'b10;
                            else begin
                                dispatch_write_pending_q <= 1'b1;
                                dispatch_write_selector_q <=
                                    dispatch_selector_q;
                                dispatch_write_producer_q <= wdata_q[10:0];
                                dispatch_write_valid_q <= wdata_q[31];
                            end
                        end
                        default: s_axi_bresp <= 2'b11;
                    endcase
                end
            end

            if (s_axi_rvalid && s_axi_rready)
                s_axi_rvalid <= 1'b0;
            if (s_axi_arvalid && s_axi_arready) begin
                if (s_axi_araddr[1:0] != 2'b00) begin
                    s_axi_rdata <= 32'd0;
                    s_axi_rresp <= 2'b11;
                    s_axi_rvalid <= 1'b1;
                end else if (s_axi_araddr[15]) begin
                    program_word_address_q <= s_axi_araddr[14:2];
                    program_read_q <= 1'b1;
                    read_program_pending_q <= 1'b1;
                end else begin
                    s_axi_rresp <= 2'b00;
                    s_axi_rvalid <= 1'b1;
                    case (s_axi_araddr[15:0])
                        16'h4000: s_axi_rdata <= DEVICE_ID;
                        16'h4004: s_axi_rdata <= VERSION;
                        16'h4008: s_axi_rdata <= {31'd0, enable_q};
                        16'h400c: s_axi_rdata <= {
                            21'd0, fault, irq_pending_q, waiting_status_q,
                            running,
                            promoted, promotion_pending, validate_valid,
                            validate_done, validate_busy, ~active_bank,
                            active_bank};
                        16'h4010: s_axi_rdata <= {
                            3'd0, validate_count_q, 4'd0, validate_first_q};
                        16'h4018: s_axi_rdata <= {
                            validate_fault_index, 4'd0, validate_fault,
                            5'd0, validate_valid, validate_done,
                            validate_busy};
                        16'h401c: s_axi_rdata <= {
                            fault_pc, 4'd0, fault_code, 7'd0, fault};
                        16'h4020: s_axi_rdata <= {20'd0, pc};
                        16'h4024: s_axi_rdata <= instructions_retired;
                        16'h4028: s_axi_rdata <= {31'd0, irq_pending_q};
                        16'h402c: s_axi_rdata <= {16'd0, irq_sources};
                        16'h4030: s_axi_rdata <=
                            {28'd0, dispatch_selector_q};
                        16'h4034: s_axi_rdata <= {
                            dispatch_valid_q[dispatch_selector_q], 20'd0,
                            dispatch_producer_q[dispatch_selector_q]};
                        default: begin
                            s_axi_rdata <= 32'd0;
                            s_axi_rresp <= 2'b11;
                        end
                    endcase
                end
            end
            if (read_program_pending_q && program_read_valid) begin
                read_program_pending_q <= 1'b0;
                s_axi_rdata <= program_read_data;
                s_axi_rresp <= 2'b00;
                s_axi_rvalid <= 1'b1;
            end
        end
    end
endmodule

`default_nettype wire
