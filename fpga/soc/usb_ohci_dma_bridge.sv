// OHCI Wishbone master to Astra's ordered SDRAM request/response interface.
// The generated OHCI engine and SDRAM controller intentionally use different
// clocks. Small asynchronous FIFOs carry one ordered request and response at a
// time without placing OHCI state/decode logic in the 60 MHz memory domain.
// OHCI descriptors are little-endian; Astra SDRAM words use 68030 big-endian
// byte lanes, so data and select lanes are reversed at the memory boundary.
`default_nettype none

module usb_ohci_dma_bridge #(
    parameter [31:0] SDRAM_BASE = 32'h02000000,
    parameter [32:0] SDRAM_BYTES = 33'd33554432
) (
    input  wire        wb_clk,
    input  wire        wb_rst,
    input  wire        wb_cyc,
    input  wire        wb_stb,
    input  wire        wb_we,
    input  wire [29:0] wb_addr,
    input  wire [31:0] wb_wdata,
    input  wire [3:0]  wb_sel,
    output wire        wb_ack,
    output wire        wb_err,
    output wire [31:0] wb_rdata,

    input  wire        mem_clk,
    input  wire        mem_rst,
    output wire        mem_lock,
    output reg         mem_valid,
    input  wire        mem_ready,
    output reg         mem_write,
    output reg  [24:0] mem_addr,
    output reg  [3:0]  mem_be,
    output reg  [31:0] mem_wdata,
    input  wire        mem_rsp_valid,
    input  wire [31:0] mem_rdata,

    input  wire        fault_clear,
    output reg         fault,
    output reg  [31:0] fault_addr
);
    localparam integer REQUEST_WIDTH = 67;
    localparam [2:0] MEM_IDLE = 3'd0;
    localparam [2:0] MEM_DECODE = 3'd1;
    localparam [2:0] MEM_REQUEST = 3'd2;
    localparam [2:0] MEM_RESPONSE = 3'd3;
    localparam [2:0] MEM_RETURN = 3'd4;
    localparam [32:0] SDRAM_BASE_EXT = {1'b0, SDRAM_BASE};
    localparam [32:0] SDRAM_LAST_WORD_EXT =
        SDRAM_BASE_EXT + SDRAM_BYTES - 33'd4;

    wire [REQUEST_WIDTH-1:0] request_write_data = {
        wb_we, wb_addr, wb_sel, wb_wdata
    };
    wire request_write_ready;
    wire request_write_valid;
    wire [2:0] request_write_level;
    wire request_overflow;
    wire request_underflow;

    wire [REQUEST_WIDTH-1:0] request_read_data;
    wire request_read_valid;
    wire request_read_ready;
    wire [2:0] request_read_level;

    wire [31:0] response_write_data;
    wire response_write_valid;
    wire response_write_ready;
    wire [2:0] response_write_level;
    wire response_overflow;
    wire response_underflow;

    wire [31:0] response_read_data;
    wire response_read_valid;
    wire response_read_ready;
    wire [2:0] response_read_level;

    reg wb_pending;
    assign request_write_valid = !wb_pending && wb_cyc && wb_stb;
    assign response_read_ready = wb_pending && response_read_valid;
    assign wb_ack = response_read_ready;
    assign wb_err = 1'b0;
    assign wb_rdata = response_read_data;

    always @(posedge wb_clk or posedge wb_rst) begin
        if (wb_rst) begin
            wb_pending <= 1'b0;
        end else begin
            if (request_write_valid && request_write_ready)
                wb_pending <= 1'b1;
            if (response_read_ready)
                wb_pending <= 1'b0;
        end
    end

    astra_async_fifo #(
        .DATA_WIDTH(REQUEST_WIDTH),
        .ADDR_WIDTH(2)
    ) request_fifo_i (
        .wr_clk(wb_clk), .wr_rst(wb_rst),
        .wr_data(request_write_data), .wr_valid(request_write_valid),
        .wr_ready(request_write_ready), .wr_level(request_write_level),
        .rd_clk(mem_clk), .rd_rst(mem_rst),
        .rd_data(request_read_data), .rd_valid(request_read_valid),
        .rd_ready(request_read_ready), .rd_level(request_read_level),
        .overflow(request_overflow), .underflow(request_underflow)
    );

    astra_async_fifo #(
        .DATA_WIDTH(32),
        .ADDR_WIDTH(2)
    ) response_fifo_i (
        .wr_clk(mem_clk), .wr_rst(mem_rst),
        .wr_data(response_write_data), .wr_valid(response_write_valid),
        .wr_ready(response_write_ready), .wr_level(response_write_level),
        .rd_clk(wb_clk), .rd_rst(wb_rst),
        .rd_data(response_read_data), .rd_valid(response_read_valid),
        .rd_ready(response_read_ready), .rd_level(response_read_level),
        .overflow(response_overflow), .underflow(response_underflow)
    );

    reg request_we_mem;
    reg [31:0] request_byte_addr_mem;
    reg [3:0] request_sel_mem;
    reg [31:0] request_wdata_mem;
    wire [32:0] request_byte_addr_ext = {1'b0, request_byte_addr_mem};
    wire request_address_valid =
        request_byte_addr_ext >= SDRAM_BASE_EXT &&
        request_byte_addr_ext <= SDRAM_LAST_WORD_EXT;

    function automatic [31:0] byte_swap32(input [31:0] value);
        byte_swap32 = {
            value[7:0], value[15:8], value[23:16], value[31:24]
        };
    endfunction

    reg [2:0] mem_state;
    reg [31:0] response_data;
    (* async_reg = "true" *) reg [1:0] wb_cyc_sync_mem;

    assign request_read_ready = mem_state == MEM_IDLE;
    assign response_write_valid = mem_state == MEM_RETURN;
    assign response_write_data = response_data;
    assign mem_lock = wb_cyc_sync_mem[1] ||
                      mem_state != MEM_IDLE || request_read_valid;

    always @(posedge mem_clk or posedge mem_rst) begin
        if (mem_rst)
            wb_cyc_sync_mem <= 2'b00;
        else
            wb_cyc_sync_mem <= {wb_cyc_sync_mem[0], wb_cyc};
    end

    always @(posedge mem_clk or posedge mem_rst) begin
        if (mem_rst) begin
            mem_state <= MEM_IDLE;
            response_data <= 32'd0;
            mem_valid <= 1'b0;
            mem_write <= 1'b0;
            mem_addr <= 25'd0;
            mem_be <= 4'd0;
            mem_wdata <= 32'd0;
            request_we_mem <= 1'b0;
            request_byte_addr_mem <= 32'd0;
            request_sel_mem <= 4'd0;
            request_wdata_mem <= 32'd0;
            fault <= 1'b0;
            fault_addr <= 32'd0;
        end else begin
            if (fault_clear)
                fault <= 1'b0;

            case (mem_state)
                MEM_IDLE: begin
                    mem_valid <= 1'b0;
                    if (request_read_valid) begin
                        request_we_mem <= request_read_data[66];
                        request_byte_addr_mem <= {
                            request_read_data[65:36], 2'b00
                        };
                        request_sel_mem <= request_read_data[35:32];
                        request_wdata_mem <= request_read_data[31:0];
                        mem_state <= MEM_DECODE;
                    end
                end
                MEM_DECODE: begin
                    if (request_address_valid) begin
                        mem_write <= request_we_mem;
                        mem_addr <= request_byte_addr_mem - SDRAM_BASE;
                        mem_be <= {
                            request_sel_mem[0], request_sel_mem[1],
                            request_sel_mem[2], request_sel_mem[3]
                        };
                        mem_wdata <= byte_swap32(request_wdata_mem);
                        mem_valid <= 1'b1;
                        mem_state <= MEM_REQUEST;
                    end else begin
                        response_data <= 32'd0;
                        fault <= 1'b1;
                        fault_addr <= request_byte_addr_mem;
                        mem_state <= MEM_RETURN;
                    end
                end
                MEM_REQUEST: begin
                    if (mem_ready) begin
                        mem_valid <= 1'b0;
                        mem_state <= MEM_RESPONSE;
                    end
                end
                MEM_RESPONSE: begin
                    if (mem_rsp_valid) begin
                        response_data <= byte_swap32(mem_rdata);
                        mem_state <= MEM_RETURN;
                    end
                end
                MEM_RETURN: begin
                    if (response_write_ready)
                        mem_state <= MEM_IDLE;
                end
                default: mem_state <= MEM_IDLE;
            endcase
        end
    end

    wire _unused = &{
        1'b0, request_write_level, request_read_level,
        response_write_level, response_read_level,
        request_overflow, request_underflow,
        response_overflow, response_underflow
    };
endmodule

`default_nettype wire
