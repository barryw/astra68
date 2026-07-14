`timescale 1ns/1ps
`default_nettype none

module tb_astra_host_boot;
    reg clk = 1'b0;
    reg rst = 1'b1;
    always #5 clk = ~clk;

    reg [7:0] rx_data = 8'd0;
    reg rx_valid = 1'b0;
    wire rx_ready;
    wire [7:0] tx_data;
    wire tx_start;
    reg tx_busy = 1'b0;
    reg boot_request = 1'b1;
    wire host_seen;
    wire boot_busy;
    wire boot_done;
    wire boot_error;
    wire [7:0] error_code;
    wire [31:0] payload_size;
    wire [31:0] payload_crc32;
    wire [31:0] initial_sp;
    wire [31:0] initial_pc;
    wire [31:0] bytes_received;
    wire mem_lock;
    wire mem_valid;
    reg mem_ready = 1'b1;
    wire mem_write;
    wire [24:0] mem_addr;
    wire [3:0] mem_be;
    wire [31:0] mem_wdata;
    reg mem_rsp_valid = 1'b0;

    reg [7:0] memory [0:31];
    reg response_pending = 1'b0;
    integer offset;
    integer index;
    reg [31:0] expected_crc;
    reg [7:0] received;
    reg [7:0] payload [0:12];

    astra_host_boot #(.RX_STALL_CYCLES(100)) dut (
        .clk(clk), .rst(rst),
        .rx_data(rx_data), .rx_valid(rx_valid), .rx_ready(rx_ready),
        .tx_data(tx_data), .tx_start(tx_start), .tx_busy(tx_busy),
        .boot_request(boot_request), .host_seen(host_seen),
        .boot_busy(boot_busy), .boot_done(boot_done),
        .boot_error(boot_error), .error_code(error_code),
        .payload_size(payload_size), .payload_crc32(payload_crc32),
        .initial_sp(initial_sp), .initial_pc(initial_pc),
        .bytes_received(bytes_received),
        .mem_lock(mem_lock), .mem_valid(mem_valid), .mem_ready(mem_ready),
        .mem_write(mem_write), .mem_addr(mem_addr), .mem_be(mem_be),
        .mem_wdata(mem_wdata), .mem_rsp_valid(mem_rsp_valid)
    );

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

    always @(posedge clk) begin
        mem_rsp_valid <= response_pending;
        response_pending <= 1'b0;
        if (mem_valid && mem_ready) begin
            if (!mem_write) $fatal(1, "boot DMA attempted a read");
            offset = mem_addr - 25'h1e00000;
            if (mem_be[3]) memory[offset] <= mem_wdata[31:24];
            if (mem_be[2]) memory[offset + 1] <= mem_wdata[23:16];
            if (mem_be[1]) memory[offset + 2] <= mem_wdata[15:8];
            if (mem_be[0]) memory[offset + 3] <= mem_wdata[7:0];
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

    task receive_byte(output [7:0] value);
        begin
            do @(negedge clk); while (!tx_start);
            value = tx_data;
        end
    endtask

    task expect_status(input [7:0] expected);
        begin
            receive_byte(received);
            if (received != expected)
                $fatal(1, "status mismatch expected=%02x actual=%02x",
                       expected, received);
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

    initial begin
        payload[0] = 8'h02;
        payload[1] = 8'h00;
        payload[2] = 8'h00;
        payload[3] = 8'h00;
        payload[4] = 8'hff;
        payload[5] = 8'he0;
        payload[6] = 8'h04;
        payload[7] = 8'h00;
        payload[8] = 8'h12;
        payload[9] = 8'h34;
        payload[10] = 8'h56;
        payload[11] = 8'h78;
        payload[12] = 8'h9a;
        expected_crc = 32'hffffffff;
        for (index = 0; index < 13; index = index + 1)
            expected_crc = crc32_byte(expected_crc, payload[index]);
        expected_crc = ~expected_crc;

        repeat (4) @(negedge clk);
        rst = 1'b0;

        send_byte(8'h01);
        expect_status(8'h00);
        receive_byte(received);
        if (received != "A") $fatal(1, "identify magic mismatch");
        for (index = 0; index < 13; index = index + 1)
            receive_byte(received);

        send_byte(8'h10);
        send_be32(7);
        send_be32(expected_crc);
        send_be32(32'h01e00000);
        expect_status(8'h03);
        if (boot_busy) $fatal(1, "undersized BOOT_BEGIN entered busy state");

        send_byte(8'h10);
        send_be32(32'h00040001);
        send_be32(expected_crc);
        send_be32(32'h01e00000);
        expect_status(8'h03);
        if (boot_busy) $fatal(1, "oversized BOOT_BEGIN entered busy state");

        send_byte(8'h10);
        send_be32(13);
        send_be32(expected_crc);
        send_be32(32'h01e00004);
        expect_status(8'h03);
        if (boot_busy) $fatal(1, "misaddressed BOOT_BEGIN entered busy state");

        send_byte(8'h10);
        send_be32(13);
        send_be32(expected_crc);
        send_be32(32'h01e00000);
        expect_status(8'h00);
        if (!boot_busy) $fatal(1, "BOOT_BEGIN did not enter busy state");

        send_byte(8'h11);
        send_byte(13);
        for (index = 0; index < 13; index = index + 1)
            send_byte(payload[index]);
        expect_status(8'h00);

        send_byte(8'h12);
        expect_status(8'h00);
        repeat (3) @(negedge clk);
        if (!boot_done || boot_busy || boot_error)
            $fatal(1, "commit flags done=%b busy=%b error=%b",
                   boot_done, boot_busy, boot_error);
        if (bytes_received != 13 || payload_size != 13 ||
            payload_crc32 != expected_crc)
            $fatal(1, "boot metadata mismatch");
        if (initial_sp != 32'h02000000 || initial_pc != 32'hffe00400)
            $fatal(1, "vector mismatch SP=%08x PC=%08x", initial_sp, initial_pc);
        for (index = 0; index < 13; index = index + 1)
            if (memory[index] != payload[index])
                $fatal(1, "memory mismatch at %0d expected=%02x actual=%02x",
                       index, payload[index], memory[index]);

        send_byte(8'h13);
        expect_status(8'h00);
        send_byte(8'h10);
        send_be32(13);
        send_be32(expected_crc ^ 32'h1);
        send_be32(32'h01e00000);
        expect_status(8'h00);
        send_byte(8'h11);
        send_byte(13);
        for (index = 0; index < 13; index = index + 1)
            send_byte(payload[index]);
        expect_status(8'h00);
        send_byte(8'h12);
        expect_status(8'h05);
        if (!boot_error || error_code != 8'h05)
            $fatal(1, "CRC failure was not latched");

        $display("PASS AstraHost validation, boot stream, SDRAM writes, vectors, and CRC");
        $finish;
    end
endmodule

`default_nettype wire
