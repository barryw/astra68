// Simple UART transmitter — 8N1, no parity
// Directly mirrors uart_rx.sv structure.
// 'busy' goes high immediately when start is asserted (combinational)
// and stays high until the stop bit completes.

module uart_tx #(
    parameter CLK_HZ = 25000000,
    parameter BAUD   = 115200
)(
    input  logic       clk,
    input  logic       rst,
    input  logic [7:0] data,
    input  logic       start,     // pulse to begin transmission
    output logic       tx,        // serial output (idle high)
    output logic       busy       // high while transmitting
);

    localparam CLKS_PER_BIT = CLK_HZ / BAUD;

    typedef enum logic [1:0] {
        IDLE, START_BIT, DATA_BITS, STOP_BIT
    } state_t;

    state_t state;
    logic [$clog2(CLKS_PER_BIT)-1:0] clk_cnt;
    logic [2:0] bit_idx;
    logic [7:0] shift;

    // Combinational busy: goes high immediately on start assertion
    // so callers never see a false "not busy" gap between start and
    // the registered state transition.
    assign busy = (state != IDLE) || start;

    always_ff @(posedge clk) begin
        if (rst) begin
            state   <= IDLE;
            tx      <= 1;
            clk_cnt <= 0;
            bit_idx <= 0;
            shift   <= 0;
        end else begin
            case (state)
                IDLE: begin
                    tx <= 1;
                    if (start) begin
                        shift   <= data;
                        state   <= START_BIT;
                        clk_cnt <= 0;
                    end
                end

                START_BIT: begin
                    tx <= 0;
                    if (clk_cnt == CLKS_PER_BIT - 1) begin
                        state   <= DATA_BITS;
                        clk_cnt <= 0;
                        bit_idx <= 0;
                    end else begin
                        clk_cnt <= clk_cnt + 1;
                    end
                end

                DATA_BITS: begin
                    tx <= shift[bit_idx];
                    if (clk_cnt == CLKS_PER_BIT - 1) begin
                        clk_cnt <= 0;
                        if (bit_idx == 7) begin
                            state <= STOP_BIT;
                        end else begin
                            bit_idx <= bit_idx + 1;
                        end
                    end else begin
                        clk_cnt <= clk_cnt + 1;
                    end
                end

                STOP_BIT: begin
                    tx <= 1;
                    if (clk_cnt == CLKS_PER_BIT - 1) begin
                        state <= IDLE;
                    end else begin
                        clk_cnt <= clk_cnt + 1;
                    end
                end
            endcase
        end
    end

endmodule
