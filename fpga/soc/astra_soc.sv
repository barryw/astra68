// =============================================================================
// Astra 68 — SoC skeleton (hardware MVP): WF68K30L + boot ROM + system RAM +
// Vesta UART, on a single ~10 MHz clock (safely under the core's ~11.6 MHz Fmax).
//
// Goal: boot astra_boot.bin from ROM and print the banner over the FTDI UART.
// Minimal by design — no SDRAM yet (stack lives in BRAM), no overlay (ROM is
// aliased at $0 so the reset vector fetch works), no interrupts.
//
// Mixed-language: this SV top instantiates the VHDL core WF68K30L_TOP and the
// e6502 uart_tx.sv / ecp5pll.sv — built with Diamond/Synplify (ghdl can't).
//
// 68030 bus: async, split DATA_IN (to CPU) / DATA_OUT (from CPU), no tristate.
// A cycle: ASn low -> decode ADR_OUT -> present read data / do write -> assert
// DSACKn=00 (32-bit port) -> CPU finishes, ASn high -> deassert.
// =============================================================================
`default_nettype none

module astra_soc (
    input  wire       clk25_mhz,
    input  wire       reset_n,     // btn[0] / BTN_PWRn, active low
    output wire       ftdi_rxd,    // FPGA TX -> host
    input  wire       ftdi_txd,    // host -> FPGA RX
    output wire [7:0] leds
);
    // -------------------------------------------------------------------------
    // Clock: 25 MHz / 8 = 3.125 MHz CPU/bus clock, from a fabric divider (clean
    // 50% duty). The WF68K30L samples DSACK and latches read data on the NEGATIVE
    // clock edge, so its negedge->posedge paths get only HALF a period: the core's
    // effective Fmax is ~half its 11.6 MHz posedge figure (~5-6 MHz). A 10 MHz PLL
    // clock booted in sim but NOT on HW (negedge paths miss timing); 3.125 MHz
    // boots on real HW (verified via boot trace). Faster needs floorplanning/
    // retiming of the core's negedge paths. ponytail: revisit clock rate later.
    reg [2:0] clkdiv = 3'd0;
    always @(posedge clk25_mhz) clkdiv <= clkdiv + 1'b1;
    wire clk = clkdiv[2];   // 25/8 = 3.125 MHz

    // Power-on reset: button + a counter (no PLL to wait on).
    reg [15:0] rst_cnt = 16'd0;
    reg        rst = 1'b1;
    always @(posedge clk) begin
        if (!reset_n)                  begin rst_cnt <= 0; rst <= 1'b1; end
        else if (rst_cnt != 16'hFFFF)  rst_cnt <= rst_cnt + 1'b1;
        else                           rst <= 1'b0;
    end

    // -------------------------------------------------------------------------
    // CPU signals
    // -------------------------------------------------------------------------
    wire [31:0] cpu_adr;
    wire [31:0] cpu_dout;      // data FROM cpu (writes)
    reg  [31:0] cpu_din;       // data TO cpu (reads)
    wire        cpu_data_en;   // cpu driving data (write)
    wire        cpu_as_n, cpu_ds_n, cpu_rw_n;
    wire [1:0]  cpu_siz;
    wire [2:0]  cpu_fc;
    reg  [1:0]  dsack_n = 2'b11;

    // Bus-control inputs {BGACKn,BRn,STERMn,AVECn,HALT_INn,BERRn}.
    // HALT_INn (bit 1) is asserted LOW during reset together with RESET_INn: the
    // WF68K30L only sequences its internal reset while RESET and HALT are BOTH
    // asserted for >10 clocks (real 68030 reset). Tying HALT_INn high left the core
    // stuck in reset (verified in QuestaSim); driving it with ~rst boots it.
    // The other five stay inactive-high, and the whole reg is syn_preserve'd because
    // Synplify constant-folds the ENTIRE WF68K30L away (~0 LUT) if these core inputs
    // read as compile-time constants — a preserved FF output is not foldable.
    // ponytail: no bus-error watchdog yet; add BERRn from a DSACK-timeout counter
    // when SDRAM/stray-access handling lands (a stray access now hangs on no DSACK).
    (* syn_preserve = 1 *) reg [5:0] cpu_ctl = 6'b111111;
    always @(posedge clk) cpu_ctl <= {4'b1111, ~rst, 1'b1};   // bit1 = HALT_INn = ~rst

    // Generic-free VHDL wrapper (mixed-lang can't bind the core's std_logic_vector
    // generic directly); it opens the unused status/arbitration outputs.
    wf68k_wrap cpu (
        .CLK        (clk),
        .ADR_OUT    (cpu_adr),
        .DATA_IN    (cpu_din),
        .DATA_OUT   (cpu_dout),
        .DATA_EN    (cpu_data_en),
        .RESET_INn  (~rst),
        .FC_OUT     (cpu_fc),
        .IPLn       (3'b111),      // no interrupts
        .DSACKn     (dsack_n),
        .SIZE       (cpu_siz),
        .ASn        (cpu_as_n),
        .RWn        (cpu_rw_n),
        .DSn        (cpu_ds_n),
        .BERRn      (cpu_ctl[0]),
        .HALT_INn   (cpu_ctl[1]),
        .AVECn      (cpu_ctl[2]),
        .STERMn     (cpu_ctl[3]),
        .BRn        (cpu_ctl[4]),
        .BGACKn     (cpu_ctl[5])
    );

    // -------------------------------------------------------------------------
    // Address decode (skeleton map)
    //   ROM  0x00000000 & 0xFFE00000 (aliased so $0/$4 vectors fetch from ROM)
    //   RAM  0x01FF8000..0x01FFFFFF (32 KB, stack top = 0x02000000)
    //   UART 0xFFF00500..0xFFF0050F (Vesta UART)
    // -------------------------------------------------------------------------
    localparam ROM_WORDS = 2048;              // 8 KB ROM (selftest ~5 KB; must match sim tb_soc.sv=2048)
    localparam RAM_WORDS = 8192;              // 32 KB system RAM (BRAM)

    wire sel_rom  = (cpu_adr[31:20] == 12'hFFE) || (cpu_adr[31:14] == 18'd0); // 0xFFE0xxxx or low 16KB
    wire sel_ram  = (cpu_adr[31:15] == 17'h03FF); // 0x01FF8000..0x01FFFFFF
    wire sel_uart = (cpu_adr[31:8]  == 24'hFFF005);

    // -------------------------------------------------------------------------
    // ROM (32-bit words, big-endian; init from boot image)
    // -------------------------------------------------------------------------
    (* rom_style = "block" *) reg [31:0] rom [0:ROM_WORDS-1];
    initial $readmemh("rom_init.hex", rom);
    reg [31:0] rom_q;
    always @(posedge clk) rom_q <= rom[cpu_adr[$clog2(ROM_WORDS)+1:2]];

    // -------------------------------------------------------------------------
    // System RAM (32-bit words + per-byte write enable), big-endian lanes
    // -------------------------------------------------------------------------
    reg [7:0] ram0 [0:RAM_WORDS-1]; // [31:24]
    reg [7:0] ram1 [0:RAM_WORDS-1]; // [23:16]
    reg [7:0] ram2 [0:RAM_WORDS-1]; // [15:8]
    reg [7:0] ram3 [0:RAM_WORDS-1]; // [7:0]
    wire [12:0] ram_a = cpu_adr[$clog2(RAM_WORDS)+1:2];
    reg  [31:0] ram_q;

    // 68030 32-bit-port byte enables from SIZE + A[1:0] (big-endian lanes).
    // lane3=[31:24]=A..00, lane2=A..01, lane1=A..10, lane0=A..11
    reg [3:0] be;
    always @* begin
        case (cpu_siz)
            2'b01: be = 4'b1000 >> cpu_adr[1:0];              // byte
            2'b10: be = cpu_adr[1] ? 4'b0011 : 4'b1100;       // word (aligned)
            default: be = 4'b1111;                            // long / line
        endcase
    end

    wire ram_we = sel_ram & cpu_data_en & ~cpu_rw_n & bus_write_stb;
    always @(posedge clk) begin
        if (ram_we) begin
            if (be[3]) ram0[ram_a] <= cpu_dout[31:24];
            if (be[2]) ram1[ram_a] <= cpu_dout[23:16];
            if (be[1]) ram2[ram_a] <= cpu_dout[15:8];
            if (be[0]) ram3[ram_a] <= cpu_dout[7:0];
        end
        ram_q <= {ram0[ram_a], ram1[ram_a], ram2[ram_a], ram3[ram_a]};
    end

    // -------------------------------------------------------------------------
    // Vesta UART (minimal: TX + status). Reuse e6502 uart_tx.
    // -------------------------------------------------------------------------
    reg        uart_start;
    reg  [7:0] uart_data;
    wire       uart_busy;
    uart_tx #(.CLK_HZ(3125000), .BAUD(115200)) uart_i (
        .clk(clk), .rst(rst), .data(uart_data), .start(uart_start),
        .tx(ftdi_rxd), .busy(uart_busy)
    );
    // UART RX (host -> FPGA). rx_ready set on byte arrival, cleared on data read.
    wire [7:0] rx_byte; wire rx_valid;
    uart_rx #(.CLK_HZ(3125000), .BAUD(115200)) urx (.clk(clk), .rst(rst), .rx(ftdi_txd),
        .data(rx_byte), .valid(rx_valid));
    reg [7:0] rx_data; reg rx_ready;
    // Read-commit strobe, not bus_write_stb: cpu_data_en is WRITE_ACCESS-only (see
    // wf68k30L_bus_interface.vhd DATA_PORT_EN), so it (and bus_write_stb, which is
    // only ever set alongside it) is always 0 during a read — bus_read_stb below is
    // its read-side twin, set in the bus FSM when cpu_rw_n=1 (verified: 1 = read).
    wire rx_data_rd = sel_uart & (cpu_adr[3:0]==4'hC) & bus_read_stb;
    always @(posedge clk) begin
        if (rst) rx_ready <= 1'b0;
        else begin
            if (rx_data_rd) rx_ready <= 1'b0;         // read of 0x...C consumes the byte
            if (rx_valid) begin rx_data <= rx_byte; rx_ready <= 1'b1; end
        end
    end
    // MMIO regs are accessed as 32-bit longs (volatile uint32_t), value natural
    // in [31:0]. UART_DATA @ 0xFFF00500 (write, char in [7:0]);
    // UART_STATUS @ 0xFFF00504 (read, [0]=TX_READY, [1]=BUSY).
    // UART_RXSTATUS @ 0xFFF00508 (read, [0]=RX_READY); UART_RXDATA @ 0xFFF0050C
    // (read, byte in [7:0]; reading clears RX_READY).
    wire uart_data_wr = sel_uart & (cpu_adr[3:0]==4'h0) & cpu_data_en & ~cpu_rw_n & bus_write_stb;
    wire [31:0] uart_rdata =
        (cpu_adr[3:0]==4'h4) ? {30'd0, uart_busy, ~uart_busy} :   // TX status (existing)
        (cpu_adr[3:0]==4'h8) ? {31'd0, rx_ready}            :     // RX status
        (cpu_adr[3:0]==4'hC) ? {24'd0, rx_data}             :     // RX data
        32'd0;

    // -------------------------------------------------------------------------
    // Bus interface FSM (async 68030 slave, registered, a few wait states)
    // -------------------------------------------------------------------------
    localparam BS_IDLE=2'd0, BS_WAIT=2'd1, BS_ACK=2'd2, BS_END=2'd3;
    reg [1:0] bs;
    reg [1:0] waitc;
    reg       bus_write_stb;   // 1-cycle write pulse to memory/uart
    reg       bus_read_stb;    // 1-cycle read-commit pulse (for read-clears-flag regs)

    always @(posedge clk) begin
        uart_start    <= 1'b0;
        bus_write_stb <= 1'b0;
        bus_read_stb  <= 1'b0;
        if (rst) begin
            bs <= BS_IDLE; dsack_n <= 2'b11;
        end else case (bs)
            BS_IDLE: begin
                dsack_n <= 2'b11;
                if (!cpu_as_n) begin waitc <= 2'd2; bs <= BS_WAIT; end
            end
            BS_WAIT: begin                        // let BRAM read settle / hold addr
                if (waitc != 0) waitc <= waitc - 1'b1;
                else begin
                    if (!cpu_rw_n && cpu_data_en) begin
                        bus_write_stb <= 1'b1;             // commit write this cycle
                        if (sel_uart && cpu_adr[3:0]==4'h0) begin
                            uart_data <= cpu_dout[7:0]; uart_start <= 1'b1;
                        end
                    end else if (cpu_rw_n) begin
                        bus_read_stb <= 1'b1;              // commit read this cycle
                    end
                    bs <= BS_ACK;
                end
            end
            BS_ACK: begin
                // present read data
                cpu_din <= sel_rom  ? rom_q :
                           sel_ram  ? ram_q :
                           sel_uart ? uart_rdata : 32'd0;
                dsack_n <= 2'b00;                 // 32-bit port ack
                bs <= BS_END;
            end
            BS_END: begin
                dsack_n <= 2'b00;
                if (cpu_as_n) begin dsack_n <= 2'b11; bs <= BS_IDLE; end
            end
            default: bs <= BS_IDLE;
        endcase
    end

    // -------------------------------------------------------------------------
    // LEDs: liveness (heartbeat) + a few CPU bus signals for at-a-glance debug
    // -------------------------------------------------------------------------
    reg [23:0] hb;
    always @(posedge clk) hb <= hb + 1'b1;
    assign leds = {hb[23], ~cpu_as_n, ~cpu_rw_n, uart_busy, cpu_adr[3:0]};

endmodule
`default_nettype wire
