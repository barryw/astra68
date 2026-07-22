// Full immutable-BRAM -> AstraHost SPI -> SDRAM stage-2 boot test.
`timescale 1ns/1ps

module tb_astra_host_boot_soc #(
    parameter [31:0] BUILD_ID = 32'h00000000,
    parameter integer TEST_BYTES = 65536,
    parameter bit PROGRESS = 1'b0,
    parameter bit EXPECT_KERNEL_PANIC = 1'b0
);
    localparam [7:0] SPI_WRITE_OP = 8'h57;
    localparam [7:0] SPI_READ_OP = 8'h52;
    localparam [7:0] SPI_TOKEN_DATA = 8'h01;
    localparam [7:0] CMD_BOOT_BEGIN = 8'h10;
    localparam [7:0] CMD_BOOT_DATA = 8'h11;
    localparam [7:0] CMD_BOOT_COMMIT = 8'h12;
    localparam [7:0] CMD_SERVICE_HELLO = 8'h20;
    localparam integer ROM_HEADER_BYTES = 32;
    localparam integer MAX_ROM_BYTES = 262176;
    localparam [31:0] KERNEL_STATUS_READY = 32'h4b304f4b;
    localparam [31:0] KERNEL_STATUS_PANIC = 32'h4b50414e;
    localparam [31:0] EARLY_LOG_MAGIC = 32'h41364c47;

    reg clk25 = 1'b0;
    reg rstn = 1'b0;
    always #20 clk25 = ~clk25;

    wire tx;
    wire [7:0] leds;
    wire [3:0] gpdi;
    tri sd_clk;
    tri sd_cmd;
    tri [3:0] sd_d;
    reg host_sck = 1'b0;
    reg host_cs_n = 1'b1;
    reg host_mosi = 1'b0;
    wire host_miso = sd_d[0];
    wire wifi_en;
    wire wifi_gpio0;
    wire sdram_clk;
    wire sdram_cke;
    wire sdram_csn;
    wire sdram_wen;
    wire sdram_rasn;
    wire sdram_casn;
    wire [1:0] sdram_ba;
    wire [1:0] sdram_dqm;
    wire [12:0] sdram_a;
    wire [15:0] sdram_d;

    assign sd_clk = host_sck;
    assign sd_cmd = host_mosi;
    assign sd_d[1] = host_cs_n;

    astra_soc #(
        .RST_MAX(16'd16),
        .SDRAM_ENABLE(1'b1),
        .SDRAM_BIST_BYTES(TEST_BYTES),
        .SDRAM_READY_DELAY(10000),
        .HDMI_ENABLE(1'b0),
        .USB_ENABLE(1'b0),
        .CPU_CLK_DIV_BIT(0),
        .UART_BAUD(12500000),
        .SD_BOOT_ENABLE(1'b1),
        .ASTRA_HOST_ENABLE(1'b1),
        .ROM_WORDS(1024),
        .SOC_BUILD_ID(BUILD_ID)
    ) dut (
        .clk25_mhz(clk25), .reset_n(rstn),
        .buttons(6'd0), .switches(4'd0),
        .ftdi_rxd(tx), .ftdi_txd(1'b1), .leds(leds), .gpdi_dp(gpdi),
        .sd_clk(sd_clk), .sd_cmd(sd_cmd), .sd_d(sd_d),
        .wifi_en(wifi_en), .wifi_gpio0(wifi_gpio0),
        .sdram_clk(sdram_clk), .sdram_cke(sdram_cke),
        .sdram_csn(sdram_csn), .sdram_wen(sdram_wen),
        .sdram_rasn(sdram_rasn), .sdram_casn(sdram_casn),
        .sdram_ba(sdram_ba), .sdram_dqm(sdram_dqm),
        .sdram_a(sdram_a), .sdram_d(sdram_d)
    );

    wire [15:0] model_dq;
    wire model_dq_oe;
    assign sdram_d = model_dq_oe ? model_dq : 16'hzzzz;

    astra_sdram_model memory (
        .sdram_clk(sdram_clk), .cke(sdram_cke), .cs(sdram_csn),
        .ras(sdram_rasn), .cas(sdram_casn), .we(sdram_wen),
        .addr(sdram_a), .ba(sdram_ba), .dqm(sdram_dqm),
        .dq_in(sdram_d), .dq_out(model_dq), .dq_oe(model_dq_oe)
    );

    reg [7:0] rom_image [0:MAX_ROM_BYTES-1];
    reg [31:0] payload_size;
    reg [31:0] payload_crc;
    integer payload_offset;
    reg [7:0] response;
    reg [7:0] ignored;

    function automatic [31:0] image_be32(input integer offset);
        image_be32 = {rom_image[offset], rom_image[offset + 1],
                      rom_image[offset + 2], rom_image[offset + 3]};
    endfunction

    task automatic spi_pause;
        begin
            @(negedge clk25);
        end
    endtask

    task automatic spi_select;
        begin
            spi_pause();
            host_sck = 1'b0;
            host_cs_n = 1'b0;
            repeat (2) @(posedge clk25);
        end
    endtask

    task automatic spi_deselect;
        begin
            spi_pause();
            host_sck = 1'b0;
            host_cs_n = 1'b1;
            repeat (2) @(posedge clk25);
        end
    endtask

    task automatic spi_transfer(input [7:0] tx_byte, output [7:0] rx_byte);
        integer bit_index;
        begin
            rx_byte = 8'h00;
            for (bit_index = 7; bit_index >= 0; bit_index = bit_index - 1) begin
                host_mosi = tx_byte[bit_index];
                spi_pause();
                rx_byte = {rx_byte[6:0], host_miso};
                host_sck = 1'b1;
                spi_pause();
                host_sck = 1'b0;
            end
        end
    endtask

    task automatic begin_write;
        begin
            spi_select();
            spi_transfer(SPI_WRITE_OP, ignored);
        end
    endtask

    task automatic write_byte(input [7:0] value);
        begin
            spi_transfer(value, ignored);
        end
    endtask

    task automatic read_response(output [7:0] value);
        reg [7:0] token;
        integer polls;
        begin : poll_loop
            value = 8'hff;
            for (polls = 0; polls < 10000; polls = polls + 1) begin
                spi_select();
                spi_transfer(SPI_READ_OP, ignored);
                spi_transfer(8'h00, token);
                if (token == SPI_TOKEN_DATA) begin
                    spi_transfer(8'h00, value);
                    spi_deselect();
                    disable poll_loop;
                end
                spi_deselect();
                repeat (8) @(posedge clk25);
            end
            $fatal(1, "AstraHost response timeout");
        end
    endtask

    task automatic require_ok(input string operation);
        begin
            read_response(response);
            if (response != 8'h00)
                $fatal(1, "%s failed with AstraHost status %02x",
                       operation, response);
        end
    endtask

    task automatic send_u32(input [31:0] value);
        begin
            write_byte(value[31:24]);
            write_byte(value[23:16]);
            write_byte(value[15:8]);
            write_byte(value[7:0]);
        end
    endtask

    task automatic send_u16(input [15:0] value);
        begin
            write_byte(value[15:8]);
            write_byte(value[7:0]);
        end
    endtask

    task automatic send_u64(input [63:0] value);
        begin
            send_u32(value[63:32]);
            send_u32(value[31:0]);
        end
    endtask

    initial begin
        $readmemh("astra68_rom.hex", rom_image);
        if ({rom_image[0], rom_image[1], rom_image[2], rom_image[3]} !=
            32'h41363852)
            $fatal(1, "invalid packaged ROM magic");
        payload_size = image_be32(8);
        payload_crc = image_be32(12);
        if (payload_size < 8 || payload_size > MAX_ROM_BYTES - ROM_HEADER_BYTES)
            $fatal(1, "invalid packaged ROM size %0d", payload_size);

        repeat (20) @(posedge clk25);
        rstn = 1'b1;

        wait (dut.host_boot_request_cpu);
        if (wifi_en !== 1'b1 || wifi_gpio0 !== 1'b1)
            $fatal(1, "AstraHost did not own the shared SPI bus");

        begin_write();
        write_byte(CMD_BOOT_BEGIN);
        send_u32(payload_size);
        send_u32(payload_crc);
        send_u32(32'h01e00000);
        spi_deselect();
        require_ok("BOOT_BEGIN");

        payload_offset = 0;
        while (payload_offset < payload_size) begin : stream_chunks
            integer chunk_size;
            integer chunk_index;
            chunk_size = payload_size - payload_offset;
            if (chunk_size > 256) chunk_size = 256;
            begin_write();
            write_byte(CMD_BOOT_DATA);
            write_byte(chunk_size == 256 ? 8'h00 : chunk_size[7:0]);
            for (chunk_index = 0; chunk_index < chunk_size;
                 chunk_index = chunk_index + 1)
                write_byte(rom_image[ROM_HEADER_BYTES + payload_offset +
                                     chunk_index]);
            spi_deselect();
            require_ok("BOOT_DATA");
            payload_offset = payload_offset + chunk_size;
        end

        begin_write();
        write_byte(CMD_BOOT_COMMIT);
        spi_deselect();
        require_ok("BOOT_COMMIT");

        // The production host switches directly from the immutable boot
        // protocol to the framed runtime service. Advertise a live host with
        // no provisioned Astra partition so the kernel can validate the
        // storage/input transport without depending on an SD image here.
        begin_write();
        write_byte(CMD_SERVICE_HELLO);
        send_u16(16'd22);
        send_u32(32'h11223344);
        send_u32(32'd1);
        send_u32(32'h00000001);
        send_u64(64'd0);
        send_u16(16'd16);
        spi_deselect();
        require_ok("SERVICE_HELLO");
    end

    initial begin
        if (PROGRESS) begin
            forever begin
                #5_000_000;
                $display("PROGRESS t=%0t pc=%08x request=%b host=%b/%b/%b bytes=%0d overlay=%b",
                         $time, dut.cpu_adr, dut.host_boot_request_cpu,
                         dut.host_boot_busy_mem, dut.host_boot_done_mem,
                         dut.host_boot_error_mem, dut.host_bytes_received_mem,
                         dut.rom_overlay_sdram);
                $fflush();
            end
        end
    end

    string uart_line = "";
    reg stage0_seen = 1'b0;
    reg bist_seen = 1'b0;
    reg host_seen = 1'b0;
    reg handoff_seen = 1'b0;
    reg stage2_seen = 1'b0;
    reg post_seen = 1'b0;
    reg expect_kernel_panic;

    initial begin
        expect_kernel_panic = EXPECT_KERNEL_PANIC;
        if ($test$plusargs("expect-kernel-panic")) expect_kernel_panic = 1'b1;
    end

    always @(posedge dut.clk) begin
        if (dut.uart_start) begin
            if (dut.uart_data == 8'h0d) begin
            end else if (dut.uart_data == 8'h0a) begin
                $display("UART: %s", uart_line);
                $fflush();
                if (uart_line == "ASTRA 68 STAGE 0 v0.2") stage0_seen <= 1'b1;
                if (uart_line == "SDRAM full BIST ... OK") bist_seen <= 1'b1;
                if (uart_line == "AstraHost ROM ..... OK") host_seen <= 1'b1;
                if (uart_line == "Starting system ROM") handoff_seen <= 1'b1;
                if (uart_line.len() > 21 &&
                    uart_line.substr(0, 20) == "ASTRA 68 SYSTEM ROM v")
                    stage2_seen <= 1'b1;
                if (uart_line.len() >= 6 && uart_line.substr(0, 5) == "FAILED")
                    $fatal(1, "stage 0 failed: %s", uart_line);
                if (uart_line == "POST FAIL" ||
                    uart_line == "HALTED: POST FAILURE")
                    $fatal(1, "stage 2 reported POST failure");
                if (uart_line == "POST PASS") begin
                    if (!stage0_seen || !bist_seen || !host_seen ||
                        !handoff_seen || !stage2_seen)
                        $fatal(1, "POST passed without complete AstraHost boot");
                    if (!dut.rom_overlay_sdram || !dut.host_boot_done_mem ||
                        dut.host_boot_error_mem)
                        $fatal(1, "invalid host handoff state");
                    if (dut.host_bytes_received_mem != payload_size)
                        $fatal(1, "host payload byte count mismatch");
                    $display("ASTRAHOST SPI BOOT PASS bytes=%0d overlay=%b I$=%0d/%0d D$=%0d",
                             payload_size, dut.rom_overlay_sdram,
                             dut.tg_icache_hits, dut.tg_icache_misses,
                             dut.tg_dcache_hits);
                    post_seen <= 1'b1;
                end
                uart_line = "";
            end else begin
                uart_line = {uart_line, dut.uart_data};
            end
        end
    end

    function automatic [23:0] model_key(input [24:0] byte_offset);
        model_key = {byte_offset[9], byte_offset[11:10],
                     byte_offset[24:12], byte_offset[8:2], 1'b0};
    endfunction

    function automatic [31:0] sdram_be32(input [24:0] byte_offset);
        reg [23:0] key;
        begin
            key = model_key(byte_offset);
            sdram_be32 = {memory.memory[key][7:0],
                          memory.memory[key][15:8],
                          memory.memory[key + 1'b1][7:0],
                          memory.memory[key + 1'b1][15:8]};
        end
    endfunction

    function automatic [7:0] sdram_byte(input [24:0] byte_offset);
        reg [23:0] key;
        begin
            key = model_key(byte_offset);
            case (byte_offset[1:0])
                2'd0: sdram_byte = memory.memory[key][7:0];
                2'd1: sdram_byte = memory.memory[key][15:8];
                2'd2: sdram_byte = memory.memory[key + 1'b1][7:0];
                default: sdram_byte = memory.memory[key + 1'b1][15:8];
            endcase
        end
    endfunction

    always @(posedge dut.clk) begin
        if (post_seen && dut.sys_scratch == KERNEL_STATUS_PANIC &&
            !expect_kernel_panic) begin : unexpected_panic
            integer log_index;
            integer log_bytes;
            log_bytes = sdram_be32(25'h0000010);
            $write("EARLY LOG:\n");
            for (log_index = 0; log_index < log_bytes && log_index < 2048;
                 log_index = log_index + 1)
                $write("%c", sdram_byte(
                    sdram_be32(25'h000000c) + log_index));
            $write("\nEND EARLY LOG\n");
            $fatal(1, "kernel panicked during normal AstraHost boot");
        end
        if (post_seen &&
            dut.sys_scratch == (expect_kernel_panic ? KERNEL_STATUS_PANIC :
                                                       KERNEL_STATUS_READY)) begin
            if (sdram_be32(25'h0000000) != EARLY_LOG_MAGIC)
                $fatal(1, "early log header missing: %08x",
                       sdram_be32(25'h0000000));
            if (sdram_be32(25'h0000008) != 32'h00004000)
                $fatal(1, "early log size mismatch: %08x",
                       sdram_be32(25'h0000008));
            if (expect_kernel_panic &&
                (sdram_be32(25'h0000018) & 32'h1) == 0)
                $fatal(1, "panic did not mark early log");
            $display("ASTRAHOST KERNEL %s PASS status=%08x log_write=%0d",
                     expect_kernel_panic ? "PANIC" : "ENTRY",
                     dut.sys_scratch, sdram_be32(25'h0000010));
            $finish;
        end
    end

    initial begin
        #2_000_000_000;
        $fatal(1, "AstraHost boot timeout pc=%08x request=%b bytes=%0d",
               dut.cpu_adr, dut.host_boot_request_cpu,
               dut.host_bytes_received_mem);
    end
endmodule
