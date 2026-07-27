`timescale 1ns/1ps
`default_nettype none

// AstraHost SPI application service. Legacy boot streaming and the versioned
// runtime block/input protocol share one parser and one native SDRAM DMA port.
module astra_host_service #(
    parameter integer RX_STALL_CYCLES = 1200000,
    parameter integer ACTIVE_TIMEOUT_CYCLES = 300000000,
    parameter [31:0] SDRAM_CPU_BASE = 32'h02000000
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
    input  wire        mem_rsp_valid,
    input  wire [31:0] mem_rdata,
    output wire        cache_flush,

    input  wire        front_guard_valid,
    input  wire [24:0] front_guard_start,
    input  wire [25:0] front_guard_end,
    input  wire        pending_guard_valid,
    input  wire [24:0] pending_guard_start,
    input  wire [25:0] pending_guard_end,

    input  wire        runtime_request_valid,
    output reg         runtime_request_ready,
    input  wire [31:0] runtime_request_id,
    input  wire [7:0]  runtime_request_op,
    input  wire [7:0]  runtime_request_flags,
    input  wire [63:0] runtime_request_lba,
    input  wire [15:0] runtime_request_sectors,
    input  wire [31:0] runtime_request_buffer,
    input  wire [31:0] runtime_request_media_generation,
    input  wire [31:0] runtime_request_host_generation,

    output reg         runtime_completion_valid,
    input  wire        runtime_completion_ready,
    output reg  [31:0] runtime_completion_id,
    output reg  [15:0] runtime_completion_status,
    output reg  [15:0] runtime_completion_sectors,
    output reg  [31:0] runtime_completion_detail,
    output reg  [31:0] runtime_completion_media_generation,
    output reg  [31:0] runtime_completion_host_generation,

    output reg         runtime_state_valid,
    input  wire        runtime_state_ready,
    output reg  [31:0] runtime_state_host_generation,
    output reg  [31:0] runtime_state_media_generation,
    output reg  [31:0] runtime_state_flags,
    output reg  [63:0] runtime_state_media_sectors,
    output reg  [15:0] runtime_state_max_sectors,

    output reg         runtime_event_valid,
    input  wire        runtime_event_ready,
    output reg  [31:0] runtime_event_host_generation,
    output reg  [31:0] runtime_event_header,
    output reg  [31:0] runtime_event_value,
    output reg  [31:0] runtime_event_timestamp,
    output reg  [31:0] runtime_event_device_sequence,

    output reg         runtime_monitor_input_valid,
    input  wire        runtime_monitor_input_ready,
    output reg  [7:0]  runtime_monitor_input_data,
    input  wire        runtime_monitor_output_valid,
    output reg         runtime_monitor_output_ready,
    input  wire [7:0]  runtime_monitor_output_data
);
    localparam [7:0] CMD_IDENTIFY    = 8'h01;
    localparam [7:0] CMD_BOOT_STATUS = 8'h02;
    localparam [7:0] CMD_BOOT_BEGIN  = 8'h10;
    localparam [7:0] CMD_BOOT_DATA   = 8'h11;
    localparam [7:0] CMD_BOOT_COMMIT = 8'h12;
    localparam [7:0] CMD_BOOT_ABORT  = 8'h13;
    localparam [7:0] CMD_SERVICE_HELLO = 8'h20;
    localparam [7:0] CMD_BLOCK_POLL    = 8'h21;
    localparam [7:0] CMD_BLOCK_PUSH    = 8'h22;
    localparam [7:0] CMD_BLOCK_FETCH   = 8'h23;
    localparam [7:0] CMD_BLOCK_COMPLETE = 8'h24;
    localparam [7:0] CMD_INPUT_EVENT    = 8'h30;
    localparam [7:0] CMD_MONITOR_WRITE  = 8'h31;
    localparam [7:0] CMD_MONITOR_READ   = 8'h32;

    localparam [7:0] STATUS_OK           = 8'h00;
    localparam [7:0] STATUS_BAD_COMMAND  = 8'h01;
    localparam [7:0] STATUS_BAD_STATE    = 8'h02;
    localparam [7:0] STATUS_BAD_ARGUMENT = 8'h03;
    localparam [7:0] STATUS_OVERFLOW     = 8'h04;
    localparam [7:0] STATUS_CRC           = 8'h05;
    localparam [7:0] STATUS_SIZE          = 8'h06;
    localparam [7:0] STATUS_BUSY          = 8'h07;
    localparam [7:0] STATUS_STALE         = 8'h08;
    localparam [7:0] STATUS_PROTECTED     = 8'h09;

    localparam [7:0] BLOCK_OP_READ  = 8'd1;
    localparam [7:0] BLOCK_OP_WRITE = 8'd2;

    localparam [31:0] STATE_LINK_UP       = 32'h00000001;
    localparam [31:0] STATE_MEDIA_PRESENT = 32'h00000002;
    localparam [31:0] STATE_WRITE_ENABLE  = 32'h00000004;
    localparam [31:0] STATE_VALID_MASK    = 32'h00000007;
    localparam [15:0] SERVICE_MAX_SECTORS = 16'd16;

    localparam [15:0] CPL_OK            = 16'h0000;
    localparam [15:0] CPL_TIMEOUT       = 16'h0004;
    localparam [15:0] CPL_HOST_RESET    = 16'h0005;
    localparam [15:0] CPL_MEDIA_CHANGED = 16'h0006;

    localparam [24:0] ROM_LOAD_OFFSET = 25'h1e00000;
    localparam [31:0] ROM_LOAD_ADDRESS = {7'd0, ROM_LOAD_OFFSET};
    localparam [31:0] ROM_MAX_BYTES = 32'h00040000;

    // One-hot state is deliberate here. This controller has many independent
    // protocol exits; binary decoding puts every transition behind a deep
    // decode/next-state mux at the 60 MHz service clock.
    localparam [28:0] S_IDLE                = 29'h00000001;
    localparam [28:0] S_BEGIN               = 29'h00000002;
    localparam [28:0] S_DATA_COUNT          = 29'h00000004;
    localparam [28:0] S_DATA                = 29'h00000008;
    localparam [28:0] S_DATA_DRAIN          = 29'h00000010;
    localparam [28:0] S_COMMIT_FLUSH        = 29'h00000020;
    localparam [28:0] S_COMMIT_WAIT         = 29'h00000040;
    localparam [28:0] S_RT_LEN_HI           = 29'h00000080;
    localparam [28:0] S_RT_LEN_LO           = 29'h00000100;
    localparam [28:0] S_RT_DATA             = 29'h00000200;
    localparam [28:0] S_RT_DRAIN            = 29'h00000400;
    localparam [28:0] S_RT_DISPATCH         = 29'h00000800;
    localparam [28:0] S_RT_WAIT_DMA         = 29'h00001000;
    localparam [28:0] S_RT_WAIT_STREAM      = 29'h00002000;
    localparam [28:0] S_RT_WAIT_CPL         = 29'h00004000;
    localparam [28:0] S_RT_WAIT_STATE       = 29'h00008000;
    localparam [28:0] S_RT_WAIT_EVENT       = 29'h00010000;
    localparam [28:0] S_RT_ACCEPT_REQUEST   = 29'h00020000;
    localparam [28:0] S_RT_POLL_RESPONSE    = 29'h00040000;
    localparam [28:0] S_RT_CAPTURE_PACKET   = 29'h00080000;
    localparam [28:0] S_RT_VALIDATE_PACKET  = 29'h00100000;
    localparam [28:0] S_DATA_DRAIN_ARM      = 29'h00200000;
    localparam [28:0] S_COMMIT_WAIT_ARM     = 29'h00400000;
    localparam [28:0] S_TRANSFER_RESET      = 29'h00800000;
    localparam [28:0] S_RT_VALIDATE_GUARD   = 29'h01000000;
    localparam [28:0] S_RT_GUARD_DECIDE     = 29'h02000000;
    localparam [28:0] S_BEGIN_GUARD_END     = 29'h04000000;
    localparam [28:0] S_BEGIN_GUARD_COMPARE = 29'h08000000;
    localparam [28:0] S_BEGIN_GUARD_DECIDE  = 29'h10000000;

    localparam [1:0] CPL_ACTION_COMMAND = 2'd0;
    localparam [1:0] CPL_ACTION_POLL    = 2'd1;
    localparam [1:0] CPL_ACTION_HELLO   = 2'd2;
    localparam [1:0] CPL_ACTION_TIMEOUT = 2'd3;

    localparam       DMA_ACTION_PUSH  = 1'b0;
    localparam       DMA_ACTION_FETCH = 1'b1;
    localparam [15:0] RUNTIME_MAX_PAYLOAD = 16'd270;

    localparam [1:0] STREAM_HEADER = 2'd0;
    localparam [1:0] STREAM_DATA   = 2'd1;
    localparam [1:0] STREAM_CRC    = 2'd2;

    reg [28:0] state;
    reg [3:0] argument_index;
    reg [31:0] begin_size;
    reg [31:0] begin_crc;
    reg        begin_size_valid;
    reg        begin_address_valid;
    reg        transfer_reset_begin;
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
    // This is intentionally a separate registered copy of the writer's idle
    // state.  The request/response phase bits also drive the SDRAM fabric;
    // feeding either one back into the large command-state mux creates a
    // device-wide routing path at 60 MHz.
    (* keep = "true" *) reg writer_available;
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

    reg [7:0] response [0:31];
    reg [5:0] response_length;
    reg [5:0] response_index;

    reg [7:0] runtime_command;
    reg [15:0] runtime_length;
    reg [15:0] runtime_index;
    // Valid payloads are at most 270 bytes. A registered terminal flag keeps
    // the 16-bit index increment out of the service-state control path.
    reg [8:0] runtime_bytes_remaining;
    reg       runtime_last_byte;
    reg [15:0] runtime_drain_remaining;
    reg [31:0] runtime_crc_state;
    reg [7:0] runtime_buffer [0:31];
    reg [31:0] packet_id_q;
    reg [31:0] packet_offset_q;
    reg [15:0] packet_count_q;
    reg [31:0] packet_crc_q;
    reg [15:0] packet_length_q;
    reg [15:0] packet_completion_status_q;
    reg [15:0] packet_completion_sectors_q;
    reg [31:0] packet_completion_detail_q;
    reg [31:0] message_word0_q;
    reg [31:0] message_word1_q;
    reg [31:0] message_word2_q;
    reg [31:0] message_word3_q;
    reg [31:0] message_word4_q;
    reg [15:0] message_tail_q;
    reg [31:0] packet_end_q;
    reg        packet_chunk_valid_q;
    reg        packet_push_length_valid_q;
    reg        packet_fetch_length_valid_q;
    reg        packet_active_id_match_q;
    (* keep = "true" *) reg packet_active_generations_match_q;
    (* keep = "true" *) reg request_host_generation_match_q;
    (* keep = "true" *) reg request_generations_match_q;
    reg        packet_push_crc_match_q;
    reg        packet_push_retry_q;
    reg        packet_fetch_retry_q;
    reg        packet_in_order_q;
    reg [24:0] runtime_dma_cpu_offset_q;
    reg [25:0] runtime_dma_cpu_end_q;
    reg        packet_push_guard_overlap_q;
    reg        guard_front_valid_q;
    reg [24:0] guard_front_start_q;
    reg [25:0] guard_front_end_q;
    reg        guard_pending_valid_q;
    reg [24:0] guard_pending_start_q;
    reg [25:0] guard_pending_end_q;
    reg        runtime_front_start_before_end_q;
    reg        runtime_front_end_after_start_q;
    reg        runtime_pending_start_before_end_q;
    reg        runtime_pending_end_after_start_q;
    reg [25:0] boot_write_end_q;
    reg        boot_front_start_before_end_q;
    reg        boot_front_end_after_start_q;
    reg        boot_pending_start_before_end_q;
    reg        boot_pending_end_after_start_q;
    reg        packet_last_completion_match_q;
    reg        packet_completion_fields_valid_q;
    reg        hello_fields_valid_q;
    reg        hello_active_generation_change_q;
    reg        hello_host_generation_change_q;
    reg        input_length_valid_q;
    reg        input_host_generation_match_q;
    reg        input_identity_valid_q;
    reg        input_exact_retry_q;
    reg        input_sequence_conflict_q;
    (* ram_style = "block", ramstyle = "block" *)
    reg [31:0] runtime_chunk [0:63];
    reg [5:0]  runtime_chunk_read_address;
    reg [5:0]  runtime_chunk_read_address_q;
    reg [31:0] runtime_chunk_read_data;

    reg active_request_valid;
    reg [31:0] active_request_id;
    reg [7:0] active_request_op;
    reg [7:0] active_request_flags;
    reg [63:0] active_request_lba;
    reg [15:0] active_request_sectors;
    reg [31:0] active_request_buffer;
    reg [31:0] active_media_generation;
    reg [31:0] active_host_generation;
    reg [31:0] active_progress;
    reg [31:0] active_timeout_count;
    reg [31:0] last_push_offset;
    reg [15:0] last_push_count;
    reg [31:0] last_push_crc;
    reg        last_push_valid;
    reg [31:0] last_fetch_offset;
    reg [15:0] last_fetch_count;
    reg [31:0] last_fetch_crc;
    reg        last_fetch_valid;

    reg [31:0] service_host_generation;
    reg [31:0] service_media_generation;
    reg        service_link_up;
    reg        state_response_required;

    reg        last_completion_valid;
    reg [31:0] last_completion_id;
    reg [15:0] last_completion_status;
    reg [15:0] last_completion_sectors;
    reg [31:0] last_completion_detail;
    reg        last_event_valid;
    reg [31:0] last_event_host_generation;
    reg [31:0] last_event_header;
    reg [31:0] last_event_value;
    reg [31:0] last_event_timestamp;
    reg [31:0] last_event_device_sequence;

    reg [1:0] completion_action;
    reg dma_action;

    reg runtime_dma_active;
    reg runtime_dma_to_memory;
    reg runtime_dma_request_valid;
    reg runtime_dma_wait_response;
    reg runtime_dma_done;
    reg [24:0] runtime_dma_address;
    reg [8:0] runtime_dma_position;
    reg [8:0] runtime_dma_remaining;
    reg [31:0] runtime_dma_crc;
    reg [31:0] runtime_dma_request_wdata;
    reg [3:0] runtime_dma_request_be;
    reg        runtime_dma_start;
    reg        runtime_dma_start_to_memory;
    reg [24:0] runtime_dma_start_address;
    reg [8:0]  runtime_dma_start_count;
    reg        runtime_dma_read_word_valid;
    reg [5:0]  runtime_dma_read_word_address;
    reg [31:0] runtime_dma_read_word_data;

    reg runtime_stream_active;
    reg runtime_stream_advance;
    reg runtime_stream_pending;
    reg [7:0] runtime_stream_pending_data;
    reg [1:0] runtime_stream_phase;
    reg [3:0] runtime_stream_header_index;
    reg [7:0] runtime_stream_data_offset;
    reg [8:0] runtime_stream_data_remaining;
    reg [1:0] runtime_stream_crc_index;
    reg [8:0] runtime_stream_count;
    reg [31:0] runtime_stream_id;
    reg [31:0] runtime_stream_offset;
    reg [31:0] runtime_stream_crc;

    wire response_active = response_index < response_length;
    wire writer_idle = writer_available;
    wire runtime_dma_idle = !runtime_dma_active &&
                            !runtime_dma_request_valid &&
                            !runtime_dma_wait_response;
    // Writer availability gates admission into the ingress register. Once a
    // byte is buffered, consuming it must not depend on the SDRAM response
    // path; that keeps the command datapath local without weakening backpressure.
    wire rx_byte_valid = rx_buffer_valid;
    wire packer_flush = state == S_COMMIT_FLUSH && writer_idle;
    wire [7:0] boot_flags = {3'd0, host_seen, boot_error, boot_done,
                             boot_busy, boot_request};

    wire boot_mem_lock = boot_busy || issue_request || request_valid ||
                         wait_response;
    wire runtime_mem_lock = runtime_dma_active || runtime_dma_request_valid ||
                            runtime_dma_wait_response;
    // Boot and runtime transfers are mutually exclusive at command admission.
    // Keep ownership local to each transfer engine so the high-fanout boot
    // status does not cross the device into the runtime DMA state datapath.
    wire runtime_owns_mem = runtime_mem_lock;
    assign mem_lock = boot_mem_lock || runtime_mem_lock;
    assign mem_valid = runtime_owns_mem ? runtime_dma_request_valid :
                                         request_valid;
    assign mem_write = runtime_owns_mem ? runtime_dma_to_memory : 1'b1;
    assign mem_addr = runtime_owns_mem ? runtime_dma_address : request_addr;
    assign mem_be = runtime_owns_mem ? runtime_dma_request_be : request_be;
    assign mem_wdata = runtime_owns_mem ? runtime_dma_request_wdata :
                                         request_wdata;
    // Every host DMA ownership interval is a CPU cache fence. Legacy ROM
    // loading writes SDRAM too, and must drain posted CPU writes before the
    // shared controller grants AstraHost access just like runtime block I/O.
    assign cache_flush = mem_lock;

`ifndef SYNTHESIS
    always @(posedge clk) begin
        if (!rst && boot_mem_lock && runtime_mem_lock)
            $fatal(1, "AstraHost boot/runtime DMA ownership overlap");
    end
`endif

    function automatic [31:0] crc32_byte(
        input [31:0] crc,
        input [7:0] value
    );
        reg [31:0] current;
        integer bit_number;
        begin
            current = crc ^ {24'd0, value};
            for (bit_number = 0; bit_number < 8; bit_number = bit_number + 1)
                current = (current >> 1) ^
                          (32'hedb88320 & (0 - current[0]));
            crc32_byte = current;
        end
    endfunction

    function automatic [15:0] runtime_be16(input integer offset);
        runtime_be16 = {runtime_buffer[offset], runtime_buffer[offset + 1]};
    endfunction

    function automatic [31:0] runtime_be32(input integer offset);
        runtime_be32 = {
            runtime_buffer[offset], runtime_buffer[offset + 1],
            runtime_buffer[offset + 2], runtime_buffer[offset + 3]
        };
    endfunction

    wire [31:0] packet_id = {
        runtime_buffer[0], runtime_buffer[1],
        runtime_buffer[2], runtime_buffer[3]
    };
    wire [31:0] packet_offset = {
        runtime_buffer[4], runtime_buffer[5],
        runtime_buffer[6], runtime_buffer[7]
    };
    wire [15:0] packet_count = {runtime_buffer[8], runtime_buffer[9]};
    wire [31:0] packet_crc = {
        runtime_buffer[10], runtime_buffer[11],
        runtime_buffer[12], runtime_buffer[13]
    };
    wire runtime_command_needs_capture =
        runtime_command == CMD_SERVICE_HELLO ||
        runtime_command == CMD_BLOCK_PUSH ||
        runtime_command == CMD_BLOCK_FETCH ||
        runtime_command == CMD_BLOCK_COMPLETE ||
        runtime_command == CMD_INPUT_EVENT;
    wire [31:0] active_byte_count =
        {7'd0, active_request_sectors, 9'd0};
    wire [32:0] captured_packet_end = {1'b0, packet_offset_q} +
                                      {17'd0, packet_count_q};
    wire [7:0] runtime_data_index = runtime_index[7:0] - 8'd14;
    wire runtime_chunk_parser_write = rx_byte_valid &&
        state == S_RT_DATA && runtime_command == CMD_BLOCK_PUSH &&
        runtime_index >= 16'd14;
    wire runtime_front_start_before_end =
        {1'b0, runtime_dma_cpu_offset_q} < guard_front_end_q;
    wire runtime_front_end_after_start =
        runtime_dma_cpu_end_q > {1'b0, guard_front_start_q};
    wire runtime_pending_start_before_end =
        {1'b0, runtime_dma_cpu_offset_q} < guard_pending_end_q;
    wire runtime_pending_end_after_start =
        runtime_dma_cpu_end_q > {1'b0, guard_pending_start_q};
    wire boot_front_start_before_end =
        {1'b0, ROM_LOAD_OFFSET} < guard_front_end_q;
    wire boot_front_end_after_start =
        boot_write_end_q > {1'b0, guard_front_start_q};
    wire boot_pending_start_before_end =
        {1'b0, ROM_LOAD_OFFSET} < guard_pending_end_q;
    wire boot_pending_end_after_start =
        boot_write_end_q > {1'b0, guard_pending_start_q};
    wire captured_packet_chunk_valid = packet_count_q != 0 &&
                                       packet_count_q <= 16'd256 &&
                                       packet_count_q[1:0] == 2'b00 &&
                                       packet_offset_q[1:0] == 2'b00 &&
                                       !captured_packet_end[32] &&
                                       captured_packet_end[31:0] <=
                                           active_byte_count;

    wire runtime_stream_data_phase = runtime_stream_active &&
        runtime_stream_phase == STREAM_DATA;
    wire runtime_stream_source_ready = !runtime_stream_data_phase ||
        runtime_chunk_read_address_q == runtime_stream_data_offset[7:2];

    // One registered read port serves both DMA and SPI response streaming.
    // The stream reads its current byte, then captures it in a one-byte skid
    // register before consulting TX backpressure. Besides making the ready/
    // valid contract explicit, this prevents the async TX FIFO's full path
    // from driving the block-RAM address in the same 60 MHz cycle.
    always @* begin
        runtime_chunk_read_address = runtime_dma_active ?
            runtime_dma_position[7:2] : 6'd0;
        if (runtime_stream_data_phase)
            runtime_chunk_read_address = runtime_stream_data_offset[7:2];
    end

    task automatic queue_simple_response(input [7:0] status);
        begin
            response[0] <= status;
            response_length <= 6'd1;
            response_index <= 6'd0;
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
            response_length <= 6'd7;
            response_index <= 6'd0;
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
            response[6] <= 8'd2;
            response[7] <= 8'h00;
            response[8] <= 8'h0f; // boot, block, input, kernel monitor
            response[9] <= boot_flags;
            response[10] <= error_code;
            response[11] <= bytes_received[31:24];
            response[12] <= bytes_received[23:16];
            response[13] <= bytes_received[15:8];
            response[14] <= bytes_received[7:0];
            response_length <= 6'd15;
            response_index <= 6'd0;
        end
    endtask

    task automatic queue_monitor_response(
        input       valid,
        input [7:0] value
    );
        begin
            response[0] <= STATUS_OK;
            response[1] <= {7'd0, valid};
            response[2] <= value;
            response_length <= valid ? 6'd3 : 6'd2;
            response_index <= 6'd0;
        end
    endtask

    task automatic queue_poll_response(
        input        valid,
        input [31:0] id,
        input [7:0]  operation,
        input [7:0]  flags,
        input [15:0] sectors,
        input [63:0] lba,
        input [31:0] buffer_address,
        input [31:0] media_generation,
        input [31:0] host_generation
    );
        begin
            response[0] <= STATUS_OK;
            response[1] <= {7'd0, valid};
            response[2] <= id[31:24];
            response[3] <= id[23:16];
            response[4] <= id[15:8];
            response[5] <= id[7:0];
            response[6] <= operation;
            response[7] <= flags;
            response[8] <= sectors[15:8];
            response[9] <= sectors[7:0];
            response[10] <= lba[63:56];
            response[11] <= lba[55:48];
            response[12] <= lba[47:40];
            response[13] <= lba[39:32];
            response[14] <= lba[31:24];
            response[15] <= lba[23:16];
            response[16] <= lba[15:8];
            response[17] <= lba[7:0];
            response[18] <= buffer_address[31:24];
            response[19] <= buffer_address[23:16];
            response[20] <= buffer_address[15:8];
            response[21] <= buffer_address[7:0];
            response[22] <= media_generation[31:24];
            response[23] <= media_generation[23:16];
            response[24] <= media_generation[15:8];
            response[25] <= media_generation[7:0];
            response[26] <= host_generation[31:24];
            response[27] <= host_generation[23:16];
            response[28] <= host_generation[15:8];
            response[29] <= host_generation[7:0];
            response_length <= 6'd30;
            response_index <= 6'd0;
        end
    endtask

    task automatic begin_completion(
        input [31:0] id,
        input [15:0] status,
        input [15:0] sectors,
        input [31:0] detail,
        input [31:0] media_generation,
        input [31:0] host_generation,
        input [1:0] action
    );
        begin
            runtime_completion_id <= id;
            runtime_completion_status <= status;
            runtime_completion_sectors <= sectors;
            runtime_completion_detail <= detail;
            runtime_completion_media_generation <= media_generation;
            runtime_completion_host_generation <= host_generation;
            runtime_completion_valid <= 1'b1;
            completion_action <= action;
            state <= S_RT_WAIT_CPL;
        end
    endtask

    function automatic [7:0] runtime_stream_header_byte(input [3:0] index);
        begin
            case (index)
                4'd0: runtime_stream_header_byte = STATUS_OK;
                4'd1: runtime_stream_header_byte = runtime_stream_id[31:24];
                4'd2: runtime_stream_header_byte = runtime_stream_id[23:16];
                4'd3: runtime_stream_header_byte = runtime_stream_id[15:8];
                4'd4: runtime_stream_header_byte = runtime_stream_id[7:0];
                4'd5: runtime_stream_header_byte = runtime_stream_offset[31:24];
                4'd6: runtime_stream_header_byte = runtime_stream_offset[23:16];
                4'd7: runtime_stream_header_byte = runtime_stream_offset[15:8];
                4'd8: runtime_stream_header_byte = runtime_stream_offset[7:0];
                4'd9: runtime_stream_header_byte =
                    {7'd0, runtime_stream_count[8]};
                default: runtime_stream_header_byte =
                    runtime_stream_count[7:0];
            endcase
        end
    endfunction

    function automatic [7:0] select_stream_byte(
        input [31:0] value,
        input [1:0] index
    );
        begin
            case (index)
                2'd0: select_stream_byte = value[31:24];
                2'd1: select_stream_byte = value[23:16];
                2'd2: select_stream_byte = value[15:8];
                default: select_stream_byte = value[7:0];
            endcase
        end
    endfunction

    reg [7:0] runtime_stream_current_byte;
    always @* begin
        case (runtime_stream_phase)
            STREAM_HEADER:
                runtime_stream_current_byte = runtime_stream_header_byte(
                    runtime_stream_header_index);
            STREAM_DATA:
                runtime_stream_current_byte = select_stream_byte(
                    runtime_chunk_read_data,
                    runtime_stream_data_offset[1:0]);
            default:
                runtime_stream_current_byte = select_stream_byte(
                    runtime_stream_crc, runtime_stream_crc_index);
        endcase
    end

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
        if (!response_active && !runtime_stream_active) begin
            (* parallel_case *) case (1'b1)
                state[0], state[1], state[2], state[7], state[8],
                state[9], state[10]:
                    parser_ready = 1'b1;
                state[3]:
                    parser_ready = writer_idle;
                default:
                    parser_ready = 1'b0;
            endcase
        end
        rx_ready = parser_ready && !rx_buffer_valid;
    end

    // Keep the ROM word packer independent of command validation. In
    // particular, BOOT_BEGIN's 32-bit size check must not become part of the
    // packer's clock-enable path in the 60 MHz SDRAM domain.
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
            writer_available <= 1'b1;
        end else begin
            if (request_valid && mem_ready) begin
                request_valid <= 1'b0;
                wait_response <= 1'b1;
            end
            if (wait_response && mem_rsp_valid) begin
                wait_response <= 1'b0;
                writer_available <= 1'b1;
            end

            if (issue_request) begin
                request_addr <= issue_addr;
                request_be <= issue_be;
                request_wdata <= issue_wdata;
                request_valid <= 1'b1;
                writer_available <= 1'b0;
            end
        end
    end

    // SDRAM responses are captured before entering the CRC network. This
    // keeps the shared controller's read-data fanout out of a four-byte CRC
    // path while allowing the next request to be issued during the CRC cycle.
    wire [31:0] runtime_read_crc_next = crc32_byte(
        crc32_byte(
            crc32_byte(
                crc32_byte(runtime_dma_crc,
                    runtime_dma_read_word_data[31:24]),
                runtime_dma_read_word_data[23:16]),
            runtime_dma_read_word_data[15:8]),
        runtime_dma_read_word_data[7:0]);

    // Explicit single-write, registered-read memory interface. The parser and
    // SDRAM readback paths are mutually exclusive by protocol state; priority
    // here also makes that exclusivity structural for block-RAM inference.
    always @(posedge clk) begin
        if (rst) begin
            runtime_chunk_read_address_q <= 6'd0;
            runtime_chunk_read_data <= 32'd0;
        end else begin
            runtime_chunk_read_address_q <= runtime_chunk_read_address;
            runtime_chunk_read_data <=
                runtime_chunk[runtime_chunk_read_address];

            if (runtime_dma_read_word_valid) begin
                runtime_chunk[runtime_dma_read_word_address] <=
                    runtime_dma_read_word_data;
            end else if (runtime_chunk_parser_write) begin
                case (runtime_data_index[1:0])
                    2'd0: runtime_chunk[runtime_data_index[7:2]][31:24] <=
                        rx_buffer_data;
                    2'd1: runtime_chunk[runtime_data_index[7:2]][23:16] <=
                        rx_buffer_data;
                    2'd2: runtime_chunk[runtime_data_index[7:2]][15:8] <=
                        rx_buffer_data;
                    default:
                        runtime_chunk[runtime_data_index[7:2]][7:0] <=
                            rx_buffer_data;
                endcase
            end
        end
    end

    // Runtime block transfers use complete 32-bit words. The wire protocol
    // limits chunks to 256 bytes and requires four-byte alignment, while the
    // CPU queue validator bounds the full buffer to installed SDRAM.
    always @(posedge clk) begin
        runtime_dma_done <= 1'b0;
        runtime_dma_read_word_valid <= 1'b0;
        if (rst) begin
            runtime_dma_active <= 1'b0;
            runtime_dma_to_memory <= 1'b0;
            runtime_dma_request_valid <= 1'b0;
            runtime_dma_wait_response <= 1'b0;
            runtime_dma_address <= 25'd0;
            runtime_dma_position <= 9'd0;
            runtime_dma_remaining <= 9'd0;
            runtime_dma_crc <= 32'hffffffff;
            runtime_dma_request_wdata <= 32'd0;
            runtime_dma_request_be <= 4'd0;
            runtime_dma_read_word_address <= 6'd0;
            runtime_dma_read_word_data <= 32'd0;
        end else begin
            // runtime_dma_read_word_valid is the response pipeline stage. The
            // chunk RAM write and CRC update consume the same captured word;
            // non-final transfers can issue their next SDRAM request here too.
            if (runtime_dma_read_word_valid) begin
                runtime_dma_crc <= runtime_read_crc_next;
                if (runtime_dma_remaining == 9'd0) begin
                    runtime_dma_active <= 1'b0;
                    runtime_dma_done <= 1'b1;
                end
            end

            if (runtime_dma_start && runtime_dma_idle) begin
                runtime_dma_active <= 1'b1;
                runtime_dma_to_memory <= runtime_dma_start_to_memory;
                runtime_dma_address <= runtime_dma_start_address;
                runtime_dma_position <= 9'd0;
                runtime_dma_remaining <= runtime_dma_start_count;
                runtime_dma_crc <= 32'hffffffff;
            end

            if (runtime_dma_request_valid && mem_ready) begin
                runtime_dma_request_valid <= 1'b0;
                runtime_dma_wait_response <= 1'b1;
            end

            if (runtime_dma_wait_response && mem_rsp_valid) begin
                runtime_dma_wait_response <= 1'b0;
                if (!runtime_dma_to_memory) begin
                    runtime_dma_read_word_address <=
                        runtime_dma_position[7:2];
                    runtime_dma_read_word_data <= mem_rdata;
                    runtime_dma_read_word_valid <= 1'b1;
                end

                if (runtime_dma_remaining == 9'd4) begin
                    runtime_dma_remaining <= 9'd0;
                    if (runtime_dma_to_memory) begin
                        runtime_dma_active <= 1'b0;
                        runtime_dma_done <= 1'b1;
                    end
                end else begin
                    runtime_dma_address <= runtime_dma_address + 25'd4;
                    runtime_dma_position <= runtime_dma_position + 9'd4;
                    runtime_dma_remaining <= runtime_dma_remaining - 9'd4;
                end
            end

            if (runtime_dma_active && runtime_dma_remaining != 9'd0 &&
                !runtime_dma_request_valid &&
                !runtime_dma_wait_response &&
                (!runtime_dma_to_memory ||
                 runtime_chunk_read_address_q ==
                    runtime_dma_position[7:2])) begin
                runtime_dma_request_valid <= 1'b1;
                runtime_dma_request_be <= 4'b1111;
                runtime_dma_request_wdata <= runtime_chunk_read_data;
            end
        end
    end

    always @(posedge clk) begin
        tx_start <= 1'b0;
        reset_packer <= 1'b0;
        reset_writer <= 1'b0;
        packer_byte_valid <= 1'b0;
        issue_request <= 1'b0;
        runtime_request_ready <= 1'b0;
        runtime_dma_start <= 1'b0;
        runtime_monitor_input_valid <= 1'b0;
        runtime_monitor_output_ready <= 1'b0;
        if (rst) begin
            state <= S_IDLE;
            argument_index <= 4'd0;
            begin_size <= 32'd0;
            begin_crc <= 32'd0;
            begin_size_valid <= 1'b0;
            begin_address_valid <= 1'b0;
            transfer_reset_begin <= 1'b0;
            data_remaining <= 9'd0;
            host_seen <= 1'b0;
            response_length <= 6'd0;
            response_index <= 6'd0;
            response[30] <= 8'd0;
            response[31] <= 8'd0;
            tx_data <= 8'd0;
            issue_addr <= 25'd0;
            issue_be <= 4'd0;
            issue_wdata <= 32'd0;
            stall_count <= 32'd0;
            rx_buffer_data <= 8'd0;
            rx_buffer_valid <= 1'b0;
            packer_byte_data <= 8'd0;
            runtime_command <= 8'd0;
            runtime_length <= 16'd0;
            runtime_index <= 16'd0;
            runtime_bytes_remaining <= 9'd0;
            runtime_last_byte <= 1'b0;
            runtime_drain_remaining <= 16'd0;
            runtime_crc_state <= 32'hffffffff;
            packet_id_q <= 32'd0;
            packet_offset_q <= 32'd0;
            packet_count_q <= 16'd0;
            packet_crc_q <= 32'd0;
            packet_length_q <= 16'd0;
            packet_completion_status_q <= 16'd0;
            packet_completion_sectors_q <= 16'd0;
            packet_completion_detail_q <= 32'd0;
            message_word0_q <= 32'd0;
            message_word1_q <= 32'd0;
            message_word2_q <= 32'd0;
            message_word3_q <= 32'd0;
            message_word4_q <= 32'd0;
            message_tail_q <= 16'd0;
            packet_end_q <= 32'd0;
            packet_chunk_valid_q <= 1'b0;
            packet_push_length_valid_q <= 1'b0;
            packet_fetch_length_valid_q <= 1'b0;
            packet_active_id_match_q <= 1'b0;
            packet_active_generations_match_q <= 1'b0;
            request_host_generation_match_q <= 1'b0;
            request_generations_match_q <= 1'b0;
            packet_push_crc_match_q <= 1'b0;
            packet_push_retry_q <= 1'b0;
            packet_fetch_retry_q <= 1'b0;
            packet_in_order_q <= 1'b0;
            runtime_dma_cpu_offset_q <= 25'd0;
            runtime_dma_cpu_end_q <= 26'd0;
            packet_push_guard_overlap_q <= 1'b0;
            guard_front_valid_q <= 1'b0;
            guard_front_start_q <= 25'd0;
            guard_front_end_q <= 26'd0;
            guard_pending_valid_q <= 1'b0;
            guard_pending_start_q <= 25'd0;
            guard_pending_end_q <= 26'd0;
            runtime_front_start_before_end_q <= 1'b0;
            runtime_front_end_after_start_q <= 1'b0;
            runtime_pending_start_before_end_q <= 1'b0;
            runtime_pending_end_after_start_q <= 1'b0;
            boot_write_end_q <= 26'd0;
            boot_front_start_before_end_q <= 1'b0;
            boot_front_end_after_start_q <= 1'b0;
            boot_pending_start_before_end_q <= 1'b0;
            boot_pending_end_after_start_q <= 1'b0;
            packet_last_completion_match_q <= 1'b0;
            packet_completion_fields_valid_q <= 1'b0;
            hello_fields_valid_q <= 1'b0;
            hello_active_generation_change_q <= 1'b0;
            hello_host_generation_change_q <= 1'b0;
            input_length_valid_q <= 1'b0;
            input_host_generation_match_q <= 1'b0;
            input_identity_valid_q <= 1'b0;
            input_exact_retry_q <= 1'b0;
            input_sequence_conflict_q <= 1'b0;
            active_request_valid <= 1'b0;
            active_request_id <= 32'd0;
            active_request_op <= 8'd0;
            active_request_flags <= 8'd0;
            active_request_lba <= 64'd0;
            active_request_sectors <= 16'd0;
            active_request_buffer <= 32'd0;
            active_media_generation <= 32'd0;
            active_host_generation <= 32'd0;
            active_progress <= 32'd0;
            active_timeout_count <= 32'd0;
            last_push_offset <= 32'd0;
            last_push_count <= 16'd0;
            last_push_crc <= 32'd0;
            last_push_valid <= 1'b0;
            last_fetch_offset <= 32'd0;
            last_fetch_count <= 16'd0;
            last_fetch_crc <= 32'd0;
            last_fetch_valid <= 1'b0;
            service_host_generation <= 32'd0;
            service_media_generation <= 32'd0;
            service_link_up <= 1'b0;
            state_response_required <= 1'b0;
            last_completion_valid <= 1'b0;
            last_completion_id <= 32'd0;
            last_completion_status <= 16'd0;
            last_completion_sectors <= 16'd0;
            last_completion_detail <= 32'd0;
            last_event_valid <= 1'b0;
            last_event_host_generation <= 32'd0;
            last_event_header <= 32'd0;
            last_event_value <= 32'd0;
            last_event_timestamp <= 32'd0;
            last_event_device_sequence <= 32'd0;
            completion_action <= CPL_ACTION_COMMAND;
            dma_action <= DMA_ACTION_PUSH;
            runtime_dma_start_to_memory <= 1'b0;
            runtime_dma_start_address <= 25'd0;
            runtime_dma_start_count <= 9'd0;
            runtime_stream_active <= 1'b0;
            runtime_stream_advance <= 1'b0;
            runtime_stream_pending <= 1'b0;
            runtime_stream_pending_data <= 8'd0;
            runtime_stream_phase <= STREAM_HEADER;
            runtime_stream_header_index <= 4'd0;
            runtime_stream_data_offset <= 8'd0;
            runtime_stream_data_remaining <= 9'd0;
            runtime_stream_crc_index <= 2'd0;
            runtime_stream_count <= 9'd0;
            runtime_stream_id <= 32'd0;
            runtime_stream_offset <= 32'd0;
            runtime_stream_crc <= 32'd0;
            runtime_completion_valid <= 1'b0;
            runtime_completion_id <= 32'd0;
            runtime_completion_status <= 16'd0;
            runtime_completion_sectors <= 16'd0;
            runtime_completion_detail <= 32'd0;
            runtime_completion_media_generation <= 32'd0;
            runtime_completion_host_generation <= 32'd0;
            runtime_state_valid <= 1'b0;
            runtime_state_host_generation <= 32'd0;
            runtime_state_media_generation <= 32'd0;
            runtime_state_flags <= 32'd0;
            runtime_state_media_sectors <= 64'd0;
            runtime_state_max_sectors <= 16'd0;
            runtime_event_valid <= 1'b0;
            runtime_event_host_generation <= 32'd0;
            runtime_event_header <= 32'd0;
            runtime_event_value <= 32'd0;
            runtime_event_timestamp <= 32'd0;
            runtime_event_device_sequence <= 32'd0;
            runtime_monitor_input_data <= 8'd0;
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

            if (service_link_up) begin
                if (active_timeout_count < ACTIVE_TIMEOUT_CYCLES)
                    active_timeout_count <= active_timeout_count + 1'b1;
            end else begin
                active_timeout_count <= 32'd0;
            end

            if (response_active && !tx_busy) begin
                tx_data <= response[response_index[4:0]];
                tx_start <= 1'b1;
                response_index <= response_index + 1'b1;
            end else if (runtime_stream_active) begin
                if (runtime_stream_pending && !tx_busy) begin
                    tx_data <= runtime_stream_pending_data;
                    tx_start <= 1'b1;
                    runtime_stream_pending <= 1'b0;
                    active_timeout_count <= 32'd0;
                    case (runtime_stream_phase)
                        STREAM_HEADER: begin
                            if (runtime_stream_header_index == 4'd10) begin
                                runtime_stream_header_index <= 4'd0;
                                runtime_stream_data_offset <= 8'd0;
                                runtime_stream_data_remaining <=
                                    runtime_stream_count;
                                if (runtime_stream_count == 0) begin
                                    runtime_stream_phase <= STREAM_CRC;
                                    runtime_stream_crc_index <= 2'd0;
                                end else begin
                                    runtime_stream_phase <= STREAM_DATA;
                                end
                            end else begin
                                runtime_stream_header_index <=
                                    runtime_stream_header_index + 1'b1;
                            end
                        end
                        STREAM_DATA: begin
                            if (runtime_stream_data_remaining <= 1) begin
                                runtime_stream_data_remaining <= 9'd0;
                                runtime_stream_phase <= STREAM_CRC;
                                runtime_stream_crc_index <= 2'd0;
                            end else begin
                                runtime_stream_data_remaining <=
                                    runtime_stream_data_remaining - 1'b1;
                                runtime_stream_data_offset <=
                                    runtime_stream_data_offset + 1'b1;
                            end
                        end
                        default: begin
                            if (runtime_stream_crc_index == 2'd3) begin
                                runtime_stream_active <= 1'b0;
                                if (runtime_stream_advance)
                                    active_progress <= active_progress +
                                        {23'd0, runtime_stream_count};
                                state <= S_IDLE;
                            end else begin
                                runtime_stream_crc_index <=
                                    runtime_stream_crc_index + 1'b1;
                            end
                        end
                    endcase
                end else if (!runtime_stream_pending &&
                             runtime_stream_source_ready) begin
                    runtime_stream_pending_data <=
                        runtime_stream_current_byte;
                    runtime_stream_pending <= 1'b1;
                end
            end

            if (state != S_IDLE && state != S_DATA_DRAIN_ARM &&
                state != S_DATA_DRAIN && state != S_COMMIT_FLUSH &&
                state != S_COMMIT_WAIT_ARM && state != S_COMMIT_WAIT &&
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

            (* parallel_case *) case (1'b1)
                state[0]: begin
                    if (rx_byte_valid) begin
                        host_seen <= 1'b1;
                        case (rx_buffer_data)
                            CMD_IDENTIFY: queue_identify_response();
                            CMD_BOOT_STATUS:
                                queue_status_response(STATUS_OK);
                            CMD_BOOT_BEGIN: begin
                                if (!boot_request || boot_busy || boot_done ||
                                    active_request_valid) begin
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
                                transfer_reset_begin <= 1'b0;
                                state <= S_TRANSFER_RESET;
                            end
                            CMD_SERVICE_HELLO, CMD_BLOCK_POLL,
                            CMD_BLOCK_PUSH, CMD_BLOCK_FETCH,
                            CMD_BLOCK_COMPLETE, CMD_INPUT_EVENT,
                            CMD_MONITOR_WRITE, CMD_MONITOR_READ: begin
                                runtime_command <= rx_buffer_data;
                                runtime_length <= 16'd0;
                                runtime_index <= 16'd0;
                                runtime_bytes_remaining <= 9'd0;
                                runtime_last_byte <= 1'b0;
                                runtime_crc_state <= 32'hffffffff;
                                active_timeout_count <= 32'd0;
                                state <= S_RT_LEN_HI;
                            end
                            default:
                                queue_simple_response(STATUS_BAD_COMMAND);
                        endcase
                    end else if (active_request_valid &&
                                 active_timeout_count >=
                                     ACTIVE_TIMEOUT_CYCLES - 1) begin
                        begin_completion(
                            active_request_id, CPL_TIMEOUT, 16'd0,
                            active_progress, active_media_generation,
                            active_host_generation, CPL_ACTION_TIMEOUT);
                    end else if (service_link_up &&
                                 active_timeout_count >=
                                     ACTIVE_TIMEOUT_CYCLES - 1) begin
                        service_link_up <= 1'b0;
                        runtime_state_host_generation <=
                            service_host_generation;
                        runtime_state_media_generation <=
                            service_media_generation;
                        runtime_state_flags <= 32'd0;
                        runtime_state_media_sectors <= 64'd0;
                        runtime_state_max_sectors <= 16'd0;
                        runtime_state_valid <= 1'b1;
                        state_response_required <= 1'b0;
                        state <= S_RT_WAIT_STATE;
                    end
                end

                state[1]: begin
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
                            if (!begin_size_valid || !begin_address_valid ||
                                rx_buffer_data != ROM_LOAD_ADDRESS[7:0]) begin
                                state <= S_IDLE;
                                queue_simple_response(STATUS_BAD_ARGUMENT);
                            end else begin
                                state <= S_BEGIN_GUARD_END;
                            end
                        end else begin
                            argument_index <= argument_index + 1'b1;
                        end
                    end
                end

                state[26]: begin
                    boot_write_end_q <= {1'b0, ROM_LOAD_OFFSET} +
                                        begin_size[25:0];
                    guard_front_valid_q <= front_guard_valid;
                    guard_front_start_q <= front_guard_start;
                    guard_front_end_q <= front_guard_end;
                    guard_pending_valid_q <= pending_guard_valid;
                    guard_pending_start_q <= pending_guard_start;
                    guard_pending_end_q <= pending_guard_end;
                    state <= S_BEGIN_GUARD_COMPARE;
                end

                state[27]: begin
                    boot_front_start_before_end_q <=
                        boot_front_start_before_end;
                    boot_front_end_after_start_q <=
                        boot_front_end_after_start;
                    boot_pending_start_before_end_q <=
                        boot_pending_start_before_end;
                    boot_pending_end_after_start_q <=
                        boot_pending_end_after_start;
                    state <= S_BEGIN_GUARD_DECIDE;
                end

                state[28]: begin
                    if ((guard_front_valid_q &&
                         boot_front_start_before_end_q &&
                         boot_front_end_after_start_q) ||
                        (guard_pending_valid_q &&
                         boot_pending_start_before_end_q &&
                         boot_pending_end_after_start_q)) begin
                        state <= S_IDLE;
                        queue_simple_response(STATUS_PROTECTED);
                    end else begin
                        transfer_reset_begin <= 1'b1;
                        state <= S_TRANSFER_RESET;
                    end
                end

                state[2]: begin
                    if (rx_byte_valid) begin
                        data_remaining <= rx_buffer_data == 0 ? 9'd256 :
                                          {1'b0, rx_buffer_data};
                        state <= S_DATA;
                    end
                end

                state[3]: begin
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
                                state <= S_DATA_DRAIN_ARM;
                            end else begin
                                data_remaining <= data_remaining - 1'b1;
                            end
                        end
                    end
                end

                // issue_request is consumed by the independent writer on the
                // following edge. Spend one local control cycle here so drain
                // decisions depend only on the registered writer handshake.
                state[21]: begin
                    state <= S_DATA_DRAIN;
                end

                state[4]: begin
                    if (writer_idle) begin
                        state <= S_IDLE;
                        queue_simple_response(STATUS_OK);
                    end
                end

                state[5]: begin
                    if (writer_idle) begin
                        issue_addr <= write_address;
                        issue_wdata <= word_data;
                        case (word_bytes)
                            2'd1: issue_be <= 4'b1000;
                            2'd2: issue_be <= 4'b1100;
                            default: issue_be <= 4'b1110;
                        endcase
                        issue_request <= 1'b1;
                        state <= S_COMMIT_WAIT_ARM;
                    end
                end

                state[22]: begin
                    state <= S_COMMIT_WAIT;
                end

                // Byte-level command decode ends before transfer-wide reset
                // side effects. This keeps SPI data and address validation out
                // of the high-fanout enables for boot metadata and the writer.
                state[23]: begin
                    reset_transfer();
                    state <= S_IDLE;
                    if (transfer_reset_begin) begin
                        boot_busy <= 1'b1;
                        payload_size <= begin_size;
                        payload_crc32 <= begin_crc;
                        payload_bytes_remaining <= begin_size[18:0];
                        payload_complete <= 1'b0;
                    end
                    transfer_reset_begin <= 1'b0;
                    queue_simple_response(STATUS_OK);
                end

                state[6]: begin
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

                state[7]: begin
                    if (rx_byte_valid) begin
                        runtime_length[15:8] <= rx_buffer_data;
                        state <= S_RT_LEN_LO;
                    end
                end

                state[8]: begin
                    if (rx_byte_valid) begin
                        runtime_length[7:0] <= rx_buffer_data;
                        runtime_index <= 16'd0;
                        runtime_bytes_remaining <=
                            {runtime_length[8], rx_buffer_data};
                        runtime_last_byte <=
                            {runtime_length[15:8], rx_buffer_data} == 16'd1;
                        runtime_crc_state <= 32'hffffffff;
                        if ({runtime_length[15:8], rx_buffer_data} == 0) begin
                            if (runtime_command_needs_capture)
                                state <= S_RT_CAPTURE_PACKET;
                            else
                                state <= S_RT_DISPATCH;
                        end else if ({runtime_length[15:8], rx_buffer_data} >
                                     RUNTIME_MAX_PAYLOAD) begin
                            runtime_drain_remaining <=
                                {runtime_length[15:8], rx_buffer_data};
                            state <= S_RT_DRAIN;
                        end else begin
                            state <= S_RT_DATA;
                        end
                    end
                end

                state[9]: begin
                    if (rx_byte_valid) begin
                        if (runtime_index < 16'd32)
                            runtime_buffer[runtime_index[4:0]] <=
                                rx_buffer_data;

                        if (runtime_command == CMD_BLOCK_PUSH &&
                            runtime_index >= 16'd14) begin
                            runtime_crc_state <= crc32_byte(
                                runtime_crc_state, rx_buffer_data);
                        end

                        if (runtime_last_byte) begin
                            runtime_bytes_remaining <= 9'd0;
                            runtime_last_byte <= 1'b0;
                            if (runtime_command_needs_capture)
                                state <= S_RT_CAPTURE_PACKET;
                            else
                                state <= S_RT_DISPATCH;
                        end else begin
                            runtime_index <= runtime_index + 1'b1;
                            runtime_bytes_remaining <=
                                runtime_bytes_remaining - 1'b1;
                            runtime_last_byte <=
                                runtime_bytes_remaining == 9'd2;
                        end
                    end
                end

                state[10]: begin
                    if (rx_byte_valid) begin
                        if (runtime_drain_remaining == 16'd1) begin
                            runtime_drain_remaining <= 16'd0;
                            state <= S_IDLE;
                            queue_simple_response(STATUS_SIZE);
                        end else begin
                            runtime_drain_remaining <=
                                runtime_drain_remaining - 1'b1;
                        end
                    end
                end

                // Protocol fields are captured separately from validation so
                // the runtime byte buffer cannot feed arithmetic, response
                // generation, and the service state mux in one 60 MHz cycle.
                state[19]: begin
                    packet_id_q <= packet_id;
                    packet_offset_q <= packet_offset;
                    packet_count_q <= packet_count;
                    packet_crc_q <= packet_crc;
                    packet_length_q <= runtime_length;
                    runtime_dma_cpu_offset_q <=
                        active_request_buffer[24:0] -
                        SDRAM_CPU_BASE[24:0] + packet_offset[24:0];
                    packet_completion_status_q <= runtime_be16(4);
                    packet_completion_sectors_q <= runtime_be16(6);
                    packet_completion_detail_q <= runtime_be32(8);
                    if (runtime_command == CMD_SERVICE_HELLO ||
                        runtime_command == CMD_INPUT_EVENT) begin
                        message_word0_q <= runtime_be32(0);
                        message_word1_q <= runtime_be32(4);
                        message_word2_q <= runtime_be32(8);
                        message_word3_q <= runtime_be32(12);
                        message_word4_q <= runtime_be32(16);
                        if (runtime_command == CMD_SERVICE_HELLO &&
                            runtime_length >= 16'd22)
                            message_tail_q <= runtime_be16(20);
                        else
                            message_tail_q <= 16'd0;
                    end else begin
                        message_word0_q <= 32'd0;
                        message_word1_q <= 32'd0;
                        message_word2_q <= 32'd0;
                        message_word3_q <= 32'd0;
                        message_word4_q <= 32'd0;
                        message_tail_q <= 16'd0;
                    end
                    state <= S_RT_VALIDATE_PACKET;
                end

                state[20]: begin
                    runtime_dma_cpu_end_q <=
                        {1'b0, runtime_dma_cpu_offset_q} +
                        {10'd0, packet_count_q};
                    guard_front_valid_q <= front_guard_valid;
                    guard_front_start_q <= front_guard_start;
                    guard_front_end_q <= front_guard_end;
                    guard_pending_valid_q <= pending_guard_valid;
                    guard_pending_start_q <= pending_guard_start;
                    guard_pending_end_q <= pending_guard_end;
                    packet_end_q <= captured_packet_end[31:0];
                    packet_chunk_valid_q <= captured_packet_chunk_valid;
                    packet_push_length_valid_q <=
                        packet_length_q >= 16'd14 &&
                        packet_length_q == packet_count_q + 16'd14;
                    packet_fetch_length_valid_q <=
                        packet_length_q == 16'd10;
                    packet_active_id_match_q <=
                        packet_id_q == active_request_id;
                    packet_active_generations_match_q <=
                        active_host_generation == service_host_generation &&
                        active_media_generation == service_media_generation;
                    packet_push_crc_match_q <=
                        ~runtime_crc_state == packet_crc_q;
                    packet_push_retry_q <= last_push_valid &&
                        packet_offset_q == last_push_offset &&
                        packet_count_q == last_push_count &&
                        packet_crc_q == last_push_crc &&
                        active_progress == captured_packet_end[31:0];
                    packet_fetch_retry_q <= last_fetch_valid &&
                        packet_offset_q == last_fetch_offset &&
                        packet_count_q == last_fetch_count &&
                        active_progress == captured_packet_end[31:0];
                    packet_in_order_q <= packet_offset_q == active_progress;
                    packet_last_completion_match_q <=
                        packet_id_q == last_completion_id &&
                        packet_completion_status_q ==
                            last_completion_status &&
                        packet_completion_sectors_q ==
                            last_completion_sectors &&
                        packet_completion_detail_q == last_completion_detail;
                    packet_completion_fields_valid_q <=
                        packet_completion_sectors_q <=
                            active_request_sectors &&
                        (packet_completion_status_q != CPL_OK ||
                         (active_progress == active_byte_count &&
                          packet_completion_sectors_q ==
                              active_request_sectors));
                    hello_fields_valid_q <=
                        packet_length_q == 16'd22 &&
                        message_word0_q != 0 &&
                        message_word1_q != 0 &&
                        message_tail_q != 0 &&
                        message_tail_q <= SERVICE_MAX_SECTORS &&
                        (message_word2_q & ~STATE_VALID_MASK) == 0 &&
                        (message_word2_q & STATE_LINK_UP) != 0 &&
                        ((message_word2_q & STATE_WRITE_ENABLE) == 0 ||
                         (message_word2_q & STATE_MEDIA_PRESENT) != 0) &&
                        (((message_word2_q & STATE_MEDIA_PRESENT) != 0) ==
                         ({message_word3_q, message_word4_q} != 0));
                    hello_host_generation_change_q <=
                        message_word0_q != service_host_generation;
                    hello_active_generation_change_q <=
                        active_request_valid &&
                        (message_word0_q != service_host_generation ||
                         message_word1_q != service_media_generation);
                    input_length_valid_q <= packet_length_q == 16'd20;
                    input_host_generation_match_q <=
                        message_word0_q == service_host_generation;
                    input_identity_valid_q <=
                        (message_word1_q & 32'hff000000) != 0 &&
                        (message_word4_q & 32'hffff0000) != 0 &&
                        (message_word4_q & 32'h0000ffff) != 0;
                    input_exact_retry_q <= last_event_valid &&
                        message_word4_q == last_event_device_sequence &&
                        message_word0_q == last_event_host_generation &&
                        message_word1_q == last_event_header &&
                        message_word2_q == last_event_value &&
                        message_word3_q == last_event_timestamp;
                    input_sequence_conflict_q <= last_event_valid &&
                        message_word4_q == last_event_device_sequence &&
                        message_word0_q == last_event_host_generation;
                    state <= S_RT_VALIDATE_GUARD;
                end

                state[24]: begin
                    runtime_front_start_before_end_q <=
                        runtime_front_start_before_end;
                    runtime_front_end_after_start_q <=
                        runtime_front_end_after_start;
                    runtime_pending_start_before_end_q <=
                        runtime_pending_start_before_end;
                    runtime_pending_end_after_start_q <=
                        runtime_pending_end_after_start;
                    state <= S_RT_GUARD_DECIDE;
                end

                state[25]: begin
                    packet_push_guard_overlap_q <=
                        (guard_front_valid_q &&
                         runtime_front_start_before_end_q &&
                         runtime_front_end_after_start_q) ||
                        (guard_pending_valid_q &&
                         runtime_pending_start_before_end_q &&
                         runtime_pending_end_after_start_q);
                    state <= S_RT_DISPATCH;
                end

                state[11]: begin
                    if (boot_busy) begin
                        state <= S_IDLE;
                        queue_simple_response(STATUS_BUSY);
                    end else if (runtime_command != CMD_SERVICE_HELLO &&
                                 runtime_command != CMD_MONITOR_WRITE &&
                                 runtime_command != CMD_MONITOR_READ &&
                                 !service_link_up) begin
                        state <= S_IDLE;
                        queue_simple_response(STATUS_BAD_STATE);
                    end else begin
                        case (runtime_command)
                            CMD_SERVICE_HELLO: begin
                                if (!hello_fields_valid_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(
                                        STATUS_BAD_ARGUMENT);
                                end else if (
                                    hello_active_generation_change_q) begin
                                    begin_completion(
                                        active_request_id,
                                        hello_host_generation_change_q ?
                                            CPL_HOST_RESET :
                                            CPL_MEDIA_CHANGED,
                                        16'd0, active_progress,
                                        active_media_generation,
                                        active_host_generation,
                                        CPL_ACTION_HELLO);
                                end else begin
                                    if (hello_host_generation_change_q) begin
                                        last_completion_valid <= 1'b0;
                                        last_event_valid <= 1'b0;
                                    end
                                    service_host_generation <=
                                        message_word0_q;
                                    service_media_generation <=
                                        message_word1_q;
                                    runtime_state_host_generation <=
                                        message_word0_q;
                                    runtime_state_media_generation <=
                                        message_word1_q;
                                    runtime_state_flags <= message_word2_q;
                                    runtime_state_media_sectors <=
                                        {message_word3_q, message_word4_q};
                                    runtime_state_max_sectors <=
                                        message_tail_q;
                                    runtime_state_valid <= 1'b1;
                                    service_link_up <= 1'b1;
                                    state_response_required <= 1'b1;
                                    state <= S_RT_WAIT_STATE;
                                end
                            end

                            CMD_BLOCK_POLL: begin
                                if (runtime_length != 0) begin
                                    state <= S_IDLE;
                                    queue_simple_response(
                                        STATUS_BAD_ARGUMENT);
                                end else if (active_request_valid) begin
                                    state <= S_IDLE;
                                    queue_poll_response(
                                        1'b1, active_request_id,
                                        active_request_op,
                                        active_request_flags,
                                        active_request_sectors,
                                        active_request_lba,
                                        active_request_buffer,
                                        active_media_generation,
                                        active_host_generation);
                                end else if (runtime_request_valid) begin
                                    runtime_request_ready <= 1'b1;
                                    // Capture the EBR-backed FIFO output before
                                    // generation checks or response packing.
                                    // The two following states keep the FIFO's
                                    // 5.8 ns clock-to-output delay out of the
                                    // service control and response muxes.
                                    active_request_valid <= 1'b0;
                                    active_request_id <= runtime_request_id;
                                    active_request_op <= runtime_request_op;
                                    active_request_flags <=
                                        runtime_request_flags;
                                    active_request_lba <= runtime_request_lba;
                                    active_request_sectors <=
                                        runtime_request_sectors;
                                    active_request_buffer <=
                                        runtime_request_buffer;
                                    active_media_generation <=
                                        runtime_request_media_generation;
                                    active_host_generation <=
                                        runtime_request_host_generation;
                                    state <= S_RT_ACCEPT_REQUEST;
                                end else begin
                                    state <= S_IDLE;
                                    queue_poll_response(
                                        1'b0, 32'd0, 8'd0, 8'd0,
                                        16'd0, 64'd0, 32'd0,
                                        service_media_generation,
                                        service_host_generation);
                                end
                            end

                            CMD_BLOCK_PUSH: begin
                                if (!packet_push_length_valid_q ||
                                    !packet_chunk_valid_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(
                                        STATUS_BAD_ARGUMENT);
                                end else if (!active_request_valid ||
                                             active_request_op !=
                                                 BLOCK_OP_READ ||
                                             !packet_active_id_match_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(STATUS_BAD_STATE);
                                end else if (!packet_active_generations_match_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(STATUS_STALE);
                                end else if (!packet_push_crc_match_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(STATUS_CRC);
                                end else if (packet_push_retry_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(STATUS_OK);
                                end else if (!packet_in_order_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(
                                        STATUS_BAD_ARGUMENT);
                                end else if (
                                    packet_push_guard_overlap_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(STATUS_PROTECTED);
                                end else begin
                                    dma_action <= DMA_ACTION_PUSH;
                                    runtime_dma_start_to_memory <= 1'b1;
                                    runtime_dma_start_address <=
                                        runtime_dma_cpu_offset_q;
                                    runtime_dma_start_count <=
                                        packet_count_q[8:0];
                                    runtime_dma_start <= 1'b1;
                                    state <= S_RT_WAIT_DMA;
                                end
                            end

                            CMD_BLOCK_FETCH: begin
                                if (!packet_fetch_length_valid_q ||
                                    !packet_chunk_valid_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(
                                        STATUS_BAD_ARGUMENT);
                                end else if (!active_request_valid ||
                                             active_request_op !=
                                                 BLOCK_OP_WRITE ||
                                             !packet_active_id_match_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(STATUS_BAD_STATE);
                                end else if (!packet_active_generations_match_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(STATUS_STALE);
                                end else if (packet_fetch_retry_q) begin
                                    runtime_stream_id <= active_request_id;
                                    runtime_stream_offset <= packet_offset_q;
                                    runtime_stream_count <= packet_count_q[8:0];
                                    runtime_stream_crc <= last_fetch_crc;
                                    runtime_stream_phase <= STREAM_HEADER;
                                    runtime_stream_header_index <= 4'd0;
                                    runtime_stream_data_offset <= 8'd0;
                                    runtime_stream_data_remaining <=
                                        packet_count_q[8:0];
                                    runtime_stream_crc_index <= 2'd0;
                                    runtime_stream_active <= 1'b1;
                                    runtime_stream_advance <= 1'b0;
                                    runtime_stream_pending <= 1'b0;
                                    state <= S_RT_WAIT_STREAM;
                                end else if (!packet_in_order_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(
                                        STATUS_BAD_ARGUMENT);
                                end else begin
                                    dma_action <= DMA_ACTION_FETCH;
                                    runtime_dma_start_to_memory <= 1'b0;
                                    runtime_dma_start_address <=
                                        runtime_dma_cpu_offset_q;
                                    runtime_dma_start_count <=
                                        packet_count_q[8:0];
                                    runtime_dma_start <= 1'b1;
                                    state <= S_RT_WAIT_DMA;
                                end
                            end

                            CMD_BLOCK_COMPLETE: begin
                                if (packet_length_q != 16'd12) begin
                                    state <= S_IDLE;
                                    queue_simple_response(
                                        STATUS_BAD_ARGUMENT);
                                end else if (!active_request_valid &&
                                    last_completion_valid &&
                                    packet_last_completion_match_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(STATUS_OK);
                                end else if (!active_request_valid ||
                                             !packet_active_id_match_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(STATUS_BAD_STATE);
                                end else if (!packet_completion_fields_valid_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(
                                        STATUS_BAD_ARGUMENT);
                                end else begin
                                    begin_completion(
                                        active_request_id,
                                        packet_completion_status_q,
                                        packet_completion_sectors_q,
                                        packet_completion_detail_q,
                                        active_media_generation,
                                        active_host_generation,
                                        CPL_ACTION_COMMAND);
                                end
                            end

                            CMD_INPUT_EVENT: begin
                                if (!input_length_valid_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(
                                        STATUS_BAD_ARGUMENT);
                                end else if (
                                    !input_host_generation_match_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(STATUS_STALE);
                                end else if (!input_identity_valid_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(
                                        STATUS_BAD_ARGUMENT);
                                end else if (input_exact_retry_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(STATUS_OK);
                                end else if (input_sequence_conflict_q) begin
                                    state <= S_IDLE;
                                    queue_simple_response(
                                        STATUS_BAD_ARGUMENT);
                                end else begin
                                    runtime_event_host_generation <=
                                        message_word0_q;
                                    runtime_event_header <= message_word1_q;
                                    runtime_event_value <= message_word2_q;
                                    runtime_event_timestamp <=
                                        message_word3_q;
                                    runtime_event_device_sequence <=
                                        message_word4_q;
                                    runtime_event_valid <= 1'b1;
                                    state <= S_RT_WAIT_EVENT;
                                end
                            end

                            CMD_MONITOR_WRITE: begin
                                if (runtime_length != 16'd1) begin
                                    state <= S_IDLE;
                                    queue_simple_response(
                                        STATUS_BAD_ARGUMENT);
                                end else if (!runtime_monitor_input_ready) begin
                                    state <= S_IDLE;
                                    queue_simple_response(STATUS_BUSY);
                                end else begin
                                    runtime_monitor_input_data <=
                                        runtime_buffer[0];
                                    runtime_monitor_input_valid <= 1'b1;
                                    state <= S_IDLE;
                                    queue_simple_response(STATUS_OK);
                                end
                            end

                            CMD_MONITOR_READ: begin
                                if (runtime_length != 0) begin
                                    state <= S_IDLE;
                                    queue_simple_response(
                                        STATUS_BAD_ARGUMENT);
                                end else if (runtime_monitor_output_valid) begin
                                    runtime_monitor_output_ready <= 1'b1;
                                    state <= S_IDLE;
                                    queue_monitor_response(
                                        1'b1,
                                        runtime_monitor_output_data);
                                end else begin
                                    state <= S_IDLE;
                                    queue_monitor_response(1'b0, 8'd0);
                                end
                            end

                            default: begin
                                state <= S_IDLE;
                                queue_simple_response(STATUS_BAD_COMMAND);
                            end
                        endcase
                    end
                end

                state[17]: begin
                    // Capture the 64-bit request-generation decision before it
                    // controls completion registers. State 18 already exists
                    // in the successful path, so this preserves its latency.
                    request_host_generation_match_q <=
                        active_host_generation == service_host_generation;
                    request_generations_match_q <=
                        active_host_generation == service_host_generation &&
                        active_media_generation == service_media_generation;
                    state <= S_RT_POLL_RESPONSE;
                end

                state[18]: begin
                    if (!request_generations_match_q) begin
                        begin_completion(
                            active_request_id,
                            !request_host_generation_match_q ?
                                CPL_HOST_RESET : CPL_MEDIA_CHANGED,
                            16'd0, 32'd0, active_media_generation,
                            active_host_generation, CPL_ACTION_POLL);
                    end else begin
                        active_request_valid <= 1'b1;
                        active_progress <= 32'd0;
                        active_timeout_count <= 32'd0;
                        last_push_valid <= 1'b0;
                        last_fetch_valid <= 1'b0;
                        state <= S_IDLE;
                        queue_poll_response(
                            1'b1, active_request_id, active_request_op,
                            active_request_flags, active_request_sectors,
                            active_request_lba, active_request_buffer,
                            active_media_generation, active_host_generation);
                    end
                end

                state[12]: begin
                    if (runtime_dma_done) begin
                        active_timeout_count <= 32'd0;
                        if (dma_action == DMA_ACTION_PUSH) begin
                            active_progress <= packet_end_q;
                            last_push_offset <= packet_offset_q;
                            last_push_count <= packet_count_q;
                            last_push_crc <= packet_crc_q;
                            last_push_valid <= 1'b1;
                            state <= S_IDLE;
                            queue_simple_response(STATUS_OK);
                        end else begin
                            runtime_stream_id <= active_request_id;
                            runtime_stream_offset <= packet_offset_q;
                            runtime_stream_count <= packet_count_q[8:0];
                            runtime_stream_crc <= ~runtime_dma_crc;
                            runtime_stream_phase <= STREAM_HEADER;
                            runtime_stream_header_index <= 4'd0;
                            runtime_stream_data_offset <= 8'd0;
                            runtime_stream_data_remaining <=
                                packet_count_q[8:0];
                            runtime_stream_crc_index <= 2'd0;
                            runtime_stream_active <= 1'b1;
                            runtime_stream_advance <= 1'b1;
                            runtime_stream_pending <= 1'b0;
                            last_fetch_offset <= packet_offset_q;
                            last_fetch_count <= packet_count_q;
                            last_fetch_crc <= ~runtime_dma_crc;
                            last_fetch_valid <= 1'b1;
                            state <= S_RT_WAIT_STREAM;
                        end
                    end
                end

                state[13]: begin
                    if (active_timeout_count >= ACTIVE_TIMEOUT_CYCLES - 1 &&
                        tx_busy) begin
                        runtime_stream_active <= 1'b0;
                        runtime_stream_pending <= 1'b0;
                        begin_completion(
                            active_request_id, CPL_TIMEOUT, 16'd0,
                            active_progress, active_media_generation,
                            active_host_generation, CPL_ACTION_TIMEOUT);
                    end
                end

                state[14]: begin
                    if (runtime_completion_valid &&
                        runtime_completion_ready) begin
                        runtime_completion_valid <= 1'b0;
                        case (completion_action)
                            CPL_ACTION_COMMAND: begin
                                last_completion_valid <= 1'b1;
                                last_completion_id <=
                                    runtime_completion_id;
                                last_completion_status <=
                                    runtime_completion_status;
                                last_completion_sectors <=
                                    runtime_completion_sectors;
                                last_completion_detail <=
                                    runtime_completion_detail;
                                active_request_valid <= 1'b0;
                                last_push_valid <= 1'b0;
                                last_fetch_valid <= 1'b0;
                                state <= S_IDLE;
                                queue_simple_response(STATUS_OK);
                            end
                            CPL_ACTION_POLL: begin
                                state <= S_IDLE;
                                queue_poll_response(
                                    1'b0, 32'd0, 8'd0, 8'd0,
                                    16'd0, 64'd0, 32'd0,
                                    service_media_generation,
                                    service_host_generation);
                            end
                            CPL_ACTION_HELLO: begin
                                active_request_valid <= 1'b0;
                                last_push_valid <= 1'b0;
                                last_fetch_valid <= 1'b0;
                                last_completion_valid <= 1'b0;
                                last_event_valid <= 1'b0;
                                service_host_generation <= message_word0_q;
                                service_media_generation <= message_word1_q;
                                runtime_state_host_generation <=
                                    message_word0_q;
                                runtime_state_media_generation <=
                                    message_word1_q;
                                runtime_state_flags <= message_word2_q;
                                runtime_state_media_sectors <=
                                    {message_word3_q, message_word4_q};
                                runtime_state_max_sectors <=
                                    message_tail_q;
                                runtime_state_valid <= 1'b1;
                                service_link_up <= 1'b1;
                                state_response_required <= 1'b1;
                                state <= S_RT_WAIT_STATE;
                            end
                            default: begin
                                active_request_valid <= 1'b0;
                                last_push_valid <= 1'b0;
                                last_fetch_valid <= 1'b0;
                                state <= S_IDLE;
                            end
                        endcase
                    end
                end

                state[15]: begin
                    if (runtime_state_valid && runtime_state_ready) begin
                        runtime_state_valid <= 1'b0;
                        state <= S_IDLE;
                        if (state_response_required)
                            queue_simple_response(STATUS_OK);
                        state_response_required <= 1'b0;
                    end
                end

                state[16]: begin
                    if (runtime_event_valid && runtime_event_ready) begin
                        last_event_valid <= 1'b1;
                        last_event_host_generation <=
                            runtime_event_host_generation;
                        last_event_header <= runtime_event_header;
                        last_event_value <= runtime_event_value;
                        last_event_timestamp <= runtime_event_timestamp;
                        last_event_device_sequence <=
                            runtime_event_device_sequence;
                        runtime_event_valid <= 1'b0;
                        state <= S_IDLE;
                        queue_simple_response(STATUS_OK);
                    end
                end

                default: state <= S_IDLE;
            endcase
        end
    end
endmodule

`default_nettype wire
