// Astra 68 — SoC bus-hang reproduction testbench (iverilog).
// Boots the SIM harness (sim_harness.c) which executes ONE Harte case, and watches for the
// RAM-instruction-fetch hang: when the CPU address bus freezes, dump the full bus state so
// we can see which access stalled and why the FSM never returns DSACK.
`timescale 1ns/1ps
module tb_soc;
    reg  clk25 = 0;
    reg  rstn  = 0;
    wire tx, leds_x;
    wire [7:0] leds;

    astra_soc #(.RST_MAX(16'd16)) dut (   // short reset for sim
        .clk25_mhz(clk25), .reset_n(rstn),
        .ftdi_rxd(tx), .ftdi_txd(1'b1),   // RX idle high (no host injection)
        .leds(leds)
    );

    always #4 clk25 = ~clk25;             // 125 MHz sim clock (functional; timing irrelevant)

    initial begin
        rstn = 0;
        repeat (40) @(posedge clk25);
        rstn = 1;
    end

    // --- UART TX char monitor (peek the SoC's uart write strobe) ---
    always @(posedge dut.clk)
        if (dut.uart_start)
            $display("[%0t] TX '%c' (0x%02x)", $time, dut.uart_data, dut.uart_data);

    // --- hang detector: a KNOWN (non-X) CPU address bus frozen => dump bus state ---
    reg [31:0] last_adr = 32'hX;
    reg        started  = 0;
    integer stall = 0;
    always @(posedge dut.clk) begin
        if (^dut.cpu_adr === 1'bx) begin
            stall = 0;                              // CPU not driving a real address yet
        end else begin
            if (!started) begin started <= 1; $display("[%0t] CPU driving adr, first=0x%08x", $time, dut.cpu_adr); end
            if (dut.cpu_adr === last_adr) stall = stall + 1;
            else begin stall = 0; last_adr = dut.cpu_adr; end
        end
        if (stall == 3000) begin
            $display("\n*** HANG: cpu_adr frozen at 0x%08x for 3000 CPU clks ***", dut.cpu_adr);
            $display("  FSM bs=%0d  as_n=%b ds_n=%b rw_n=%b dsack_n=%b  siz=%b fc=%b",
                     dut.bs, dut.cpu_as_n, dut.cpu_ds_n, dut.cpu_rw_n, dut.dsack_n, dut.cpu_siz, dut.cpu_fc);
            $display("  sel_rom=%b sel_ram=%b sel_uart=%b  cpu_din=0x%08x ram_q=0x%08x rom_q=0x%08x",
                     dut.sel_rom, dut.sel_ram, dut.sel_uart, dut.cpu_din, dut.ram_q, dut.rom_q);
            $display("  data_en=%b dout=0x%08x  waitc=%0d bus_read_stb=%b bus_write_stb=%b",
                     dut.cpu_data_en, dut.cpu_dout, dut.waitc, dut.bus_read_stb, dut.bus_write_stb);
            $finish;
        end
    end

    // heartbeat: periodic CPU state so we can see boot progress / where it sits
    integer hb = 0;
    always @(posedge dut.clk) begin
        hb = hb + 1;
        if (hb % 4000 == 0)
            $display("[%0t] hb=%0d rst=%b as_n=%b rw_n=%b adr=0x%08x dsack=%b bs=%0d",
                     $time, hb, dut.rst, dut.cpu_as_n, dut.cpu_rw_n, dut.cpu_adr, dut.dsack_n, dut.bs);
    end

    initial begin
        $dumpfile("tb_soc.vcd");
        $dumpvars(0, tb_soc);
        #6_000_000;                       // 6 ms sim cap (~ tens of k CPU clks)
        $display("\n*** TIMEOUT — no hang detected in window ***");
        $finish;
    end
endmodule
