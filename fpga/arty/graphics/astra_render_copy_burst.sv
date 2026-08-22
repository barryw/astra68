// Copyright (c) 2026 Astra68 contributors
//
// Bounded 64-bit AXI mover for validated same-format copies. Each source
// burst is captured completely before its matching write starts, so chunks
// can be walked from either end without violating memmove overlap semantics.
`timescale 1ns/1ps
`default_nettype none

module astra_render_copy_burst #(
    parameter integer AXI_ID_WIDTH = 6,
    parameter [AXI_ID_WIDTH-1:0] READ_ID = {AXI_ID_WIDTH{1'b0}},
    parameter [AXI_ID_WIDTH-1:0] WRITE_ID = {AXI_ID_WIDTH{1'b0}}
) (
    input  wire                         clk,
    input  wire                         reset,
    input  wire                         start,
    input  wire                         abort,
    input  wire                         reverse,
    input  wire [31:0]                  source_address,
    input  wire [31:0]                  destination_address,
    input  wire [31:0]                  source_pitch,
    input  wire [31:0]                  destination_pitch,
    input  wire [17:0]                  row_bytes,
    input  wire [15:0]                  row_count,
    output reg                          busy,
    output reg                          done,
    output reg                          aborted,
    output reg                          read_error,
    output reg                          write_error,
    output reg  [31:0]                  fault_detail,
    output reg  [31:0]                  bytes_copied,

    output wire [AXI_ID_WIDTH-1:0]      m_axi_arid,
    output wire [31:0]                  m_axi_araddr,
    output wire [7:0]                   m_axi_arlen,
    output wire [2:0]                   m_axi_arsize,
    output wire [1:0]                   m_axi_arburst,
    output wire [3:0]                   m_axi_arcache,
    output wire [2:0]                   m_axi_arprot,
    output wire [3:0]                   m_axi_arqos,
    output wire                         m_axi_arvalid,
    input  wire                         m_axi_arready,
    input  wire [AXI_ID_WIDTH-1:0]      m_axi_rid,
    input  wire [63:0]                  m_axi_rdata,
    input  wire [1:0]                   m_axi_rresp,
    input  wire                         m_axi_rlast,
    input  wire                         m_axi_rvalid,
    output wire                         m_axi_rready,

    output wire [AXI_ID_WIDTH-1:0]      m_axi_awid,
    output wire [31:0]                  m_axi_awaddr,
    output wire [7:0]                   m_axi_awlen,
    output wire [2:0]                   m_axi_awsize,
    output wire [1:0]                   m_axi_awburst,
    output wire [3:0]                   m_axi_awcache,
    output wire [2:0]                   m_axi_awprot,
    output wire [3:0]                   m_axi_awqos,
    output wire                         m_axi_awvalid,
    input  wire                         m_axi_awready,
    output wire [63:0]                  m_axi_wdata,
    output wire [7:0]                   m_axi_wstrb,
    output wire                         m_axi_wlast,
    output wire                         m_axi_wvalid,
    input  wire                         m_axi_wready,
    input  wire [AXI_ID_WIDTH-1:0]      m_axi_bid,
    input  wire [1:0]                   m_axi_bresp,
    input  wire                         m_axi_bvalid,
    output wire                         m_axi_bready
);
    localparam [3:0] ST_IDLE = 4'd0;
    localparam [3:0] ST_PLAN = 4'd1;
    localparam [3:0] ST_AR = 4'd2;
    localparam [3:0] ST_R = 4'd3;
    localparam [3:0] ST_R_DRAIN = 4'd4;
    localparam [3:0] ST_AW = 4'd5;
    localparam [3:0] ST_W = 4'd6;
    localparam [3:0] ST_B = 4'd7;
    localparam [3:0] ST_FINISH_OK = 4'd8;
    localparam [3:0] ST_FINISH_ABORT = 4'd9;
    localparam [3:0] ST_FINISH_ERROR = 4'd10;
    localparam [3:0] ST_PLAN_SOURCE_LIMIT = 4'd11;
    localparam [3:0] ST_PLAN_DESTINATION_LIMIT = 4'd12;
    localparam [3:0] ST_PLAN_START = 4'd13;
    localparam [3:0] ST_PLAN_ADDRESS = 4'd14;
    localparam [3:0] ST_PLAN_CHUNK = 4'd15;

    function automatic [7:0] low_strobes(input [2:0] lanes);
        begin
            low_strobes = lanes == 3'd0 ? 8'hff :
                (9'h001 << lanes) - 9'h001;
        end
    endfunction

    function automatic [4:0] page_limit(
        input [31:0] address,
        input reverse_direction
    );
        begin
            if (reverse_direction)
                page_limit = address[11:7] == 5'd0 ?
                    {1'b0, address[6:3]} + 5'd1 : 5'd16;
            else
                page_limit = address[11:7] == 5'd31 ?
                    5'd16 - {1'b0, address[6:3]} : 5'd16;
        end
    endfunction

    reg [3:0] state;
    reg reverse_q;
    reg abort_pending;
    reg read_error_seen;
    reg [7:0] first_strobes_q;
    reg [7:0] last_strobes_q;
    reg [7:0] chunk_first_strobes_q;
    reg [7:0] chunk_last_strobes_q;
    reg [7:0] write_strobes_q;
    reg [17:0] row_bytes_q;
    reg [12:0] row_beats_q;
    reg [15:0] rows_remaining_q;
    reg [31:0] source_pitch_q;
    reg [31:0] destination_pitch_q;
    reg [31:0] source_row_beat_q;
    reg [31:0] destination_row_beat_q;
    reg [12:0] beat_cursor_q;
    reg [12:0] chunk_start_q;
    reg [4:0] chunk_beats_q;
    reg [4:0] remaining_limit_q;
    reg [4:0] source_limit_q;
    reg [4:0] source_remaining_limit_q;
    reg [4:0] destination_limit_q;
    reg chunk_finishes_row_q;
    reg [31:0] planning_source_address_q;
    reg [31:0] planning_destination_address_q;
    reg [31:0] chunk_source_address_q;
    reg [31:0] chunk_destination_address_q;
    reg [4:0] read_index_q;
    reg [4:0] write_index_q;
    (* ram_style = "distributed" *) reg [63:0] data [0:15];

    wire [12:0] planning_end_index = reverse_q ?
        beat_cursor_q - 13'd1 : beat_cursor_q;
    wire [12:0] planning_remaining = reverse_q ? beat_cursor_q :
        row_beats_q - beat_cursor_q;
    wire read_bad = m_axi_rid != READ_ID || m_axi_rresp != 2'b00;
    wire read_expected_last = read_index_q + 5'd1 == chunk_beats_q;

    assign m_axi_arid = READ_ID;
    assign m_axi_araddr = chunk_source_address_q;
    assign m_axi_arlen = {3'd0, chunk_beats_q} - 8'd1;
    assign m_axi_arsize = 3'b011;
    assign m_axi_arburst = 2'b01;
    assign m_axi_arcache = 4'b0011;
    assign m_axi_arprot = 3'b000;
    assign m_axi_arqos = 4'b0000;
    assign m_axi_arvalid = state == ST_AR;
    assign m_axi_rready = state == ST_R || state == ST_R_DRAIN;

    assign m_axi_awid = WRITE_ID;
    assign m_axi_awaddr = chunk_destination_address_q;
    assign m_axi_awlen = {3'd0, chunk_beats_q} - 8'd1;
    assign m_axi_awsize = 3'b011;
    assign m_axi_awburst = 2'b01;
    assign m_axi_awcache = 4'b0011;
    assign m_axi_awprot = 3'b000;
    assign m_axi_awqos = 4'b0000;
    assign m_axi_awvalid = state == ST_AW;
    assign m_axi_wdata = data[write_index_q[3:0]];
    assign m_axi_wstrb = write_strobes_q;
    assign m_axi_wlast = write_index_q + 5'd1 == chunk_beats_q;
    assign m_axi_wvalid = state == ST_W;
    assign m_axi_bready = state == ST_B;

    always @(posedge clk) begin
        if (reset) begin
            state <= ST_IDLE;
            reverse_q <= 1'b0;
            abort_pending <= 1'b0;
            read_error_seen <= 1'b0;
            first_strobes_q <= 8'hff;
            last_strobes_q <= 8'hff;
            chunk_first_strobes_q <= 8'hff;
            chunk_last_strobes_q <= 8'hff;
            write_strobes_q <= 8'hff;
            row_bytes_q <= 18'd0;
            row_beats_q <= 13'd0;
            rows_remaining_q <= 16'd0;
            source_pitch_q <= 32'd0;
            destination_pitch_q <= 32'd0;
            source_row_beat_q <= 32'd0;
            destination_row_beat_q <= 32'd0;
            beat_cursor_q <= 13'd0;
            chunk_start_q <= 13'd0;
            chunk_beats_q <= 5'd0;
            remaining_limit_q <= 5'd0;
            source_limit_q <= 5'd0;
            source_remaining_limit_q <= 5'd0;
            destination_limit_q <= 5'd0;
            chunk_finishes_row_q <= 1'b0;
            planning_source_address_q <= 32'd0;
            planning_destination_address_q <= 32'd0;
            chunk_source_address_q <= 32'd0;
            chunk_destination_address_q <= 32'd0;
            read_index_q <= 5'd0;
            write_index_q <= 5'd0;
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
                ST_IDLE: begin
                    if (start) begin
                        reverse_q <= reverse;
                        abort_pending <= 1'b0;
                        read_error_seen <= 1'b0;
                        first_strobes_q <= 8'hff << source_address[2:0];
                        last_strobes_q <= low_strobes(
                            source_address[2:0] + row_bytes[2:0]);
                        row_bytes_q <= row_bytes;
                        row_beats_q <=
                            ({15'd0, source_address[2:0]} + row_bytes +
                             18'd7) >> 3;
                        rows_remaining_q <= row_count;
                        source_pitch_q <= source_pitch;
                        destination_pitch_q <= destination_pitch;
                        source_row_beat_q <=
                            {source_address[31:3], 3'b000};
                        destination_row_beat_q <=
                            {destination_address[31:3], 3'b000};
                        beat_cursor_q <= reverse ?
                            (({15'd0, source_address[2:0]} + row_bytes +
                              18'd7) >> 3) : 13'd0;
                        busy <= 1'b1;
                        aborted <= 1'b0;
                        read_error <= 1'b0;
                        write_error <= 1'b0;
                        fault_detail <= 32'd0;
                        bytes_copied <= 32'd0;
                        if (row_bytes == 18'd0 || row_count == 16'd0 ||
                            source_address[2:0] !=
                                destination_address[2:0] ||
                            source_pitch[2:0] != 3'd0 ||
                            destination_pitch[2:0] != 3'd0) begin
                            read_error <= 1'b1;
                            fault_detail <= 32'h00010000;
                            state <= ST_FINISH_ERROR;
                        end else begin
                            state <= ST_PLAN;
                        end
                    end
                end

                ST_PLAN: begin
                    if (abort_pending || abort) begin
                        state <= ST_FINISH_ABORT;
                    end else begin
                        planning_source_address_q <= source_row_beat_q +
                            ({19'd0, planning_end_index} << 3);
                        planning_destination_address_q <=
                            destination_row_beat_q +
                            ({19'd0, planning_end_index} << 3);
                        remaining_limit_q <= planning_remaining > 13'd16 ?
                            5'd16 : planning_remaining[4:0];
                        state <= ST_PLAN_SOURCE_LIMIT;
                    end
                end

                ST_PLAN_SOURCE_LIMIT: begin
                    source_limit_q <= page_limit(
                        planning_source_address_q, reverse_q);
                    state <= ST_PLAN_DESTINATION_LIMIT;
                end

                ST_PLAN_DESTINATION_LIMIT: begin
                    source_remaining_limit_q <=
                        source_limit_q < remaining_limit_q ?
                            source_limit_q : remaining_limit_q;
                    destination_limit_q <= page_limit(
                        planning_destination_address_q, reverse_q);
                    state <= ST_PLAN_CHUNK;
                end

                ST_PLAN_CHUNK: begin
                    chunk_beats_q <=
                        destination_limit_q < source_remaining_limit_q ?
                            destination_limit_q : source_remaining_limit_q;
                    state <= ST_PLAN_START;
                end

                ST_PLAN_START: begin
                    chunk_start_q <= reverse_q ?
                        beat_cursor_q - {8'd0, chunk_beats_q} :
                        beat_cursor_q;
                    chunk_finishes_row_q <= reverse_q ?
                        beat_cursor_q == {8'd0, chunk_beats_q} :
                        beat_cursor_q + {8'd0, chunk_beats_q} == row_beats_q;
                    state <= ST_PLAN_ADDRESS;
                end

                ST_PLAN_ADDRESS: begin
                    chunk_source_address_q <= source_row_beat_q +
                        ({19'd0, chunk_start_q} << 3);
                    chunk_destination_address_q <= destination_row_beat_q +
                        ({19'd0, chunk_start_q} << 3);
                    read_index_q <= 5'd0;
                    read_error_seen <= 1'b0;
                    chunk_first_strobes_q <= chunk_start_q == 13'd0 ?
                        first_strobes_q : 8'hff;
                    chunk_last_strobes_q <=
                        chunk_start_q + {8'd0, chunk_beats_q} == row_beats_q ?
                            last_strobes_q : 8'hff;
                    state <= ST_AR;
                end

                ST_AR: begin
                    if (abort_pending || abort) begin
                        state <= ST_FINISH_ABORT;
                    end else if (m_axi_arready) begin
                        state <= ST_R;
                    end
                end

                ST_R: begin
                    if (m_axi_rvalid) begin
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
                            end else if (read_bad || read_error_seen) begin
                                state <= ST_FINISH_ERROR;
                            end else if (abort_pending || abort) begin
                                state <= ST_FINISH_ABORT;
                            end else begin
                                write_index_q <= 5'd0;
                                write_strobes_q <= chunk_beats_q == 5'd1 ?
                                    chunk_first_strobes_q &
                                        chunk_last_strobes_q :
                                    chunk_first_strobes_q;
                                state <= ST_AW;
                            end
                        end else if (read_expected_last) begin
                            read_error <= 1'b1;
                            fault_detail <= 32'h00020002;
                            state <= ST_R_DRAIN;
                        end else begin
                            read_index_q <= read_index_q + 5'd1;
                        end
                    end
                end

                ST_R_DRAIN: begin
                    if (m_axi_rvalid && m_axi_rlast)
                        state <= ST_FINISH_ERROR;
                end

                ST_AW: begin
                    if (abort_pending || abort) begin
                        state <= ST_FINISH_ABORT;
                    end else if (m_axi_awready) begin
                        state <= ST_W;
                    end
                end

                ST_W: begin
                    if (m_axi_wready) begin
                        if (m_axi_wlast)
                            state <= ST_B;
                        else begin
                            write_index_q <= write_index_q + 5'd1;
                            write_strobes_q <=
                                write_index_q + 5'd2 == chunk_beats_q ?
                                    chunk_last_strobes_q : 8'hff;
                        end
                    end
                end

                ST_B: begin
                    if (m_axi_bvalid) begin
                        if (m_axi_bid != WRITE_ID || m_axi_bresp != 2'b00) begin
                            write_error <= 1'b1;
                            fault_detail <= {16'h0003,
                                {{(8-AXI_ID_WIDTH){1'b0}}, m_axi_bid},
                                6'd0, m_axi_bresp};
                            state <= ST_FINISH_ERROR;
                        end else if (abort_pending || abort) begin
                            state <= ST_FINISH_ABORT;
                        end else if (chunk_finishes_row_q) begin
                            bytes_copied <= bytes_copied + row_bytes_q;
                            if (rows_remaining_q == 16'd1) begin
                                state <= ST_FINISH_OK;
                            end else begin
                                rows_remaining_q <= rows_remaining_q - 16'd1;
                                source_row_beat_q <= reverse_q ?
                                    source_row_beat_q - source_pitch_q :
                                    source_row_beat_q + source_pitch_q;
                                destination_row_beat_q <= reverse_q ?
                                    destination_row_beat_q -
                                        destination_pitch_q :
                                    destination_row_beat_q +
                                        destination_pitch_q;
                                beat_cursor_q <= reverse_q ?
                                    row_beats_q : 13'd0;
                                state <= ST_PLAN;
                            end
                        end else begin
                            beat_cursor_q <= reverse_q ?
                                beat_cursor_q - {8'd0, chunk_beats_q} :
                                beat_cursor_q + {8'd0, chunk_beats_q};
                            state <= ST_PLAN;
                        end
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
