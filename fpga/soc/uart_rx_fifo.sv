module uart_rx_fifo #(
    parameter integer DEPTH = 128
) (
    input  wire       clk,
    input  wire       rst,
    input  wire [7:0] push_data,
    input  wire       push,
    input  wire       pop,
    input  wire       clear_overrun,
    output wire [7:0] data,
    output wire       empty,
    output wire       full,
    output reg        overrun,
    output wire [7:0] level
);
    localparam integer ADDR_WIDTH = $clog2(DEPTH);
    localparam integer COUNT_WIDTH = $clog2(DEPTH + 1);

    reg [7:0] storage [0:DEPTH-1];
    reg [ADDR_WIDTH-1:0] read_ptr = {ADDR_WIDTH{1'b0}};
    reg [ADDR_WIDTH-1:0] write_ptr = {ADDR_WIDTH{1'b0}};
    reg [COUNT_WIDTH-1:0] count = {COUNT_WIDTH{1'b0}};

    wire do_pop = pop && !empty;
    wire do_push = push && (!full || do_pop);

    assign data = storage[read_ptr];
    assign empty = count == 0;
    assign full = count == DEPTH;
    assign level = {{(8-COUNT_WIDTH){1'b0}}, count};

    always @(posedge clk) begin
        if (rst) begin
            read_ptr <= {ADDR_WIDTH{1'b0}};
            write_ptr <= {ADDR_WIDTH{1'b0}};
            count <= {COUNT_WIDTH{1'b0}};
            overrun <= 1'b0;
        end else begin
            if (clear_overrun)
                overrun <= 1'b0;
            if (push && full && !do_pop)
                overrun <= 1'b1;

            if (do_push) begin
                storage[write_ptr] <= push_data;
                write_ptr <= write_ptr + 1'b1;
            end
            if (do_pop)
                read_ptr <= read_ptr + 1'b1;

            case ({do_push, do_pop})
                2'b10: count <= count + 1'b1;
                2'b01: count <= count - 1'b1;
                default: count <= count;
            endcase
        end
    end

    initial begin
        if (DEPTH < 2 || (DEPTH & (DEPTH - 1)) != 0)
            $error("uart_rx_fifo DEPTH must be a power of two >= 2");
        if (COUNT_WIDTH > 8)
            $error("uart_rx_fifo DEPTH must fit the 8-bit level output");
    end
endmodule
