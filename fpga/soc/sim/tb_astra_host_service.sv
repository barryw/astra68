`timescale 1ns/1ps
`default_nettype none

module tb_astra_host_service;
    reg clk = 1'b0;
    reg rst = 1'b1;
    always #5 clk = ~clk;

    reg [7:0] rx_data = 8'd0;
    reg rx_valid = 1'b0;
    wire rx_ready;
    wire [7:0] tx_data;
    wire tx_start;
    reg tx_busy = 1'b0;

    wire mem_lock;
    wire mem_valid;
    reg mem_ready = 1'b1;
    wire mem_write;
    wire [24:0] mem_addr;
    wire [3:0] mem_be;
    wire [31:0] mem_wdata;
    reg mem_rsp_valid = 1'b0;
    reg [31:0] mem_rdata = 32'd0;
    wire cache_flush;

    reg front_guard_valid = 1'b0;
    reg [24:0] front_guard_start = 25'd0;
    reg [25:0] front_guard_end = 26'd0;
    reg pending_guard_valid = 1'b0;
    reg [24:0] pending_guard_start = 25'd0;
    reg [25:0] pending_guard_end = 26'd0;

    reg request_valid = 1'b0;
    wire request_ready;
    reg [31:0] request_id = 32'd0;
    reg [7:0] request_op = 8'd0;
    reg [7:0] request_flags = 8'd0;
    reg [63:0] request_lba = 64'd0;
    reg [15:0] request_sectors = 16'd0;
    reg [31:0] request_buffer = 32'd0;
    reg [31:0] request_media_generation = 32'd0;
    reg [31:0] request_host_generation = 32'd0;

    wire completion_valid;
    reg completion_ready = 1'b0;
    wire [31:0] completion_id;
    wire [15:0] completion_status;
    wire [15:0] completion_sectors;
    wire [31:0] completion_detail;
    wire [31:0] completion_media_generation;
    wire [31:0] completion_host_generation;

    wire state_valid;
    reg state_ready = 1'b0;
    wire [31:0] state_host_generation;
    wire [31:0] state_media_generation;
    wire [31:0] state_flags;
    wire [63:0] state_media_sectors;
    wire [15:0] state_max_sectors;

    wire event_valid;
    reg event_ready = 1'b0;
    wire [31:0] event_host_generation;
    wire [31:0] event_header;
    wire [31:0] event_value;
    wire [31:0] event_timestamp;
    wire [31:0] event_device_sequence;

    wire monitor_input_valid;
    reg monitor_input_ready = 1'b0;
    wire [7:0] monitor_input_data;
    reg monitor_output_valid = 1'b0;
    wire monitor_output_ready;
    reg [7:0] monitor_output_data = 8'd0;

    reg [7:0] memory [0:2047];
    reg [7:0] boot_memory [0:31];
    reg response_pending = 1'b0;
    reg [31:0] response_data = 32'd0;
    integer mem_transactions = 0;
    integer cache_flush_cycles = 0;
    integer saved_cache_flush_cycles;

    reg [7:0] tx_queue [0:4095];
    integer tx_write = 0;
    integer tx_read = 0;
    integer i;
    integer saved_transactions;
    integer offset;
    reg [7:0] received;
    reg [31:0] crc;
    reg [31:0] received_crc;
    reg [31:0] boot_crc;

    astra_host_service #(
        .RX_STALL_CYCLES(100),
        .ACTIVE_TIMEOUT_CYCLES(2000)
    ) dut (
        .clk(clk), .rst(rst),
        .rx_data(rx_data), .rx_valid(rx_valid), .rx_ready(rx_ready),
        .tx_data(tx_data), .tx_start(tx_start), .tx_busy(tx_busy),
        .boot_request(1'b1), .host_seen(), .boot_busy(), .boot_done(),
        .boot_error(), .error_code(), .payload_size(), .payload_crc32(),
        .initial_sp(), .initial_pc(), .bytes_received(),
        .mem_lock(mem_lock), .mem_valid(mem_valid), .mem_ready(mem_ready),
        .mem_write(mem_write), .mem_addr(mem_addr), .mem_be(mem_be),
        .mem_wdata(mem_wdata), .mem_rsp_valid(mem_rsp_valid),
        .mem_rdata(mem_rdata), .cache_flush(cache_flush),
        .front_guard_valid(front_guard_valid),
        .front_guard_start(front_guard_start),
        .front_guard_end(front_guard_end),
        .pending_guard_valid(pending_guard_valid),
        .pending_guard_start(pending_guard_start),
        .pending_guard_end(pending_guard_end),
        .runtime_request_valid(request_valid),
        .runtime_request_ready(request_ready),
        .runtime_request_id(request_id), .runtime_request_op(request_op),
        .runtime_request_flags(request_flags),
        .runtime_request_lba(request_lba),
        .runtime_request_sectors(request_sectors),
        .runtime_request_buffer(request_buffer),
        .runtime_request_media_generation(request_media_generation),
        .runtime_request_host_generation(request_host_generation),
        .runtime_completion_valid(completion_valid),
        .runtime_completion_ready(completion_ready),
        .runtime_completion_id(completion_id),
        .runtime_completion_status(completion_status),
        .runtime_completion_sectors(completion_sectors),
        .runtime_completion_detail(completion_detail),
        .runtime_completion_media_generation(completion_media_generation),
        .runtime_completion_host_generation(completion_host_generation),
        .runtime_state_valid(state_valid),
        .runtime_state_ready(state_ready),
        .runtime_state_host_generation(state_host_generation),
        .runtime_state_media_generation(state_media_generation),
        .runtime_state_flags(state_flags),
        .runtime_state_media_sectors(state_media_sectors),
        .runtime_state_max_sectors(state_max_sectors),
        .runtime_event_valid(event_valid),
        .runtime_event_ready(event_ready),
        .runtime_event_host_generation(event_host_generation),
        .runtime_event_header(event_header),
        .runtime_event_value(event_value),
        .runtime_event_timestamp(event_timestamp),
        .runtime_event_device_sequence(event_device_sequence),
        .runtime_monitor_input_valid(monitor_input_valid),
        .runtime_monitor_input_ready(monitor_input_ready),
        .runtime_monitor_input_data(monitor_input_data),
        .runtime_monitor_output_valid(monitor_output_valid),
        .runtime_monitor_output_ready(monitor_output_ready),
        .runtime_monitor_output_data(monitor_output_data)
    );

    function automatic [31:0] crc32_byte(
        input [31:0] current_crc,
        input [7:0] value
    );
        reg [31:0] current;
        integer bit_number;
        begin
            current = current_crc ^ {24'd0, value};
            for (bit_number = 0; bit_number < 8;
                 bit_number = bit_number + 1)
                current = (current >> 1) ^
                          (32'hedb88320 & (0 - current[0]));
            crc32_byte = current;
        end
    endfunction

    function automatic [7:0] pattern(input integer index);
        pattern = index[7:0] ^ 8'ha5;
    endfunction

    always @(posedge clk) begin
        if (tx_start) begin
            tx_queue[tx_write] <= tx_data;
            tx_write <= tx_write + 1;
        end

        if (cache_flush)
            cache_flush_cycles <= cache_flush_cycles + 1;

        mem_rsp_valid <= response_pending;
        mem_rdata <= response_data;
        response_pending <= 1'b0;
        if (mem_valid && mem_ready) begin
            mem_transactions <= mem_transactions + 1;
            if (mem_addr >= 25'h1e00000) begin
                offset = {7'd0, mem_addr - 25'h1e00000};
                if (!mem_write)
                    $fatal(1, "boot memory transaction was a read");
                if (mem_be[3]) boot_memory[offset] <= mem_wdata[31:24];
                if (mem_be[2]) boot_memory[offset + 1] <= mem_wdata[23:16];
                if (mem_be[1]) boot_memory[offset + 2] <= mem_wdata[15:8];
                if (mem_be[0]) boot_memory[offset + 3] <= mem_wdata[7:0];
                response_data <= 32'd0;
            end else begin
                offset = {7'd0, mem_addr};
                if (mem_write) begin
                    if (mem_be[3]) memory[offset] <= mem_wdata[31:24];
                    if (mem_be[2]) memory[offset + 1] <= mem_wdata[23:16];
                    if (mem_be[1]) memory[offset + 2] <= mem_wdata[15:8];
                    if (mem_be[0]) memory[offset + 3] <= mem_wdata[7:0];
                    response_data <= 32'd0;
                end else begin
                    response_data <= {
                        memory[offset], memory[offset + 1],
                        memory[offset + 2], memory[offset + 3]
                    };
                end
            end
            response_pending <= 1'b1;
        end
    end

    task send_byte(input [7:0] value);
        begin
            while (!rx_ready) @(negedge clk);
            rx_data = value;
            rx_valid = 1'b1;
            @(negedge clk);
            rx_valid = 1'b0;
        end
    endtask

    task send_be16(input [15:0] value);
        begin
            send_byte(value[15:8]);
            send_byte(value[7:0]);
        end
    endtask

    task send_be32(input [31:0] value);
        begin
            send_byte(value[31:24]);
            send_byte(value[23:16]);
            send_byte(value[15:8]);
            send_byte(value[7:0]);
        end
    endtask

    task send_be64(input [63:0] value);
        begin
            send_be32(value[63:32]);
            send_be32(value[31:0]);
        end
    endtask

    task receive_byte(output [7:0] value);
        begin
            while (tx_read == tx_write) @(negedge clk);
            value = tx_queue[tx_read];
            tx_read = tx_read + 1;
        end
    endtask

    task expect_byte(input [7:0] expected);
        begin
            receive_byte(received);
            if (received !== expected)
                $fatal(1, "byte mismatch expected=%02x actual=%02x",
                       expected, received);
        end
    endtask

    task start_runtime(input [7:0] command, input [15:0] length);
        begin
            send_byte(command);
            send_be16(length);
        end
    endtask

    task send_hello(input [31:0] host_generation,
                    input [31:0] media_generation);
        begin
            start_runtime(8'h20, 16'd22);
            send_be32(host_generation);
            send_be32(media_generation);
            send_be32(32'h00000007);
            send_be64(64'd2048);
            send_be16(16'd4);
        end
    endtask

    task pulse_state_ready;
        begin
            while (!state_valid) @(negedge clk);
            state_ready = 1'b1;
            @(negedge clk);
            state_ready = 1'b0;
        end
    endtask

    task pulse_completion_ready;
        begin
            completion_ready = 1'b1;
            @(negedge clk);
            completion_ready = 1'b0;
        end
    endtask

    task poll_request(
        input [31:0] id,
        input [7:0] operation,
        input [31:0] buffer_address,
        input [31:0] media_generation,
        input [31:0] host_generation
    );
        begin
            request_id = id;
            request_op = operation;
            request_flags = 8'h5a;
            request_lba = 64'd12;
            request_sectors = 16'd1;
            request_buffer = buffer_address;
            request_media_generation = media_generation;
            request_host_generation = host_generation;
            request_valid = 1'b1;
            start_runtime(8'h21, 16'd0);
            while (!request_ready) @(negedge clk);
            request_valid = 1'b0;
            expect_byte(8'h00);
            expect_byte(8'h01);
            expect_byte(id[31:24]);
            expect_byte(id[23:16]);
            expect_byte(id[15:8]);
            expect_byte(id[7:0]);
            expect_byte(operation);
            for (i = 7; i < 30; i = i + 1)
                receive_byte(received);
        end
    endtask

    task send_push(input [31:0] id, input [31:0] chunk_offset);
        begin
            crc = 32'hffffffff;
            for (i = 0; i < 256; i = i + 1)
                crc = crc32_byte(crc, pattern(chunk_offset + i));
            crc = ~crc;
            start_runtime(8'h22, 16'd270);
            send_be32(id);
            send_be32(chunk_offset);
            send_be16(16'd256);
            send_be32(crc);
            for (i = 0; i < 256; i = i + 1)
                send_byte(pattern(chunk_offset + i));
            expect_byte(8'h00);
        end
    endtask

    task send_push_bad_crc(input [31:0] id, input [31:0] chunk_offset);
        begin
            crc = 32'hffffffff;
            for (i = 0; i < 256; i = i + 1)
                crc = crc32_byte(crc, pattern(chunk_offset + i));
            crc = ~crc;
            start_runtime(8'h22, 16'd270);
            send_be32(id);
            send_be32(chunk_offset);
            send_be16(16'd256);
            send_be32(crc ^ 32'd1);
            for (i = 0; i < 256; i = i + 1)
                send_byte(pattern(chunk_offset + i));
            expect_byte(8'h05);
        end
    endtask

    task send_push_protected(input [31:0] id, input [31:0] chunk_offset);
        begin
            crc = 32'hffffffff;
            for (i = 0; i < 256; i = i + 1)
                crc = crc32_byte(crc, pattern(chunk_offset + i));
            crc = ~crc;
            start_runtime(8'h22, 16'd270);
            send_be32(id);
            send_be32(chunk_offset);
            send_be16(16'd256);
            send_be32(crc);
            for (i = 0; i < 256; i = i + 1)
                send_byte(pattern(chunk_offset + i));
            expect_byte(8'h09);
        end
    endtask

    task send_fetch(input [31:0] id, input [31:0] chunk_offset);
        begin
            start_runtime(8'h23, 16'd10);
            send_be32(id);
            send_be32(chunk_offset);
            send_be16(16'd256);
            fork
                begin
                    // Hold the transport full while the stream crosses a
                    // 32-bit chunk boundary. The pending byte and RAM address
                    // must remain stable until the FIFO accepts it.
                    wait (dut.runtime_stream_active &&
                          dut.runtime_stream_phase == 2'd1 &&
                          dut.runtime_stream_data_offset == 8'd3);
                    tx_busy = 1'b1;
                    repeat (7) @(negedge clk);
                    tx_busy = 1'b0;
                end
                begin
                    expect_byte(8'h00);
                    expect_byte(id[31:24]);
                    expect_byte(id[23:16]);
                    expect_byte(id[15:8]);
                    expect_byte(id[7:0]);
                    expect_byte(chunk_offset[31:24]);
                    expect_byte(chunk_offset[23:16]);
                    expect_byte(chunk_offset[15:8]);
                    expect_byte(chunk_offset[7:0]);
                    expect_byte(8'h01);
                    expect_byte(8'h00);
                    crc = 32'hffffffff;
                    for (i = 0; i < 256; i = i + 1) begin
                        receive_byte(received);
                        if (received !== memory[768 + chunk_offset + i])
                            $fatal(1, "fetch data mismatch offset=%0d", i);
                        crc = crc32_byte(crc, received);
                    end
                    crc = ~crc;
                    received_crc = 32'd0;
                    for (i = 0; i < 4; i = i + 1) begin
                        receive_byte(received);
                        received_crc = {received_crc[23:0], received};
                    end
                    if (received_crc != crc)
                        $fatal(1,
                            "fetch CRC mismatch expected=%08x actual=%08x",
                            crc, received_crc);
                end
            join
        end
    endtask

    task send_complete(input [31:0] id);
        begin
            start_runtime(8'h24, 16'd12);
            send_be32(id);
            send_be16(16'd0);
            send_be16(16'd1);
            send_be32(32'd0);
            while (!completion_valid) @(negedge clk);
            if (completion_id != id || completion_status != 0 ||
                completion_sectors != 1)
                $fatal(1, "completion record mismatch");
            pulse_completion_ready();
            expect_byte(8'h00);
            start_runtime(8'h24, 16'd12);
            send_be32(id);
            send_be16(16'd0);
            send_be16(16'd1);
            send_be32(32'd0);
            expect_byte(8'h00);
            if (completion_valid)
                $fatal(1, "duplicate completion was queued twice");
        end
    endtask

    initial begin
        #1000000;
        $fatal(1,
            "service test timeout state=%0d rx_ready=%b response=%0d/%0d dma=%b req=%b wait=%b rem=%0d owns=%b memv=%b rsp=%b txns=%0d active=%b tx=%0d/%0d",
            dut.state, rx_ready, dut.response_index, dut.response_length,
            dut.runtime_dma_active, dut.runtime_dma_request_valid,
            dut.runtime_dma_wait_response, dut.runtime_dma_remaining,
            dut.runtime_owns_mem, mem_valid, mem_rsp_valid, mem_transactions,
            dut.active_request_valid,
            tx_read, tx_write);
    end

    initial begin
        for (i = 0; i < 2048; i = i + 1)
            memory[i] = 8'd0;
        for (i = 0; i < 32; i = i + 1)
            boot_memory[i] = 8'd0;

        repeat (5) @(negedge clk);
        rst = 1'b0;

        // The legacy boot stream remains byte-for-byte compatible.
        $display("service test: legacy boot");
        send_byte(8'h01);
        expect_byte(8'h00);
        expect_byte("A");
        expect_byte("6");
        expect_byte("8");
        expect_byte("H");
        expect_byte(8'd1);
        expect_byte(8'd2);
        expect_byte(8'h00);
        expect_byte(8'h0f);
        for (i = 9; i < 15; i = i + 1)
            receive_byte(received);

        boot_crc = 32'hffffffff;
        for (i = 0; i < 8; i = i + 1)
            boot_crc = crc32_byte(boot_crc, i == 0 ? 8'h02 :
                i == 4 ? 8'hff : i == 5 ? 8'he0 : i == 6 ? 8'h04 : 8'h00);
        boot_crc = ~boot_crc;
        saved_cache_flush_cycles = cache_flush_cycles;
        pending_guard_start = 25'h1e00000;
        pending_guard_end = 26'h1e00008;
        pending_guard_valid = 1'b1;
        saved_transactions = mem_transactions;
        send_byte(8'h10);
        send_be32(32'd8);
        send_be32(boot_crc);
        send_be32(32'h01e00000);
        expect_byte(8'h09);
        if (mem_transactions != saved_transactions)
            $fatal(1, "protected boot begin reached SDRAM");
        pending_guard_valid = 1'b0;

        send_byte(8'h10);
        send_be32(32'd8);
        send_be32(boot_crc);
        send_be32(32'h01e00000);
        expect_byte(8'h00);
        send_byte(8'h11);
        send_byte(8'd8);
        send_byte(8'h02); send_byte(8'h00); send_byte(8'h00); send_byte(8'h00);
        send_byte(8'hff); send_byte(8'he0); send_byte(8'h04); send_byte(8'h00);
        expect_byte(8'h00);
        send_byte(8'h12);
        expect_byte(8'h00);
        if (boot_memory[0] != 8'h02 || boot_memory[4] != 8'hff)
            $fatal(1, "legacy boot data mismatch");
        if (cache_flush_cycles == saved_cache_flush_cycles)
            $fatal(1, "legacy boot DMA did not request cache invalidation");

        // Monitor traffic is independent of storage negotiation. Each write
        // is one retry-safe byte and each read consumes at most one queued
        // response byte.
        $display("service test: kernel monitor");
        start_runtime(8'h31, 16'd1);
        send_byte("h");
        expect_byte(8'h07);
        if (monitor_input_valid)
            $fatal(1, "full monitor input queue accepted a byte");

        monitor_input_ready = 1'b1;
        start_runtime(8'h31, 16'd1);
        send_byte("h");
        wait (monitor_input_valid);
        if (monitor_input_data != "h")
            $fatal(1, "monitor input byte mismatch");
        expect_byte(8'h00);
        monitor_input_ready = 1'b0;

        start_runtime(8'h31, 16'd0);
        expect_byte(8'h03);
        start_runtime(8'h32, 16'd0);
        expect_byte(8'h00);
        expect_byte(8'h00);

        monitor_output_data = "K";
        monitor_output_valid = 1'b1;
        start_runtime(8'h32, 16'd0);
        wait (monitor_output_ready);
        @(negedge clk);
        monitor_output_valid = 1'b0;
        expect_byte(8'h00);
        expect_byte(8'h01);
        expect_byte("K");

        $display("service test: hello");
        start_runtime(8'h20, 16'd0);
        expect_byte(8'h03);
        send_hello(32'd1, 32'd2);
        while (!state_valid) @(negedge clk);
        if (state_host_generation != 1 || state_media_generation != 2 ||
            state_flags != 7 || state_media_sectors != 2048 ||
            state_max_sectors != 4)
            $fatal(1, "HELLO state record mismatch");
        repeat (3) @(negedge clk);
        if (!state_valid)
            $fatal(1, "HELLO state record did not honor backpressure");
        pulse_state_ready();
        expect_byte(8'h00);

        $display("service test: read request");
        poll_request(32'h11, 8'd1, 32'h02000100, 32'd2, 32'd1);
        saved_cache_flush_cycles = cache_flush_cycles;
        saved_transactions = mem_transactions;
        send_push_bad_crc(32'h11, 32'd0);
        if (mem_transactions != saved_transactions)
            $fatal(1, "bad-CRC push reached SDRAM");
        front_guard_start = 25'd256;
        front_guard_end = 26'd512;
        front_guard_valid = 1'b1;
        send_push_protected(32'h11, 32'd0);
        if (mem_transactions != saved_transactions)
            $fatal(1, "protected push reached SDRAM");
        front_guard_valid = 1'b0;
        send_push(32'h11, 32'd0);
        if (cache_flush_cycles == saved_cache_flush_cycles)
            $fatal(1, "SD-to-RAM DMA did not request cache invalidation");
        for (i = 0; i < 256; i = i + 1)
            if (memory[256 + i] !== pattern(i))
                $fatal(1, "push data mismatch offset=%0d", i);
        saved_transactions = mem_transactions;
        send_push(32'h11, 32'd0);
        if (mem_transactions != saved_transactions)
            $fatal(1, "duplicate push repeated SDRAM DMA");
        send_push(32'h11, 32'd256);

        start_runtime(8'h24, 16'd12);
        send_be32(32'h12);
        send_be16(16'd0);
        send_be16(16'd1);
        send_be32(32'd0);
        expect_byte(8'h02);
        if (completion_valid)
            $fatal(1, "wrong-id completion was queued");

        start_runtime(8'h24, 16'd12);
        send_be32(32'h11);
        send_be16(16'd0);
        send_be16(16'd0);
        send_be32(32'd0);
        expect_byte(8'h03);
        if (completion_valid)
            $fatal(1, "short successful completion was queued");
        send_complete(32'h11);

        $display("service test: write request");
        for (i = 0; i < 512; i = i + 1)
            memory[768 + i] = 8'(i * 3 + 7);
        poll_request(32'h22, 8'd2, 32'h02000300, 32'd2, 32'd1);
        send_fetch(32'h22, 32'd0);
        saved_transactions = mem_transactions;
        send_fetch(32'h22, 32'd0);
        if (mem_transactions != saved_transactions)
            $fatal(1, "duplicate fetch repeated SDRAM DMA");
        send_fetch(32'h22, 32'd256);
        send_complete(32'h22);

        $display("service test: input");
        start_runtime(8'h30, 16'd20);
        send_be32(32'd1);
        send_be32(32'h01020003);
        send_be32(32'h89abcdef);
        send_be32(32'h12345678);
        send_be32(32'h0010002a);
        while (!event_valid) @(negedge clk);
        if (event_host_generation != 1 ||
            event_header != 32'h01020003 ||
            event_value != 32'h89abcdef ||
            event_timestamp != 32'h12345678 ||
            event_device_sequence != 32'h0010002a)
            $fatal(1, "input event record mismatch");
        repeat (3) @(negedge clk);
        if (!event_valid)
            $fatal(1, "input event did not honor backpressure");
        event_ready = 1'b1;
        @(negedge clk);
        event_ready = 1'b0;
        expect_byte(8'h00);
        start_runtime(8'h30, 16'd20);
        send_be32(32'd1);
        send_be32(32'h01020003);
        send_be32(32'h89abcdef);
        send_be32(32'h12345678);
        send_be32(32'h0010002a);
        expect_byte(8'h00);
        if (event_valid)
            $fatal(1, "duplicate input event was queued twice");

        start_runtime(8'h30, 16'd20);
        send_be32(32'd1);
        send_be32(32'h01020003);
        send_be32(32'h89abcdee);
        send_be32(32'h12345678);
        send_be32(32'h0010002a);
        expect_byte(8'h03);
        if (event_valid)
            $fatal(1, "conflicting input retry was queued");

        start_runtime(8'h30, 16'd20);
        send_be32(32'd2);
        send_be32(32'h01020003);
        send_be32(32'd0);
        send_be32(32'd0);
        send_be32(32'h0010002b);
        expect_byte(8'h08);

        start_runtime(8'h30, 16'd20);
        send_be32(32'd1);
        send_be32(32'h00020003);
        send_be32(32'd0);
        send_be32(32'd0);
        send_be32(32'h0010002b);
        expect_byte(8'h03);

        // A card generation change completes in-flight work before publishing
        // the new state, so the CPU never observes a request against new media.
        $display("service test: generations");
        poll_request(32'h33, 8'd1, 32'h02000500, 32'd2, 32'd1);
        send_hello(32'd1, 32'd3);
        while (!completion_valid) @(negedge clk);
        if (completion_id != 32'h33 || completion_status != 16'h0006)
            $fatal(1, "media-change completion mismatch");
        pulse_completion_ready();
        pulse_state_ready();
        expect_byte(8'h00);

        // A host generation mismatch is consumed and completed as stale.
        request_id = 32'h40;
        request_op = 8'd1;
        request_flags = 0;
        request_lba = 0;
        request_sectors = 1;
        request_buffer = 32'h02000600;
        request_media_generation = 3;
        request_host_generation = 2;
        request_valid = 1'b1;
        start_runtime(8'h21, 16'd0);
        while (!request_ready) @(negedge clk);
        request_valid = 1'b0;
        while (!completion_valid) @(negedge clk);
        if (completion_id != 32'h40 || completion_status != 16'h0005)
            $fatal(1, "stale-host completion mismatch");
        pulse_completion_ready();
        expect_byte(8'h00);
        expect_byte(8'h00);
        for (i = 2; i < 30; i = i + 1)
            receive_byte(received);

        // A host that abandons an active request cannot wedge the queue.
        $display("service test: timeout");
        poll_request(32'h44, 8'd1, 32'h02000700, 32'd3, 32'd1);
        repeat (2005) @(negedge clk);
        if (!completion_valid || completion_id != 32'h44 ||
            completion_status != 16'h0004)
            $fatal(1, "abandoned request did not time out");
        pulse_completion_ready();

        // The same watchdog must publish link-down state even when no request
        // is active, without injecting an unsolicited SPI response byte.
        while (!state_valid) @(negedge clk);
        if (state_host_generation != 32'd1 ||
            state_media_generation != 32'd3 || state_flags != 0 ||
            state_media_sectors != 0 || state_max_sectors != 0)
            $fatal(1, "link timeout state mismatch");
        pulse_state_ready();
        repeat (4) @(negedge clk);
        if (tx_read != tx_write)
            $fatal(1, "link timeout emitted an unsolicited response");

        start_runtime(8'h21, 16'd0);
        expect_byte(8'h02);
        send_hello(32'd2, 32'd3);
        pulse_state_ready();
        expect_byte(8'h00);
        if (state_flags != 32'h00000007)
            $fatal(1, "runtime link did not recover after HELLO");

        $display("PASS AstraHost boot, block/input/monitor, DMA, retry, timeout, and recovery");
        $finish;
    end
endmodule

`default_nettype wire
