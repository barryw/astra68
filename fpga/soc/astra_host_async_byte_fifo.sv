// Small dual-clock byte FIFO for the AstraHost SPI transport.
//
// The SPI pins are clocked by the ESP32 master, while debug_bridge runs in the
// SDRAM clock domain. This FIFO keeps those domains separated so the SPI slave
// can run from SCK instead of oversampling SCK with clk_pixel.
// Hardware-proven implementation carried forward from NovaVM's bridge; the
// AstraHost application protocol is implemented separately.

module astra_host_async_byte_fifo #(
    parameter int ADDR_WIDTH = 9
) (
    input  logic       wr_clk,
    input  logic       wr_rst,
    input  logic [7:0] wr_data,
    input  logic       wr_valid,
    output logic       wr_ready,

    input  logic       rd_clk,
    input  logic       rd_rst,
    output logic [7:0] rd_data,
    output logic       rd_valid,
    input  logic       rd_ready,

    output logic       overflow,
    output logic       underflow
);

    localparam int DEPTH = 1 << ADDR_WIDTH;

    (* ram_style = "block", ramstyle = "block" *)
    logic [7:0] mem [0:DEPTH-1];

    logic [ADDR_WIDTH:0] wr_bin;
    logic [ADDR_WIDTH:0] wr_gray;
    logic [ADDR_WIDTH:0] rd_bin;
    logic [ADDR_WIDTH:0] rd_gray;

    logic [ADDR_WIDTH:0] rd_gray_wr_1;
    logic [ADDR_WIDTH:0] rd_gray_wr_2;
    logic [ADDR_WIDTH:0] wr_gray_rd_1;
    logic [ADDR_WIDTH:0] wr_gray_rd_2;

    function automatic logic [ADDR_WIDTH:0] bin_to_gray;
        input logic [ADDR_WIDTH:0] value;
        begin
            bin_to_gray = (value >> 1) ^ value;
        end
    endfunction

    wire [ADDR_WIDTH:0] wr_bin_next = wr_bin + 1'b1;
    wire [ADDR_WIDTH:0] wr_gray_next = bin_to_gray(wr_bin_next);
    wire full = (wr_gray_next == {
        ~rd_gray_wr_2[ADDR_WIDTH:ADDR_WIDTH-1],
         rd_gray_wr_2[ADDR_WIDTH-2:0]
    });

    wire fifo_empty = (rd_gray == wr_gray_rd_2);
    wire push = wr_valid && !full;

    assign wr_ready = !full;

    logic [7:0] rd_data_reg;
    logic       rd_valid_reg;

    wire output_pop = rd_valid_reg && rd_ready;
    wire prefetch = (!rd_valid_reg || output_pop) && !fifo_empty;

    assign rd_valid = rd_valid_reg;
    assign rd_data = rd_data_reg;

    always_ff @(posedge wr_clk) begin
        if (push)
            mem[wr_bin[ADDR_WIDTH-1:0]] <= wr_data;
    end

    always_ff @(posedge wr_clk or posedge wr_rst) begin
        if (wr_rst) begin
            wr_bin      <= '0;
            wr_gray     <= '0;
            rd_gray_wr_1 <= '0;
            rd_gray_wr_2 <= '0;
            overflow    <= 1'b0;
        end else begin
            rd_gray_wr_1 <= rd_gray;
            rd_gray_wr_2 <= rd_gray_wr_1;

            if (wr_valid && full)
                overflow <= 1'b1;

            if (push) begin
                wr_bin  <= wr_bin_next;
                wr_gray <= wr_gray_next;
            end
        end
    end

    always_ff @(posedge rd_clk) begin
        if (prefetch)
            rd_data_reg <= mem[rd_bin[ADDR_WIDTH-1:0]];
    end

    always_ff @(posedge rd_clk or posedge rd_rst) begin
        if (rd_rst) begin
            rd_bin      <= '0;
            rd_gray     <= '0;
            wr_gray_rd_1 <= '0;
            wr_gray_rd_2 <= '0;
            rd_valid_reg <= 1'b0;
            underflow   <= 1'b0;
        end else begin
            wr_gray_rd_1 <= wr_gray;
            wr_gray_rd_2 <= wr_gray_rd_1;

            if (rd_ready && !rd_valid_reg && fifo_empty)
                underflow <= 1'b1;

            if (prefetch) begin
                rd_bin  <= rd_bin + 1'b1;
                rd_gray <= bin_to_gray(rd_bin + 1'b1);
                rd_valid_reg <= 1'b1;
            end else if (output_pop) begin
                rd_valid_reg <= 1'b0;
            end
        end
    end

endmodule
