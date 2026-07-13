//
// sdram.v
//
// sdram controller implementation for the MiST board adaptation
// of Luddes NES core
// http://code.google.com/p/mist-board/
// 
// Copyright (c) 2013 Till Harbaum <till@harbaum.org> 
// 
// This source file is free software: you can redistribute it and/or modify 
// it under the terms of the GNU General Public License as published 
// by the Free Software Foundation, either version 3 of the License, or 
// (at your option) any later version. 
// 
// This source file is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of 
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License 
// along with this program.  If not, see <http://www.gnu.org/licenses/>. 
//

module sdram
(
	// interface to the MT48LC16M16 chip
        input     [15:0] sd_data_in,
        output    [15:0] sd_data_out,
	output    [12:0] sd_addr,    // 13 bit multiplexed address bus
	output     [1:0] sd_dqm,     // two byte masks
	output     [1:0] sd_ba,      // two banks
	output           sd_cs,      // a single chip select
	output           sd_we,      // write enable
	output           sd_ras,     // row address select
	output           sd_cas,     // columns address select

	// cpu/chipset interface
	input            init,       // init signal after FPGA config to initialize RAM
	input            clk,        // sdram is accessed at up to 128MHz
	input            clkref,     // reference clock to sync to
	output           we_out, // Tristate control signal
	
	input     [24:0] addrA,      // 25 bit byte address
	input            weA,        // cpu/chipset requests write
	input      [7:0] dinA,       // data input from chipset/cpu
	input            oeA,        // cpu requests data
	output reg [7:0] doutA,      // data output to cpu
	output reg       doneA,      // one clk pulse when port A transfer completed

	input     [24:0] addrB,      // 25 bit byte address
	input            weB,        // cpu/chipset requests write
	input      [7:0] dinB,       // data input from chipset/cpu
	input            oeB,        // ppu requests data
	output reg [7:0] doutB,      // data output to ppu
	output reg       doneB,      // one clk pulse when port B transfer completed

	// Page-mode burst read port (sdram_clk domain). Mutually exclusive with
	// ports A/B via the `streaming` flag.
	input            stream_req,    // pulse to start a burst
	input     [24:0] stream_addr,   // start BYTE address
	input     [13:0] stream_words,  // # 16-bit words to read (max 16384 = 16K page)
	input            stream_ready,  // consumer back-pressure: hold READ issuance when low
	output reg[15:0] stream_dout,
	output reg       stream_valid,  // 1-clk strobe per word
	output reg       stream_busy,
	output reg       stream_done    // 1-clk pulse at completion
);

// no burst configured
localparam RASCAS_DELAY   = 3'd2;   // tRCD=20ns -> 2 cycles@85MHz
localparam BURST_LENGTH   = 3'b000; // 000=1, 001=2, 010=4, 011=8
localparam ACCESS_TYPE    = 1'b0;   // 0=sequential, 1=interleaved
localparam CAS_LATENCY    = 3'd3;   // 2/3 allowed
localparam OP_MODE        = 2'b00;  // only 00 (standard operation) allowed
localparam NO_WRITE_BURST = 1'b1;   // 0= write burst enabled, 1=only single access write

