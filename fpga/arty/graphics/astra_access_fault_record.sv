// Copyright (c) 2026 Astra68 contributors
//
// Sticky access-fault record shared by every Astra AXI4-Lite slave.
//
// The HPS turns any SLVERR/DECERR on its FPGA bridges into an asynchronous
// SError, so no Astra slave answers a CPU access with an error. A store the
// register file refuses is dropped, a load from an unmapped offset returns
// zero, both are answered OKAY, and they are counted here instead.
//
//   COUNT  [15:0] faults since the last clear, saturating at 0xffff.
//                 Any store to COUNT clears COUNT and FIRST.
//   FIRST  [31]   1 = store, 0 = load
//          [17:16] reason: 2 = rejected (valid register, refused by state,
//                 value, or strobe), 3 = unmapped or misaligned offset
//          [15:0] byte offset within the slave's window
//                 FIRST describes the first fault since the clear; 0 when
//                 COUNT is 0.
//
// Faults are sampled on the response handshake, so the slave keeps its
// response code in its own register and feeds it here.
`timescale 1ns/1ps
`default_nettype none

module astra_access_fault_record (
    input  wire        clk,
    input  wire        reset,
    input  wire        clear,
    input  wire        write_fault,
    input  wire [1:0]  write_reason,
    input  wire [15:0] write_offset,
    input  wire        read_fault,
    input  wire [1:0]  read_reason,
    input  wire [15:0] read_offset,
    output wire [31:0] count_word,
    output reg  [31:0] first_word
);
    reg [15:0] count_q;
    wire [15:0] base = clear ? 16'd0 : count_q;
    wire [16:0] sum = {1'b0, base} + {16'd0, write_fault} +
                      {16'd0, read_fault};

    assign count_word = {16'd0, count_q};

    always @(posedge clk) begin
        if (reset) begin
            count_q <= 16'd0;
            first_word <= 32'd0;
        end else begin
            count_q <= sum[16] ? 16'hffff : sum[15:0];
            if (clear)
                first_word <= 32'd0;
            if (base == 16'd0 && write_fault)
                first_word <= {1'b1, 13'd0, write_reason, write_offset};
            else if (base == 16'd0 && read_fault)
                first_word <= {1'b0, 13'd0, read_reason, read_offset};
        end
    end
endmodule

`default_nettype wire
