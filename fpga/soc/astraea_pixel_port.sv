// Exact big-endian 8/16/32-bit transaction port. Transfers are split at SDRAM
// word boundaries, so unaligned graphics data never modifies adjacent bytes.
`default_nettype none

module astraea_pixel_port (
    input  wire        clk,
    input  wire        rst,
    input  wire        start,
    input  wire        write,
    input  wire [1:0]  size,
    input  wire [24:0] byte_addr,
    input  wire [31:0] wdata,
    output reg  [31:0] rdata,
    output reg         busy,
    output reg         done,

    output wire        mem_lock,
    output wire        mem_valid,
    input  wire        mem_ready,
    output wire        mem_write,
    output wire [24:0] mem_addr,
    output wire [3:0]  mem_be,
    output wire [31:0] mem_wdata,
    input  wire        mem_rsp_valid,
    input  wire [31:0] mem_rdata
);
    localparam [1:0] SIZE_BYTE = 2'd0;
    localparam [1:0] SIZE_WORD = 2'd1;
    localparam [1:0] SIZE_LONG = 2'd2;

    localparam [1:0] ST_IDLE = 2'd0;
    localparam [1:0] ST_ISSUE = 2'd1;
    localparam [1:0] ST_WAIT = 2'd2;

    function automatic [3:0] byte_enable(input [1:0] lane);
        byte_enable = 4'b1000 >> lane;
    endfunction

    function automatic [31:0] place_byte(
        input [1:0] lane,
        input [7:0] value
    );
        begin
            case (lane)
                2'd0: place_byte = {value, 24'd0};
                2'd1: place_byte = {8'd0, value, 16'd0};
                2'd2: place_byte = {16'd0, value, 8'd0};
                default: place_byte = {24'd0, value};
            endcase
        end
    endfunction

    function automatic [7:0] select_byte(
        input [1:0] lane,
        input [31:0] value
    );
        begin
            case (lane)
                2'd0: select_byte = value[31:24];
                2'd1: select_byte = value[23:16];
                2'd2: select_byte = value[15:8];
                default: select_byte = value[7:0];
            endcase
        end
    endfunction

    function automatic [7:0] operation_byte(
        input [1:0] operation_size,
        input [1:0] index,
        input [31:0] value
    );
        begin
            case (operation_size)
                SIZE_BYTE: operation_byte = value[7:0];
                SIZE_WORD: operation_byte = index[0] ? value[7:0] :
                                                       value[15:8];
                default: begin
                    case (index)
                        2'd0: operation_byte = value[31:24];
                        2'd1: operation_byte = value[23:16];
                        2'd2: operation_byte = value[15:8];
                        default: operation_byte = value[7:0];
                    endcase
                end
            endcase
        end
    endfunction

    function automatic [31:0] put_operation_byte(
        input [31:0] old_value,
        input [1:0] operation_size,
        input [1:0] index,
        input [7:0] value
    );
        reg [31:0] result;
        begin
            result = old_value;
            case (operation_size)
                SIZE_BYTE: result[7:0] = value;
                SIZE_WORD: if (!index[0]) result[15:8] = value;
                           else result[7:0] = value;
                default: begin
                    case (index)
                        2'd0: result[31:24] = value;
                        2'd1: result[23:16] = value;
                        2'd2: result[15:8] = value;
                        default: result[7:0] = value;
                    endcase
                end
            endcase
            put_operation_byte = result;
        end
    endfunction

    reg [1:0] state;
    reg operation_write;
    reg [1:0] operation_size;
    reg [24:0] current_addr;
    reg [31:0] write_value;
    reg [1:0] byte_index;

    wire [1:0] last_byte_index = operation_size == SIZE_BYTE ? 2'd0 :
                                 operation_size == SIZE_WORD ? 2'd1 : 2'd3;
    wire [7:0] current_write_byte =
        operation_byte(operation_size, byte_index, write_value);

    assign mem_lock = state != ST_IDLE;
    assign mem_valid = state == ST_ISSUE;
    assign mem_write = operation_write;
    assign mem_addr = {current_addr[24:2], 2'b00};
    assign mem_be = operation_write ? byte_enable(current_addr[1:0]) :
                                      4'b1111;
    assign mem_wdata = place_byte(current_addr[1:0], current_write_byte);

    always @(posedge clk) begin
        done <= 1'b0;
        if (rst) begin
            state <= ST_IDLE;
            operation_write <= 1'b0;
            operation_size <= SIZE_BYTE;
            current_addr <= 25'd0;
            write_value <= 32'd0;
            rdata <= 32'd0;
            byte_index <= 2'd0;
            busy <= 1'b0;
            done <= 1'b0;
        end else begin
            case (state)
                ST_IDLE: begin
                    busy <= 1'b0;
                    if (start) begin
                        operation_write <= write;
                        operation_size <= size > SIZE_LONG ? SIZE_LONG : size;
                        current_addr <= byte_addr;
                        write_value <= wdata;
                        rdata <= 32'd0;
                        byte_index <= 2'd0;
                        busy <= 1'b1;
                        state <= ST_ISSUE;
                    end
                end
                ST_ISSUE: begin
                    if (mem_ready)
                        state <= ST_WAIT;
                end
                ST_WAIT: begin
                    if (mem_rsp_valid) begin
                        if (!operation_write)
                            rdata <= put_operation_byte(
                                rdata, operation_size, byte_index,
                                select_byte(current_addr[1:0], mem_rdata));
                        if (byte_index != last_byte_index) begin
                            current_addr <= current_addr + 25'd1;
                            byte_index <= byte_index + 2'd1;
                            state <= ST_ISSUE;
                        end else begin
                            busy <= 1'b0;
                            done <= 1'b1;
                            state <= ST_IDLE;
                        end
                    end
                end
                default: begin
                    busy <= 1'b0;
                    done <= 1'b1;
                    state <= ST_IDLE;
                end
            endcase
        end
    end
endmodule

`default_nettype wire
