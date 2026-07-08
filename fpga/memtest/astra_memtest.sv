// =============================================================================
// Astra 68 — SDRAM memtest / bring-up
//
// First real hardware exercise of the ULX3S SDRAM (MT48LC16M16, 32 MB, 16-bit).
// In the e6502 project the SDRAM pads were declared but `sdram_clk` was tied
// low, so the chip was never actually clocked on hardware. This bitstream:
//
//   1. Brings up the SDRAM via the proven MiST sdram.v controller.
//   2. Data-bus walking test at address 0 (each DQ line 0 and 1).
//   3. Full-chip march: write a position-dependent pattern across all 32 MB,
//      then read it all back and compare. Zero errors proves 32 MB of
//      distinct, retentive cells — which also rules out address aliasing
//      (a 16 MB part would collide addr and addr+16M and fail the low half).
//   4. Reports over UART (115200 8N1, FTDI serial) and the 8 LEDs.
//
// Reuses e6502 RTL unchanged: sdram.v, ecp5pll.sv, uart_tx.sv.
//
// LEDs: during march = live address[24:17] (visible sweep). Final: PASS = all
// 8 on steady; FAIL = 0xAA blink. Press the power button to re-run.
//
// Clocking: 100 MHz controller clock + 6.25 MHz clkref (16:1), copied verbatim
// from the e6502 pll_sdram_inst known to synthesize on this board. The SDRAM
// chip clock pin is the 100 MHz clock inverted (180 deg skew) — the standard
// chip-clock relationship. If every read fails, that phase is tuning knob #1.
// =============================================================================

