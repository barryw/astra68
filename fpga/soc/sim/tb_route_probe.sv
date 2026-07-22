// Prove the diagnostic stage-0 image executes without SDRAM or host services.
`timescale 1ns/1ps

module tb_route_probe;
    reg clk25 = 1'b0;
    reg rstn = 1'b0;
    always #20 clk25 = ~clk25;

    wire tx;
    wire [7:0] leds;
    wire [3:0] gpdi;
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

    astra_soc #(
        .RST_MAX(16'd16),
        .SDRAM_ENABLE(1'b0),
        .SD_BOOT_ENABLE(1'b1),
        .HDMI_ENABLE(1'b1),
        .USB_ENABLE(1'b0),
        .CPU_CLK_DIV_BIT(0),
        .UART_BAUD(12500000),
        .ROM_WORDS(1024)
    ) dut (
        .clk25_mhz(clk25), .reset_n(rstn),
        .buttons(6'd0), .switches(4'd0),
        .ftdi_rxd(tx), .ftdi_txd(1'b1), .leds(leds), .gpdi_dp(gpdi),
        .sd_clk(), .sd_cmd(), .sd_d(), .wifi_en(), .wifi_gpio0(),
        .sdram_clk(sdram_clk), .sdram_cke(sdram_cke),
        .sdram_csn(sdram_csn), .sdram_wen(sdram_wen),
        .sdram_rasn(sdram_rasn), .sdram_casn(sdram_casn),
        .sdram_ba(sdram_ba), .sdram_dqm(sdram_dqm),
        .sdram_a(sdram_a), .sdram_d(sdram_d)
    );

    initial begin
        repeat (20) @(posedge clk25);
        rstn = 1'b1;
    end

    string uart_line = "";
    reg [31:0] probe_id;
    reg [31:0] probe_sys;
    reg [31:0] probe_mem;
    reg [31:0] probe_errors;
    reg [31:0] probe_host;
    reg [31:0] probe_cycles;
    reg [31:0] video_id;
    reg [31:0] video_caps;
    reg [31:0] video_ctrl;
    reg [31:0] video_before;
    reg [31:0] video_first;
    reg [31:0] video_last;
    reg video_probe_seen = 1'b0;
    reg route_probe_seen = 1'b0;
    reg white_pixel_seen = 1'b0;
    reg previous_as_n = 1'b1;
    integer bus_cycles = 0;

    always @(posedge dut.video_pixel_clk) begin
        if (dut.post_console_rgb == 24'he8edf2)
            white_pixel_seen <= 1'b1;
        if (route_probe_seen && white_pixel_seen) begin
            $display("ROUTE/VIDEO PROBE PASS cycles=%0d", probe_cycles);
            $finish;
        end
    end

    always @(posedge dut.clk) begin
        previous_as_n <= dut.cpu_as_n;
        if (previous_as_n && !dut.cpu_as_n) begin
            bus_cycles <= bus_cycles + 1;
            if ($test$plusargs("trace-bus") && bus_cycles < 256)
                $display("BUS %0d fc=%b rw=%b siz=%b adr=%08x din=%08x dout=%08x",
                         bus_cycles, dut.cpu_fc, dut.cpu_rw_n, dut.cpu_siz,
                         dut.cpu_adr, dut.cpu_din, dut.cpu_dout);
        end
    end

    always @(posedge dut.clk) begin
        if (dut.uart_start) begin
            if (dut.uart_data == 8'h0d) begin
                // CR is emitted before LF and is not part of the parsed line.
            end else if (dut.uart_data == 8'h0a) begin
                $display("UART: %s", uart_line);
                if ($sscanf(uart_line,
                            "ASTRA VIDEO PROBE id=%h caps=%h ctrl=%h before=%h first=%h last=%h",
                            video_id, video_caps, video_ctrl, video_before,
                            video_first, video_last) == 6) begin
                    if (video_id != 32'h56454741)
                        $fatal(1, "video probe read the wrong Vega ID: %08x",
                               video_id);
                    if (video_caps != 32'h00000077)
                        $fatal(1, "video probe read the wrong capabilities: %08x",
                               video_caps);
                    if (video_ctrl != 0 || video_before != 32'h20 ||
                        video_first != 32'h41 || video_last != 32'h45)
                        $fatal(1, "video text aperture mismatch");
                    if (dut.g_hdmi.post_console_i.cell_mem[182] != 8'h41 ||
                        dut.g_hdmi.post_console_i.cell_mem[198] != 8'h45)
                        $fatal(1, "video text RAM did not retain the banner");
                    video_probe_seen = 1'b1;
                end else if ($sscanf(uart_line,
                            "ASTRA ROUTE PROBE id=%h sys=%h mem=%h err=%h host=%h cycles=%h",
                            probe_id, probe_sys, probe_mem, probe_errors,
                            probe_host, probe_cycles) == 6) begin
                    if (probe_id != 32'h56535441)
                        $fatal(1, "route probe read the wrong Vesta ID: %08x",
                               probe_id);
                    if (probe_sys[2] != 1'b1)
                        $fatal(1, "route probe reset overlay is missing: %08x",
                               probe_sys);
                    if (probe_mem != 0 || probe_errors != 0 || probe_host != 0)
                        $fatal(1, "disabled-service status was not zero");
                    if (probe_cycles == 0)
                        $fatal(1, "CPU cycle counter did not advance");
                    if (!video_probe_seen)
                        $fatal(1, "route probe completed before video probe");
                    route_probe_seen = 1'b1;
                end
                uart_line = "";
            end else begin
                uart_line = {uart_line, dut.uart_data};
            end
        end
    end

    initial begin
        #50_000_000;
        $fatal(1, "route probe timeout adr=%08x", dut.cpu_adr);
    end
endmodule