localparam MODE = { 3'b000, NO_WRITE_BURST, OP_MODE, CAS_LATENCY, ACCESS_TYPE, BURST_LENGTH};

// ---------------------------------------------------------------------
// ----------------- page-mode streaming read FSM ----------------------
// ---------------------------------------------------------------------
//
// Burst-reads `stream_words` 16-bit words starting at byte address
// `stream_addr`, replicating the single-access run_addr decode exactly:
//   row  = a[21:9]   col = {a[24],a[8:1]}   bank = a[23:22]
// For the 512KB region this exposes only 512 bytes (256 words) per row, so
// the FSM re-opens a row every 256 words. Reads use A10=0 (page mode, row
// stays open); the row is closed by an explicit PRECHARGE and an
// unconditional AUTO_REFRESH is issued once per row boundary.
//
// While `streaming` is high the controller's normal run_cmd path is forced
// to CMD_INHIBIT and sd_cmd/sd_addr/sd_ba/sd_dqm are driven from this FSM's
// registered command/address (see the muxes near sd_cmd/sd_addr below). The
// free-running `q` counter keeps spinning; only the *outputs* are overridden.
//
// All writes to streaming/stream_* live in THIS single clocked block.

// command-constant aliases (the CMD_* localparams are declared further down;
// localparams are module-scoped so referencing them here is legal)
localparam [3:0] STR_CMD_INHIBIT      = 4'b1111;
localparam [3:0] STR_CMD_NOP          = 4'b0111;
localparam [3:0] STR_CMD_ACTIVE       = 4'b0011;
localparam [3:0] STR_CMD_READ         = 4'b0101;
localparam [3:0] STR_CMD_PRECHARGE    = 4'b0010;
localparam [3:0] STR_CMD_AUTO_REFRESH = 4'b0001;

// timing parameters in clk cycles (dedicated to the stream FSM; NOT `q`)
localparam [3:0] STR_TRCD = 4'd2;   // ACTIVE -> READ
localparam [3:0] STR_TRP  = 4'd2;   // PRECHARGE -> ACTIVE
localparam [3:0] STR_TRC  = 4'd7;   // ACTIVE -> ACTIVE (full row cycle)
localparam [3:0] STR_TRAS = 4'd5;   // ACTIVE -> PRECHARGE (row-active minimum)
localparam [3:0] STR_CAS  = 4'd3;   // READ -> data on bus
localparam [3:0] STR_TRFC = 4'd9;   // AUTO_REFRESH -> next ACTIVE (refresh cycle,
                                    // ~66ns@100MHz=7cy; 9 gives HW margin). MUST be
                                    // honoured after AUTO_REFRESH or the just-refreshed
                                    // rows read back garbage (HW: rows 1..31 = 0xAA).

// FSM states
localparam [3:0] S_IDLE      = 4'd0;
localparam [3:0] S_PRE_ALL   = 4'd1;   // PRECHARGE all banks (clean entry)
localparam [3:0] S_ACTIVATE  = 4'd2;   // ACTIVE(bank,row)
localparam [3:0] S_READ      = 4'd3;   // issue READ(col,A10=0), 1/clk
localparam [3:0] S_DRAIN     = 4'd4;   // wait for CAS pipe to flush
localparam [3:0] S_PRECHARGE = 4'd5;   // PRECHARGE(bank) at row end
localparam [3:0] S_REFRESH   = 4'd6;   // AUTO_REFRESH (1/row, unconditional)
localparam [3:0] S_DONE      = 4'd7;   // 1-clk done pulse
localparam [3:0] S_ENTER     = 4'd8;   // tRFC NOP wait before the first command

reg        streaming;
reg [3:0]  str_state;
reg [3:0]  str_dly;         // generic inter-command delay countdown
reg [3:0]  str_rc;          // tRC countdown from ACTIVE (gates next ACTIVE)
reg [3:0]  str_ras;         // tRAS countdown from ACTIVE (gates PRECHARGE)
reg [24:0] cur_addr;        // running byte address
reg [13:0] words_left;      // words still to read across the whole burst (max 16384)
reg [8:0]  row_words;       // words already issued in the current row (<=256)
reg [1:0]  open_bank;       // bank of the row currently activated (for PRECHARGE)

// registered command/address presented to the chip while streaming
reg [3:0]  str_cmd;
reg [12:0] str_addr;
reg [1:0]  str_ba;
reg [1:0]  str_dqm;

// CAS read pipeline tracking. We register the command, so the chip decodes our
// READ on the edge AFTER the FSM drives STR_CMD_READ (E+1). The model presents
// data on sd_data_in CAS(=3) cycles after it decodes the READ (so it has loaded
// sd_data_in by E+1+3 = E+4) and sd_data_in is then stably sampleable on the
// FOLLOWING edge E+5. Net: a READ driven on FSM edge E is captured on edge E+5.
// We mark rd_inflight[0] on edge E and shift up one stage per clk, firing the
// capture when the marker reaches rd_inflight[4] (5 edges after the READ).
// NOTE: page_dma.READY_MARGIN must be >= this depth + 1; if you widen
// rd_inflight, bump it (else the skid FIFO can overflow on stream_ready deassert).
reg [4:0]  rd_inflight;     // 5-deep in-flight READ tracker (E .. E+5 capture)
// Read pacing: issue a READ then a NOP gap (1 word / 2 clk). Back-to-back 1/clk
// reads are mis-captured on real silicon (the DQ data eye drifts cycle-to-cycle;
// the fixed-offset capture grabs an adjacent word). Isolating each read — exactly
// what the proven single-access path does — fixes it. page_dma drains at 2 clk/word
// so this costs ~no throughput. 0 = issue this clk, 1 = gap (NOP) this clk.
reg        str_pace;

// row-boundary helpers, derived from the CURRENT cur_addr
wire [8:0] cur_col  = {cur_addr[24], cur_addr[8:1]};  // 9-bit column
wire [1:0] cur_bank = cur_addr[23:22];
wire [12:0] cur_row = cur_addr[21:9];
// Close the row on the PHYSICAL 512-byte (256-column) boundary, i.e. when the
// word being issued sits in the last column of the row (cur_addr[8:1]==8'hFF).
// Keying on the read COUNT (row_words==255) only coincides with the physical
// boundary when the burst starts row-aligned; an unaligned start would keep
// the now-wrong row open and read another row's data. The next ACTIVATE then
// uses the already-advanced cur_addr (its new row[21:9]).
wire        row_full = (cur_addr[8:1] == 8'hFF);        // last column of phys row
wire        last_word = (words_left == 14'd1);          // issuing the final word

always @(posedge clk) begin
	stream_valid <= 1'b0;
	stream_done  <= 1'b0;

	// advance the in-flight shift register every clk
	rd_inflight <= {rd_inflight[3:0], 1'b0};

	// emit a word when its READ reaches the end of the capture pipeline
	// (5 edges after the READ was driven; sd_data_in then holds the word).
	if (rd_inflight[4]) begin
		stream_dout  <= sd_data_in;
		stream_valid <= 1'b1;
	end

	if (init) begin
		streaming   <= 1'b0;
		str_state   <= S_IDLE;
		stream_busy <= 1'b0;
		str_cmd     <= STR_CMD_INHIBIT;
		str_addr    <= 13'd0;
		str_ba      <= 2'd0;
		str_dqm     <= 2'b00;
		str_dly     <= 4'd0;
		str_rc      <= 4'd0;
		cur_addr    <= 25'd0;
		words_left  <= 14'd0;
		row_words   <= 9'd0;
		open_bank   <= 2'd0;
		rd_inflight <= 5'd0;
		str_ras     <= 4'd0;
		str_pace    <= 1'b0;
		stream_dout <= 16'd0;   // determinism: explicit reg init
	end else begin
		// tRC / tRAS countdowns run continuously once armed at ACTIVE.
		if (str_rc  != 0) str_rc  <= str_rc  - 4'd1;
		if (str_ras != 0) str_ras <= str_ras - 4'd1;

		case (str_state)

		S_IDLE: begin
			str_cmd <= STR_CMD_INHIBIT;
			// latch a request only when the controller is out of reset
			if (stream_req && (reset == 0) && (stream_words != 0)) begin
				streaming   <= 1'b1;
				stream_busy <= 1'b1;
				cur_addr    <= stream_addr;
				words_left  <= stream_words;
				row_words   <= 9'd0;
				rd_inflight <= 5'd0;
				str_state   <= S_ENTER;
				str_dly     <= STR_TRFC;
			end
		end

		// Wait tRFC before issuing ANY command. The single-access path issues
		// opportunistic AUTO_REFRESHes whenever no port request is pending, and one
		// may have landed the cycle before stream_req. The chip is busy for tRFC
		// after a refresh; a PRECHARGE/ACTIVE inside that window corrupts the
		// just-refreshed rows (HW symptom: row 0 garbled, +word shift). streaming=1
		// is already set, so no further refresh can sneak in during this wait.
		S_ENTER: begin
			str_cmd <= STR_CMD_NOP;
			if (str_dly == 0) str_state <= S_PRE_ALL;
			else              str_dly   <= str_dly - 4'd1;
		end

		// PRECHARGE all banks (A10=1) for a guaranteed-clean entry, then tRP.
		S_PRE_ALL: begin
			if (str_dly == 0) begin
				str_cmd  <= STR_CMD_PRECHARGE;
				str_addr <= 13'b0010000000000;  // A10=1 -> all banks
				str_ba   <= 2'd0;
				str_dqm  <= 2'b00;
				str_dly  <= STR_TRP;            // wait tRP before ACTIVATE
			end else begin
				str_cmd <= STR_CMD_NOP;
				str_dly <= str_dly - 4'd1;
				if (str_dly == 4'd1)
					str_state <= S_ACTIVATE;
			end
		end

		// ACTIVE(bank,row), then tRCD before the first READ.
		S_ACTIVATE: begin
			if (str_dly == 0) begin
				// honour tRC across the intervening PRECHARGE/REFRESH: do not
				// re-ACTIVATE until the ACTIVE-to-ACTIVE window has elapsed.
				if (str_rc == 0) begin
					str_cmd   <= STR_CMD_ACTIVE;
					str_addr  <= cur_row;
					str_ba    <= cur_bank;
					open_bank <= cur_bank;      // remember for the row-end PRECHARGE
					str_dqm   <= 2'b00;
					str_rc    <= STR_TRC;       // arm next-ACTIVE guard
					str_ras   <= STR_TRAS;      // arm PRECHARGE guard
					str_dly   <= STR_TRCD;      // wait tRCD before READ
				end else begin
					str_cmd <= STR_CMD_NOP;
				end
			end else begin
				str_cmd <= STR_CMD_NOP;
				str_dly <= str_dly - 4'd1;
				if (str_dly == 4'd1) begin
					str_state <= S_READ;
					row_words <= 9'd0;
					str_pace  <= 1'b0;   // first read of the row issues immediately
				end
			end
		end

		// Issue one READ per clk (A10=0, page mode). col from cur_addr.
		// Advance cur_addr by 2 bytes/word, count words; stop at row end or
		// when the whole burst is exhausted.
		//
		// Consumer back-pressure: only ISSUE a READ when stream_ready is high.
		// When low, hold the row OPEN and present a NOP — do NOT mark the CAS
		// pipeline, do NOT advance cur_addr/words_left/row_words, and do NOT
		// leave S_READ. The CAS drain at the top of this block keeps shifting
		// rd_inflight and pulsing stream_valid, so up to CAS(=3) reads already
		// in flight still surface byte-exact and in order; a sustained pause
		// quiesces stream_valid once the pipeline empties (row still open).
		// str_ras/str_rc keep counting down continuously, so a held-open row
		// still satisfies tRAS for the eventual PRECHARGE. When stream_ready
		// re-asserts, issuance resumes from the held column (cur_col unchanged).
		S_READ: begin
			// Paced: issue a READ only on a non-gap cycle while ready; otherwise
			// NOP. str_pace toggles 0->1 after each READ so reads land 1 per 2 clk,
			// isolating each read's DQ data (see str_pace decl). The row stays open
			// through the NOP gap (tRAS/tRC keep counting).
			if (stream_ready && !str_pace) begin
				str_cmd  <= STR_CMD_READ;
				// 13-bit address: [12:11]=0, [10]=A10=0 (page mode, no auto-pre),
				// [9]=0, [8:0]=column = {a[24],a[8:1]} = cur_col.
				str_addr <= {2'b00, 1'b0, 1'b0, cur_col};
				str_ba   <= cur_bank;
				str_dqm  <= 2'b00;
				// mark this READ into the CAS pipeline (OR into the freshly shifted
				// stage 0; the shift above already cleared bit 0 this cycle)
				rd_inflight[0] <= 1'b1;

				cur_addr   <= cur_addr + 25'd2;
				words_left <= words_left - 14'd1;
				row_words  <= row_words + 9'd1;
				str_pace   <= 1'b1;          // next clk = gap (NOP)

				if (last_word || row_full) begin
					// stop issuing; let the capture pipeline drain before PRECHARGE
					str_state <= S_DRAIN;
				end
			end else begin
				// gap cycle, or paused (stream_ready low): NOP, hold the row open,
				// freeze the column counter, and re-arm to issue next clk.
				str_cmd  <= STR_CMD_NOP;
				str_pace <= 1'b0;
			end
		end

		// Hold (NOP) until every issued READ's data has landed (rd_inflight
		// empties; note rd_inflight[4] of the LAST read fires one more cycle)
		// AND tRAS since ACTIVE has elapsed. Only then PRECHARGE the bank.
		S_DRAIN: begin
			str_cmd <= STR_CMD_NOP;
			if ((rd_inflight == 5'd0) && (str_ras == 4'd0))
				str_state <= S_PRECHARGE;
		end

		// PRECHARGE the open bank (A10=0), then tRP.
		S_PRECHARGE: begin
			if (str_dly == 0) begin
				str_cmd  <= STR_CMD_PRECHARGE;
				str_addr <= 13'd0;       // A10=0 -> single-bank precharge
				str_ba   <= open_bank;   // bank of the row that was activated
				str_dqm  <= 2'b00;
				str_dly  <= STR_TRP;
			end else begin
				str_cmd <= STR_CMD_NOP;
				str_dly <= str_dly - 4'd1;
				if (str_dly == 4'd1)
					str_state <= S_REFRESH;
			end
		end

		// AUTO_REFRESH once per row boundary, unconditional (all banks idle).
		S_REFRESH: begin
			if (str_dly == 0) begin
				str_cmd  <= STR_CMD_AUTO_REFRESH;
				str_addr <= 13'd0;
				str_ba   <= 2'd0;
				str_dqm  <= 2'b00;
				str_dly  <= STR_TRFC;    // tRFC: refresh must complete before re-ACTIVATE
			end else begin
				str_cmd <= STR_CMD_NOP;
				str_dly <= str_dly - 4'd1;
				if (str_dly == 4'd1) begin
					if (words_left == 0)
						str_state <= S_DONE;
					else
						str_state <= S_ACTIVATE;  // next row (tRC guard applies)
				end
			end
		end

		S_DONE: begin
			str_cmd     <= STR_CMD_INHIBIT;
			streaming   <= 1'b0;
			stream_busy <= 1'b0;
			stream_done <= 1'b1;
			str_state   <= S_IDLE;
		end

		default: str_state <= S_IDLE;
		endcase
	end
end


// ---------------------------------------------------------------------
// ------------------------ cycle state machine ------------------------
// ---------------------------------------------------------------------

localparam STATE_FIRST     = 3'd0;   // first state in cycle
localparam STATE_CMD_START = 3'd1;   // state in which a new command can be started
localparam STATE_CMD_CONT  = STATE_CMD_START + RASCAS_DELAY;      // 3 command can be continued
localparam STATE_CMD_READ  = STATE_CMD_CONT + CAS_LATENCY + 1'd1; // 6 read state
localparam STATE_LAST      = 3'd7;  // last state in cycle

reg clkref_last;
reg [2:0] q;
always @(posedge clk) begin
	// SDRAM (state machine) clock is 85MHz. Synchronize this to systems 21.477 Mhz clock
   // force counter to pass state LAST->FIRST exactly after the rising edge of clkref
   clkref_last <= clkref;

   q <= q + 1'd1;
   if (q==STATE_LAST) q<=STATE_FIRST;
   if (~clkref_last & clkref) q<=STATE_FIRST + 1'd1;

end

// ---------------------------------------------------------------------
// --------------------------- startup/reset ---------------------------
// ---------------------------------------------------------------------

// Initialize the SDRAM during the first 31 controller cycles after reset.
// Keep this counter only as wide as the loaded range. A wider counter creates
// unreachable high bits that still feed the reset command/address mux in
// synthesis and shows up as a false 100 MHz SDRAM timing path.
reg [4:0] reset;
always @(posedge clk) begin
	if(init)	reset <= 5'd31;
	else if((q == STATE_LAST) && (reset != 0))
		reset <= reset - 5'd1;
end

// ---------------------------------------------------------------------
// ------------------ generate ram control signals ---------------------
// ---------------------------------------------------------------------

// all possible commands
localparam CMD_INHIBIT         = 4'b1111;
localparam CMD_NOP             = 4'b0111;
localparam CMD_ACTIVE          = 4'b0011;
localparam CMD_READ            = 4'b0101;
localparam CMD_WRITE           = 4'b0100;
localparam CMD_BURST_TERMINATE = 4'b0110;
localparam CMD_PRECHARGE       = 4'b0010;
localparam CMD_AUTO_REFRESH    = 4'b0001;
localparam CMD_LOAD_MODE       = 4'b0000;

wire [3:0] sd_cmd;   // current command sent to sd ram

// clkref high - port A
// clkref low  - port B
//
// Stage the wide port mux before STATE_CMD_START. The request bridges hold
// addr/data/we/oe until done, so sampling one SDRAM clock earlier is safe and
// keeps the controller's 100 MHz command cycle off a wide external mux path.
reg        req_port_a_r;
reg        req_we_r;
reg        req_oe_r;
reg [24:0] req_addr_r;
reg  [7:0] req_din_r;

always @(posedge clk) begin
	if(init) begin
		req_port_a_r <= 1'b0;
		req_we_r     <= 1'b0;
		req_oe_r     <= 1'b0;
		req_addr_r   <= 25'd0;
		req_din_r    <= 8'd0;
	end else begin
		req_port_a_r <= clkref;
		req_we_r     <= clkref ? weA   : weB;
		req_oe_r     <= clkref ? oeA   : oeB;
		req_addr_r   <= clkref ? addrA : addrB;
		req_din_r    <= clkref ? dinA  : dinB;
	end
end

reg        cycle_port_a;
reg        cycle_we;
reg        cycle_oe;
reg [24:0] cycle_addr;
reg  [7:0] cycle_din;

wire [7:0] dout = cycle_addr[0]?sd_data_in[7:0]:sd_data_in[15:8];
// Structurally gate the DQ-bus drive off during a stream burst: a port write
// latched before the burst must not drive write data against the chip's read
// data while the stream FSM owns the bus.
assign     we_out = (q == STATE_CMD_CONT) && cycle_we && !streaming;

always @(posedge clk) begin
	doneA <= 1'b0;
	doneB <= 1'b0;
	if(init || (reset != 0)) begin
		cycle_port_a <= 1'b0;
		cycle_we     <= 1'b0;
		cycle_oe     <= 1'b0;
		cycle_addr   <= 25'd0;
		cycle_din    <= 8'd0;
	end else begin
		if(q == STATE_CMD_START) begin
			cycle_port_a <= req_port_a_r;
			// Do not arm a port write/read while the stream FSM owns the bus —
			// a pending port request must wait until streaming clears. addr/din
			// still latch harmlessly; only the action bits are suppressed.
			cycle_we     <= req_we_r       && !streaming;
			cycle_oe     <= !req_we_r && req_oe_r && !streaming;
			cycle_addr   <= req_addr_r;
			cycle_din    <= req_din_r;
		end
		if(q == STATE_CMD_READ && cycle_oe) begin
			if(cycle_port_a) begin
				doutA <= dout;
				doneA <= 1'b1;
			end
			else begin
				doutB <= dout;
				doneB <= 1'b1;
			end
			cycle_oe <= 1'b0;
		end
		if(q == STATE_CMD_CONT && cycle_we) begin
			if(cycle_port_a) doneA <= 1'b1;
			else             doneB <= 1'b1;
			cycle_we <= 1'b0;
		end
	end
end

wire [3:0] reset_cmd = 
	((q == STATE_CMD_START) && (reset == 5'd13))?CMD_PRECHARGE:
	((q == STATE_CMD_START) && (reset == 5'd2 ))?CMD_LOAD_MODE:
	CMD_INHIBIT;

wire [3:0] run_cmd =
	((req_we_r || req_oe_r) && (q == STATE_CMD_START))?CMD_ACTIVE:
	( cycle_we          && (q == STATE_CMD_CONT ))?CMD_WRITE:
	( cycle_oe          && (q == STATE_CMD_CONT ))?CMD_READ:
	(!(req_we_r || req_oe_r) && (q == STATE_CMD_START))?CMD_AUTO_REFRESH:
	CMD_INHIBIT;

// While streaming, the normal run_cmd path is forced to INHIBIT and the
// command comes from the stream FSM's registered command. reset always wins
// (streaming is only ever set while reset==0, so the streaming branch only
// applies during normal run).
assign sd_cmd = (reset != 0) ? reset_cmd :
                streaming     ? str_cmd   :
                                run_cmd;

wire [12:0] reset_addr = (reset == 5'd13)?13'b0010000000000:MODE;

wire [12:0] run_addr =
	(q == STATE_CMD_START)?req_addr_r[21:9]:{ 4'b0010, cycle_addr[24], cycle_addr[8:1]};

assign sd_data_out = we_out?{ cycle_din, cycle_din }:16'b0;
//register SDRAM output signals
assign sd_addr = (reset != 0) ? reset_addr :
                 streaming     ? str_addr   :
                                 run_addr;

assign sd_ba = (reset != 0) ? 2'b00 :
               streaming     ? str_ba :
               (q == STATE_CMD_START) ? req_addr_r[23:22] : cycle_addr[23:22];

assign sd_dqm = streaming ? str_dqm :
                we_out     ? { cycle_addr[0], ~cycle_addr[0] } : 2'b00;

// drive control signals according to current command
assign sd_cs  = sd_cmd[3];
assign sd_ras = sd_cmd[2];
assign sd_cas = sd_cmd[1];
assign sd_we  = sd_cmd[0];

endmodule
