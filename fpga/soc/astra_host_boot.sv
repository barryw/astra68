`timescale 1ns/1ps
`default_nettype none

// AstraHost boot protocol engine. The ESP32-facing physical transport provides
// byte streams; this block validates the command sequence and writes the ROM
// payload into SDRAM through the native DMA port.
module astra_host_boot #(
    parameter integer RX_STALL_CYCLES = 1500000
) (
    input  wire        clk,
    input  wire        rst,

    input  wire [7:0]  rx_data,
    input  wire        rx_valid,
    output reg         rx_ready,
    output reg  [7:0]  tx_data,
    output reg         tx_start,
    input  wire        tx_busy,

    input  wire        boot_request,
    output reg         host_seen,
    output reg         boot_busy,
    output reg         boot_done,
    output reg         boot_error,
    output reg  [7:0]  error_code,
    output reg  [31:0] payload_size,
    output reg  [31:0] payload_crc32,
    output reg  [31:0] initial_sp,
    output reg  [31:0] initial_pc,
    output reg  [31:0] bytes_received,

    output wire        mem_lock,
    output wire        mem_valid,
    input  wire        mem_ready,
    output wire        mem_write,
    output wire [24:0] mem_addr,
    output wire [3:0]  mem_be,
    output wire [31:0] mem_wdata,
    input  wire        mem_rsp_valid
);
    localparam [7:0] CMD_IDENTIFY    = 8'h01;
    localparam [7:0] CMD_BOOT_STATUS = 8'h02;
    localparam [7:0] CMD_BOOT_BEGIN  = 8'h10;
    localparam [7:0] CMD_BOOT_DATA   = 8'h11;
    localparam [7:0] CMD_BOOT_COMMIT = 8'h12;
    localparam [7:0] CMD_BOOT_ABORT  = 8'h13;

    localparam [7:0] STATUS_OK           = 8'h00;
    localparam [7:0] STATUS_BAD_COMMAND  = 8'h01;
    localparam [7:0] STATUS_BAD_STATE    = 8'h02;
    localparam [7:0] STATUS_BAD_ARGUMENT = 8'h03;
    localparam [7:0] STATUS_OVERFLOW     = 8'h04;
    localparam [7:0] STATUS_CRC           = 8'h05;
    localparam [7:0] STATUS_SIZE          = 8'h06;

    localparam [24:0] ROM_LOAD_OFFSET = 25'h1e00000;
    localparam [31:0] ROM_LOAD_ADDRESS = {7'd0, ROM_LOAD_OFFSET};
    localparam [31:0] ROM_MAX_BYTES = 32'h00040000;

    localparam [3:0] S_IDLE          = 4'd0;
    localparam [3:0] S_BEGIN         = 4'd1;
    localparam [3:0] S_DATA_COUNT    = 4'd2;
    localparam [3:0] S_DATA          = 4'd3;
    localparam [3:0] S_DATA_DRAIN    = 4'd4;
    localparam [3:0] S_COMMIT_FLUSH  = 4'd5;
    localparam [3:0] S_COMMIT_WAIT   = 4'd6;

    reg [3:0] state;
    reg [3:0] argument_index;
    reg [31:0] begin_size;
    reg [31:0] begin_crc;
    reg        begin_size_valid;
    reg        begin_address_valid;
    reg [8:0] data_remaining;
    reg [31:0] crc_state;
    reg [1:0] word_bytes;
    reg [31:0] word_data;
    reg [24:0] write_address;
    reg request_valid;
    reg wait_response;
    reg [24:0] request_addr;
    reg [3:0] request_be;
    reg [31:0] request_wdata;
    reg        issue_request;
    reg [24:0] issue_addr;
    reg [3:0]  issue_be;
    reg [31:0] issue_wdata;
    reg [31:0] stall_count;
    reg [7:0] rx_buffer_data;
    reg       rx_buffer_valid;
    reg       parser_ready;
    reg       reset_packer;
    reg       reset_writer;
    reg [7:0] packer_byte_data;
    reg       packer_byte_valid;
    // ROM_MAX_BYTES fits in 19 bits. The registered terminal flag keeps a
    // 32-bit count/size comparison out of the byte parser's control path.
    reg [18:0] payload_bytes_remaining;
    reg        payload_complete;

    reg [7:0] response [0:15];
    reg [4:0] response_length;
    reg [4:0] response_index;

    wire response_active = response_index < response_length;
    wire writer_idle = !issue_request && !request_valid && !wait_response;
    // Writer availability gates admission into the ingress register. Once a
    // byte is buffered, consuming it must not depend on the SDRAM response
    // path; that keeps the command datapath local without weakening backpressure.
    wire rx_byte_valid = rx_buffer_valid;
    wire packer_flush = state == S_COMMIT_FLUSH && writer_idle;
    wire [7:0] boot_flags = {3'd0, host_seen, boot_error, boot_done,
                             boot_busy, boot_request};

    assign mem_lock = boot_busy || issue_request || request_valid ||
                      wait_response;
    assign mem_valid = request_valid;
    assign mem_write = 1'b1;
    assign mem_addr = request_addr;
    assign mem_be = request_be;
    assign mem_wdata = request_wdata;

    function automatic [31:0] crc32_byte(
        input [31:0] crc,
        input [7:0] value
    );
        reg [31:0] current;
        integer bit_number;
        begin
            current = crc ^ value;
            for (bit_number = 0; bit_number < 8; bit_number = bit_number + 1)
                current = (current >> 1) ^
                          (32'hedb88320 & (0 - current[0]));
            crc32_byte = current;
        end
    endfunction

    task automatic queue_simple_response(input [7:0] status);
        begin
            response[0] <= status;
            response_length <= 5'd1;
            response_index <= 5'd0;
        end
    endtask

    task automatic queue_status_response(input [7:0] status);
        begin
            response[0] <= status;
            response[1] <= boot_flags;
            response[2] <= error_code;
            response[3] <= bytes_received[31:24];
            response[4] <= bytes_received[23:16];
            response[5] <= bytes_received[15:8];
            response[6] <= bytes_received[7:0];
            response_length <= 5'd7;
            response_index <= 5'd0;
        end
    endtask

    task automatic queue_identify_response;
        begin
            response[0] <= STATUS_OK;
            response[1] <= "A";
            response[2] <= "6";
            response[3] <= "8";
            response[4] <= "H";
            response[5] <= 8'd1;
            response[6] <= 8'd0;
            response[7] <= 8'h00;
            response[8] <= 8'h01; // boot-stream capability
            response[9] <= boot_flags;
            response[10] <= error_code;
            response[11] <= bytes_received[31:24];
            response[12] <= bytes_received[23:16];
            response[13] <= bytes_received[15:8];
            response[14] <= bytes_received[7:0];
            response_length <= 5'd15;
            response_index <= 5'd0;
        end
    endtask

    task automatic reset_transfer;
        begin
            boot_busy <= 1'b0;
            boot_done <= 1'b0;
            boot_error <= 1'b0;
            error_code <= STATUS_OK;
            payload_size <= 32'd0;
            payload_crc32 <= 32'd0;
            initial_sp <= 32'd0;
            initial_pc <= 32'd0;
            bytes_received <= 32'd0;
            payload_bytes_remaining <= 19'd0;
            payload_complete <= 1'b0;
            crc_state <= 32'hffffffff;
            reset_packer <= 1'b1;
            reset_writer <= 1'b1;
            packer_byte_valid <= 1'b0;
        end
    endtask

    always @* begin
        parser_ready = 1'b0;
        if (!response_active) begin
            case (state)
                S_IDLE, S_BEGIN, S_DATA_COUNT:
                    parser_ready = 1'b1;
                S_DATA:
                    parser_ready = writer_idle;
                default:
                    parser_ready = 1'b0;
            endcase
        end
        rx_ready = parser_ready && !rx_buffer_valid;
    end

    // Keep the ROM word packer independent of command validation. In
    // particular, BOOT_BEGIN's 32-bit size check must not become part of the
    // packer's clock-enable path in the 75 MHz SDRAM domain.
    always @(posedge clk) begin
        if (rst || reset_packer) begin
            word_bytes <= 2'd0;
            word_data <= 32'd0;
            write_address <= ROM_LOAD_OFFSET;
        end else if (packer_byte_valid) begin
            case (word_bytes)
                2'd0: begin
                    word_data[31:24] <= packer_byte_data;
                    word_bytes <= 2'd1;
                end
                2'd1: begin
                    word_data[23:16] <= packer_byte_data;
                    word_bytes <= 2'd2;
                end
                2'd2: begin
                    word_data[15:8] <= packer_byte_data;
                    word_bytes <= 2'd3;
                end
                default: begin
                    write_address <= write_address + 25'd4;
                    word_bytes <= 2'd0;
                end
            endcase
        end else if (packer_flush) begin
            write_address <= write_address + {23'd0, word_bytes};
            word_bytes <= 2'd0;
        end
    end

    // The command engine emits a registered request record. Keeping the SDRAM
    // handshake here prevents response generation and command validation from
    // becoming part of the DMA request registers' clock-enable paths.
    always @(posedge clk) begin
        if (rst || reset_writer) begin
            request_valid <= 1'b0;
            wait_response <= 1'b0;
            request_addr <= 25'd0;
            request_be <= 4'd0;
            request_wdata <= 32'd0;
        end else begin
            if (request_valid && mem_ready) begin
                request_valid <= 1'b0;
                wait_response <= 1'b1;
            end
            if (wait_response && mem_rsp_valid)
                wait_response <= 1'b0;

            if (issue_request) begin
                request_addr <= issue_addr;
                request_be <= issue_be;
                request_wdata <= issue_wdata;
                request_valid <= 1'b1;
            end
        end
    end

    always @(posedge clk) begin
        tx_start <= 1'b0;
        reset_packer <= 1'b0;
        reset_writer <= 1'b0;
        packer_byte_valid <= 1'b0;
        issue_request <= 1'b0;
        if (rst) begin
            state <= S_IDLE;
            argument_index <= 4'd0;
            begin_size <= 32'd0;
            begin_crc <= 32'd0;
            begin_size_valid <= 1'b0;
            begin_address_valid <= 1'b0;
            data_remaining <= 9'd0;
            host_seen <= 1'b0;
            response_length <= 5'd0;
            response_index <= 5'd0;
            response[15] <= 8'd0;
            issue_addr <= 25'd0;
            issue_be <= 4'd0;
            issue_wdata <= 32'd0;
            stall_count <= 32'd0;
            rx_buffer_data <= 8'd0;
            rx_buffer_valid <= 1'b0;
            packer_byte_data <= 8'd0;
            reset_transfer();
        end else begin
            // Isolate the block-RAM FIFO clock-to-Q path from command decode.
            // One byte every two memory clocks still exceeds 20 MHz SPI demand.
            if (rx_byte_valid)
                rx_buffer_valid <= 1'b0;
            if (rx_valid && rx_ready) begin
                rx_buffer_data <= rx_data;
                rx_buffer_valid <= 1'b1;
            end

            if (response_active && !tx_busy) begin
                tx_data <= response[response_index];
                tx_start <= 1'b1;
                response_index <= response_index + 1'b1;
            end

            if (state != S_IDLE && state != S_DATA_DRAIN &&
                state != S_COMMIT_FLUSH && state != S_COMMIT_WAIT &&
                rx_ready && !rx_valid) begin
                if (stall_count == RX_STALL_CYCLES - 1) begin
                    state <= S_IDLE;
                    stall_count <= 32'd0;
                    queue_simple_response(STATUS_BAD_ARGUMENT);
                end else begin
                    stall_count <= stall_count + 1'b1;
                end
            end else begin
                stall_count <= 32'd0;
            end

            case (state)
                S_IDLE: begin
                    if (rx_byte_valid) begin
                        host_seen <= 1'b1;
                        case (rx_buffer_data)
                            CMD_IDENTIFY: queue_identify_response();
                            CMD_BOOT_STATUS:
                                queue_status_response(STATUS_OK);
                            CMD_BOOT_BEGIN: begin
                                if (!boot_request || boot_busy || boot_done) begin
                                    queue_simple_response(STATUS_BAD_STATE);
                                end else begin
                                    argument_index <= 4'd0;
                                    begin_size <= 32'd0;
                                    begin_crc <= 32'd0;
                                    begin_size_valid <= 1'b0;
                                    begin_address_valid <= 1'b1;
                                    state <= S_BEGIN;
                                end
                            end
                            CMD_BOOT_DATA: begin
                                if (!boot_busy) begin
                                    queue_simple_response(STATUS_BAD_STATE);
                                end else begin
                                    state <= S_DATA_COUNT;
                                end
                            end
                            CMD_BOOT_COMMIT: begin
                                if (!boot_busy) begin
                                    queue_simple_response(STATUS_BAD_STATE);
                                end else if (word_bytes != 0) begin
                                    state <= S_COMMIT_FLUSH;
                                end else begin
                                    state <= S_COMMIT_WAIT;
                                end
                            end
                            CMD_BOOT_ABORT: begin
                                reset_transfer();
                                queue_simple_response(STATUS_OK);
                            end
                            default:
                                queue_simple_response(STATUS_BAD_COMMAND);
                        endcase
                    end
                end

                S_BEGIN: begin
                    if (rx_byte_valid) begin
                        if (argument_index < 4) begin
                            begin_size <= {begin_size[23:0], rx_buffer_data};
                            if (argument_index == 3)
                                begin_size_valid <=
                                    {begin_size[23:0], rx_buffer_data} >= 8 &&
                                    {begin_size[23:0], rx_buffer_data} <=
                                        ROM_MAX_BYTES;
                        end else if (argument_index < 8) begin
                            begin_crc <= {begin_crc[23:0], rx_buffer_data};
                        end else begin
                            case (argument_index)
                                4'd8: begin_address_valid <=
                                    rx_buffer_data == ROM_LOAD_ADDRESS[31:24];
                                4'd9: begin_address_valid <=
                                    begin_address_valid &&
                                    rx_buffer_data == ROM_LOAD_ADDRESS[23:16];
                                4'd10: begin_address_valid <=
                                    begin_address_valid &&
                                    rx_buffer_data == ROM_LOAD_ADDRESS[15:8];
                                default: begin end
                            endcase
                        end

                        if (argument_index == 11) begin
                            state <= S_IDLE;
                            if (!begin_size_valid || !begin_address_valid ||
                                rx_buffer_data != ROM_LOAD_ADDRESS[7:0]) begin
                                queue_simple_response(STATUS_BAD_ARGUMENT);
                            end else begin
                                reset_transfer();
                                boot_busy <= 1'b1;
                                payload_size <= begin_size;
                                payload_crc32 <= begin_crc;
                                payload_bytes_remaining <= begin_size[18:0];
                                payload_complete <= 1'b0;
                                queue_simple_response(STATUS_OK);
                            end
                        end else begin
                            argument_index <= argument_index + 1'b1;
                        end
                    end
                end

                S_DATA_COUNT: begin
                    if (rx_byte_valid) begin
                        data_remaining <= rx_buffer_data == 0 ? 9'd256 :
                                          {1'b0, rx_buffer_data};
                        state <= S_DATA;
                    end
                end

                S_DATA: begin
                    if (rx_byte_valid) begin
                        if (payload_complete) begin
                            boot_busy <= 1'b0;
                            boot_error <= 1'b1;
                            error_code <= STATUS_OVERFLOW;
                            state <= S_IDLE;
                            queue_simple_response(STATUS_OVERFLOW);
                        end else begin
                            crc_state <= crc32_byte(crc_state, rx_buffer_data);
                            if (bytes_received < 4)
                                initial_sp <= {initial_sp[23:0],
                                               rx_buffer_data};
                            else if (bytes_received < 8)
                                initial_pc <= {initial_pc[23:0],
                                               rx_buffer_data};
                            bytes_received <= bytes_received + 1'b1;
                            payload_bytes_remaining <=
                                payload_bytes_remaining - 1'b1;
                            if (payload_bytes_remaining == 19'd1)
                                payload_complete <= 1'b1;
                            packer_byte_data <= rx_buffer_data;
                            packer_byte_valid <= 1'b1;

                            if (word_bytes == 2'd3) begin
                                issue_addr <= write_address;
                                issue_be <= 4'b1111;
                                issue_wdata <= {word_data[31:8],
                                                rx_buffer_data};
                                issue_request <= 1'b1;
                            end

                            if (data_remaining == 1) begin
                                data_remaining <= 9'd0;
                                state <= S_DATA_DRAIN;
                            end else begin
                                data_remaining <= data_remaining - 1'b1;
                            end
                        end
                    end
                end

                S_DATA_DRAIN: begin
                    if (writer_idle) begin
                        state <= S_IDLE;
                        queue_simple_response(STATUS_OK);
                    end
                end

                S_COMMIT_FLUSH: begin
                    if (writer_idle) begin
                        issue_addr <= write_address;
                        issue_wdata <= word_data;
                        case (word_bytes)
                            2'd1: issue_be <= 4'b1000;
                            2'd2: issue_be <= 4'b1100;
                            default: issue_be <= 4'b1110;
                        endcase
                        issue_request <= 1'b1;
                        state <= S_COMMIT_WAIT;
                    end
                end

                S_COMMIT_WAIT: begin
                    if (writer_idle) begin
                        state <= S_IDLE;
                        boot_busy <= 1'b0;
                        if (!payload_complete) begin
                            boot_error <= 1'b1;
                            error_code <= STATUS_SIZE;
                            queue_simple_response(STATUS_SIZE);
                        end else if (~crc_state != payload_crc32) begin
                            boot_error <= 1'b1;
                            error_code <= STATUS_CRC;
                            queue_simple_response(STATUS_CRC);
                        end else begin
                            boot_done <= 1'b1;
                            error_code <= STATUS_OK;
                            queue_simple_response(STATUS_OK);
                        end
                    end
                end

                default: state <= S_IDLE;
            endcase
        end
    end
endmodule

`default_nettype wire
