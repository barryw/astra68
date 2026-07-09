// FPGA-only UART smoke test for ULX3S FTDI TX path.
// Sends a repeated banner without involving the CPU core or bus.
`default_nettype none

module uart_smoke (
    input  wire       clk25_mhz,
    input  wire       reset_n,
    output wire       ftdi_rxd,
    input  wire       ftdi_txd,
    output wire [7:0] leds
);
    wire unused_ftdi_txd = ftdi_txd;

    localparam integer CLK_HZ = 25000000;
    localparam integer MSG_LEN = 17;
    localparam integer GAP_CLKS = CLK_HZ / 2;

    reg [23:0] hb = 24'd0;
    always @(posedge clk25_mhz) hb <= hb + 1'b1;

    reg [7:0] uart_data = 8'h00;
    reg       uart_start = 1'b0;
    wire      uart_busy;

    uart_tx #(.CLK_HZ(CLK_HZ), .BAUD(115200)) uart_i (
        .clk(clk25_mhz),
        .rst(!reset_n),
        .data(uart_data),
        .start(uart_start),
        .tx(ftdi_rxd),
        .busy(uart_busy)
    );

    reg [7:0] idx = 8'd0;
    reg [24:0] gap = 25'd0;

    function automatic [7:0] msg(input [7:0] i);
        case (i)
            8'd0:  msg = "U";
            8'd1:  msg = "A";
            8'd2:  msg = "R";
            8'd3:  msg = "T";
            8'd4:  msg = " ";
            8'd5:  msg = "S";
            8'd6:  msg = "M";
            8'd7:  msg = "O";
            8'd8:  msg = "K";
            8'd9:  msg = "E";
            8'd10: msg = " ";
            8'd11: msg = "O";
            8'd12: msg = "K";
            8'd13: msg = "\r";
            8'd14: msg = "\n";
            8'd15: msg = "\r";
            8'd16: msg = "\n";
            default: msg = 8'h00;
        endcase
    endfunction

    always @(posedge clk25_mhz) begin
        uart_start <= 1'b0;
        if (!reset_n) begin
            idx <= 8'd0;
            gap <= 25'd0;
        end else if (gap != 25'd0) begin
            gap <= gap - 1'b1;
        end else if (!uart_busy && !uart_start) begin
            uart_data <= msg(idx);
            uart_start <= 1'b1;
            if (idx == MSG_LEN - 1) begin
                idx <= 8'd0;
                gap <= GAP_CLKS[24:0];
            end else begin
                idx <= idx + 1'b1;
            end
        end
    end

    assign leds = {hb[23], unused_ftdi_txd, 1'b0, uart_busy, idx[3:0]};
endmodule

`default_nettype wire
