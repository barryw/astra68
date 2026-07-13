// Astra 68 — SoC RUN-path reproduction testbench.
// Boots the real Harte UART harness, injects one paced RUN frame, and watches for
// exception/fault loops. The old frozen-address detector is still useful, but the
// current failure keeps changing address, so this bench also traces vector fetches
// and CPU-space cycles after the frame has been consumed.
`timescale 1ns/1ps
module tb_soc #(
    parameter CPU_TG68K = 1'b0
);
    reg  clk25 = 0;
    reg  rstn  = 0;
    reg  host_rx = 1'b1;
    wire tx;
    wire [7:0] leds;

    astra_soc #(
        .RST_MAX(16'd16),
        .CPU_TG68K(CPU_TG68K),
        .SDRAM_ENABLE(1'b0),
        .HDMI_ENABLE(1'b0)
    ) dut (   // short reset for sim
        .clk25_mhz(clk25), .reset_n(rstn),
        .ftdi_rxd(tx), .ftdi_txd(host_rx),
        .leds(leds)
    );

    always #4 clk25 = ~clk25;             // 125 MHz sim clock (functional; timing irrelevant)

    initial begin
        rstn = 0;
        repeat (40) @(posedge clk25);
        rstn = 1;
    end

    // --- UART TX char monitor (peek the SoC's uart write strobe) ---
    reg boot_seen = 1'b0;
    always @(posedge dut.clk)
        if (dut.uart_start) begin
            $display("[%0t] TX '%c' (0x%02x)", $time, dut.uart_data, dut.uart_data);
            if (dut.uart_data == 8'h52) boot_seen <= 1'b1; // 'R'
        end

    // --- UART RX frame injector ---
    localparam integer UART_BIT_CLKS = 3125000 / 115200; // matches uart_rx instance
    reg [7:0] run_frame [0:67];
    integer rx_reads = 0;
    integer post_frame = 0;

    always @(posedge dut.clk)
        if (dut.rx_data_rd)
            rx_reads = rx_reads + 1;

    task automatic uart_send_byte(input [7:0] b);
        integer bitn;
        begin
            host_rx = 1'b0; repeat (UART_BIT_CLKS) @(posedge dut.clk); // start
            for (bitn = 0; bitn < 8; bitn = bitn + 1) begin
                host_rx = b[bitn];
                repeat (UART_BIT_CLKS) @(posedge dut.clk);
            end
            host_rx = 1'b1; repeat (UART_BIT_CLKS) @(posedge dut.clk); // stop
        end
    endtask

    task automatic send_paced_byte(input [7:0] b, input integer idx);
        integer want_reads;
        integer timeout;
        begin
            want_reads = rx_reads + 1;
            uart_send_byte(b);
            timeout = 0;
            while (rx_reads < want_reads && timeout < 200000) begin
                timeout = timeout + 1;
                @(posedge dut.clk);
            end
            if (rx_reads < want_reads) begin
                $display("[%0t] RX INJECT TIMEOUT byte[%0d]=0x%02x reads=%0d want=%0d",
                         $time, idx, b, rx_reads, want_reads);
                $finish;
            end
            repeat (20) @(posedge dut.clk);
        end
    endtask

    integer fi;
    initial begin
        // RUN: d0=1, d1=2, ccr=0, instruction D041 (ADD.W D1,D0), then checksum.
        for (fi = 0; fi < 68; fi = fi + 1) run_frame[fi] = 8'h00;
        run_frame[0]  = 8'h55; // host sync
        run_frame[1]  = 8'h42; // LEN = CMD + payload + checksum = 66
        run_frame[2]  = 8'h01; // CMD_RUN
        run_frame[6]  = 8'h01; // d0 = 1
        run_frame[10] = 8'h02; // d1 = 2
        run_frame[64] = 8'h02; // ilen = 2
        run_frame[65] = 8'hD0;
        run_frame[66] = 8'h41;
        run_frame[67] = 8'h17; // sum(CMD..payload) & 0xff

        wait (boot_seen);
        repeat (200) @(posedge dut.clk);
        $display("[%0t] Injecting paced RUN frame (%0d bytes)", $time, 68);
        for (fi = 0; fi < 68; fi = fi + 1)
            send_paced_byte(run_frame[fi], fi);
        post_frame = 1;
        $display("[%0t] RUN frame consumed; watching execute/exception path", $time);
    end

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

    // Trace bus activity after the frame reaches run_case. Low-ROM reads are
    // exception vector fetches; FC=111 is CPU space (IACK/special cycles).
    integer post_cycles = 0;
    integer vector_fetches = 0;
    integer vector_num;
    always @(posedge dut.clk) begin
        if (post_frame && (dut.bus_read_stb || dut.bus_write_stb)) begin
            post_cycles = post_cycles + 1;
            if (dut.cpu_adr[31:14] == 18'd0 && dut.bus_read_stb) begin
                vector_num = dut.cpu_adr[9:2];
                vector_fetches = vector_fetches + 1;
                $display("[%0t] VECTOR[%0d] fetch adr=0x%08x fc=%b siz=%b rom_q=0x%08x",
                         $time, vector_num, dut.cpu_adr, dut.cpu_fc, dut.cpu_siz, dut.rom_q);
            end else if (dut.cpu_fc == 3'b111) begin
                $display("[%0t] CPU-SPACE %s adr=0x%08x siz=%b dout=0x%08x",
                         $time, dut.cpu_rw_n ? "RD" : "WR", dut.cpu_adr, dut.cpu_siz, dut.cpu_dout);
            end else if (post_cycles <= 180) begin
                $display("[%0t] BUS %s adr=0x%08x fc=%b siz=%b sel(ram,rom,uart)=%b%b%b dout=0x%08x din_src=0x%08x",
                         $time, dut.cpu_rw_n ? "RD" : "WR", dut.cpu_adr, dut.cpu_fc, dut.cpu_siz,
                         dut.sel_ram, dut.sel_rom, dut.sel_uart, dut.cpu_dout,
                         dut.sel_rom ? dut.rom_q : (dut.sel_ram ? dut.ram_q : dut.uart_rdata));
            end
            if (vector_fetches == 12) begin
                $display("[%0t] Saw 12 vector fetches after RUN; stopping for fault-loop analysis", $time);
                $finish;
            end
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
        #60_000_000;
        $display("\n*** TIMEOUT — no hang detected in window ***");
        $finish;
    end
endmodule
