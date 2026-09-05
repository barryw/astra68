// Copyright (c) 2026 Astra68 contributors
//
// Shared renderer pixel writer. Producers submit validated physical pixel
// addresses and canonical big-endian pixel values. Adjacent pixels are merged
// into 64-bit strobed beats before bounded AXI write issue.
`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module astra_render_pixel_writer #(
    parameter integer AXI_ID_WIDTH = 6,
    parameter [AXI_ID_WIDTH-1:0] AXI_ID = {AXI_ID_WIDTH{1'b0}},
    parameter integer FIFO_DEPTH = 16,
    parameter integer MAX_OUTSTANDING = 8
) (
    input  wire                         clk,
    input  wire                         reset,
    input  wire                         start,
    input  wire                         abort,
    input  wire                         flush,
    output wire                         flush_ready,
    input  wire                         barrier,
    output wire                         barrier_ready,
    output reg                          barrier_done,
    input  wire                         pixel_valid,
    output wire                         pixel_ready,
    input  wire [31:0]                  pixel_address,
    input  wire [7:0]                   pixel_format,
    input  wire [31:0]                  pixel_value,
    output reg                          busy,
    output reg                          done,
    output reg                          aborted,
    output reg                          write_error,
    output reg  [31:0]                  fault_detail,
    output reg  [31:0]                  pixels_accepted,
    output reg  [31:0]                  bytes_written,

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
    localparam integer FIFO_POINTER_WIDTH = $clog2(FIFO_DEPTH);
    localparam integer FIFO_COUNT_WIDTH = $clog2(FIFO_DEPTH + 1);
    localparam integer OUTSTANDING_WIDTH = $clog2(MAX_OUTSTANDING + 1);

    function automatic [63:0] byte_mask(input [7:0] strobes);
        integer lane;
        begin
            byte_mask = 64'd0;
            for (lane = 0; lane < 8; lane = lane + 1)
                if (strobes[lane])
                    byte_mask[lane * 8 +: 8] = 8'hff;
        end
    endfunction

    function automatic [3:0] strobe_count(input [7:0] strobes);
        integer lane;
        begin
            strobe_count = 4'd0;
            for (lane = 0; lane < 8; lane = lane + 1)
                strobe_count = strobe_count + strobes[lane];
        end
    endfunction

    reg pack_valid;
    (* extract_enable = "no" *)
    reg [31:0] pack_address;
    reg [63:0] pack_data;
    reg [7:0] pack_strobes;
    reg [31:0] pixel_stage_address [0:1];
    reg [63:0] pixel_stage_data [0:1];
    reg [7:0] pixel_stage_strobes [0:1];
    reg [1:0] pixel_stage_count;
    reg ingress_valid;
    reg [31:0] ingress_pixel_address;
    reg [7:0] ingress_pixel_format;
    reg [31:0] ingress_pixel_value;
    reg flush_pending;
    reg barrier_pending;
    reg abort_pending;
    reg start_q;

    reg fifo_stage_valid;
    reg [31:0] fifo_stage_address;
    reg [63:0] fifo_stage_data;
    reg [7:0] fifo_stage_strobes;

    reg [31:0] fifo_address [0:FIFO_DEPTH-1];
    (* ram_style = "distributed", ramstyle = "MLAB" *)
    reg [63:0] fifo_data [0:FIFO_DEPTH-1];
    reg [7:0] fifo_strobes [0:FIFO_DEPTH-1];
    reg [FIFO_POINTER_WIDTH-1:0] fifo_write_pointer;
    reg [FIFO_POINTER_WIDTH-1:0] fifo_read_pointer;
    reg [FIFO_COUNT_WIDTH-1:0] fifo_count;

    reg issue_valid;
    reg issue_aw_sent;
    reg issue_w_sent;
    reg [31:0] issue_address;
    reg [63:0] issue_data;
    reg [7:0] issue_strobes;
    reg [OUTSTANDING_WIDTH-1:0] outstanding_count;

    reg [7:0] ingress_strobes;
    reg [63:0] ingress_data;
    wire [31:0] ingress_beat_address =
        {ingress_pixel_address[31:3], 3'b000};

    always @* begin
        ingress_strobes = 8'd0;
        ingress_data = 64'd0;
        case (ingress_pixel_format)
            `ASTRA_RENDER_FORMAT_INDEX8: begin
                ingress_strobes =
                    8'b00000001 << ingress_pixel_address[2:0];
                ingress_data = {56'd0, ingress_pixel_value[7:0]} <<
                               (ingress_pixel_address[2:0] * 8);
            end
            `ASTRA_RENDER_FORMAT_RGB565: begin
                ingress_strobes =
                    8'b00000011 << ingress_pixel_address[2:0];
                ingress_data = {48'd0, ingress_pixel_value[7:0],
                                 ingress_pixel_value[15:8]} <<
                               (ingress_pixel_address[2:0] * 8);
            end
            default: begin
                ingress_strobes =
                    8'b00001111 << ingress_pixel_address[2:0];
                ingress_data = {32'd0, ingress_pixel_value[7:0],
                                 ingress_pixel_value[15:8],
                                 ingress_pixel_value[23:16],
                                 ingress_pixel_value[31:24]} <<
                               (ingress_pixel_address[2:0] * 8);
            end
        endcase
    end

    wire pixel_stage_valid = pixel_stage_count != 2'd0;
    wire [31:0] staged_pixel_address = pixel_stage_address[0];
    wire [63:0] staged_pixel_data = pixel_stage_data[0];
    wire [7:0] staged_pixel_strobes = pixel_stage_strobes[0];
    wire [28:0] staged_address_difference =
        pack_address[31:3] ^ staged_pixel_address[31:3];
    wire staged_address_diff0 = |staged_address_difference[5:0];
    wire staged_address_diff1 = |staged_address_difference[11:6];
    wire staged_address_diff2 = |staged_address_difference[17:12];
    wire staged_address_diff3 = |staged_address_difference[23:18];
    wire staged_address_diff4 = |staged_address_difference[28:24];
    wire staged_pixel_changes_beat = pack_valid &&
        (staged_address_diff0 || staged_address_diff1 ||
         staged_address_diff2 || staged_address_diff3 ||
         staged_address_diff4);
    wire aw_accept = m_axi_awvalid && m_axi_awready;
    wire w_accept = m_axi_wvalid && m_axi_wready;
    wire issue_complete = issue_valid &&
        (issue_aw_sent || aw_accept) && (issue_w_sent || w_accept);
    // Use only registered queue occupancy for admission. Looking through an
    // AXI handshake here makes downstream READY timing part of the pixel
    // staging clock-enable path. A full queue therefore releases its staged
    // beat on the cycle after the completed issue frees an entry.
    wire fifo_space = fifo_count < FIFO_DEPTH;
    wire fifo_stage_drain = fifo_stage_valid && fifo_space;
    wire fifo_stage_ready = !fifo_stage_valid || fifo_stage_drain;
    wire pixel_stage_drain = pixel_stage_valid && busy &&
        !flush_pending && !abort_pending &&
        fifo_stage_ready;
    wire pixel_stage_ready = pixel_stage_count != 2'd2;
    wire ingress_drain = ingress_valid && busy && !flush_pending &&
        !barrier_pending && !abort_pending && pixel_stage_ready;
    wire ingress_ready = !ingress_valid || (busy && pixel_stage_ready);
    // Producers stop VALID before flush/barrier/abort. Keep READY describing
    // only registered ingress capacity so writer policy cannot feed back into
    // every renderer FSM's state decode.
    assign pixel_ready = busy && ingress_ready;
    wire ingress_accept = pixel_valid && pixel_ready;
    wire pixel_accept = ingress_drain;
    assign flush_ready = busy && !flush_pending && !barrier_pending &&
        !abort_pending &&
        !pixel_valid && !ingress_valid && !pixel_stage_valid &&
        (!pack_valid || fifo_stage_ready);
    wire flush_accept = flush && flush_ready;
    assign barrier_ready = busy && !flush_pending && !barrier_pending &&
        !abort_pending && !pixel_valid && !ingress_valid &&
        !pixel_stage_valid &&
        (!pack_valid || fifo_stage_ready);
    wire barrier_accept = barrier && barrier_ready;
    wire fifo_stage_load =
        (pixel_stage_drain && staged_pixel_changes_beat) ||
        ((flush_accept || barrier_accept) && pack_valid);
    wire fifo_push = fifo_stage_drain;
    wire [31:0] fifo_push_address = fifo_stage_address;
    wire [63:0] fifo_push_data = fifo_stage_data;
    wire [7:0] fifo_push_strobes = fifo_stage_strobes;
    wire b_accept = m_axi_bvalid && m_axi_bready;

    assign m_axi_awid = AXI_ID;
    assign m_axi_awaddr = issue_address;
    assign m_axi_awlen = 8'd0;
    assign m_axi_awsize = 3'b011;
    assign m_axi_awburst = 2'b01;
    assign m_axi_awcache = 4'b0011;
    assign m_axi_awprot = 3'b000;
    assign m_axi_awqos = 4'b0000;
    assign m_axi_awvalid = issue_valid && !issue_aw_sent;
    assign m_axi_wdata = issue_data;
    assign m_axi_wstrb = issue_strobes;
    assign m_axi_wlast = 1'b1;
    assign m_axi_wvalid = issue_valid && !issue_w_sent;
    assign m_axi_bready = busy;

    always @(posedge clk) begin
        if (reset) begin
            pack_valid <= 1'b0;
            pack_address <= 32'd0;
            pack_data <= 64'd0;
            pack_strobes <= 8'd0;
            pixel_stage_address[0] <= 32'd0;
            pixel_stage_address[1] <= 32'd0;
            pixel_stage_data[0] <= 64'd0;
            pixel_stage_data[1] <= 64'd0;
            pixel_stage_strobes[0] <= 8'd0;
            pixel_stage_strobes[1] <= 8'd0;
            pixel_stage_count <= 2'd0;
            ingress_valid <= 1'b0;
            ingress_pixel_address <= 32'd0;
            ingress_pixel_format <= 8'd0;
            ingress_pixel_value <= 32'd0;
            flush_pending <= 1'b0;
            barrier_pending <= 1'b0;
            abort_pending <= 1'b0;
            start_q <= 1'b0;
            fifo_stage_valid <= 1'b0;
            fifo_stage_address <= 32'd0;
            fifo_stage_data <= 64'd0;
            fifo_stage_strobes <= 8'd0;
            fifo_write_pointer <= {FIFO_POINTER_WIDTH{1'b0}};
            fifo_read_pointer <= {FIFO_POINTER_WIDTH{1'b0}};
            fifo_count <= {FIFO_COUNT_WIDTH{1'b0}};
            issue_valid <= 1'b0;
            issue_aw_sent <= 1'b0;
            issue_w_sent <= 1'b0;
            issue_address <= 32'd0;
            issue_data <= 64'd0;
            issue_strobes <= 8'd0;
            outstanding_count <= {OUTSTANDING_WIDTH{1'b0}};
            busy <= 1'b0;
            done <= 1'b0;
            barrier_done <= 1'b0;
            aborted <= 1'b0;
            write_error <= 1'b0;
            fault_detail <= 32'd0;
            pixels_accepted <= 32'd0;
            bytes_written <= 32'd0;
        end else begin
            done <= 1'b0;
            barrier_done <= 1'b0;
            start_q <= start;

            if (start_q && !busy) begin
                pack_valid <= 1'b0;
                pack_strobes <= 8'd0;
                pixel_stage_count <= 2'd0;
                ingress_valid <= 1'b0;
                flush_pending <= 1'b0;
                barrier_pending <= 1'b0;
                abort_pending <= 1'b0;
                fifo_stage_valid <= 1'b0;
                fifo_write_pointer <= {FIFO_POINTER_WIDTH{1'b0}};
                fifo_read_pointer <= {FIFO_POINTER_WIDTH{1'b0}};
                fifo_count <= {FIFO_COUNT_WIDTH{1'b0}};
                issue_valid <= 1'b0;
                issue_aw_sent <= 1'b0;
                issue_w_sent <= 1'b0;
                outstanding_count <= {OUTSTANDING_WIDTH{1'b0}};
                busy <= 1'b1;
                aborted <= 1'b0;
                write_error <= 1'b0;
                fault_detail <= 32'd0;
                pixels_accepted <= 32'd0;
                bytes_written <= 32'd0;
            end else if (busy) begin
                if (abort && !abort_pending) begin
                    abort_pending <= 1'b1;
                    flush_pending <= 1'b0;
                    barrier_pending <= 1'b0;
                    pack_valid <= 1'b0;
                    pixel_stage_count <= 2'd0;
                    ingress_valid <= 1'b0;
                    fifo_stage_valid <= 1'b0;
                    fifo_write_pointer <= {FIFO_POINTER_WIDTH{1'b0}};
                    fifo_read_pointer <= {FIFO_POINTER_WIDTH{1'b0}};
                    fifo_count <= {FIFO_COUNT_WIDTH{1'b0}};
                end else begin
                    if (fifo_push) begin
                        fifo_address[fifo_write_pointer] <= fifo_push_address;
                        fifo_data[fifo_write_pointer] <= fifo_push_data;
                        fifo_strobes[fifo_write_pointer] <= fifo_push_strobes;
                        fifo_write_pointer <= fifo_write_pointer + 1'b1;
                    end

                    if (fifo_stage_drain)
                        fifo_stage_valid <= 1'b0;
                    if (fifo_stage_ready) begin
                        // Keep the elastic stage's payload shadowed while it
                        // is available. Beat-change detection then controls
                        // only the valid bit, not the 104-bit payload enable.
                        // A simultaneous drain/load still transfers one beat
                        // per cycle without a comparator-to-array path.
                        fifo_stage_address <= pack_address;
                        fifo_stage_data <= pack_data;
                        fifo_stage_strobes <= pack_strobes;
                    end
                    if (fifo_stage_load)
                        fifo_stage_valid <= 1'b1;

                    if (pixel_stage_drain) begin
                        pixels_accepted <= pixels_accepted + 32'd1;
                        if (!pack_valid || staged_pixel_changes_beat) begin
                            pack_valid <= 1'b1;
                            pack_address <= staged_pixel_address;
                            pack_data <= staged_pixel_data;
                            pack_strobes <= staged_pixel_strobes;
                        end else begin
                            pack_data <=
                                (pack_data & ~byte_mask(staged_pixel_strobes)) |
                                staged_pixel_data;
                            pack_strobes <=
                                pack_strobes | staged_pixel_strobes;
                        end
                    end

                    if (ingress_accept) begin
                        ingress_valid <= 1'b1;
                        ingress_pixel_address <= pixel_address;
                        ingress_pixel_format <= pixel_format;
                        ingress_pixel_value <= pixel_value;
                    end else if (ingress_drain) begin
                        ingress_valid <= 1'b0;
                    end

                    case ({pixel_accept, pixel_stage_drain})
                        2'b10: begin
                            if (pixel_stage_count == 2'd0) begin
                                pixel_stage_address[0] <=
                                    ingress_beat_address;
                                pixel_stage_data[0] <= ingress_data;
                                pixel_stage_strobes[0] <= ingress_strobes;
                            end else begin
                                pixel_stage_address[1] <=
                                    ingress_beat_address;
                                pixel_stage_data[1] <= ingress_data;
                                pixel_stage_strobes[1] <= ingress_strobes;
                            end
                            pixel_stage_count <= pixel_stage_count + 2'd1;
                        end
                        2'b01: begin
                            if (pixel_stage_count == 2'd2) begin
                                pixel_stage_address[0] <=
                                    pixel_stage_address[1];
                                pixel_stage_data[0] <= pixel_stage_data[1];
                                pixel_stage_strobes[0] <=
                                    pixel_stage_strobes[1];
                            end
                            pixel_stage_count <= pixel_stage_count - 2'd1;
                        end
                        2'b11: begin
                            if (pixel_stage_count == 2'd1) begin
                                pixel_stage_address[0] <=
                                    ingress_beat_address;
                                pixel_stage_data[0] <= ingress_data;
                                pixel_stage_strobes[0] <= ingress_strobes;
                            end else begin
                                pixel_stage_address[0] <=
                                    pixel_stage_address[1];
                                pixel_stage_data[0] <= pixel_stage_data[1];
                                pixel_stage_strobes[0] <=
                                    pixel_stage_strobes[1];
                                pixel_stage_address[1] <=
                                    ingress_beat_address;
                                pixel_stage_data[1] <= ingress_data;
                                pixel_stage_strobes[1] <= ingress_strobes;
                            end
                        end
                        default: begin end
                    endcase

                    if (flush_accept) begin
                        flush_pending <= 1'b1;
                        pack_valid <= 1'b0;
                    end else if (barrier_accept) begin
                        barrier_pending <= 1'b1;
                        pack_valid <= 1'b0;
                    end
                end

                if (!issue_valid && !abort && !abort_pending &&
                    fifo_count != 0 &&
                    outstanding_count < MAX_OUTSTANDING) begin
                    issue_valid <= 1'b1;
                    issue_aw_sent <= 1'b0;
                    issue_w_sent <= 1'b0;
                    issue_address <= fifo_address[fifo_read_pointer];
                    issue_data <= fifo_data[fifo_read_pointer];
                    issue_strobes <= fifo_strobes[fifo_read_pointer];
                end else if (issue_valid) begin
                    if (aw_accept)
                        issue_aw_sent <= 1'b1;
                    if (w_accept)
                        issue_w_sent <= 1'b1;
                    if (issue_complete) begin
                        issue_valid <= 1'b0;
                        issue_aw_sent <= 1'b0;
                        issue_w_sent <= 1'b0;
                        fifo_read_pointer <= fifo_read_pointer + 1'b1;
                        bytes_written <= bytes_written +
                                         strobe_count(issue_strobes);
                    end
                end

                if (!abort && !abort_pending) begin
                    case ({fifo_push, issue_complete})
                        2'b10: fifo_count <= fifo_count + 1'b1;
                        2'b01: fifo_count <= fifo_count - 1'b1;
                        default: begin end
                    endcase
                end

                case ({issue_complete, b_accept})
                    2'b10: outstanding_count <= outstanding_count + 1'b1;
                    2'b01: begin
                        if (outstanding_count != 0)
                            outstanding_count <= outstanding_count - 1'b1;
                    end
                    default: begin end
                endcase

                if (b_accept) begin
                    if (outstanding_count == 0 && !issue_complete) begin
                        write_error <= 1'b1;
                        if (!write_error)
                            fault_detail <= 32'h00010000;
                    end else if (m_axi_bid != AXI_ID || m_axi_bresp != 2'b00) begin
                        write_error <= 1'b1;
                        if (!write_error)
                            fault_detail <= {16'h0002,
                                {{(8-AXI_ID_WIDTH){1'b0}}, m_axi_bid},
                                6'd0, m_axi_bresp};
                    end
                end

                // An accepted AW/W pair creates a response obligation. Do not
                // report an abort complete in the same cycle that the final
                // channel handshake creates that obligation.
                if (abort_pending && outstanding_count == 0 &&
                    !issue_valid) begin
                    busy <= 1'b0;
                    done <= 1'b1;
                    aborted <= 1'b1;
                    abort_pending <= 1'b0;
                end else if (barrier_pending && !pack_valid &&
                             !fifo_stage_valid && fifo_count == 0 &&
                             !issue_valid && outstanding_count == 0) begin
                    barrier_done <= 1'b1;
                    barrier_pending <= 1'b0;
                end else if (flush_pending && !pack_valid &&
                             !fifo_stage_valid && fifo_count == 0 &&
                             !issue_valid && outstanding_count == 0) begin
                    busy <= 1'b0;
                    done <= 1'b1;
                    flush_pending <= 1'b0;
                end
            end
        end
    end
endmodule

`default_nettype wire