`default_nettype none

module astra_memtest (
    input  wire        clk25_mhz,
    input  wire        reset_n,     // btn[0] / BTN_PWRn, active low — manual re-run
    output wire [7:0]  leds,
    output wire        ftdi_rxd,    // FPGA TX -> host (serial out)

    // SDRAM — MT48LC16M16
    output wire        sdram_clk,
    output wire        sdram_cke,
    output wire        sdram_csn,
    output wire        sdram_wen,
    output wire        sdram_rasn,
    output wire        sdram_casn,
    output wire [1:0]  sdram_ba,
    output wire [1:0]  sdram_dqm,
    output wire [12:0] sdram_a,
    inout  wire [15:0] sdram_d
);
    localparam [24:0] MEM_TOP = 25'h1FFFFFF;   // 32 MB - 1 (top byte address)

    // -------------------------------------------------------------------------
    // Clocks
    // -------------------------------------------------------------------------
    wire [3:0] pll_o;
    wire       pll_locked;

    // 75 MHz SDRAM clock — generous timing margin for a clean bring-up. The
    // un-floorplanned MiST command path closes ~70-90 MHz; a slower chip clock
    // also maximizes the SDRAM's own setup/hold margins. The real machine will
    // floorplan + phase-tune for a higher clock later.
    ecp5pll #(
        .in_hz  (25000000),
        .out0_hz(75000000),    // SDRAM controller clock
        .out1_hz(0),
        .out2_hz(0),
        .out3_hz(0)
    ) pll_i (
        .clk_i       (clk25_mhz),
        .clk_o       (pll_o),
        .locked      (pll_locked),
        .reset       (1'b0),
        .standby     (1'b0),
        .phasesel    (2'b0),
        .phasedir    (1'b0),
        .phasestep   (1'b0),
        .phaseloadreg(1'b0)
    );

    wire clk = pll_o[0];       // 75 MHz

    // clkref = clk/16 in fabric. sdram.v samples clkref in the clk domain (it is
    // a slow strobe, not a separate clock domain), so a divider is fine — and it
    // frees the PLL from synthesizing a very low frequency at a 16:1 ratio.
    reg [3:0] refdiv = 4'd0;
    always @(posedge clk) refdiv <= refdiv + 4'd1;
    wire clkref = refdiv[3];   // clk/16, 50% duty

    assign sdram_clk = ~clk;   // chip clock: 180 deg skew (tuning knob)
    assign sdram_cke = 1'b1;  // MiST sdram.v assumes CKE always high

    // -------------------------------------------------------------------------
    // Reset / power-up. Hold until PLL locks, then a long window during which
    // `init` runs the controller's internal SDRAM init (precharge/refresh/mode).
    // -------------------------------------------------------------------------
    reg [1:0] lock_sync = 2'b00;
    reg [1:0] rstn_sync = 2'b11;
    always @(posedge clk) begin
        lock_sync <= {lock_sync[0], pll_locked};
        rstn_sync <= {rstn_sync[0], reset_n};
    end
    wire locked = lock_sync[1];
    wire manual = ~rstn_sync[1];

    reg [19:0] boot_cnt = 20'd0;    // ~10 ms @100 MHz, well past SDRAM 100 us
    reg        booted   = 1'b0;
    always @(posedge clk) begin
        if (!locked || manual) begin
            boot_cnt <= 20'd0;
            booted   <= 1'b0;
        end else if (!booted) begin
            if (boot_cnt == 20'hFFFFF) booted <= 1'b1;
            else                       boot_cnt <= boot_cnt + 20'd1;
        end
    end
    wire sd_init = (boot_cnt < 20'h80000);   // init high for first half of window
    wire rst     = !booted;

    // -------------------------------------------------------------------------
    // SDRAM controller (port A only; port B + stream port tied off)
    // -------------------------------------------------------------------------
    wire [15:0] sd_data_out;
    wire        sd_we_out;
    wire [12:0] sd_addr;
    wire [1:0]  sd_dqm, sd_ba;
    wire        sd_cs, sd_we, sd_ras, sd_cas;

    assign sdram_a    = sd_addr;
    assign sdram_dqm  = sd_dqm;
    assign sdram_ba   = sd_ba;
    assign sdram_csn  = sd_cs;
    assign sdram_wen  = sd_we;
    assign sdram_rasn = sd_ras;
    assign sdram_casn = sd_cas;
    assign sdram_d    = sd_we_out ? sd_data_out : 16'hzzzz;

    reg  [24:0] a_addr;
    reg  [7:0]  a_din;
    reg         a_we, a_oe;
    wire [7:0]  a_dout;
    wire        a_done;

    sdram sdram_i (
        .sd_data_in (sdram_d),
        .sd_data_out(sd_data_out),
        .sd_addr    (sd_addr),
        .sd_dqm     (sd_dqm),
        .sd_ba      (sd_ba),
        .sd_cs      (sd_cs),
        .sd_we      (sd_we),
        .sd_ras     (sd_ras),
        .sd_cas     (sd_cas),
        .init       (sd_init),
        .clk        (clk),
        .clkref     (clkref),
        .we_out     (sd_we_out),
        .addrA      (a_addr), .weA(a_we), .dinA(a_din), .oeA(a_oe),
        .doutA      (a_dout), .doneA(a_done),
        .addrB      (25'd0), .weB(1'b0), .dinB(8'd0), .oeB(1'b0),
        .doutB      (), .doneB(),
        .stream_req (1'b0), .stream_addr(25'd0), .stream_words(14'd0),
        .stream_ready(1'b1),
        .stream_dout(), .stream_valid(), .stream_busy(), .stream_done()
    );

    // -------------------------------------------------------------------------
    // Single-access engine. Contract (from e6502 xram_sdram.sv): hold we/oe
    // high until doneA, then deassert; read data valid the cycle AFTER doneA.
    //   request : set acc_addr/acc_wdata/acc_is_wr, pulse acc_go (1 clk)
    //   respond : acc_done pulses 1 clk; acc_rdata holds the read byte
    // -------------------------------------------------------------------------
    localparam [1:0] AE_IDLE = 2'd0, AE_ACTIVE = 2'd1, AE_CAP = 2'd2;
    reg [1:0]  ae_state;
    reg        ae_is_wr;
    reg        acc_go, acc_is_wr;
    reg [24:0] acc_addr;
    reg [7:0]  acc_wdata;
    reg [7:0]  acc_rdata;
    reg        acc_done;

    always @(posedge clk) begin
        acc_done <= 1'b0;
        if (rst) begin
            ae_state <= AE_IDLE; a_we <= 1'b0; a_oe <= 1'b0;
        end else case (ae_state)
            AE_IDLE: begin
                a_we <= 1'b0; a_oe <= 1'b0;
                if (acc_go) begin
                    a_addr   <= acc_addr;
                    a_din    <= acc_wdata;
                    a_we     <= acc_is_wr;
                    a_oe     <= ~acc_is_wr;
                    ae_is_wr <= acc_is_wr;
                    ae_state <= AE_ACTIVE;
                end
            end
            AE_ACTIVE: if (a_done) begin
                a_we <= 1'b0; a_oe <= 1'b0;
                if (ae_is_wr) begin acc_done <= 1'b1; ae_state <= AE_IDLE; end
                else                ae_state <= AE_CAP;
            end
            AE_CAP: begin
                acc_rdata <= a_dout;
                acc_done  <= 1'b1;
                ae_state  <= AE_IDLE;
            end
            default: ae_state <= AE_IDLE;
        endcase
    end

    // Position-dependent byte pattern for a given address.
    function [7:0] pat(input [24:0] addr);
        pat = addr[7:0] ^ addr[15:8] ^ addr[23:16] ^ {7'd0, addr[24]} ^ 8'hA5;
    endfunction

    // -------------------------------------------------------------------------
    // UART transmit: uart_tx + a one-byte send wrapper (tx_go pulse / tx_ready)
    // -------------------------------------------------------------------------
    reg  [7:0] tx_data;
    reg        tx_start;
    wire       tx_busy;
    uart_tx #(.CLK_HZ(75000000), .BAUD(115200)) uart_i (
        .clk(clk), .rst(rst), .data(tx_data), .start(tx_start),
        .tx(ftdi_rxd), .busy(tx_busy)
    );

    localparam [1:0] TW_IDLE=2'd0, TW_START=2'd1, TW_HI=2'd2, TW_LO=2'd3;
    reg [1:0] tw_state;
    reg       tx_go;
    // Gate on tx_go: tw_state lags one cycle behind the tx_go pulse, so without
    // the !tx_go term tx_ready reads high for the cycle after a byte is issued
    // and the print FSM would fire a second byte, dropping the first.
    wire      tx_ready = (tw_state == TW_IDLE) && !tx_go;
    always @(posedge clk) begin
        tx_start <= 1'b0;
        if (rst) tw_state <= TW_IDLE;
        else case (tw_state)
            TW_IDLE:  if (tx_go)   tw_state <= TW_START;
            TW_START: begin tx_start <= 1'b1; tw_state <= TW_HI; end
            TW_HI:    if (tx_busy)  tw_state <= TW_LO;
            TW_LO:    if (!tx_busy) tw_state <= TW_IDLE;
            default:  tw_state <= TW_IDLE;
        endcase
    end

    // ASCII hex digit for a nibble.
    function [7:0] asc_hex; input [3:0] n;
        asc_hex = (n < 4'd10) ? (8'h30 + {4'd0, n}) : (8'h37 + {4'd0, n});
    endfunction

    // Report message ROM. Each message is a 0-terminated byte string.
    localparam [3:0]
        MSG_BANNER = 4'd0,   // "ASTRA68 SDRAM MEMTEST\r\n"
        MSG_DBUS   = 4'd1,   // "DBUS="
        MSG_OK     = 4'd2,   // "OK"
        MSG_BAD    = 4'd3,   // "FAIL"
        MSG_ERRS   = 4'd4,   // "\r\nERRS="
        MSG_FIRST  = 4'd5,   // " FIRST="
        MSG_NL     = 4'd6,   // "\r\n"
        MSG_PASS   = 4'd7,   // "RESULT: PASS 32MB\r\n"
        MSG_FAILR  = 4'd8;   // "RESULT: MEMTEST FAIL\r\n"

    function [7:0] rep_char; input [3:0] sel; input [4:0] i;
        begin
            rep_char = 8'h00;
            case (sel)
            MSG_BANNER: case (i)
                0:rep_char="A";1:rep_char="S";2:rep_char="T";3:rep_char="R";4:rep_char="A";
                5:rep_char="6";6:rep_char="8";7:rep_char=" ";8:rep_char="S";9:rep_char="D";
                10:rep_char="R";11:rep_char="A";12:rep_char="M";13:rep_char=" ";14:rep_char="M";
                15:rep_char="E";16:rep_char="M";17:rep_char="T";18:rep_char="E";19:rep_char="S";
                20:rep_char="T";21:rep_char=8'h0D;22:rep_char=8'h0A;default:rep_char=8'h00;
            endcase
            MSG_DBUS: case (i)
                0:rep_char="D";1:rep_char="B";2:rep_char="U";3:rep_char="S";4:rep_char="=";
                default:rep_char=8'h00; endcase
            MSG_OK: case (i) 0:rep_char="O";1:rep_char="K";default:rep_char=8'h00; endcase
            MSG_BAD: case (i)
                0:rep_char="F";1:rep_char="A";2:rep_char="I";3:rep_char="L";
                default:rep_char=8'h00; endcase
            MSG_ERRS: case (i)
                0:rep_char=8'h0D;1:rep_char=8'h0A;2:rep_char="E";3:rep_char="R";4:rep_char="R";
                5:rep_char="S";6:rep_char="=";default:rep_char=8'h00; endcase
            MSG_FIRST: case (i)
                0:rep_char=" ";1:rep_char="F";2:rep_char="I";3:rep_char="R";4:rep_char="S";
                5:rep_char="T";6:rep_char="=";default:rep_char=8'h00; endcase
            MSG_NL: case (i) 0:rep_char=8'h0D;1:rep_char=8'h0A;default:rep_char=8'h00; endcase
            MSG_PASS: case (i)
                0:rep_char="R";1:rep_char="E";2:rep_char="S";3:rep_char="U";4:rep_char="L";
                5:rep_char="T";6:rep_char=":";7:rep_char=" ";8:rep_char="P";9:rep_char="A";
                10:rep_char="S";11:rep_char="S";12:rep_char=" ";13:rep_char="3";14:rep_char="2";
                15:rep_char="M";16:rep_char="B";17:rep_char=8'h0D;18:rep_char=8'h0A;
                default:rep_char=8'h00; endcase
            MSG_FAILR: case (i)
                0:rep_char="R";1:rep_char="E";2:rep_char="S";3:rep_char="U";4:rep_char="L";
                5:rep_char="T";6:rep_char=":";7:rep_char=" ";8:rep_char="M";9:rep_char="E";
                10:rep_char="M";11:rep_char="T";12:rep_char="E";13:rep_char="S";14:rep_char="T";
                15:rep_char=" ";16:rep_char="F";17:rep_char="A";18:rep_char="I";19:rep_char="L";
                20:rep_char=8'h0D;21:rep_char=8'h0A;default:rep_char=8'h00; endcase
            default: rep_char = 8'h00;
            endcase
        end
    endfunction

    // -------------------------------------------------------------------------
    // Main sequencer
    // -------------------------------------------------------------------------
    localparam [5:0]
        M_WAIT=6'd0, M_BANNER=6'd1,
        M_DBUS_W=6'd2, M_DBUS_WW=6'd3, M_DBUS_R=6'd4, M_DBUS_RW=6'd5,
        M_MW=6'd6, M_MWW=6'd7, M_MR=6'd8, M_MRW=6'd9,
        M_R0=6'd10, M_R1=6'd11, M_R2=6'd12, M_R3=6'd13, M_R4=6'd14,
        M_R5=6'd15, M_R6=6'd16, M_R7=6'd17, M_R8=6'd18, M_DONE=6'd19,
        PR_STR=6'd20, PR_HEX=6'd21, PR_ONE=6'd22, M_HOLD=6'd23;

    reg [5:0]  state, pr_ret;
    reg [3:0]  str_sel;
    reg [4:0]  str_i;
    reg [31:0] hex_val;
    reg [3:0]  hex_n, hex_i;
    reg [24:0] march_addr;
    reg [4:0]  dbus_i;
    reg [31:0] err_cnt;
    reg [24:0] first_fail;
    reg        dbus_ok;
    reg [7:0]  led_r;
    reg [24:0] heartbeat;
    reg [7:0]  oc;              // one-char progress print
    reg [25:0] dly;            // report re-print delay (~1.1 s @60MHz)

    assign leds = led_r;

    function [7:0] dbus_pat; input [4:0] i;
        case (i)
            0:dbus_pat=8'h00;1:dbus_pat=8'hFF;2:dbus_pat=8'hAA;3:dbus_pat=8'h55;
            4:dbus_pat=8'h01;5:dbus_pat=8'h02;6:dbus_pat=8'h04;7:dbus_pat=8'h08;
            8:dbus_pat=8'h10;9:dbus_pat=8'h20;10:dbus_pat=8'h40;11:dbus_pat=8'h80;
            default:dbus_pat=8'h00;
        endcase
    endfunction
    localparam [4:0] DBUS_N = 5'd12;

    wire [3:0] cur_nib = (hex_val >> ({1'b0,(hex_n - 4'd1 - hex_i)} << 2)) & 32'hF;

    always @(posedge clk) begin
        acc_go <= 1'b0;
        tx_go  <= 1'b0;
        heartbeat <= heartbeat + 25'd1;

        if (rst) begin
            state <= M_WAIT; str_i <= 5'd0; march_addr <= 25'd0;
            dbus_i <= 5'd0; err_cnt <= 32'd0; first_fail <= 25'd0;
            dbus_ok <= 1'b1; led_r <= 8'h01;
        end else case (state)

        // ---- wait for boot ----
        M_WAIT: begin
            led_r <= heartbeat[23] ? 8'h81 : 8'h01;   // slow blink = alive
            if (booted) begin str_sel<=MSG_BANNER; str_i<=5'd0; pr_ret<=M_DBUS_W; state<=PR_STR; end
        end

        // ---- data-bus walking test @ addr 0 ----
        M_DBUS_W: begin
            led_r <= 8'h03;
            acc_addr <= 25'd0; acc_wdata <= dbus_pat(dbus_i); acc_is_wr <= 1'b1;
            acc_go <= 1'b1; state <= M_DBUS_WW;
        end
        M_DBUS_WW: if (acc_done) begin
            acc_addr <= 25'd0; acc_is_wr <= 1'b0; acc_go <= 1'b1; state <= M_DBUS_R;
        end
        M_DBUS_R: state <= M_DBUS_RW;    // let acc_go pulse land
        M_DBUS_RW: if (acc_done) begin
            if (acc_rdata != dbus_pat(dbus_i)) dbus_ok <= 1'b0;
            if (dbus_i == DBUS_N - 5'd1) begin march_addr <= 25'd0; state <= M_MW; end
            else begin dbus_i <= dbus_i + 5'd1; state <= M_DBUS_W; end
        end

        // ---- march: write pass ----
        M_MW: begin
            acc_addr <= march_addr; acc_wdata <= pat(march_addr); acc_is_wr <= 1'b1;
            acc_go <= 1'b1; led_r <= march_addr[24:17]; state <= M_MWW;
        end
        M_MWW: if (acc_done) begin
            if (march_addr == MEM_TOP) begin march_addr <= 25'd0; state <= M_MR; end
            else if (march_addr[20:0] == 21'h1FFFFF) begin       // every 2 MB
                oc <= "w"; march_addr <= march_addr + 25'd1; pr_ret <= M_MW; state <= PR_ONE;
            end else begin march_addr <= march_addr + 25'd1; state <= M_MW; end
        end

        // ---- march: read + compare pass ----
        M_MR: begin
            acc_addr <= march_addr; acc_is_wr <= 1'b0;
            acc_go <= 1'b1; led_r <= march_addr[24:17]; state <= M_MRW;
        end
        M_MRW: if (acc_done) begin
            if (acc_rdata != pat(march_addr)) begin
                if (err_cnt == 32'd0) first_fail <= march_addr;
                err_cnt <= err_cnt + 32'd1;
            end
            if (march_addr == MEM_TOP) begin str_sel<=MSG_NL; str_i<=5'd0; pr_ret<=M_R1; state<=PR_STR; end
            else if (march_addr[20:0] == 21'h1FFFFF) begin       // every 2 MB
                oc <= "r"; march_addr <= march_addr + 25'd1; pr_ret <= M_MR; state <= PR_ONE;
            end else begin march_addr <= march_addr + 25'd1; state <= M_MR; end
        end

        // ---- report ----
        M_R1: begin str_sel<=MSG_DBUS;  str_i<=5'd0; pr_ret<=M_R2; state<=PR_STR; end
        M_R2: begin str_sel<=dbus_ok?MSG_OK:MSG_BAD; str_i<=5'd0; pr_ret<=M_R3; state<=PR_STR; end
        M_R3: begin str_sel<=MSG_ERRS;  str_i<=5'd0; pr_ret<=M_R4; state<=PR_STR; end
        M_R4: begin hex_val<=err_cnt; hex_n<=4'd8; hex_i<=5'd0; pr_ret<=M_R5; state<=PR_HEX; end
        M_R5: begin str_sel<=MSG_FIRST; str_i<=5'd0; pr_ret<=M_R6; state<=PR_STR; end
        M_R6: begin hex_val<={7'd0,first_fail}; hex_n<=4'd7; hex_i<=5'd0; pr_ret<=M_R7; state<=PR_HEX; end
        M_R7: begin str_sel<=MSG_NL;    str_i<=5'd0; pr_ret<=M_R8; state<=PR_STR; end
        M_R8: begin str_sel<=(err_cnt==32'd0 && dbus_ok)?MSG_PASS:MSG_FAILR; str_i<=5'd0; pr_ret<=M_DONE; state<=PR_STR; end

        M_DONE: begin
            led_r <= (err_cnt==32'd0 && dbus_ok) ? 8'hFF : (heartbeat[23]?8'hAA:8'h00);
            dly <= 26'd0; state <= M_HOLD;
        end
        // Re-print the report ~once/sec so it is always catchable on the wire.
        M_HOLD: begin
            led_r <= (err_cnt==32'd0 && dbus_ok) ? 8'hFF : (heartbeat[23]?8'hAA:8'h00);
            if (dly == 26'h3FFFFFF) begin str_sel<=MSG_NL; str_i<=5'd0; pr_ret<=M_R1; state<=PR_STR; end
            else dly <= dly + 26'd1;
        end

        // ---- print helpers ----
        PR_STR: if (rep_char(str_sel,str_i)==8'h00) state <= pr_ret;
                else if (tx_ready) begin
                    tx_data <= rep_char(str_sel,str_i); tx_go <= 1'b1; str_i <= str_i + 5'd1;
                end
        PR_HEX: if (hex_i == hex_n) state <= pr_ret;
                else if (tx_ready) begin
                    tx_data <= asc_hex(cur_nib); tx_go <= 1'b1; hex_i <= hex_i + 4'd1;
                end
        PR_ONE: if (tx_ready) begin tx_data <= oc; tx_go <= 1'b1; state <= pr_ret; end

        default: state <= M_DONE;
        endcase
    end

endmodule
`default_nettype wire
