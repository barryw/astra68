// Routes Astraea render reads in the host aperture to a second AXI read
// port. The aperture is arena offsets [HOST_APERTURE_OFFSET,
// + HOST_APERTURE_BYTES), the render batch window, and the second port
// reads host (HPS) memory at host_base + (offset - HOST_APERTURE_OFFSET).
// With host_base == 0 every read goes to the Media RAM port unchanged.
// Writes never pass through here: they always stay in Media RAM.
//
// Responses stay in request order without buffering data: a one-bit FIFO
// records the port of each issued burst, and R is taken only from the port
// of the oldest burst (the other port is held off with RREADY low). Both
// ports can have reads in flight, so host-source / Media-destination reads
// overlap.
//
// Host bursts longer than HOST_MAX_BEATS are issued as consecutive pieces
// of at most HOST_MAX_BEATS beats, and RLAST is passed up only on the last
// beat of the last piece. On the DE25 F2SDRAM path a narrow 64-bit read
// burst of 136 or more beats never returns (128 beats and fewer do, measured
// 2026-09-28), which wedged the engine on its 256-beat ring prefetch. 32
// beats keeps 4x margin and, with the port's 8 outstanding reads at 300
// cycles of latency, is as fast as 64 in the perf cases (16 is not). Media
// RAM bursts are not split. Only INCR bursts are split.
//
// host_base may only change while the engine is stopped (the control
// register rejects writes while it is enabled or busy), so a change never
// lands inside a burst.
`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module astra_render_host_reads #(
    parameter [31:0] ARENA_BASE = 32'h40000000,
    parameter integer AXI_ID_WIDTH = 3,
    parameter integer OUTSTANDING_BITS = 4,
    // Power of two, at most 256.
    parameter integer HOST_MAX_BEATS = 32
) (
    input  wire                    clk,
    input  wire                    reset,
    input  wire [31:0]             host_base,

    input  wire [AXI_ID_WIDTH-1:0] s_arid,
    input  wire [31:0]             s_araddr,
    input  wire [7:0]              s_arlen,
    input  wire [2:0]              s_arsize,
    input  wire [1:0]              s_arburst,
    input  wire [3:0]              s_arcache,
    input  wire [2:0]              s_arprot,
    input  wire                    s_arvalid,
    output wire                    s_arready,
    output wire [AXI_ID_WIDTH-1:0] s_rid,
    output wire [63:0]             s_rdata,
    output wire [1:0]              s_rresp,
    output wire                    s_rlast,
    output wire                    s_rvalid,
    input  wire                    s_rready,

    output wire [AXI_ID_WIDTH-1:0] m_arid,
    output wire [31:0]             m_araddr,
    output wire [7:0]              m_arlen,
    output wire [2:0]              m_arsize,
    output wire [1:0]              m_arburst,
    output wire [3:0]              m_arcache,
    output wire [2:0]              m_arprot,
    output wire                    m_arvalid,
    input  wire                    m_arready,
    input  wire [AXI_ID_WIDTH-1:0] m_rid,
    input  wire [63:0]             m_rdata,
    input  wire [1:0]              m_rresp,
    input  wire                    m_rlast,
    input  wire                    m_rvalid,
    output wire                    m_rready,

    output wire [AXI_ID_WIDTH-1:0] h_arid,
    output wire [31:0]             h_araddr,
    output wire [7:0]              h_arlen,
    output wire [2:0]              h_arsize,
    output wire [1:0]              h_arburst,
    output wire [3:0]              h_arcache,
    output wire [2:0]              h_arprot,
    output wire                    h_arvalid,
    input  wire                    h_arready,
    input  wire [AXI_ID_WIDTH-1:0] h_rid,
    input  wire [63:0]             h_rdata,
    input  wire [1:0]              h_rresp,
    input  wire                    h_rlast,
    input  wire                    h_rvalid,
    output wire                    h_rready
);
    localparam [31:0] WINDOW_BASE =
        ARENA_BASE + `ASTRA_RENDER_HOST_APERTURE_OFFSET;
    localparam [31:0] WINDOW_LIMIT =
        WINDOW_BASE + `ASTRA_RENDER_HOST_APERTURE_BYTES;

    // One registered AR slot: nothing downstream reaches s_arready.
    reg                    ar_valid_q;
    reg                    ar_host_q;
    reg [31:0]             ar_addr_q;
    reg [AXI_ID_WIDTH-1:0] ar_id_q;
    reg [7:0]              ar_len_q;
    reg [2:0]              ar_size_q;
    reg [1:0]              ar_burst_q;
    reg [3:0]              ar_cache_q;
    reg [2:0]              ar_prot_q;
    // Port of each burst (or host piece) in flight, oldest at the head
    // (1 = host), and whether it ends the engine's burst.
    localparam integer ORDER_DEPTH = 1 << OUTSTANDING_BITS;
    reg                        order_q [0:ORDER_DEPTH-1];
    reg                        order_final_q [0:ORDER_DEPTH-1];
    reg [OUTSTANDING_BITS-1:0] order_head_q, order_tail_q;
    reg [OUTSTANDING_BITS:0]   outstanding_q;
    wire port_host_q = order_q[order_head_q];
    wire port_final_q = order_final_q[order_head_q];
    localparam [7:0] PIECE_LEN = HOST_MAX_BEATS - 1;
    wire ar_split = ar_host_q && ar_burst_q == 2'b01 &&
                    ar_len_q > PIECE_LEN;
    wire [7:0] ar_issue_len = ar_split ? PIECE_LEN : ar_len_q;

    wire to_host = host_base != 32'd0 && s_araddr >= WINDOW_BASE &&
                   s_araddr < WINDOW_LIMIT;
    wire may_issue = outstanding_q != ORDER_DEPTH;
    wire issue_ready = ar_host_q ? h_arready : m_arready;
    wire ar_fire = ar_valid_q && may_issue && issue_ready;
    wire port_rlast = port_host_q ? h_rlast : m_rlast;
    wire r_last_fire = s_rvalid && s_rready && port_rlast;

    assign s_arready = !ar_valid_q;

    assign m_arvalid = ar_valid_q && may_issue && !ar_host_q;
    assign h_arvalid = ar_valid_q && may_issue && ar_host_q;
    assign m_arid = ar_id_q;       assign h_arid = ar_id_q;
    assign m_araddr = ar_addr_q;   assign h_araddr = ar_addr_q;
    assign m_arlen = ar_len_q;     assign h_arlen = ar_issue_len;
    assign m_arsize = ar_size_q;   assign h_arsize = ar_size_q;
    assign m_arburst = ar_burst_q; assign h_arburst = ar_burst_q;
    assign m_arcache = ar_cache_q; assign h_arcache = ar_cache_q;
    assign m_arprot = ar_prot_q;   assign h_arprot = ar_prot_q;

    assign s_rid = port_host_q ? h_rid : m_rid;
    assign s_rdata = port_host_q ? h_rdata : m_rdata;
    assign s_rresp = port_host_q ? h_rresp : m_rresp;
    assign s_rlast = port_rlast && port_final_q;
    assign s_rvalid = outstanding_q != 0 &&
                      (port_host_q ? h_rvalid : m_rvalid);
    assign m_rready = outstanding_q != 0 && !port_host_q && s_rready;
    assign h_rready = outstanding_q != 0 && port_host_q && s_rready;

    always @(posedge clk) begin
        if (reset) begin
            ar_valid_q <= 1'b0;
            order_head_q <= {OUTSTANDING_BITS{1'b0}};
            order_tail_q <= {OUTSTANDING_BITS{1'b0}};
            outstanding_q <= {(OUTSTANDING_BITS + 1){1'b0}};
        end else begin
            if (s_arvalid && s_arready) begin
                ar_valid_q <= 1'b1;
                ar_host_q <= to_host;
                ar_addr_q <= to_host ?
                    host_base + (s_araddr - WINDOW_BASE) : s_araddr;
                ar_id_q <= s_arid;
                ar_len_q <= s_arlen;
                ar_size_q <= s_arsize;
                ar_burst_q <= s_arburst;
                ar_cache_q <= s_arcache;
                ar_prot_q <= s_arprot;
            end else if (ar_fire && ar_split) begin
                ar_addr_q <= ar_addr_q + (HOST_MAX_BEATS << ar_size_q);
                ar_len_q <= ar_len_q - HOST_MAX_BEATS;
            end else if (ar_fire) begin
                ar_valid_q <= 1'b0;
            end
            if (ar_fire) begin
                order_q[order_tail_q] <= ar_host_q;
                order_final_q[order_tail_q] <= !ar_split;
                order_tail_q <= order_tail_q + 1'b1;
            end
            if (r_last_fire)
                order_head_q <= order_head_q + 1'b1;
            outstanding_q <= outstanding_q + (ar_fire ? 1'b1 : 1'b0) -
                             (r_last_fire ? 1'b1 : 1'b0);
        end
    end
endmodule

`default_nettype wire
