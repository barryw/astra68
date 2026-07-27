`timescale 1ns/1ps
`default_nettype none

// CPU-visible AstraHost runtime queues. The CPU stages and atomically submits
// validated block requests; the SPI/SDRAM service consumes them in mem_clk.
// Completions, controller/media state, and input events return through separate
// asynchronous FIFOs so no multi-bit status bus is sampled across a clock
// boundary without a handshake.
module astra_host_runtime #(
    parameter [31:0] SDRAM_CPU_BASE = 32'h02000000,
    parameter [31:0] SDRAM_BYTES = 32'h02000000,
    parameter [15:0] MAX_SECTORS = 16,
    parameter integer QUEUE_ADDR_WIDTH = 2,
    parameter integer INPUT_ADDR_WIDTH = 4,
    parameter integer MONITOR_ADDR_WIDTH = 7
) (
    input  wire        cpu_clk,
    input  wire        cpu_rst,
    input  wire        host_select,
    input  wire        input_select,
    input  wire [5:0]  reg_index,
    input  wire        write_strobe,
    input  wire [31:0] write_data,
    input  wire [3:0]  byte_enable,
    output reg  [31:0] read_data,
    output wire        storage_irq,
    output wire        input_irq,
    output wire        monitor_irq,

    input  wire        mem_clk,
    input  wire        mem_rst,

    output wire        request_valid,
    input  wire        request_ready,
    output wire [31:0] request_id,
    output wire [7:0]  request_op,
    output wire [7:0]  request_flags,
    output wire [63:0] request_lba,
    output wire [15:0] request_sectors,
    output wire [31:0] request_buffer,
    output wire [31:0] request_media_generation,
    output wire [31:0] request_host_generation,

    input  wire        completion_valid,
    output wire        completion_ready,
    input  wire [31:0] completion_id,
    input  wire [15:0] completion_status,
    input  wire [15:0] completion_sectors,
    input  wire [31:0] completion_detail,
    input  wire [31:0] completion_media_generation,
    input  wire [31:0] completion_host_generation,

    input  wire        state_valid,
    output wire        state_ready,
    input  wire [31:0] state_host_generation,
    input  wire [31:0] state_media_generation,
    input  wire [31:0] state_flags,
    input  wire [63:0] state_media_sectors,
    input  wire [15:0] state_max_sectors,

    input  wire        event_valid,
    output wire        event_ready,
    input  wire [31:0] event_host_generation,
    input  wire [31:0] event_header,
    input  wire [31:0] event_value,
    input  wire [31:0] event_timestamp,
    input  wire [31:0] event_device_sequence,

    input  wire        monitor_input_valid,
    output wire        monitor_input_ready,
    input  wire [7:0]  monitor_input_data,
    output wire        monitor_output_valid,
    input  wire        monitor_output_ready,
    output wire [7:0]  monitor_output_data
);
    localparam [7:0] BLOCK_OP_READ  = 8'd1;
    localparam [7:0] BLOCK_OP_WRITE = 8'd2;
    localparam [7:0] BLOCK_OP_FLUSH = 8'd3;

    localparam [31:0] STATE_LINK_UP       = 32'h00000001;
    localparam [31:0] STATE_MEDIA_PRESENT = 32'h00000002;
    localparam [31:0] STATE_WRITE_ENABLE  = 32'h00000004;

    localparam [31:0] ERR_BAD_OP        = 32'h00000001;
    localparam [31:0] ERR_BAD_COUNT     = 32'h00000002;
    localparam [31:0] ERR_BAD_BUFFER    = 32'h00000004;
    localparam [31:0] ERR_NO_MEDIA      = 32'h00000008;
    localparam [31:0] ERR_WRITE_PROTECT = 32'h00000010;
    localparam [31:0] ERR_LBA_RANGE     = 32'h00000020;
    localparam [31:0] ERR_QUEUE_FULL    = 32'h00000040;
    localparam [31:0] ERR_BAD_ID        = 32'h00000080;
    localparam [31:0] ERR_BAD_FLAGS     = 32'h00000100;

    // Host-service register indices, relative to Vesta offset 0x150.
    localparam [5:0] HR_ID             = 6'h00;
    localparam [5:0] HR_VERSION        = 6'h01;
    localparam [5:0] HR_CAPS           = 6'h02;
    localparam [5:0] HR_STATE          = 6'h03;
    localparam [5:0] HR_MEDIA_GEN      = 6'h04;
    localparam [5:0] HR_MEDIA_SIZE_HI  = 6'h05;
    localparam [5:0] HR_MEDIA_SIZE_LO  = 6'h06;
    localparam [5:0] HR_QUEUE          = 6'h07;
    localparam [5:0] HR_REQ_ID         = 6'h08;
    localparam [5:0] HR_REQ_OP         = 6'h09;
    localparam [5:0] HR_REQ_LBA_HI     = 6'h0a;
    localparam [5:0] HR_REQ_LBA_LO     = 6'h0b;
    localparam [5:0] HR_REQ_SECTORS    = 6'h0c;
    localparam [5:0] HR_REQ_BUFFER     = 6'h0d;
    localparam [5:0] HR_REQ_SUBMIT     = 6'h0e;
    localparam [5:0] HR_CPL_ID         = 6'h0f;
    localparam [5:0] HR_CPL_STATUS     = 6'h10;
    localparam [5:0] HR_CPL_DETAIL     = 6'h11;
    localparam [5:0] HR_CPL_MEDIA_GEN  = 6'h12;
    localparam [5:0] HR_CPL_HOST_GEN   = 6'h13;
    localparam [5:0] HR_CPL_POP        = 6'h14;
    localparam [5:0] HR_ERROR          = 6'h15;
    localparam [5:0] HR_HOST_GEN       = 6'h16;
    localparam [5:0] HR_STATE_ACK      = 6'h17;
    localparam [5:0] HR_MAX_SECTORS    = 6'h18;
    localparam [5:0] HR_MONITOR_ID      = 6'h19;
    localparam [5:0] HR_MONITOR_VERSION = 6'h1a;
    localparam [5:0] HR_MONITOR_CAPS    = 6'h1b;
    localparam [5:0] HR_MONITOR_STATUS  = 6'h1c;
    localparam [5:0] HR_MONITOR_RX_DATA = 6'h1d;
    localparam [5:0] HR_MONITOR_RX_POP  = 6'h1e;
    localparam [5:0] HR_MONITOR_TX_DATA = 6'h1f;
    localparam [5:0] HR_MONITOR_ERROR   = 6'h20;

    // Input register indices, relative to Vesta offset 0x700.
    localparam [5:0] IR_ID             = 6'h00;
    localparam [5:0] IR_VERSION        = 6'h01;
    localparam [5:0] IR_CAPS           = 6'h02;
    localparam [5:0] IR_STATUS         = 6'h03;
    localparam [5:0] IR_HEADER         = 6'h04;
    localparam [5:0] IR_VALUE          = 6'h05;
    localparam [5:0] IR_TIMESTAMP      = 6'h06;
    localparam [5:0] IR_DEVICE_SEQ     = 6'h07;
    localparam [5:0] IR_HOST_GEN       = 6'h08;
    localparam [5:0] IR_POP            = 6'h09;

    reg [31:0] staged_id;
    reg [31:0] staged_op_flags;
    reg [31:0] staged_lba_hi;
    reg [31:0] staged_lba_lo;
    reg [31:0] staged_sectors;
    reg [31:0] staged_buffer;
    reg [31:0] error_status;

    reg [31:0] current_host_generation;
    reg [31:0] current_media_generation;
    reg [31:0] current_state_flags;
    reg [63:0] current_media_sectors;
    reg [15:0] current_max_sectors;
    reg        state_change_pending;
    reg [31:0] monitor_error_status;

    function automatic [31:0] merge_bytes(
        input [31:0] old_value,
        input [31:0] new_value,
        input [3:0] enables
    );
        begin
            merge_bytes = old_value;
            if (enables[3]) merge_bytes[31:24] = new_value[31:24];
            if (enables[2]) merge_bytes[23:16] = new_value[23:16];
            if (enables[1]) merge_bytes[15:8] = new_value[15:8];
            if (enables[0]) merge_bytes[7:0] = new_value[7:0];
        end
    endfunction

    wire [31:0] byte_mask = {
        {8{byte_enable[3]}}, {8{byte_enable[2]}},
        {8{byte_enable[1]}}, {8{byte_enable[0]}}
    };
    wire submit_attempt = host_select && write_strobe &&
                          reg_index == HR_REQ_SUBMIT && byte_enable[0] &&
                          write_data[0];
    wire completion_pop = host_select && write_strobe &&
                          reg_index == HR_CPL_POP && byte_enable[0] &&
                          write_data[0];
    wire state_ack = host_select && write_strobe &&
                     reg_index == HR_STATE_ACK && byte_enable[0] &&
                     write_data[0];
    wire event_pop = input_select && write_strobe &&
                     reg_index == IR_POP && byte_enable[0] && write_data[0];
    wire monitor_rx_pop_attempt = host_select && write_strobe &&
        reg_index == HR_MONITOR_RX_POP && byte_enable[0] && write_data[0];
    wire monitor_tx_push_attempt = host_select && write_strobe &&
        reg_index == HR_MONITOR_TX_DATA && byte_enable[0];
    wire monitor_error_clear = host_select && write_strobe &&
        reg_index == HR_MONITOR_ERROR;

    wire [7:0] staged_op = staged_op_flags[7:0];
    wire [7:0] staged_flags = staged_op_flags[15:8];
    wire [15:0] sector_count = staged_sectors[15:0];
    wire [63:0] staged_lba = {staged_lba_hi, staged_lba_lo};
    wire [64:0] lba_end = {1'b0, staged_lba} + sector_count;
    wire [32:0] transfer_bytes = {17'd0, sector_count, 9'd0};
    wire [32:0] buffer_end = {1'b0, staged_buffer} + transfer_bytes;
    wire [32:0] sdram_end = {1'b0, SDRAM_CPU_BASE} + SDRAM_BYTES;

    wire op_is_rw = staged_op == BLOCK_OP_READ || staged_op == BLOCK_OP_WRITE;
    wire op_is_flush = staged_op == BLOCK_OP_FLUSH;
    wire valid_op = op_is_rw || op_is_flush;
    wire valid_count = op_is_rw ?
        sector_count != 0 && sector_count <= MAX_SECTORS &&
        sector_count <= current_max_sectors : sector_count == 0;
    wire valid_buffer = op_is_flush ||
        (staged_buffer[1:0] == 2'b00 &&
         staged_buffer >= SDRAM_CPU_BASE &&
         buffer_end <= sdram_end && !buffer_end[32]);
    wire media_present = (current_state_flags & STATE_MEDIA_PRESENT) != 0;
    wire write_enabled = (current_state_flags & STATE_WRITE_ENABLE) != 0;
    wire valid_lba = op_is_flush ||
        (!lba_end[64] && lba_end <= {1'b0, current_media_sectors});

    wire [31:0] submit_error =
        !valid_op ? ERR_BAD_OP :
        staged_id == 0 ? ERR_BAD_ID :
        staged_flags != 0 ? ERR_BAD_FLAGS :
        !valid_count ? ERR_BAD_COUNT :
        !valid_buffer ? ERR_BAD_BUFFER :
        !media_present ? ERR_NO_MEDIA :
        staged_op == BLOCK_OP_WRITE && !write_enabled ? ERR_WRITE_PROTECT :
        !valid_lba ? ERR_LBA_RANGE :
        32'd0;

    wire [223:0] request_fifo_write_data = {
        current_host_generation,
        current_media_generation,
        staged_id,
        staged_lba,
        staged_flags,
        staged_op,
        sector_count,
        staged_buffer
    };
    wire [223:0] request_fifo_read_data;
    wire request_fifo_write_ready;
    wire [QUEUE_ADDR_WIDTH:0] request_write_level;
    wire [QUEUE_ADDR_WIDTH:0] request_read_level;
    wire [4:0] request_write_level_5 = request_write_level;
    wire request_fifo_overflow;
    wire request_fifo_underflow;
    wire submit_valid = submit_attempt && submit_error == 0 &&
                        request_fifo_write_ready;

    astra_async_fifo #(
        .DATA_WIDTH(224), .ADDR_WIDTH(QUEUE_ADDR_WIDTH)
    ) request_fifo_i (
        .wr_clk(cpu_clk), .wr_rst(cpu_rst),
        .wr_data(request_fifo_write_data), .wr_valid(submit_valid),
        .wr_ready(request_fifo_write_ready), .wr_level(request_write_level),
        .rd_clk(mem_clk), .rd_rst(mem_rst),
        .rd_data(request_fifo_read_data), .rd_valid(request_valid),
        .rd_ready(request_ready), .rd_level(request_read_level),
        .overflow(request_fifo_overflow), .underflow(request_fifo_underflow)
    );

    assign request_buffer = request_fifo_read_data[31:0];
    assign request_sectors = request_fifo_read_data[47:32];
    assign request_op = request_fifo_read_data[55:48];
    assign request_flags = request_fifo_read_data[63:56];
    assign request_lba = request_fifo_read_data[127:64];
    assign request_id = request_fifo_read_data[159:128];
    assign request_media_generation = request_fifo_read_data[191:160];
    assign request_host_generation = request_fifo_read_data[223:192];

    wire [159:0] completion_fifo_write_data = {
        completion_host_generation,
        completion_media_generation,
        completion_detail,
        completion_status,
        completion_sectors,
        completion_id
    };
    wire [159:0] completion_fifo_read_data;
    wire completion_fifo_read_valid;
    wire [QUEUE_ADDR_WIDTH:0] completion_write_level;
    wire [QUEUE_ADDR_WIDTH:0] completion_read_level;
    wire [4:0] completion_read_level_5 = completion_read_level;
    wire completion_fifo_overflow;
    wire completion_fifo_underflow;

    astra_async_fifo #(
        .DATA_WIDTH(160), .ADDR_WIDTH(QUEUE_ADDR_WIDTH)
    ) completion_fifo_i (
        .wr_clk(mem_clk), .wr_rst(mem_rst),
        .wr_data(completion_fifo_write_data), .wr_valid(completion_valid),
        .wr_ready(completion_ready), .wr_level(completion_write_level),
        .rd_clk(cpu_clk), .rd_rst(cpu_rst),
        .rd_data(completion_fifo_read_data),
        .rd_valid(completion_fifo_read_valid), .rd_ready(completion_pop),
        .rd_level(completion_read_level),
        .overflow(completion_fifo_overflow),
        .underflow(completion_fifo_underflow)
    );

    wire [191:0] state_fifo_write_data = {
        16'd0, state_max_sectors, state_media_sectors, state_flags,
        state_media_generation, state_host_generation
    };
    wire [191:0] state_fifo_read_data;
    wire state_fifo_read_valid;
    wire state_fifo_pop = state_fifo_read_valid;

    astra_async_fifo #(.DATA_WIDTH(192), .ADDR_WIDTH(2)) state_fifo_i (
        .wr_clk(mem_clk), .wr_rst(mem_rst),
        .wr_data(state_fifo_write_data), .wr_valid(state_valid),
        .wr_ready(state_ready), .wr_level(),
        .rd_clk(cpu_clk), .rd_rst(cpu_rst),
        .rd_data(state_fifo_read_data), .rd_valid(state_fifo_read_valid),
        .rd_ready(state_fifo_pop), .rd_level(), .overflow(), .underflow()
    );

    wire [159:0] event_fifo_write_data = {
        event_host_generation, event_device_sequence, event_timestamp,
        event_value, event_header
    };
    wire [159:0] event_fifo_read_data;
    wire event_fifo_read_valid;
    wire [INPUT_ADDR_WIDTH:0] event_write_level;
    wire [INPUT_ADDR_WIDTH:0] event_read_level;
    wire [4:0] event_read_level_5 = event_read_level;
    wire event_fifo_overflow;
    wire event_fifo_underflow;

    astra_async_fifo #(
        .DATA_WIDTH(160), .ADDR_WIDTH(INPUT_ADDR_WIDTH)
    ) event_fifo_i (
        .wr_clk(mem_clk), .wr_rst(mem_rst),
        .wr_data(event_fifo_write_data), .wr_valid(event_valid),
        .wr_ready(event_ready), .wr_level(event_write_level),
        .rd_clk(cpu_clk), .rd_rst(cpu_rst),
        .rd_data(event_fifo_read_data), .rd_valid(event_fifo_read_valid),
        .rd_ready(event_pop), .rd_level(event_read_level),
        .overflow(event_fifo_overflow), .underflow(event_fifo_underflow)
    );

    wire [7:0] monitor_rx_fifo_read_data;
    wire monitor_rx_fifo_read_valid;
    wire [MONITOR_ADDR_WIDTH:0] monitor_rx_write_level;
    wire [MONITOR_ADDR_WIDTH:0] monitor_rx_read_level;
    wire [7:0] monitor_rx_read_level_8 = monitor_rx_read_level;
    wire monitor_rx_fifo_overflow;
    wire monitor_rx_fifo_underflow;

    astra_async_fifo #(
        .DATA_WIDTH(8), .ADDR_WIDTH(MONITOR_ADDR_WIDTH)
    ) monitor_rx_fifo_i (
        .wr_clk(mem_clk), .wr_rst(mem_rst),
        .wr_data(monitor_input_data), .wr_valid(monitor_input_valid),
        .wr_ready(monitor_input_ready), .wr_level(monitor_rx_write_level),
        .rd_clk(cpu_clk), .rd_rst(cpu_rst),
        .rd_data(monitor_rx_fifo_read_data),
        .rd_valid(monitor_rx_fifo_read_valid),
        .rd_ready(monitor_rx_pop_attempt), .rd_level(monitor_rx_read_level),
        .overflow(monitor_rx_fifo_overflow),
        .underflow(monitor_rx_fifo_underflow)
    );

    wire monitor_tx_fifo_write_ready;
    wire [MONITOR_ADDR_WIDTH:0] monitor_tx_write_level;
    wire [MONITOR_ADDR_WIDTH:0] monitor_tx_read_level;
    wire [7:0] monitor_tx_write_level_8 = monitor_tx_write_level;
    wire monitor_tx_fifo_overflow;
    wire monitor_tx_fifo_underflow;

    astra_async_fifo #(
        .DATA_WIDTH(8), .ADDR_WIDTH(MONITOR_ADDR_WIDTH)
    ) monitor_tx_fifo_i (
        .wr_clk(cpu_clk), .wr_rst(cpu_rst),
        .wr_data(write_data[7:0]), .wr_valid(monitor_tx_push_attempt),
        .wr_ready(monitor_tx_fifo_write_ready),
        .wr_level(monitor_tx_write_level),
        .rd_clk(mem_clk), .rd_rst(mem_rst),
        .rd_data(monitor_output_data), .rd_valid(monitor_output_valid),
        .rd_ready(monitor_output_ready), .rd_level(monitor_tx_read_level),
        .overflow(monitor_tx_fifo_overflow),
        .underflow(monitor_tx_fifo_underflow)
    );

    assign storage_irq = completion_fifo_read_valid || state_change_pending;
    assign input_irq = event_fifo_read_valid;
    assign monitor_irq = monitor_rx_fifo_read_valid;

    always @(posedge cpu_clk) begin
        if (cpu_rst) begin
            staged_id <= 32'd0;
            staged_op_flags <= 32'd0;
            staged_lba_hi <= 32'd0;
            staged_lba_lo <= 32'd0;
            staged_sectors <= 32'd0;
            staged_buffer <= SDRAM_CPU_BASE;
            error_status <= 32'd0;
            current_host_generation <= 32'd0;
            current_media_generation <= 32'd0;
            current_state_flags <= 32'd0;
            current_media_sectors <= 64'd0;
            current_max_sectors <= MAX_SECTORS;
            state_change_pending <= 1'b0;
            monitor_error_status <= 32'd0;
        end else begin
            if (host_select && write_strobe) begin
                case (reg_index)
                    HR_REQ_ID:
                        staged_id <= merge_bytes(staged_id, write_data,
                                                 byte_enable);
                    HR_REQ_OP:
                        staged_op_flags <= merge_bytes(staged_op_flags,
                                                       write_data, byte_enable);
                    HR_REQ_LBA_HI:
                        staged_lba_hi <= merge_bytes(staged_lba_hi, write_data,
                                                     byte_enable);
                    HR_REQ_LBA_LO:
                        staged_lba_lo <= merge_bytes(staged_lba_lo, write_data,
                                                     byte_enable);
                    HR_REQ_SECTORS:
                        staged_sectors <= merge_bytes(staged_sectors,
                                                      write_data, byte_enable);
                    HR_REQ_BUFFER:
                        staged_buffer <= merge_bytes(staged_buffer, write_data,
                                                     byte_enable);
                    HR_ERROR:
                        error_status <= error_status &
                                        ~(write_data & byte_mask);
                    default: begin end
                endcase
            end

            if (submit_attempt) begin
                if (submit_error != 0)
                    error_status <= error_status | submit_error;
                else if (!request_fifo_write_ready)
                    error_status <= error_status | ERR_QUEUE_FULL;
            end

            if (state_ack)
                state_change_pending <= 1'b0;
            if (state_fifo_read_valid) begin
                if (state_fifo_read_data[31:0] !=
                        current_host_generation ||
                    state_fifo_read_data[63:32] !=
                        current_media_generation ||
                    state_fifo_read_data[95:64] != current_state_flags ||
                    state_fifo_read_data[159:96] !=
                        current_media_sectors ||
                    state_fifo_read_data[175:160] != current_max_sectors)
                    state_change_pending <= 1'b1;
                current_host_generation <= state_fifo_read_data[31:0];
                current_media_generation <= state_fifo_read_data[63:32];
                current_state_flags <= state_fifo_read_data[95:64];
                current_media_sectors <= state_fifo_read_data[159:96];
                current_max_sectors <= state_fifo_read_data[175:160];
            end

            if (monitor_error_clear ||
                (monitor_rx_pop_attempt && !monitor_rx_fifo_read_valid) ||
                (monitor_tx_push_attempt &&
                 !monitor_tx_fifo_write_ready)) begin
                monitor_error_status <=
                    (monitor_error_status &
                     ~(monitor_error_clear ?
                       (write_data & byte_mask) : 32'd0)) |
                    {30'd0,
                     monitor_tx_push_attempt &&
                        !monitor_tx_fifo_write_ready,
                     monitor_rx_pop_attempt &&
                        !monitor_rx_fifo_read_valid};
            end
        end
    end

    always @* begin
        read_data = 32'd0;
        if (host_select) begin
            case (reg_index)
                HR_ID: read_data = 32'h484f5354; // "HOST"
                HR_VERSION: read_data = 32'h00010000;
                HR_CAPS: read_data = 32'h00000007; // read, write, flush
                HR_STATE: read_data = current_state_flags;
                HR_MEDIA_GEN: read_data = current_media_generation;
                HR_MEDIA_SIZE_HI: read_data = current_media_sectors[63:32];
                HR_MEDIA_SIZE_LO: read_data = current_media_sectors[31:0];
                HR_QUEUE: read_data = {
                    8'd0, 3'd0, completion_fifo_read_valid,
                    3'd0, completion_read_level_5,
                    3'd0, request_fifo_write_ready,
                    3'd0, request_write_level_5
                };
                HR_REQ_ID: read_data = staged_id;
                HR_REQ_OP: read_data = staged_op_flags;
                HR_REQ_LBA_HI: read_data = staged_lba_hi;
                HR_REQ_LBA_LO: read_data = staged_lba_lo;
                HR_REQ_SECTORS: read_data = staged_sectors;
                HR_REQ_BUFFER: read_data = staged_buffer;
                HR_CPL_ID: read_data = completion_fifo_read_data[31:0];
                HR_CPL_STATUS: read_data = {
                    completion_fifo_read_data[63:48],
                    completion_fifo_read_data[47:32]
                };
                HR_CPL_DETAIL: read_data = completion_fifo_read_data[95:64];
                HR_CPL_MEDIA_GEN:
                    read_data = completion_fifo_read_data[127:96];
                HR_CPL_HOST_GEN:
                    read_data = completion_fifo_read_data[159:128];
                HR_ERROR: read_data = error_status;
                HR_HOST_GEN: read_data = current_host_generation;
                HR_STATE_ACK: read_data = {31'd0, state_change_pending};
                HR_MAX_SECTORS: read_data = {16'd0, current_max_sectors};
                HR_MONITOR_ID: read_data = 32'h4d4f4e49; // "MONI"
                HR_MONITOR_VERSION: read_data = 32'h00010000;
                HR_MONITOR_CAPS: read_data = 32'h00000007;
                HR_MONITOR_STATUS: read_data = {
                    8'd0, monitor_tx_write_level_8,
                    monitor_rx_read_level_8, 4'd0,
                    monitor_error_status[1:0],
                    monitor_tx_fifo_write_ready,
                    monitor_rx_fifo_read_valid
                };
                HR_MONITOR_RX_DATA:
                    read_data = {24'd0, monitor_rx_fifo_read_data};
                HR_MONITOR_ERROR: read_data = monitor_error_status;
                default: read_data = 32'd0;
            endcase
        end else if (input_select) begin
            case (reg_index)
                IR_ID: read_data = 32'h494e5054; // "INPT"
                IR_VERSION: read_data = 32'h00010000;
                IR_CAPS: read_data = 32'h00000007; // keyboard/pointer/gamepad
                IR_STATUS: read_data = {
                    23'd0, event_fifo_read_valid, 3'd0,
                    event_read_level_5
                };
                IR_HEADER: read_data = event_fifo_read_data[31:0];
                IR_VALUE: read_data = event_fifo_read_data[63:32];
                IR_TIMESTAMP: read_data = event_fifo_read_data[95:64];
                IR_DEVICE_SEQ: read_data = event_fifo_read_data[127:96];
                IR_HOST_GEN: read_data = event_fifo_read_data[159:128];
                default: read_data = 32'd0;
            endcase
        end
    end

`ifndef SYNTHESIS
    initial begin
        if (MAX_SECTORS == 0)
            $fatal(1, "astra_host_runtime MAX_SECTORS must be nonzero");
        if (SDRAM_BYTES == 0)
            $fatal(1, "astra_host_runtime SDRAM_BYTES must be nonzero");
        if (MONITOR_ADDR_WIDTH < 2 || MONITOR_ADDR_WIDTH > 7)
            $fatal(1, "monitor FIFO must contain 4 to 128 bytes");
    end
`endif
endmodule

`default_nettype wire
