// Controller-only ULX3S hardware gate for the Astra 32-bit SDRAM path.
`default_nettype none

module astra_sdram32_hwtest #(
    parameter integer SDRAM_READ_LATENCY = 3
) (
    input  wire        clk25_mhz,
    input  wire        reset_n,
    output wire        ftdi_rxd,
    output wire [7:0]  leds,
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
    localparam [5:0] LAST_OP = 6'd37;
    localparam [2:0] T_WAIT = 3'd0, T_WRITE_REQ = 3'd1,
                     T_WRITE_RSP = 3'd2, T_READ_REQ = 3'd3,
                     T_READ_RSP = 3'd4, T_DONE = 3'd5;

    wire [3:0] pll_o;
    wire pll_locked;
    ecp5pll #(
        .in_hz(25000000),
        .out0_hz(75000000),
        .out1_hz(0),
        .out2_hz(0),
        .out3_hz(0)
    ) pll_i (
        .clk_i(clk25_mhz),
        .clk_o(pll_o),
        .locked(pll_locked),
        .reset(1'b0),
        .standby(1'b0),
        .phasesel(2'b00),
        .phasedir(1'b0),
        .phasestep(1'b0),
        .phaseloadreg(1'b0)
    );
    wire clk = pll_o[0];

    reg [1:0] lock_sync = 2'b00;
    reg [1:0] reset_sync = 2'b00;
    always @(posedge clk) begin
        lock_sync <= {lock_sync[0], pll_locked};
        reset_sync <= {reset_sync[0], reset_n};
    end
    wire rst = !lock_sync[1] || !reset_sync[1];

    function automatic [31:0] merge_masked(
        input [31:0] original,
        input [31:0] replacement,
        input [3:0] enables
    );
        integer lane;
        begin
            merge_masked = original;
            for (lane = 0; lane < 4; lane = lane + 1)
                if (enables[lane])
                    merge_masked[lane * 8 +: 8] =
                        replacement[lane * 8 +: 8];
        end
    endfunction

    function automatic [3:0] op_mask(input [5:0] index);
        integer value;
        begin
            value = ((index - 6'd8) >> 1) + 1;
            op_mask = value[3:0];
        end
    endfunction

    function automatic [24:0] op_addr(input [5:0] index);
        reg [3:0] mask;
        begin
            if (index < 6'd8)
                op_addr = {17'd0, index, 2'b00};
            else begin
                mask = op_mask(index);
                op_addr = 25'h0000100 + {19'd0, mask, 2'b00};
            end
        end
    endfunction

    function automatic [3:0] op_be(input [5:0] index);
        begin
            if (index < 6'd8 || !index[0])
                op_be = 4'b1111;
            else
                op_be = op_mask(index);
        end
    endfunction

    function automatic [31:0] op_wdata(input [5:0] index);
        begin
            case (index)
                6'd0: op_wdata = 32'h00000001;
                6'd1: op_wdata = 32'h00010000;
                6'd2: op_wdata = 32'h00000100;
                6'd3: op_wdata = 32'h01000000;
                6'd4: op_wdata = 32'h11223344;
                6'd5: op_wdata = 32'ha55ac33c;
                6'd6: op_wdata = 32'h00000000;
                6'd7: op_wdata = 32'hffffffff;
                default: op_wdata = index[0] ? 32'ha1b2c3d4 : 32'h10203040;
            endcase
        end
    endfunction

    function automatic [31:0] op_expected(input [5:0] index);
        reg [3:0] mask;
        begin
            if (index < 6'd8)
                op_expected = op_wdata(index);
            else if (!index[0])
                op_expected = 32'h10203040;
            else begin
                mask = op_mask(index);
                op_expected = merge_masked(32'h10203040, 32'ha1b2c3d4,
                                           mask);
            end
        end
    endfunction

    reg [2:0] test_state = T_WAIT;
    reg [19:0] wait_count = 20'd0;
    reg [5:0] op_index = 6'd0;
    reg test_done = 1'b0;
    reg test_pass = 1'b0;
    reg [5:0] report_op = 6'd0;
    reg [31:0] report_expected = 32'd0;
    reg [31:0] report_actual = 32'd0;

    wire cpu_valid = test_state == T_WRITE_REQ || test_state == T_READ_REQ;
    wire cpu_ready;
    wire cpu_write = test_state == T_WRITE_REQ;
    wire cpu_rsp_valid;
    wire [31:0] cpu_rdata;

    always @(posedge clk) begin
        if (rst) begin
            test_state <= T_WAIT;
            wait_count <= 20'd0;
            op_index <= 6'd0;
            test_done <= 1'b0;
            test_pass <= 1'b0;
            report_op <= 6'd0;
            report_expected <= 32'd0;
            report_actual <= 32'd0;
        end else begin
            case (test_state)
                T_WAIT: begin
                    // The physical core completes its JEDEC power-up sequence
                    // after about 8K clocks at 75 MHz; leave ample margin.
                    if (wait_count == 20'd200000)
                        test_state <= T_WRITE_REQ;
                    else
                        wait_count <= wait_count + 20'd1;
                end
                T_WRITE_REQ: if (cpu_ready) test_state <= T_WRITE_RSP;
                T_WRITE_RSP: if (cpu_rsp_valid) test_state <= T_READ_REQ;
                T_READ_REQ: if (cpu_ready) test_state <= T_READ_RSP;
                T_READ_RSP: if (cpu_rsp_valid) begin
                    if (cpu_rdata != op_expected(op_index)) begin
                        test_done <= 1'b1;
                        test_pass <= 1'b0;
                        report_op <= op_index;
                        report_expected <= op_expected(op_index);
                        report_actual <= cpu_rdata;
                        test_state <= T_DONE;
                    end else if (op_index == LAST_OP) begin
                        test_done <= 1'b1;
                        test_pass <= 1'b1;
                        report_op <= op_index;
                        report_expected <= op_expected(op_index);
                        report_actual <= cpu_rdata;
                        test_state <= T_DONE;
                    end else begin
                        op_index <= op_index + 6'd1;
                        test_state <= T_WRITE_REQ;
                    end
                end
                default: test_state <= T_DONE;
            endcase
        end
    end

    wire [15:0] sd_data_out;
    wire sd_data_oe;
    wire unused_dma_ready;
    wire unused_dma_rsp_valid;
    wire [31:0] unused_dma_rdata;
    sdram32_controller #(
        .SDRAM_MHZ(75),
        .SDRAM_READ_LATENCY(SDRAM_READ_LATENCY)
    ) controller_i (
        .clk(clk),
        .rst(rst),
        .cpu_valid(cpu_valid),
        .cpu_ready(cpu_ready),
        .cpu_write(cpu_write),
        .cpu_addr(op_addr(op_index)),
        .cpu_be(op_be(op_index)),
        .cpu_wdata(op_wdata(op_index)),
        .cpu_lock(1'b0),
        .cpu_rsp_valid(cpu_rsp_valid),
        .cpu_rdata(cpu_rdata),
        .video_lock(1'b0),
        .video_valid(1'b0),
        .video_ready(),
        .video_write(1'b0),
        .video_addr(25'd0),
        .video_be(4'd0),
        .video_wdata(32'd0),
        .video_rsp_valid(),
        .video_rdata(),
        .dma_lock(1'b0),
        .dma_valid(1'b0),
        .dma_ready(unused_dma_ready),
        .dma_write(1'b0),
        .dma_addr(25'd0),
        .dma_be(4'd0),
        .dma_wdata(32'd0),
        .dma_rsp_valid(unused_dma_rsp_valid),
        .dma_rdata(unused_dma_rdata),
        .sdram_data_in(sdram_d),
        .sdram_data_out(sd_data_out),
        .sdram_data_oe(sd_data_oe),
        .sdram_clk(sdram_clk),
        .sdram_cke(sdram_cke),
        .sdram_cs(sdram_csn),
        .sdram_ras(sdram_rasn),
        .sdram_cas(sdram_casn),
        .sdram_we(sdram_wen),
        .sdram_dqm(sdram_dqm),
        .sdram_addr(sdram_a),
        .sdram_ba(sdram_ba)
    );
    assign sdram_d = sd_data_oe ? sd_data_out : 16'hzzzz;

    function automatic [7:0] hex_digit(input [3:0] value);
        hex_digit = value < 10 ? (8'h30 | {4'd0, value}) :
                    (8'h37 + {4'd0, value});
    endfunction

    function automatic [7:0] report_char(input [5:0] position);
        integer shift;
        reg [31:0] shifted;
        begin
            case (position)
                0: report_char = "S";
                1: report_char = "3";
                2: report_char = "2";
                3: report_char = " ";
                4: report_char = "L";
                5: report_char = "=";
                6: begin
                    case (SDRAM_READ_LATENCY)
                        1: report_char = "1";
                        2: report_char = "2";
                        default: report_char = "3";
                    endcase
                end
                7: report_char = " ";
                8: report_char = "R";
                9: report_char = "=";
                10: report_char = test_pass ? "P" : "F";
                11: report_char = " ";
                12: report_char = "O";
                13: report_char = "P";
                14: report_char = "=";
                15: report_char = hex_digit({2'b00, report_op[5:4]});
                16: report_char = hex_digit(report_op[3:0]);
                17: report_char = " ";
                18: report_char = "E";
                19: report_char = "=";
                28: report_char = " ";
                29: report_char = "A";
                30: report_char = "=";
                39: report_char = 8'h0d;
                40: report_char = 8'h0a;
                default: begin
                    if (position >= 20 && position <= 27) begin
                        shift = (27 - position) * 4;
                        shifted = report_expected >> shift;
                        report_char = hex_digit(shifted[3:0]);
                    end else if (position >= 31 && position <= 38) begin
                        shift = (38 - position) * 4;
                        shifted = report_actual >> shift;
                        report_char = hex_digit(shifted[3:0]);
                    end else begin
                        report_char = "?";
                    end
                end
            endcase
        end
    endfunction

    localparam [1:0] TX_ARM = 2'd0, TX_WAIT_BUSY = 2'd1,
                     TX_WAIT_IDLE = 2'd2, TX_PAUSE = 2'd3;
    reg [1:0] tx_state = TX_ARM;
    reg [5:0] tx_index = 6'd0;
    reg [22:0] tx_pause = 23'd0;
    reg [7:0] tx_data = 8'd0;
    reg tx_start = 1'b0;
    wire tx_busy;

    uart_tx #(.CLK_HZ(75000000), .BAUD(115200)) uart_i (
        .clk(clk), .rst(rst), .data(tx_data), .start(tx_start),
        .tx(ftdi_rxd), .busy(tx_busy)
    );

    always @(posedge clk) begin
        tx_start <= 1'b0;
        if (rst) begin
            tx_state <= TX_ARM;
            tx_index <= 6'd0;
            tx_pause <= 23'd0;
        end else if (test_done) begin
            case (tx_state)
                TX_ARM: if (!tx_busy) begin
                    tx_data <= report_char(tx_index);
                    tx_start <= 1'b1;
                    tx_state <= TX_WAIT_BUSY;
                end
                TX_WAIT_BUSY: if (tx_busy) tx_state <= TX_WAIT_IDLE;
                TX_WAIT_IDLE: if (!tx_busy) begin
                    if (tx_index == 6'd40) begin
                        tx_index <= 6'd0;
                        tx_pause <= 23'd0;
                        tx_state <= TX_PAUSE;
                    end else begin
                        tx_index <= tx_index + 6'd1;
                        tx_state <= TX_ARM;
                    end
                end
                TX_PAUSE: begin
                    tx_pause <= tx_pause + 23'd1;
                    if (&tx_pause) tx_state <= TX_ARM;
                end
            endcase
        end
    end

    assign leds = test_done ? (test_pass ? 8'hff : {2'b10, report_op}) :
                  wait_count[19:12];
endmodule

`default_nettype wire
