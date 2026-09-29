// Simulation-only 64-bit AXI memory with a DDR-like latency: many reads
// outstanding, READ_LATENCY cycles from AR to first R, one R beat per cycle,
// one W beat per cycle, B WRITE_LATENCY cycles after the last W beat.
// Performance measurement only; functional tests use the stricter
// astra_render_axi_memory_model.
`timescale 1ns/1ps
`default_nettype none

module astra_render_axi_latency_model #(
    parameter integer AXI_ID_WIDTH = 6,
    parameter integer MEMORY_BYTES = 1048576,
    parameter [31:0] BASE_ADDRESS = 32'd0,
    parameter integer READ_LATENCY = 25,
    parameter integer WRITE_LATENCY = 25,
    parameter integer MAX_READS = 16,
    parameter integer MAX_WRITES = 64,
    // Writes become visible to reads only when their response is sent, as
    // a slow memory may order them: exposes a read that passes a write.
    parameter bit WRITE_AT_RESPONSE = 1'b0,
    // Responses for FAST_ID return after FAST_LATENCY and may overtake
    // other IDs' responses, as AXI allows across IDs. -1 disables.
    parameter integer FAST_ID = -1,
    parameter integer FAST_LATENCY = 4,
    // A write burst to this address is answered SLVERR (0: none).
    parameter [31:0] ERROR_ADDRESS = 32'd0,
    // Read bursts longer than this are accepted and never answered, as the
    // DE25 F2SDRAM path does with narrow bursts over 128 beats (0: none).
    parameter integer LOST_READ_BEATS = 0
) (
    input  wire                         clk,
    input  wire                         reset,
    input  wire [AXI_ID_WIDTH-1:0]      s_axi_arid,
    input  wire [31:0]                  s_axi_araddr,
    input  wire [7:0]                   s_axi_arlen,
    input  wire                         s_axi_arvalid,
    output wire                         s_axi_arready,
    output reg  [AXI_ID_WIDTH-1:0]      s_axi_rid,
    output reg  [63:0]                  s_axi_rdata,
    output wire [1:0]                   s_axi_rresp,
    output reg                          s_axi_rlast,
    output reg                          s_axi_rvalid,
    input  wire                         s_axi_rready,
    input  wire [AXI_ID_WIDTH-1:0]      s_axi_awid,
    input  wire [31:0]                  s_axi_awaddr,
    input  wire [7:0]                   s_axi_awlen,
    input  wire                         s_axi_awvalid,
    output wire                         s_axi_awready,
    input  wire [63:0]                  s_axi_wdata,
    input  wire [7:0]                   s_axi_wstrb,
    input  wire                         s_axi_wlast,
    input  wire                         s_axi_wvalid,
    output wire                         s_axi_wready,
    output reg  [AXI_ID_WIDTH-1:0]      s_axi_bid,
    output wire [1:0]                   s_axi_bresp,
    output reg                          s_axi_bvalid,
    input  wire                         s_axi_bready,
    output reg  [31:0]                  read_transactions,
    output reg  [31:0]                  write_transactions,
    output reg  [31:0]                  read_beats,
    output reg  [31:0]                  write_beats
);
    reg [7:0] memory [0:MEMORY_BYTES-1];
    reg [31:0] cycle;

    task automatic write_byte(input [31:0] address, input [7:0] value);
        memory[address - BASE_ADDRESS] = value;
    endtask
    function automatic [7:0] read_byte(input [31:0] address);
        read_byte = memory[address - BASE_ADDRESS];
    endfunction
    task automatic clear_memory(input [7:0] value);
        integer i;
        for (i = 0; i < MEMORY_BYTES; i = i + 1) memory[i] = value;
    endtask

    // Read request queue.
    reg [31:0] ar_addr [0:MAX_READS-1];
    reg [8:0]  ar_beats [0:MAX_READS-1];
    reg [AXI_ID_WIDTH-1:0] ar_id [0:MAX_READS-1];
    reg [31:0] ar_time [0:MAX_READS-1];
    integer ar_head, ar_tail, ar_count;
    reg [31:0] r_addr;
    reg [8:0] r_left;

    // Write address queue, B queue.
    reg [31:0] aw_addr [0:MAX_WRITES-1];
    reg [8:0]  aw_beats [0:MAX_WRITES-1];
    reg [AXI_ID_WIDTH-1:0] aw_id_q [0:MAX_WRITES-1];
    integer aw_head, aw_tail, aw_count;
    reg [31:0] w_addr;
    reg [8:0] w_left;
    reg w_active;
    reg [AXI_ID_WIDTH-1:0] b_id [0:MAX_WRITES-1];
    reg [31:0] b_time [0:MAX_WRITES-1];
    integer b_head, b_tail, b_count;
    // Held beats for WRITE_AT_RESPONSE, one run per response.
    localparam integer HELD = 8192;
    reg [31:0] held_addr [0:HELD-1];
    reg [63:0] held_data [0:HELD-1];
    reg [7:0] held_strb [0:HELD-1];
    integer held_tail;
    integer b_first [0:MAX_WRITES-1];
    integer b_beats [0:MAX_WRITES-1];
    integer w_first, hb, b_pick;
    integer b_sel;
    reg b_error [0:MAX_WRITES-1];
    reg b_error_q;

    assign s_axi_arready = !reset && ar_count < MAX_READS;
    assign s_axi_rresp = 2'b00;
    assign s_axi_awready = !reset && aw_count < MAX_WRITES;
    // W accepted only against a known burst (AW first or same cycle ok).
    assign s_axi_wready = !reset && (w_active || aw_count != 0) &&
        b_count < MAX_WRITES;
    assign s_axi_bresp = b_error_q ? 2'b10 : 2'b00;

    integer lane, off;
    reg [31:0] wa;
    always @(posedge clk) begin
        if (reset) begin
            cycle <= 0; ar_head = 0; ar_tail = 0; ar_count = 0;
            aw_head = 0; aw_tail = 0; aw_count = 0; w_active = 0;
            b_head = 0; b_tail = 0; b_count = 0; r_left = 0;
            held_tail = 0; w_first = 0; b_sel <= 0;
            s_axi_rvalid <= 0; s_axi_rlast <= 0; s_axi_bvalid <= 0;
            read_transactions <= 0; write_transactions <= 0;
            read_beats <= 0; write_beats <= 0;
        end else begin
            cycle <= cycle + 1;
            if (s_axi_bvalid && s_axi_bready) begin
                if (WRITE_AT_RESPONSE)
                    for (hb = 0; hb < b_beats[b_sel]; hb = hb + 1) begin
                        off = held_addr[(b_first[b_sel] + hb) % HELD] -
                            BASE_ADDRESS;
                        for (lane = 0; lane < 8; lane = lane + 1)
                            if (held_strb[(b_first[b_sel] + hb) % HELD][lane])
                                memory[off + lane] =
                                    held_data[(b_first[b_sel] + hb) % HELD][lane*8 +: 8];
                    end
                // Remove the answered entry, keeping the rest in order.
                for (hb = (b_sel - b_head + MAX_WRITES) % MAX_WRITES; hb > 0;
                     hb = hb - 1) begin
                    b_id[(b_head + hb) % MAX_WRITES] =
                        b_id[(b_head + hb - 1) % MAX_WRITES];
                    b_time[(b_head + hb) % MAX_WRITES] =
                        b_time[(b_head + hb - 1) % MAX_WRITES];
                    b_first[(b_head + hb) % MAX_WRITES] =
                        b_first[(b_head + hb - 1) % MAX_WRITES];
                    b_beats[(b_head + hb) % MAX_WRITES] =
                        b_beats[(b_head + hb - 1) % MAX_WRITES];
                    b_error[(b_head + hb) % MAX_WRITES] =
                        b_error[(b_head + hb - 1) % MAX_WRITES];
                end
                b_head = (b_head + 1) % MAX_WRITES; b_count = b_count - 1;
            end
            if (s_axi_rvalid && s_axi_rready) begin
                s_axi_rvalid <= 1'b0;
                read_beats <= read_beats + 1;
            end
            if ((!s_axi_rvalid || s_axi_rready)) begin
                if (r_left == 0 && ar_count != 0 &&
                    cycle >= ar_time[ar_head]) begin
                    r_addr = ar_addr[ar_head]; r_left = ar_beats[ar_head];
                    s_axi_rid <= ar_id[ar_head];
                    ar_head = (ar_head + 1) % MAX_READS;
                    ar_count = ar_count - 1;
                end
                if (r_left != 0) begin
                    off = r_addr - BASE_ADDRESS;
                    for (lane = 0; lane < 8; lane = lane + 1)
                        s_axi_rdata[lane*8 +: 8] <= memory[off + lane];
                    s_axi_rlast <= r_left == 1;
                    s_axi_rvalid <= 1'b1;
                    r_addr = r_addr + 8; r_left = r_left - 1;
                end
            end
            if (s_axi_arvalid && s_axi_arready && LOST_READ_BEATS != 0 &&
                s_axi_arlen + 1 > LOST_READ_BEATS) begin
                read_transactions <= read_transactions + 1;
            end else if (s_axi_arvalid && s_axi_arready) begin
                if (s_axi_araddr[11:0] + (s_axi_arlen + 1) * 8 > 4096)
                    $fatal(1, "latency model: read crossed 4KiB");
                ar_addr[ar_tail] = s_axi_araddr;
                ar_beats[ar_tail] = s_axi_arlen + 1;
                ar_id[ar_tail] = s_axi_arid;
                ar_time[ar_tail] = cycle + READ_LATENCY;
                ar_tail = (ar_tail + 1) % MAX_READS;
                ar_count = ar_count + 1;
                read_transactions <= read_transactions + 1;
            end
            if (s_axi_awvalid && s_axi_awready) begin
                aw_addr[aw_tail] = s_axi_awaddr;
                aw_beats[aw_tail] = s_axi_awlen + 1;
                aw_id_q[aw_tail] = s_axi_awid;
                aw_tail = (aw_tail + 1) % MAX_WRITES;
                aw_count = aw_count + 1;
            end
            if (s_axi_wvalid && s_axi_wready) begin
                if (!w_active) begin
                    w_addr = aw_addr[aw_head]; w_left = aw_beats[aw_head];
                    w_active = 1;
                    w_first = held_tail;
                end
                off = w_addr - BASE_ADDRESS;
                if (WRITE_AT_RESPONSE) begin
                    held_addr[held_tail % HELD] = w_addr;
                    held_data[held_tail % HELD] = s_axi_wdata;
                    held_strb[held_tail % HELD] = s_axi_wstrb;
                    held_tail = held_tail + 1;
                end else
                    for (lane = 0; lane < 8; lane = lane + 1)
                        if (s_axi_wstrb[lane])
                            memory[off + lane] = s_axi_wdata[lane*8 +: 8];
                if (s_axi_wlast != (w_left == 1))
                    $fatal(1, "latency model: WLAST mismatch");
                write_beats <= write_beats + 1;
                w_addr = w_addr + 8; w_left = w_left - 1;
                if (w_left == 0) begin
                    w_active = 0;
                    b_id[b_tail] = aw_id_q[aw_head];
                    b_first[b_tail] = w_first;
                    b_error[b_tail] = ERROR_ADDRESS != 0 &&
                        aw_addr[aw_head] == ERROR_ADDRESS;
                    b_beats[b_tail] = held_tail - w_first;
                    b_time[b_tail] = cycle + (aw_id_q[aw_head] == FAST_ID ?
                        FAST_LATENCY : WRITE_LATENCY);
                    b_tail = (b_tail + 1) % MAX_WRITES;
                    b_count = b_count + 1;
                    aw_head = (aw_head + 1) % MAX_WRITES;
                    aw_count = aw_count - 1;
                    write_transactions <= write_transactions + 1;
                end
            end
            // Registered B from the post-update queue (array reads inside
            // continuous assigns do not track element writes in iverilog).
            // A ready FAST_ID response overtakes the queue head.
            b_pick = b_head;
            for (hb = 0; hb < b_count; hb = hb + 1)
                if (b_pick == b_head && FAST_ID >= 0 &&
                    b_id[(b_head + hb) % MAX_WRITES] == FAST_ID &&
                    cycle + 1 >= b_time[(b_head + hb) % MAX_WRITES])
                    b_pick = (b_head + hb) % MAX_WRITES;
            s_axi_bvalid <= b_count != 0 && cycle + 1 >= b_time[b_pick];
            s_axi_bid <= b_id[b_pick];
            b_sel <= b_pick;
            b_error_q <= b_error[b_pick];
        end
    end
endmodule
`default_nettype wire
