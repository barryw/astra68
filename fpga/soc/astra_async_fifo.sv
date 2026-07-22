`timescale 1ns/1ps
`default_nettype none

// Dual-clock FIFO for fixed-width Astra service records. Both reset inputs
// must be asserted together; their release may occur independently in each
// clock domain. Gray-coded pointers are synchronized before full/empty tests.
module astra_async_fifo #(
    parameter integer DATA_WIDTH = 32,
    parameter integer ADDR_WIDTH = 2
) (
    input  wire                       wr_clk,
    input  wire                       wr_rst,
    input  wire [DATA_WIDTH-1:0]      wr_data,
    input  wire                       wr_valid,
    output wire                       wr_ready,
    output wire [ADDR_WIDTH:0]        wr_level,

    input  wire                       rd_clk,
    input  wire                       rd_rst,
    output wire [DATA_WIDTH-1:0]      rd_data,
    output wire                       rd_valid,
    input  wire                       rd_ready,
    output wire [ADDR_WIDTH:0]        rd_level,

    output reg                        overflow,
    output reg                        underflow
);
    localparam integer DEPTH = 1 << ADDR_WIDTH;

    (* ram_style = "block", ramstyle = "block" *)
    reg [DATA_WIDTH-1:0] memory [0:DEPTH-1];

    reg [ADDR_WIDTH:0] wr_binary;
    reg [ADDR_WIDTH:0] wr_gray;
    reg [ADDR_WIDTH:0] rd_binary;
    reg [ADDR_WIDTH:0] rd_gray;
    reg [ADDR_WIDTH:0] rd_gray_wr_1;
    reg [ADDR_WIDTH:0] rd_gray_wr_2;
    reg [ADDR_WIDTH:0] wr_gray_rd_1;
    reg [ADDR_WIDTH:0] wr_gray_rd_2;

    reg [DATA_WIDTH-1:0] rd_data_reg;
    reg                  rd_valid_reg;

    function automatic [ADDR_WIDTH:0] binary_to_gray(
        input [ADDR_WIDTH:0] value
    );
        binary_to_gray = (value >> 1) ^ value;
    endfunction

    function automatic [ADDR_WIDTH:0] gray_to_binary(
        input [ADDR_WIDTH:0] value
    );
        integer bit_index;
        begin
            gray_to_binary[ADDR_WIDTH] = value[ADDR_WIDTH];
            for (bit_index = ADDR_WIDTH - 1; bit_index >= 0;
                 bit_index = bit_index - 1)
                gray_to_binary[bit_index] =
                    gray_to_binary[bit_index + 1] ^ value[bit_index];
        end
    endfunction

    wire [ADDR_WIDTH:0] wr_binary_next = wr_binary + 1'b1;
    wire [ADDR_WIDTH:0] wr_gray_next = binary_to_gray(wr_binary_next);
    wire full = wr_gray == {
        ~rd_gray_wr_2[ADDR_WIDTH:ADDR_WIDTH-1],
         rd_gray_wr_2[ADDR_WIDTH-2:0]
    };
    wire empty = rd_gray == wr_gray_rd_2;
    wire push = wr_valid && !full;
    wire pop = rd_valid_reg && rd_ready;
    wire prefetch = (!rd_valid_reg || pop) && !empty;

    assign wr_ready = !full;
    assign rd_data = rd_data_reg;
    assign rd_valid = rd_valid_reg;
    assign wr_level = wr_binary - gray_to_binary(rd_gray_wr_2);
    assign rd_level = gray_to_binary(wr_gray_rd_2) - rd_binary +
                      {{ADDR_WIDTH{1'b0}}, rd_valid_reg};

    always @(posedge wr_clk) begin
        if (push)
            memory[wr_binary[ADDR_WIDTH-1:0]] <= wr_data;
    end

    always @(posedge wr_clk or posedge wr_rst) begin
        if (wr_rst) begin
            wr_binary <= {ADDR_WIDTH+1{1'b0}};
            wr_gray <= {ADDR_WIDTH+1{1'b0}};
            rd_gray_wr_1 <= {ADDR_WIDTH+1{1'b0}};
            rd_gray_wr_2 <= {ADDR_WIDTH+1{1'b0}};
            overflow <= 1'b0;
        end else begin
            rd_gray_wr_1 <= rd_gray;
            rd_gray_wr_2 <= rd_gray_wr_1;
            if (wr_valid && full)
                overflow <= 1'b1;
            if (push) begin
                wr_binary <= wr_binary_next;
                wr_gray <= wr_gray_next;
            end
        end
    end

    always @(posedge rd_clk) begin
        if (prefetch)
            rd_data_reg <= memory[rd_binary[ADDR_WIDTH-1:0]];
    end

    always @(posedge rd_clk or posedge rd_rst) begin
        if (rd_rst) begin
            rd_binary <= {ADDR_WIDTH+1{1'b0}};
            rd_gray <= {ADDR_WIDTH+1{1'b0}};
            wr_gray_rd_1 <= {ADDR_WIDTH+1{1'b0}};
            wr_gray_rd_2 <= {ADDR_WIDTH+1{1'b0}};
            rd_valid_reg <= 1'b0;
            underflow <= 1'b0;
        end else begin
            wr_gray_rd_1 <= wr_gray;
            wr_gray_rd_2 <= wr_gray_rd_1;
            if (rd_ready && !rd_valid_reg && empty)
                underflow <= 1'b1;
            if (prefetch) begin
                rd_binary <= rd_binary + 1'b1;
                rd_gray <= binary_to_gray(rd_binary + 1'b1);
                rd_valid_reg <= 1'b1;
            end else if (pop) begin
                rd_valid_reg <= 1'b0;
            end
        end
    end

`ifndef SYNTHESIS
    initial begin
        if (ADDR_WIDTH < 2)
            $fatal(1, "astra_async_fifo ADDR_WIDTH must be at least 2");
        if (DATA_WIDTH < 1)
            $fatal(1, "astra_async_fifo DATA_WIDTH must be positive");
    end
`endif
endmodule

`default_nettype wire
