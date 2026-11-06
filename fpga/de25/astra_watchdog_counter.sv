// HPS cold reset leaves FPGA fabric live. Count watchdog assertions until
// fabric reconfiguration; never clear this state with an HPS reset signal.
`timescale 1ns/1ps
`default_nettype none

module astra_watchdog_counter (
    input  wire        clock,
    input  wire        watchdog_reset_n,
    output wire [28:0] count
);
    (* ASYNC_REG = "TRUE" *) reg [2:0] reset_sync_q = 3'b000;
    reg watchdog_previous_q = 1'b0;
    reg armed_q = 1'b0;
    reg [28:0] count_q = 29'd0;

    always @(posedge clock) begin
        reset_sync_q <= {reset_sync_q[1:0], watchdog_reset_n};
        watchdog_previous_q <= reset_sync_q[2];
        if (reset_sync_q[2])
            armed_q <= 1'b1;
        if (armed_q && watchdog_previous_q && !reset_sync_q[2] &&
            !(&count_q))
            count_q <= count_q + 29'd1;
    end

    assign count = count_q;
endmodule

`default_nettype wire
