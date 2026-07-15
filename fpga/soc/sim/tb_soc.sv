`timescale 1ns/1ps

module tb_soc;
    localparam integer UART_BAUD = 460800;
    localparam integer UART_BIT_CLKS = 12500000 / UART_BAUD;

    reg clk25 = 1'b0;
    reg rstn = 1'b0;
    reg host_rx = 1'b1;
    wire tx;
    wire [7:0] leds;

    astra_soc #(
        .RST_MAX(16'd16),
        .SDRAM_ENABLE(1'b0),
        .HDMI_ENABLE(1'b0),
        .CPU_CLK_DIV_BIT(0),
        .UART_BAUD(UART_BAUD)
    ) dut (
        .clk25_mhz(clk25), .reset_n(rstn),
        .buttons(6'd0), .switches(4'd0),
        .ftdi_rxd(tx), .ftdi_txd(host_rx), .leds(leds)
    );

    always #4 clk25 = ~clk25;

    initial begin
        repeat (40) @(posedge clk25);
        rstn = 1'b1;
    end

    reg boot_seen = 1'b0;
    reg collecting = 1'b0;
    reg response_done = 1'b0;
    reg [7:0] response [0:64];
    integer response_count = 0;
    integer rx_reads = 0;

    always @(posedge dut.clk) begin
        if (dut.rx_data_rd)
            rx_reads <= rx_reads + 1;

        if (dut.uart_start) begin
            if (!boot_seen && dut.uart_data == 8'h52)
                boot_seen <= 1'b1;
            if (!collecting && dut.uart_data == 8'haa) begin
                collecting <= 1'b1;
                response[0] <= dut.uart_data;
                response_count <= 1;
            end else if (collecting && response_count < 65) begin
                response[response_count] <= dut.uart_data;
                response_count <= response_count + 1;
                if (response_count == 64)
                    response_done <= 1'b1;
            end
        end
    end

    task automatic uart_send_byte(input [7:0] value);
        integer bit_number;
        begin
            host_rx = 1'b0;
            repeat (UART_BIT_CLKS) @(posedge dut.clk);
            for (bit_number = 0; bit_number < 8; bit_number = bit_number + 1) begin
                host_rx = value[bit_number];
                repeat (UART_BIT_CLKS) @(posedge dut.clk);
            end
            host_rx = 1'b1;
            repeat (UART_BIT_CLKS) @(posedge dut.clk);
        end
    endtask

    reg [7:0] run_frame [0:67];
    integer index;
    integer checksum;
    initial begin
        for (index = 0; index < 68; index = index + 1)
            run_frame[index] = 8'h00;
        run_frame[0] = 8'h55;
        run_frame[1] = 8'h42;
        run_frame[2] = 8'h01;
        run_frame[6] = 8'h01;
        run_frame[10] = 8'h02;
        run_frame[64] = 8'h02;
        run_frame[65] = 8'hd0;
        run_frame[66] = 8'h41;
        run_frame[67] = 8'h17;

        wait (boot_seen);
        repeat (200) @(posedge dut.clk);
        for (index = 0; index < 68; index = index + 1)
            uart_send_byte(run_frame[index]);

        wait (response_done);
        @(posedge dut.clk);

        if (rx_reads !== 68) begin
            $display("FAIL RX pop count got=%0d expected=68", rx_reads);
            $fatal;
        end
        if (response[0] !== 8'haa || response[1] !== 8'h3f || response[2] !== 8'h81) begin
            $display("FAIL RESULT header %02x %02x %02x", response[0], response[1], response[2]);
            $fatal;
        end
        if ({response[3], response[4], response[5], response[6]} !== 32'h00000003) begin
            $display("FAIL D0 result %02x%02x%02x%02x",
                     response[3], response[4], response[5], response[6]);
            $fatal;
        end
        if ({response[7], response[8], response[9], response[10]} !== 32'h00000002) begin
            $display("FAIL D1 changed");
            $fatal;
        end
        if (response[63] !== 8'h00) begin
            $display("FAIL CCR result %02x", response[63]);
            $fatal;
        end

        checksum = 0;
        for (index = 2; index < 64; index = index + 1)
            checksum = checksum + response[index];
        if (response[64] !== checksum[7:0]) begin
            $display("FAIL RESULT checksum got=%02x expected=%02x", response[64], checksum[7:0]);
            $fatal;
        end

        $display("PASS Harte protocol-v2 RUN path");
        $finish;
    end

    initial begin
        #100_000_000;
        $display("FAIL timeout boot=%0d rx_reads=%0d response_bytes=%0d",
                 boot_seen, rx_reads, response_count);
        $fatal;
    end
endmodule
