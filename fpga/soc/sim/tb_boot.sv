// Astra 68 boot-ROM UART gate.
`timescale 1ns/1ps

module tb_boot;
    reg clk25 = 1'b0;
    reg rstn = 1'b0;
    wire tx;
    wire [7:0] leds;

    astra_soc #(
        .RST_MAX(16'd16),
        .SDRAM_ENABLE(1'b0),
        .HDMI_ENABLE(1'b0),
        .USB_ENABLE(1'b0),
        .CPU_CLK_DIV_BIT(0)
    ) dut (
        .clk25_mhz(clk25),
        .reset_n(rstn),
        .buttons(6'd0),
        .switches(4'd0),
        .ftdi_rxd(tx),
        .ftdi_txd(1'b1),
        .leds(leds)
    );

    always #4 clk25 = ~clk25;

    initial begin
        repeat (40) @(posedge clk25);
        rstn = 1'b1;
    end

    string uart_line = "";
    reg banner_seen = 1'b0;
    reg build_seen = 1'b0;
    reg cpu_seen = 1'b0;
    reg vesta_seen = 1'b0;

    always @(posedge dut.clk) begin
        if (dut.uart_start) begin
            if (dut.uart_data == 8'h0d) begin
                // CR is part of the wire protocol but not the line comparison.
            end else if (dut.uart_data == 8'h0a) begin
                $display("UART: %s", uart_line);
                if (uart_line.len() > 21 &&
                    uart_line.substr(0, 20) == "ASTRA 68 SYSTEM ROM v")
                    banner_seen <= 1'b1;
                if (uart_line.len() >= 74 &&
                    uart_line.substr(0, 6) == "Built: ")
                    build_seen <= 1'b1;
                if (uart_line == "CPU:    TG68K.C 68030 MMU2 @ 12500000 Hz")
                    cpu_seen <= 1'b1;
                if (uart_line == "Vesta:  v1.0")
                    vesta_seen <= 1'b1;
                if (uart_line == "HALTED: POST FAILURE") begin
                    if (!banner_seen)
                        $fatal(1, "POST halt received without the Astra boot banner");
                    if (!build_seen)
                        $fatal(1, "POST halt received without ROM build provenance");
                    if (!cpu_seen)
                        $fatal(1, "POST halt received without the TG68K030 CPU line");
                    if (!vesta_seen)
                        $fatal(1, "POST halt received without Vesta discovery");
                    $display("BOOT UART NO-SDRAM PATH PASS");
                    $finish;
                end
                uart_line = "";
            end else begin
                uart_line = {uart_line, dut.uart_data};
            end
        end
    end

    initial begin
        #100_000_000;
        $fatal(1, "boot UART timeout adr=0x%08x fc=%b rw=%b as=%b dsack=%b",
               dut.cpu_adr, dut.cpu_fc, dut.cpu_rw_n, dut.cpu_as_n,
               dut.dsack_n);
    end
endmodule
