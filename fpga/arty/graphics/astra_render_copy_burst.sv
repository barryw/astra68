// Copyright (c) 2026 Astra68 contributors
//
// Bounded 64-bit AXI mover for validated same-format copies. Each source
// burst is captured completely before its matching write starts, so chunks
// can be walked from either end without violating memmove overlap semantics.
// Source bytes are realigned onto the destination lanes inside the mover;
// AXI accesses remain aligned, full-width INCR bursts.
`timescale 1ns/1ps
`default_nettype none

module astra_render_copy_burst #(
    parameter integer AXI_ID_WIDTH = 6,
    parameter [AXI_ID_WIDTH-1:0] READ_ID = {AXI_ID_WIDTH{1'b0}},
    parameter [AXI_ID_WIDTH-1:0] WRITE_ID = {AXI_ID_WIDTH{1'b0}}
) (
    input wire clk, input wire reset, input wire start, input wire abort,
    input wire reverse, input wire [31:0] source_address,
    input wire [31:0] destination_address, input wire [31:0] source_pitch,
    input wire [31:0] destination_pitch, input wire [17:0] row_bytes,
    input wire [15:0] row_count, output reg busy, output reg done,
    output reg aborted, output reg read_error, output reg write_error,
    output reg [31:0] fault_detail, output reg [31:0] bytes_copied,
    output wire [AXI_ID_WIDTH-1:0] m_axi_arid,
    output wire [31:0] m_axi_araddr, output wire [7:0] m_axi_arlen,
    output wire [2:0] m_axi_arsize, output wire [1:0] m_axi_arburst,
    output wire [3:0] m_axi_arcache, output wire [2:0] m_axi_arprot,
    output wire [3:0] m_axi_arqos, output wire m_axi_arvalid,
    input wire m_axi_arready, input wire [AXI_ID_WIDTH-1:0] m_axi_rid,
    input wire [63:0] m_axi_rdata, input wire [1:0] m_axi_rresp,
    input wire m_axi_rlast, input wire m_axi_rvalid,
    output wire m_axi_rready, output wire [AXI_ID_WIDTH-1:0] m_axi_awid,
    output wire [31:0] m_axi_awaddr, output wire [7:0] m_axi_awlen,
    output wire [2:0] m_axi_awsize, output wire [1:0] m_axi_awburst,
    output wire [3:0] m_axi_awcache, output wire [2:0] m_axi_awprot,
    output wire [3:0] m_axi_awqos, output wire m_axi_awvalid,
    input wire m_axi_awready, output wire [63:0] m_axi_wdata,
    output wire [7:0] m_axi_wstrb, output wire m_axi_wlast,
    output wire m_axi_wvalid, input wire m_axi_wready,
    input wire [AXI_ID_WIDTH-1:0] m_axi_bid,
    input wire [1:0] m_axi_bresp, input wire m_axi_bvalid,
    output wire m_axi_bready
);
    localparam [4:0] ST_IDLE = 5'd0, ST_PLAN = 5'd1,
        ST_PLAN_SOURCE_LIMIT = 5'd2, ST_PLAN_DESTINATION_LIMIT = 5'd3,
        ST_PLAN_CHUNK = 5'd4, ST_PLAN_START = 5'd5,
        ST_PLAN_ADDRESS = 5'd6,
        ST_PREPARE_WRITE = 5'd7,
        ST_AR = 5'd8, ST_R = 5'd9, ST_R_DRAIN = 5'd10,
        ST_AW = 5'd11, ST_W = 5'd12, ST_B = 5'd13,
        ST_FINISH_OK = 5'd14, ST_FINISH_ABORT = 5'd15,
        ST_FINISH_ERROR = 5'd16, ST_PREPARE_PAIR = 5'd17;

    function automatic [7:0] low_strobes(input [2:0] lanes);
        begin
            low_strobes = lanes == 3'd0 ? 8'hff :
                (9'h001 << lanes) - 9'h001;
        end
    endfunction

    function automatic [12:0] page_bytes(
        input [31:0] address, input reverse_direction
    );
        begin
            if (reverse_direction)
                page_bytes = address[11:0] == 12'd0 ?
                    13'd4096 : {1'b0, address[11:0]};
            else
                page_bytes = 13'd4096 - {1'b0, address[11:0]};
        end
    endfunction

    function automatic [8:0] chunk_capacity(
        input [2:0] source_lane, input [2:0] destination_lane
    );
        begin
            chunk_capacity = 9'd128 - {6'd0,
                source_lane > destination_lane ?
                    source_lane : destination_lane};
        end
    endfunction

    reg [4:0] state;
    reg reverse_q, abort_pending, read_error_seen;
    reg [7:0] chunk_first_strobes_q, chunk_last_strobes_q, write_strobes_q;
    reg [17:0] row_bytes_q, byte_cursor_q, bytes_remaining_q,
        chunk_start_q, remaining_limit_q;
    reg [15:0] rows_remaining_q;
    reg [31:0] source_pitch_q, destination_pitch_q,
        source_row_address_q, destination_row_address_q,
        planning_source_address_q, planning_destination_address_q,
        chunk_source_address_q, chunk_destination_address_q;
    reg [12:0] source_limit_q, source_remaining_limit_q,
        destination_limit_q;
    reg [7:0] chunk_bytes_q;
    reg [8:0] forward_chunk_limit_q;
    reg chunk_finishes_row_q;
    reg [2:0] chunk_source_lane_q, chunk_destination_lane_q,
        alignment_shift_q;
    reg [4:0] read_beats_q, write_beats_q, read_index_q, write_index_q,
        realignment_index_q;
    reg [127:0] realignment_pair_q;
    reg [63:0] realigned_write_data_q;
    (* ram_style = "distributed" *) reg [63:0] data [0:15];

    wire read_bad = m_axi_rid != READ_ID || m_axi_rresp != 2'b00;
    wire read_expected_last = read_index_q + 5'd1 == read_beats_q;
    wire [31:0] cursor_source_address = source_row_address_q +
        {14'd0, byte_cursor_q};
    wire [31:0] cursor_destination_address = destination_row_address_q +
        {14'd0, byte_cursor_q};
    wire [31:0] chunk_source_logical = source_row_address_q +
        {14'd0, chunk_start_q};
    wire [31:0] chunk_destination_logical = destination_row_address_q +
        {14'd0, chunk_start_q};
    wire [2:0] next_chunk_source_lane = chunk_source_logical[2:0] +
        chunk_bytes_q[2:0];
    wire [2:0] next_chunk_destination_lane =
        chunk_destination_logical[2:0] + chunk_bytes_q[2:0];

    reg [127:0] next_realignment_pair;
    always @* begin
        next_realignment_pair = 128'd0;
        if (chunk_source_lane_q >= chunk_destination_lane_q) begin
            if (realignment_index_q < read_beats_q)
                next_realignment_pair[63:0] =
                    data[realignment_index_q[3:0]];
            if (realignment_index_q + 5'd1 < read_beats_q)
                next_realignment_pair[127:64] =
                    data[realignment_index_q[3:0] + 4'd1];
        end else begin
            if (realignment_index_q != 5'd0 &&
                realignment_index_q - 5'd1 < read_beats_q)
                next_realignment_pair[63:0] =
                    data[realignment_index_q[3:0] - 4'd1];
            if (realignment_index_q < read_beats_q)
                next_realignment_pair[127:64] =
                    data[realignment_index_q[3:0]];
        end
    end

    assign m_axi_arid = READ_ID;
    assign m_axi_araddr = chunk_source_address_q;
    assign m_axi_arlen = {3'd0, read_beats_q} - 8'd1;
    assign m_axi_arsize = 3'b011;
    assign m_axi_arburst = 2'b01;
    assign m_axi_arcache = 4'b0011;
    assign m_axi_arprot = 3'b000;
    assign m_axi_arqos = 4'b0000;
    assign m_axi_arvalid = state == ST_AR;
    assign m_axi_rready = state == ST_R || state == ST_R_DRAIN;
    assign m_axi_awid = WRITE_ID;
    assign m_axi_awaddr = chunk_destination_address_q;
    assign m_axi_awlen = {3'd0, write_beats_q} - 8'd1;
    assign m_axi_awsize = 3'b011;
    assign m_axi_awburst = 2'b01;
    assign m_axi_awcache = 4'b0011;
    assign m_axi_awprot = 3'b000;
    assign m_axi_awqos = 4'b0000;
    assign m_axi_awvalid = state == ST_AW;
    assign m_axi_wdata = chunk_source_lane_q == chunk_destination_lane_q ?
        data[write_index_q[3:0]] : realigned_write_data_q;
    assign m_axi_wstrb = write_strobes_q;
    assign m_axi_wlast = write_index_q + 5'd1 == write_beats_q;
    assign m_axi_wvalid = state == ST_W;
    assign m_axi_bready = state == ST_B;

    always @(posedge clk) begin
        if (reset) begin
            state <= ST_IDLE;
            reverse_q <= 1'b0;
            abort_pending <= 1'b0;
            read_error_seen <= 1'b0;
            chunk_first_strobes_q <= 8'hff;
            chunk_last_strobes_q <= 8'hff;
            write_strobes_q <= 8'hff;
            row_bytes_q <= 18'd0;
            rows_remaining_q <= 16'd0;
            source_pitch_q <= 32'd0;
            destination_pitch_q <= 32'd0;
            source_row_address_q <= 32'd0;
            destination_row_address_q <= 32'd0;
            byte_cursor_q <= 18'd0;
            bytes_remaining_q <= 18'd0;
            chunk_start_q <= 18'd0;
            chunk_bytes_q <= 8'd0;
            forward_chunk_limit_q <= 9'd0;
            remaining_limit_q <= 18'd0;
            source_limit_q <= 13'd0;
            source_remaining_limit_q <= 13'd0;
            destination_limit_q <= 13'd0;
            chunk_finishes_row_q <= 1'b0;
            planning_source_address_q <= 32'd0;
            planning_destination_address_q <= 32'd0;
            chunk_source_address_q <= 32'd0;
            chunk_destination_address_q <= 32'd0;
            chunk_source_lane_q <= 3'd0;
            chunk_destination_lane_q <= 3'd0;
            alignment_shift_q <= 3'd0;
            read_beats_q <= 5'd0;
            write_beats_q <= 5'd0;
            read_index_q <= 5'd0;
            write_index_q <= 5'd0;
            realignment_index_q <= 5'd0;
            realignment_pair_q <= 128'd0;
            realigned_write_data_q <= 64'd0;
            busy <= 1'b0;
            done <= 1'b0;
            aborted <= 1'b0;
            read_error <= 1'b0;
            write_error <= 1'b0;
            fault_detail <= 32'd0;
            bytes_copied <= 32'd0;
        end else begin
            done <= 1'b0;
            if (abort && busy)
                abort_pending <= 1'b1;
            case (state)
                ST_IDLE: if (start) begin
                    reverse_q <= reverse;
                    abort_pending <= 1'b0;
                    read_error_seen <= 1'b0;
                    row_bytes_q <= row_bytes;
                    rows_remaining_q <= row_count;
                    source_pitch_q <= source_pitch;
                    destination_pitch_q <= destination_pitch;
                    source_row_address_q <= source_address;
                    destination_row_address_q <= destination_address;
                    byte_cursor_q <= reverse ? row_bytes : 18'd0;
                    bytes_remaining_q <= row_bytes;
                    forward_chunk_limit_q <= chunk_capacity(
                        source_address[2:0], destination_address[2:0]);
                    busy <= 1'b1;
                    aborted <= 1'b0;
                    read_error <= 1'b0;
                    write_error <= 1'b0;
                    fault_detail <= 32'd0;
                    bytes_copied <= 32'd0;
                    if (row_bytes == 18'd0 || row_count == 16'd0 ||
                        source_pitch[2:0] != 3'd0 ||
                        destination_pitch[2:0] != 3'd0) begin
                        read_error <= 1'b1;
                        fault_detail <= 32'h00010000;
                        state <= ST_FINISH_ERROR;
                    end else state <= ST_PLAN;
                end

                ST_PLAN: if (abort_pending || abort) begin
                    state <= ST_FINISH_ABORT;
                end else begin
                    planning_source_address_q <= cursor_source_address;
                    planning_destination_address_q <=
                        cursor_destination_address;
                    if (reverse_q)
                        remaining_limit_q <= bytes_remaining_q > 18'd120 ?
                            18'd120 : bytes_remaining_q;
                    else
                        remaining_limit_q <= bytes_remaining_q >
                            {9'd0, forward_chunk_limit_q} ?
                                {9'd0, forward_chunk_limit_q} :
                                bytes_remaining_q;
                    state <= ST_PLAN_SOURCE_LIMIT;
                end

                ST_PLAN_SOURCE_LIMIT: begin
                    source_limit_q <= page_bytes(
                        planning_source_address_q, reverse_q);
                    state <= ST_PLAN_DESTINATION_LIMIT;
                end

                ST_PLAN_DESTINATION_LIMIT: begin
                    source_remaining_limit_q <=
                        source_limit_q < remaining_limit_q ?
                            source_limit_q : remaining_limit_q[12:0];
                    destination_limit_q <= page_bytes(
                        planning_destination_address_q, reverse_q);
                    state <= ST_PLAN_CHUNK;
                end

                ST_PLAN_CHUNK: begin
                    chunk_bytes_q <=
                        destination_limit_q < source_remaining_limit_q ?
                            destination_limit_q[7:0] :
                            source_remaining_limit_q[7:0];
                    state <= ST_PLAN_START;
                end

                ST_PLAN_START: begin
                    chunk_start_q <= reverse_q ?
                        byte_cursor_q - {10'd0, chunk_bytes_q} : byte_cursor_q;
                    chunk_finishes_row_q <=
                        bytes_remaining_q == {10'd0, chunk_bytes_q};
                    state <= ST_PLAN_ADDRESS;
                end

                ST_PLAN_ADDRESS: begin
                    chunk_source_address_q <=
                        {chunk_source_logical[31:3], 3'b000};
                    chunk_destination_address_q <=
                        {chunk_destination_logical[31:3], 3'b000};
                    chunk_source_lane_q <= chunk_source_logical[2:0];
                    chunk_destination_lane_q <=
                        chunk_destination_logical[2:0];
                    alignment_shift_q <= chunk_source_logical[2:0] -
                        chunk_destination_logical[2:0];
                    read_beats_q <=
                        ({15'd0, chunk_source_logical[2:0]} +
                         {10'd0, chunk_bytes_q} + 18'd7) >> 3;
                    write_beats_q <=
                        ({15'd0, chunk_destination_logical[2:0]} +
                         {10'd0, chunk_bytes_q} + 18'd7) >> 3;
                    chunk_first_strobes_q <=
                        8'hff << chunk_destination_logical[2:0];
                    chunk_last_strobes_q <= low_strobes(
                        chunk_destination_logical[2:0] +
                        chunk_bytes_q[2:0]);
                    read_index_q <= 5'd0;
                    read_error_seen <= 1'b0;
                    state <= ST_AR;
                end

                ST_AR: if (abort_pending || abort)
                    state <= ST_FINISH_ABORT;
                else if (m_axi_arready) state <= ST_R;

                ST_R: if (m_axi_rvalid) begin
                    data[read_index_q[3:0]] <= m_axi_rdata;
                    if (read_bad) begin
                        read_error <= 1'b1;
                        read_error_seen <= 1'b1;
                        if (!read_error_seen)
                            fault_detail <= {16'h0002,
                                {{(8-AXI_ID_WIDTH){1'b0}}, m_axi_rid},
                                5'd0, m_axi_rlast, m_axi_rresp};
                    end
                    if (m_axi_rlast) begin
                        if (!read_expected_last) begin
                            read_error <= 1'b1;
                            fault_detail <= 32'h00020001;
                            state <= ST_FINISH_ERROR;
                        end else if (read_bad || read_error_seen)
                            state <= ST_FINISH_ERROR;
                        else if (abort_pending || abort)
                            state <= ST_FINISH_ABORT;
                        else begin
                            write_index_q <= 5'd0;
                            realignment_index_q <= 5'd0;
                            write_strobes_q <= write_beats_q == 5'd1 ?
                                chunk_first_strobes_q &
                                    chunk_last_strobes_q :
                                chunk_first_strobes_q;
                            state <= chunk_source_lane_q ==
                                chunk_destination_lane_q ?
                                    ST_AW : ST_PREPARE_PAIR;
                        end
                    end else if (read_expected_last) begin
                        read_error <= 1'b1;
                        fault_detail <= 32'h00020002;
                        state <= ST_R_DRAIN;
                    end else read_index_q <= read_index_q + 5'd1;
                end

                ST_R_DRAIN: if (m_axi_rvalid && m_axi_rlast)
                    state <= ST_FINISH_ERROR;

                ST_PREPARE_PAIR: begin
                    realignment_pair_q <= next_realignment_pair;
                    realignment_index_q <= realignment_index_q + 5'd1;
                    state <= ST_PREPARE_WRITE;
                end

                ST_PREPARE_WRITE: begin
                    realigned_write_data_q <= realignment_pair_q >>
                        {alignment_shift_q, 3'b000};
                    realignment_pair_q <= next_realignment_pair;
                    realignment_index_q <= realignment_index_q + 5'd1;
                    state <= ST_AW;
                end

                ST_AW: if (abort_pending || abort)
                    state <= ST_FINISH_ABORT;
                else if (m_axi_awready) state <= ST_W;

                ST_W: if (m_axi_wready) begin
                    if (m_axi_wlast) state <= ST_B;
                    else begin
                        write_index_q <= write_index_q + 5'd1;
                        if (chunk_source_lane_q != chunk_destination_lane_q) begin
                            realigned_write_data_q <= realignment_pair_q >>
                                {alignment_shift_q, 3'b000};
                            realignment_pair_q <= next_realignment_pair;
                            realignment_index_q <=
                                realignment_index_q + 5'd1;
                        end
                        write_strobes_q <=
                            write_index_q + 5'd2 == write_beats_q ?
                                chunk_last_strobes_q : 8'hff;
                    end
                end

                ST_B: if (m_axi_bvalid) begin
                    if (m_axi_bid != WRITE_ID || m_axi_bresp != 2'b00) begin
                        write_error <= 1'b1;
                        fault_detail <= {16'h0003,
                            {{(8-AXI_ID_WIDTH){1'b0}}, m_axi_bid},
                            6'd0, m_axi_bresp};
                        state <= ST_FINISH_ERROR;
                    end else if (abort_pending || abort)
                        state <= ST_FINISH_ABORT;
                    else if (chunk_finishes_row_q) begin
                        bytes_copied <= bytes_copied + row_bytes_q;
                        if (rows_remaining_q == 16'd1)
                            state <= ST_FINISH_OK;
                        else begin
                            rows_remaining_q <= rows_remaining_q - 16'd1;
                            source_row_address_q <= reverse_q ?
                                source_row_address_q - source_pitch_q :
                                source_row_address_q + source_pitch_q;
                            destination_row_address_q <= reverse_q ?
                                destination_row_address_q -
                                    destination_pitch_q :
                                destination_row_address_q +
                                    destination_pitch_q;
                            byte_cursor_q <= reverse_q ? row_bytes_q : 18'd0;
                            bytes_remaining_q <= row_bytes_q;
                            forward_chunk_limit_q <= chunk_capacity(
                                source_row_address_q[2:0],
                                destination_row_address_q[2:0]);
                            state <= ST_PLAN;
                        end
                    end else begin
                        bytes_remaining_q <= bytes_remaining_q -
                            {10'd0, chunk_bytes_q};
                        byte_cursor_q <= reverse_q ?
                            byte_cursor_q - {10'd0, chunk_bytes_q} :
                            byte_cursor_q + {10'd0, chunk_bytes_q};
                        forward_chunk_limit_q <= chunk_capacity(
                            next_chunk_source_lane,
                            next_chunk_destination_lane);
                        state <= ST_PLAN;
                    end
                end

                ST_FINISH_OK: begin
                    busy <= 1'b0;
                    done <= 1'b1;
                    state <= ST_IDLE;
                end
                ST_FINISH_ABORT: begin
                    busy <= 1'b0;
                    done <= 1'b1;
                    aborted <= 1'b1;
                    state <= ST_IDLE;
                end
                ST_FINISH_ERROR: begin
                    busy <= 1'b0;
                    done <= 1'b1;
                    state <= ST_IDLE;
                end
                default: begin
                    busy <= 1'b0;
                    done <= 1'b1;
                    state <= ST_IDLE;
                end
            endcase
        end
    end
endmodule

`default_nettype wire
