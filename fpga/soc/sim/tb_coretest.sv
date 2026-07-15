// Astra 68 coretest SoC simulation gate.
`timescale 1ns/1ps

module tb_coretest #(
    parameter DEBUG_SCRATCH_PARAM = 1'b0,
    parameter DEBUG_BOOT_PARAM = 1'b0,
    parameter DEBUG_IRQ_PARAM = 1'b0,
    parameter DEBUG_PMMU_PARAM = 1'b0,
    parameter DEBUG_CHK_PARAM = 1'b0,
    parameter SDRAM_ENABLE_PARAM = 1'b0,
    parameter TRAP_EXCEPTION_MARKERS_PARAM = 1'b0,
    parameter [63:0] SIM_TIMEOUT_PS_PARAM = 64'd0
);
    localparam DEBUG_SCRATCH = DEBUG_SCRATCH_PARAM;
    localparam DEBUG_BOOT = DEBUG_BOOT_PARAM;
    localparam DEBUG_IRQ = DEBUG_IRQ_PARAM;
    localparam DEBUG_PMMU = DEBUG_PMMU_PARAM;
    localparam DEBUG_CHK = DEBUG_CHK_PARAM;
    localparam DEBUG_IRQ_BUS = 1'b0;
    localparam TRAP_EXCEPTION_MARKERS = TRAP_EXCEPTION_MARKERS_PARAM;
    localparam [63:0] SIM_TIMEOUT_PS = SIM_TIMEOUT_PS_PARAM;

    reg clk25 = 0;
    reg rstn = 0;
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
    localparam IRQ_REQ_ADDR = 32'hfff00600;
    localparam BERR_REQ_ADDR = 32'hfff00604;
    localparam BERR_TARGET_ADDR = 32'hfff00608;
    localparam BERR_SDRAM_TARGET_ADDR = 32'h02000100;
    localparam FC_PROBE_ARM_ADDR = 32'hfff0060c;
    // BERR_REQ write encoding:
    //   bit0=arm, bits3:1=expected FC, bit4=expected RWn, bit5=expected RMCn low.
    localparam MOVES_FC_READ_ADDR = 32'h01ffad00;
    localparam MOVES_FC_WRITE_ADDR = 32'h01ffad04;
    localparam ATOMIC_RMC_CAS_ADDR = 32'h01ffae00;
    localparam ATOMIC_RMC_ARM_ADDR = 32'h01ffae04;
    localparam ATOMIC_RMC_TAS_ADDR = 32'h01ffae10;
    localparam ATOMIC_RMC_CAS2_ADDR0 = 32'h01ffae20;
    localparam ATOMIC_RMC_CAS2_ADDR1 = 32'h01ffae24;
    localparam DATA_FC_SUP_WRITE_ADDR = 32'h01ffaf00;
    localparam DATA_FC_SUP_READ_ADDR = 32'h01ffaf04;
    localparam DATA_FC_USER_WRITE_ADDR = 32'h01ffaf08;
    localparam DATA_FC_USER_READ_ADDR = 32'h01ffaf0c;
    localparam PROG_FC_SUP_ADDR = 32'h01ffaf20;
    localparam PROG_FC_USER_ADDR = 32'h01ffaf30;
    localparam DEFAULT_HANDLER_ADDR = 32'hffe004aa;
    localparam EXC_REC_BASE = 32'h01ff9270;
    localparam EXC_ALT_VBR_BASE = 32'h01ff9500;
    localparam BKPT_ACK_ADDR = 32'h00000000;
    reg [2:0] sim_ipln = 3'b111;
    reg sim_avecn = 1'b1;
    reg sim_berr_arm = 1'b0;
    reg sim_berr_active = 1'b0;
    reg sim_berr_sdram_target = 1'b0;
    reg [2:0] sim_berr_expect_fc = 3'b000;
    reg sim_berr_expect_rw_n = 1'b0;
    reg sim_berr_expect_rmc = 1'b0;
    wire irq_req_write = dut.bus_write_stb
        && dut.cpu_adr == (IRQ_REQ_ADDR + 32'd2);
    wire fc_probe_arm_write = dut.bus_write_stb
        && dut.cpu_adr == (FC_PROBE_ARM_ADDR + 32'd2);
    wire berr_req_write = dut.bus_write_stb
        && dut.cpu_adr == (BERR_REQ_ADDR + 32'd2);
    wire [31:0] sim_berr_target_addr = sim_berr_sdram_target
        ? BERR_SDRAM_TARGET_ADDR : BERR_TARGET_ADDR;
    wire sim_berr_match = sim_berr_arm && !dut.cpu_as_n
        && dut.cpu_adr == sim_berr_target_addr;
    wire sim_berrn = (sim_berr_active || sim_berr_match) ? 1'b0 : 1'b1;
    reg [1:0] atomic_rmc_mode = 2'd0;
    reg atomic_rmc_read_only = 1'b0;
    reg [3:0] atomic_rmc_seen_read = 4'd0;
    reg [3:0] atomic_rmc_seen_write = 4'd0;
    reg fc_moves_probe_enabled = 1'b0;
    reg fc_data_prog_probe_enabled = 1'b0;
    reg [31:0] last_prog_adr0 = 32'd0;
    reg [31:0] last_prog_adr1 = 32'd0;
    reg [31:0] last_prog_adr2 = 32'd0;
    reg [31:0] last_prog_adr3 = 32'd0;
    reg [31:0] last_prog_data0 = 32'd0;
    reg [31:0] last_prog_data1 = 32'd0;
    reg [31:0] last_prog_data2 = 32'd0;
    reg [31:0] last_prog_data3 = 32'd0;
    reg [31:0] last_alt_vec_adr = 32'd0;
    reg [31:0] last_alt_vec_data = 32'd0;
    wire irq_trace_arm =
        (last_prog_adr0[15:0] >= 16'h0b20 && last_prog_adr0[15:0] < 16'h0b80) ||
        (last_prog_adr1[15:0] >= 16'h0b20 && last_prog_adr1[15:0] < 16'h0b80) ||
        (last_prog_adr2[15:0] >= 16'h0b20 && last_prog_adr2[15:0] < 16'h0b80) ||
        (last_prog_adr3[15:0] >= 16'h0b20 && last_prog_adr3[15:0] < 16'h0b80) ||
        (last_prog_adr0[15:0] >= 16'h0bc0 && last_prog_adr0[15:0] < 16'h0c20) ||
        (last_prog_adr1[15:0] >= 16'h0bc0 && last_prog_adr1[15:0] < 16'h0c20) ||
        (last_prog_adr2[15:0] >= 16'h0bc0 && last_prog_adr2[15:0] < 16'h0c20) ||
        (last_prog_adr3[15:0] >= 16'h0bc0 && last_prog_adr3[15:0] < 16'h0c20);
    integer bkpt_ack_seen = 0;

    astra_soc #(
        .RST_MAX(16'd16),
        .SDRAM_ENABLE(SDRAM_ENABLE_PARAM),
        .SDRAM_READY_DELAY(10000),
        .HDMI_ENABLE(1'b0)
    ) dut (
        .clk25_mhz(clk25),
        .reset_n(rstn),
        .buttons(6'd0),
        .switches(4'd0),
        .ftdi_rxd(tx),
        .ftdi_txd(1'b1),
        .leds(leds),
        .gpdi_dp(gpdi),
        .sdram_clk(sdram_clk),
        .sdram_cke(sdram_cke),
        .sdram_csn(sdram_csn),
        .sdram_wen(sdram_wen),
        .sdram_rasn(sdram_rasn),
        .sdram_casn(sdram_casn),
        .sdram_ba(sdram_ba),
        .sdram_dqm(sdram_dqm),
        .sdram_a(sdram_a),
        .sdram_d(sdram_d)
`ifdef ASTRA_SOC_SIM_IRQ
        , .sim_ipln(sim_ipln)
        , .sim_avecn(sim_avecn)
        , .sim_berrn(sim_berrn)
`endif
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

    always #4 clk25 = ~clk25;

    initial begin
        rstn = 0;
        repeat (40) @(posedge clk25);
        rstn = 1;
    end

    function automatic [31:0] ram_word(input [31:0] addr);
        reg [12:0] idx;
        begin
            idx = addr[14:2];
            ram_word = {dut.ram0[idx], dut.ram1[idx], dut.ram2[idx], dut.ram3[idx]};
        end
    endfunction

    always @(posedge dut.clk) begin
        if (!rstn || dut.rst) begin
            sim_ipln <= 3'b111;
            sim_avecn <= 1'b1;
            sim_berr_arm <= 1'b0;
            sim_berr_active <= 1'b0;
            sim_berr_sdram_target <= 1'b0;
            sim_berr_expect_fc <= 3'b000;
            sim_berr_expect_rw_n <= 1'b0;
            sim_berr_expect_rmc <= 1'b0;
        end else if (irq_req_write) begin
            if (dut.cpu_dout[2:0] == 3'd0) begin
                sim_ipln <= 3'b111;
                sim_avecn <= 1'b1;
            end else begin
                sim_ipln <= ~dut.cpu_dout[2:0];
                sim_avecn <= 1'b0;
            end
        end else begin
            if (dut.cpu_as_n) begin
                sim_berr_active <= 1'b0;
            end
            if (berr_req_write) begin
                sim_berr_arm <= dut.cpu_dout[0];
                sim_berr_expect_fc <= dut.cpu_dout[3:1];
                sim_berr_expect_rw_n <= dut.cpu_dout[4];
                sim_berr_expect_rmc <= dut.cpu_dout[5];
                sim_berr_sdram_target <= dut.cpu_dout[6];
                if (DEBUG_IRQ) begin
                    $display("[%0t] BERR_ARM dout=0x%08x expect_fc=%b expect_rw=%b expect_rmc=%b adr=0x%08x tgpc=0x%08x",
                             $time, dut.cpu_dout, dut.cpu_dout[3:1],
                             dut.cpu_dout[4], dut.cpu_dout[5],
                             dut.cpu_adr, dut.tg_dbg_status);
                end
            end
            if (sim_berr_match) begin
                if (DEBUG_IRQ) begin
                    $display("[%0t] BERR_MATCH adr=0x%08x fc=%b rw=%b siz=%b be=%b dout=0x%08x din=0x%08x tgpc=0x%08x",
                             $time, dut.cpu_adr, dut.cpu_fc, dut.cpu_rw_n,
                             dut.cpu_siz, dut.be, dut.cpu_dout,
                             dut.cpu_din, dut.tg_dbg_status);
                end
                if (dut.cpu_fc !== sim_berr_expect_fc
                    || dut.cpu_rw_n !== sim_berr_expect_rw_n) begin
                    $fatal(1, "BERR probe expected FC=%b RWn=%b, got FC=%b RWn=%b",
                           sim_berr_expect_fc, sim_berr_expect_rw_n,
                           dut.cpu_fc, dut.cpu_rw_n);
                end
                if (dut.cpu_rmc_n !== ~sim_berr_expect_rmc) begin
                    $fatal(1, "BERR probe expected RMCn=%b, got RMCn=%b",
                           ~sim_berr_expect_rmc, dut.cpu_rmc_n);
                end
                if (sim_berr_sdram_target &&
                    (dut.cpu_siz !== 2'b00 || dut.be !== 4'b1111 ||
                     dut.cpu_dout !== 32'h13579bdf)) begin
                    $fatal(1, "combined write expected SIZE=00 BE=1111 DATA=13579bdf, got SIZE=%b BE=%b DATA=%08x",
                           dut.cpu_siz, dut.be, dut.cpu_dout);
                end
                sim_berr_active <= 1'b1;
                sim_berr_arm <= 1'b0;
            end
        end
    end

    always @(posedge dut.clk) begin
        if (DEBUG_IRQ && !dut.cpu_as_n &&
            dut.cpu_adr >= BERR_SDRAM_TARGET_ADDR &&
            dut.cpu_adr < BERR_SDRAM_TARGET_ADDR + 32'd4) begin
            $display("[%0t] SDRAM_TARGET adr=%08x rw=%b siz=%b be=%b data=%08x as=%b dsack=%b berr=%b start=%b state=%0d pc=%08x",
                     $time, dut.cpu_adr, dut.cpu_rw_n, dut.cpu_siz, dut.be,
                     dut.cpu_dout, dut.cpu_as_n, dut.cpu_dsack_n, sim_berrn,
                     dut.sdram_cpu_start, dut.bs, dut.tg_dbg_status);
        end
    end

    always @(posedge dut.clk) begin
        if (!rstn || dut.rst) begin
            fc_moves_probe_enabled <= 1'b0;
            fc_data_prog_probe_enabled <= 1'b0;
        end else if (fc_probe_arm_write) begin
            fc_moves_probe_enabled <= dut.cpu_dout[0];
            fc_data_prog_probe_enabled <= dut.cpu_dout[1];
        end
    end

    always @(posedge dut.clk) begin
        if (!rstn || dut.rst) begin
            last_prog_adr0 <= 32'd0;
            last_prog_adr1 <= 32'd0;
            last_prog_adr2 <= 32'd0;
            last_prog_adr3 <= 32'd0;
            last_prog_data0 <= 32'd0;
            last_prog_data1 <= 32'd0;
            last_prog_data2 <= 32'd0;
            last_prog_data3 <= 32'd0;
            last_alt_vec_adr <= 32'd0;
            last_alt_vec_data <= 32'd0;
        end else begin
            if (dut.bus_read_stb && dut.cpu_fc[1:0] == 2'b10) begin
                last_prog_adr3 <= last_prog_adr2;
                last_prog_adr2 <= last_prog_adr1;
                last_prog_adr1 <= last_prog_adr0;
                last_prog_adr0 <= dut.cpu_adr;
                last_prog_data3 <= last_prog_data2;
                last_prog_data2 <= last_prog_data1;
                last_prog_data1 <= last_prog_data0;
                last_prog_data0 <= dut.cpu_din;
            end
            if (dut.bus_read_stb
                && dut.cpu_adr >= EXC_ALT_VBR_BASE
                && dut.cpu_adr < (EXC_ALT_VBR_BASE + 32'h100)) begin
                last_alt_vec_adr <= {dut.cpu_adr[31:2], 2'b00};
                last_alt_vec_data <= ram_word({dut.cpu_adr[31:2], 2'b00});
            end
        end
    end

    always @(posedge dut.clk) begin
        if (SIM_TIMEOUT_PS != 64'd0 && $time >= SIM_TIMEOUT_PS) begin
            $fatal(1, "sim timeout t=%0t adr=0x%08x fc=%b rw=%b din=0x%08x dout=0x%08x ipln=%b avecn=%b tgpc=%08x imm=%08x arin=%08x prog=%08x/%08x %08x/%08x %08x/%08x %08x/%08x",
                   $time, dut.cpu_adr, dut.cpu_fc, dut.cpu_rw_n,
                   dut.cpu_din, dut.cpu_dout, sim_ipln, sim_avecn,
                   dut.tg_dbg_status, dut.tg_dbg_imm, dut.tg_dbg_arin,
                   last_prog_adr0, last_prog_data0,
                   last_prog_adr1, last_prog_data1,
                   last_prog_adr2, last_prog_data2,
                   last_prog_adr3, last_prog_data3);
        end
    end

    always @(posedge dut.clk) begin
        if (rstn && !dut.rst) begin
            if (coretest_started && dut.bus_read_stb && dut.cpu_fc[1:0] == 2'b10
                && dut.cpu_adr == DEFAULT_HANDLER_ADDR) begin
                $fatal(1, "entered default handler adr=0x%08x din=0x%08x ipln=%b avecn=%b tgpc=%08x imm=%08x arin=%08x vec=%08x altvec=%08x/%08x fprog=%08x/%08x %08x/%08x %08x/%08x %08x/%08x rec=%08x %08x %08x %08x %08x %08x %08x stk=%08x %08x %08x %08x prog=%08x/%08x %08x/%08x %08x/%08x %08x/%08x",
                       dut.cpu_adr, dut.cpu_din, sim_ipln, sim_avecn,
                       dut.tg_dbg_status,
                       dut.tg_dbg_imm,
                       dut.tg_dbg_arin,
                       dut.dbg_fault_vec_adr,
                       last_alt_vec_adr, last_alt_vec_data,
                       dut.dbg_fault_prog_adr0, dut.dbg_fault_prog_data0,
                       dut.dbg_fault_prog_adr1, dut.dbg_fault_prog_data1,
                       dut.dbg_fault_prog_adr2, dut.dbg_fault_prog_data2,
                       dut.dbg_fault_prog_adr3, dut.dbg_fault_prog_data3,
                       ram_word(EXC_REC_BASE + 32'h00),
                       ram_word(EXC_REC_BASE + 32'h04),
                       ram_word(EXC_REC_BASE + 32'h08),
                       ram_word(EXC_REC_BASE + 32'h0c),
                       ram_word(EXC_REC_BASE + 32'h10),
                       ram_word(EXC_REC_BASE + 32'h1c),
                       ram_word(EXC_REC_BASE + 32'h20),
                       ram_word(IRQ_REQ_ADDR + 32'h00),
                       ram_word(IRQ_REQ_ADDR + 32'h04),
                       ram_word(IRQ_REQ_ADDR + 32'h08),
                       ram_word(IRQ_REQ_ADDR + 32'h0c),
                       last_prog_adr0, last_prog_data0,
                       last_prog_adr1, last_prog_data1,
                       last_prog_adr2, last_prog_data2,
                       last_prog_adr3, last_prog_data3);
            end
            if (fc_moves_probe_enabled && dut.bus_read_stb && dut.cpu_adr == MOVES_FC_READ_ADDR
                && dut.cpu_fc !== 3'b010) begin
                $fatal(1, "MOVES SFC probe expected FC=010, got %b", dut.cpu_fc);
            end
            if (fc_moves_probe_enabled && dut.bus_write_stb && dut.cpu_adr == MOVES_FC_WRITE_ADDR
                && dut.cpu_fc !== 3'b001) begin
                $fatal(1, "MOVES DFC probe expected FC=001, got %b", dut.cpu_fc);
            end
            if (fc_data_prog_probe_enabled && dut.bus_write_stb && dut.cpu_adr == DATA_FC_SUP_WRITE_ADDR
                && dut.cpu_fc !== 3'b101) begin
                $fatal(1, "supervisor data write expected FC=101, got %b", dut.cpu_fc);
            end
            if (fc_data_prog_probe_enabled && dut.bus_read_stb && dut.cpu_adr == DATA_FC_SUP_READ_ADDR
                && dut.cpu_fc !== 3'b101) begin
                $fatal(1, "supervisor data read expected FC=101, got %b", dut.cpu_fc);
            end
            if (fc_data_prog_probe_enabled && dut.bus_write_stb && dut.cpu_adr == DATA_FC_USER_WRITE_ADDR
                && dut.cpu_fc !== 3'b001) begin
                $fatal(1, "user data write expected FC=001, got %b dout=0x%08x prog=%08x/%08x %08x/%08x %08x/%08x %08x/%08x",
                       dut.cpu_fc, dut.cpu_dout,
                       last_prog_adr0, last_prog_data0,
                       last_prog_adr1, last_prog_data1,
                       last_prog_adr2, last_prog_data2,
                       last_prog_adr3, last_prog_data3);
            end
            if (fc_data_prog_probe_enabled && dut.bus_read_stb && dut.cpu_adr == DATA_FC_USER_READ_ADDR
                && dut.cpu_fc !== 3'b001) begin
                $fatal(1, "user data read expected FC=001, got %b", dut.cpu_fc);
            end
            if (fc_data_prog_probe_enabled && dut.bus_read_stb && dut.cpu_adr == PROG_FC_SUP_ADDR
                && dut.cpu_fc !== 3'b110) begin
                $fatal(1, "supervisor program fetch expected FC=110, got %b", dut.cpu_fc);
            end
            if (fc_data_prog_probe_enabled && dut.bus_read_stb && dut.cpu_adr == PROG_FC_USER_ADDR
                && dut.cpu_fc !== 3'b010) begin
                $fatal(1, "user program fetch expected FC=010, got %b", dut.cpu_fc);
            end
            if (dut.bus_read_stb && dut.cpu_fc == 3'b111
                && dut.cpu_adr[31:8] == 24'h000000) begin
                if (dut.cpu_adr !== BKPT_ACK_ADDR) begin
                    $fatal(1, "BKPT acknowledge expected addr=0x%08x, got 0x%08x",
                           BKPT_ACK_ADDR, dut.cpu_adr);
                end
                if (dut.cpu_rw_n !== 1'b1) begin
                    $fatal(1, "BKPT acknowledge expected read cycle");
                end
                bkpt_ack_seen <= bkpt_ack_seen + 1;
            end
        end
    end

    function automatic [3:0] atomic_rmc_addr_bit(input [31:0] addr);
        begin
            case (addr)
                ATOMIC_RMC_CAS_ADDR: atomic_rmc_addr_bit = 4'b0001;
                ATOMIC_RMC_TAS_ADDR: atomic_rmc_addr_bit = 4'b0010;
                ATOMIC_RMC_CAS2_ADDR0: atomic_rmc_addr_bit = 4'b0100;
                ATOMIC_RMC_CAS2_ADDR1: atomic_rmc_addr_bit = 4'b1000;
                default: atomic_rmc_addr_bit = 4'b0000;
            endcase
        end
    endfunction

    function automatic [3:0] atomic_rmc_expected(input [1:0] mode);
        begin
            case (mode)
                2'd1: atomic_rmc_expected = 4'b0001;
                2'd2: atomic_rmc_expected = 4'b0010;
                2'd3: atomic_rmc_expected = 4'b1100;
                default: atomic_rmc_expected = 4'b0000;
            endcase
        end
    endfunction

    always @(posedge dut.clk) begin
        if (!rstn || dut.rst) begin
            atomic_rmc_mode <= 2'd0;
            atomic_rmc_read_only <= 1'b0;
            atomic_rmc_seen_read <= 4'd0;
            atomic_rmc_seen_write <= 4'd0;
        end else begin
            if (atomic_rmc_mode != 2'd0 && dut.bus_read_stb
                && atomic_rmc_addr_bit(dut.cpu_adr) != 4'd0) begin
                atomic_rmc_seen_read <= atomic_rmc_seen_read
                    | atomic_rmc_addr_bit(dut.cpu_adr);
                if (dut.cpu_rmc_n !== 1'b0) begin
                    $fatal(1, "atomic RMC probe expected read RMCn=0");
                end
            end
            if (atomic_rmc_mode != 2'd0 && dut.bus_write_stb
                && atomic_rmc_addr_bit(dut.cpu_adr) != 4'd0) begin
                if (atomic_rmc_read_only) begin
                    $fatal(1, "atomic RMC probe unexpected write mode=%0d addr=0x%08x",
                           atomic_rmc_mode, dut.cpu_adr);
                end
                atomic_rmc_seen_write <= atomic_rmc_seen_write
                    | atomic_rmc_addr_bit(dut.cpu_adr);
                if (dut.cpu_rmc_n !== 1'b0) begin
                    $fatal(1, "atomic RMC probe expected write RMCn=0");
                end
            end
            if (dut.bus_write_stb && dut.cpu_adr == ATOMIC_RMC_ARM_ADDR) begin
                if (dut.cpu_dout[1:0] != 2'd0) begin
                    atomic_rmc_mode <= dut.cpu_dout[1:0];
                    atomic_rmc_read_only <= dut.cpu_dout[2];
                    atomic_rmc_seen_read <= 4'd0;
                    atomic_rmc_seen_write <= 4'd0;
                end else begin
                    if ((atomic_rmc_seen_read & atomic_rmc_expected(atomic_rmc_mode))
                        != atomic_rmc_expected(atomic_rmc_mode)) begin
                        $fatal(1, "atomic RMC probe missing read mode=%0d seen=%b exp=%b",
                               atomic_rmc_mode, atomic_rmc_seen_read,
                               atomic_rmc_expected(atomic_rmc_mode));
                    end
                    if (atomic_rmc_read_only) begin
                        if ((atomic_rmc_seen_write & atomic_rmc_expected(atomic_rmc_mode))
                            != 4'b0000) begin
                            $fatal(1, "atomic RMC probe saw write in read-only mode=%0d seen=%b",
                                   atomic_rmc_mode, atomic_rmc_seen_write);
                        end
                    end else if ((atomic_rmc_seen_write & atomic_rmc_expected(atomic_rmc_mode))
                        != atomic_rmc_expected(atomic_rmc_mode)) begin
                        $fatal(1, "atomic RMC probe missing write mode=%0d seen=%b exp=%b",
                               atomic_rmc_mode, atomic_rmc_seen_write,
                               atomic_rmc_expected(atomic_rmc_mode));
                    end
                    atomic_rmc_mode <= 2'd0;
                    atomic_rmc_read_only <= 1'b0;
                end
            end
        end
    end

    integer irq_trace_left = 0;
    always @(posedge dut.clk) begin
        if (DEBUG_IRQ && irq_trace_left == 0 && irq_trace_arm) begin
            irq_trace_left <= 20000;
            $display("[%0t] IRQ_TRACE_ARM ipln=%b avecn=%b tgpc=0x%08x prog=%08x/%08x",
                     $time, sim_ipln, sim_avecn, dut.tg_dbg_status,
                     last_prog_adr0, last_prog_data0);
        end

        if (DEBUG_IRQ && irq_req_write) begin
            if (irq_trace_arm) begin
                irq_trace_left <= 20000;
                $display("[%0t] IRQ_REQ dout=0x%08x ipln=%b avecn=%b tgpc=0x%08x prog=%08x/%08x",
                         $time, dut.cpu_dout, sim_ipln, sim_avecn, dut.tg_dbg_status,
                         last_prog_adr0, last_prog_data0);
            end else begin
                irq_trace_left <= 0;
                $display("[%0t] IRQ_REQ_IGN dout=0x%08x ipln=%b avecn=%b tgpc=0x%08x prog=%08x/%08x",
                         $time, dut.cpu_dout, sim_ipln, sim_avecn, dut.tg_dbg_status,
                         last_prog_adr0, last_prog_data0);
            end
        end else if (irq_trace_left > 0) begin
            irq_trace_left <= irq_trace_left - 1;
        end

        if (DEBUG_IRQ
            && (dut.tg_dbg_imm[21] || dut.tg_dbg_imm[20] || dut.tg_dbg_imm[19] || dut.tg_dbg_imm[18])) begin
            $display("[%0t] TG_SR flags=%02x sv=%b presv=%b chg=%b directSR=%b toSR=%b priv=%b rwe=%b waddr=%x state=%x opclo=%03x data=%08x pc=%08x adr=%08x rd=%b wr=%b fc=%b",
                     $time,
                     dut.tg_dbg_imm[31:24],
                     dut.tg_dbg_imm[23],
                     dut.tg_dbg_imm[22],
                     dut.tg_dbg_imm[21],
                     dut.tg_dbg_imm[20],
                     dut.tg_dbg_imm[19],
                     dut.tg_dbg_imm[18],
                     dut.tg_dbg_imm[17],
                     dut.tg_dbg_imm[16:13],
                     dut.tg_dbg_imm[12:11],
                     dut.tg_dbg_imm[10:0],
                     dut.tg_dbg_arin,
                     dut.tg_dbg_status,
                     dut.cpu_adr,
                     dut.bus_read_stb,
                     dut.bus_write_stb,
                     dut.cpu_fc);
        end

        if (DEBUG_IRQ && dut.tg_dbg_imm[17]
            && ((dut.tg_dbg_imm[16:13] == 4'hf
                 && dut.tg_dbg_status >= 32'hffe04680 && dut.tg_dbg_status < 32'hffe046c0)
                || (dut.tg_dbg_status >= 32'hffe00520 && dut.tg_dbg_status < 32'hffe005c0
                    && (dut.tg_dbg_imm[16:13] == 4'h8 || dut.tg_dbg_imm[16:13] == 4'hf)))) begin
            $display("[%0t] TG_WR waddr=%x wdata=%08x opclo=%03x pc=%08x adr=%08x rd=%b wr=%b fc=%b",
                     $time,
                     dut.tg_dbg_imm[16:13],
                     dut.tg_dbg_arin,
                     dut.tg_dbg_imm[10:0],
                     dut.tg_dbg_status,
                     dut.cpu_adr,
                     dut.bus_read_stb,
                     dut.bus_write_stb,
                     dut.cpu_fc);
        end

        if (DEBUG_IRQ && DEBUG_IRQ_BUS && irq_trace_left > 0
            && ((dut.cpu_fc == 3'b111 && (dut.bus_read_stb || dut.bus_write_stb || !dut.cpu_as_n))
                || (dut.cpu_adr >= 32'h01ffff80 && dut.cpu_adr < 32'h02000000)
                || (dut.cpu_adr >= 32'h01ff9270 && dut.cpu_adr < 32'h01ff9290)
                || (dut.cpu_adr >= 32'h01ff9500 && dut.cpu_adr < 32'h01ff9600)
                || (dut.cpu_adr >= IRQ_REQ_ADDR && dut.cpu_adr < (IRQ_REQ_ADDR + 32'h40)))) begin
            $display("[%0t] IRQ_TG_BUS adr=0x%08x rd=%b wr=%b asn=%b rwn=%b fc=%b siz=%b be=%b din=0x%08x dout=0x%08x ipln=%b avecn=%b tgpc=0x%08x",
                     $time,
                     dut.cpu_adr,
                     dut.bus_read_stb,
                     dut.bus_write_stb,
                     dut.cpu_as_n,
                     dut.cpu_rw_n,
                     dut.cpu_fc,
                     dut.cpu_siz,
                     dut.be,
                     dut.cpu_din,
                     dut.cpu_dout,
                     sim_ipln,
                     sim_avecn,
                     dut.tg_dbg_status);
        end

    end

    always @(posedge dut.clk) begin
        if (DEBUG_CHK
            && dut.g_tg68k_enabled.tg_cpu.u_cpu.exe_pc >= 32'hffe01e50
            && dut.g_tg68k_enabled.tg_cpu.u_cpu.exe_pc < 32'hffe01ec0) begin
            $display("[%0t] CHK_TRACE exe=%08x pc=%08x pcadd=%08x pcdelta=%08x state=%02x next=%02x busstate=%x opcode=%04x execchk=%b setchk=%b writeadd=%b writepc=%b writenext=%b frame2=%b vec=%08x stackdata=%08x busadr=%08x rw=%b",
                     $time,
                     dut.g_tg68k_enabled.tg_cpu.u_cpu.exe_pc,
                     dut.g_tg68k_enabled.tg_cpu.u_cpu.tg68_pc,
                     dut.g_tg68k_enabled.tg_cpu.u_cpu.tg68_pc_add,
                     dut.g_tg68k_enabled.tg_cpu.u_cpu.pc_datab,
                     dut.g_tg68k_enabled.tg_cpu.u_cpu.micro_state,
                     dut.g_tg68k_enabled.tg_cpu.u_cpu.next_micro_state,
                     dut.g_tg68k_enabled.tg_cpu.u_cpu.state,
                     dut.g_tg68k_enabled.tg_cpu.u_cpu.opcode,
                     dut.g_tg68k_enabled.tg_cpu.u_cpu.exec[43],
                     dut.g_tg68k_enabled.tg_cpu.u_cpu.set[43],
                     dut.g_tg68k_enabled.tg_cpu.u_cpu.exec[25],
                     dut.g_tg68k_enabled.tg_cpu.u_cpu.writepc,
                     dut.g_tg68k_enabled.tg_cpu.u_cpu.writepcnext,
                     dut.g_tg68k_enabled.tg_cpu.u_cpu.use_vbr_stackframe,
                     dut.g_tg68k_enabled.tg_cpu.u_cpu.trap_vector,
                     dut.g_tg68k_enabled.tg_cpu.u_cpu.data_write_tmp,
                     dut.cpu_adr, dut.cpu_rw_n);
        end
    end

    reg [127:0] shift = 128'd0;
    reg [127:0] next_shift;
    reg saw_fail = 1'b0;
    reg coretest_started = 1'b0;
    integer debug_boot_cycles = 0;
    integer berr_setup_trace_left = 0;
    integer pmmu_wait_cycles = 0;
    integer pmmu_trace_count = 0;
    reg pmmu_trace_armed = 1'b0;
    localparam [14*8-1:0] START_SIG = "CORETEST START";
    localparam [13*8-1:0] PASS_SIG = "CORETEST PASS";
    localparam [13*8-1:0] FAIL_SIG = "CORETEST FAIL";

    always @(posedge dut.clk) begin
        if (dut.uart_start) begin
            $write("%c", dut.uart_data);
            $fflush();
            next_shift = {shift[119:0], dut.uart_data};
            shift <= next_shift;
            if (next_shift[14*8-1:0] == START_SIG) begin
                coretest_started <= 1'b1;
            end
            if (TRAP_EXCEPTION_MARKERS && coretest_started && !saw_fail && dut.uart_data == 8'h45
                && shift[7:0] != 8'h52 && shift[7:0] != 8'h54) begin
                $fatal(1, "unexpected default exception marker adr=0x%08x fc=%b rw=%b ipln=%b avecn=%b vec=%08x tgpc=%08x tgtrap=%08x tgvbr=%08x prog=%08x/%08x %08x/%08x %08x/%08x %08x/%08x faultprog=%08x/%08x %08x/%08x %08x/%08x %08x/%08x",
                       dut.cpu_adr, dut.cpu_fc, dut.cpu_rw_n,
                       sim_ipln, sim_avecn,
                       dut.dbg_fault_vec_adr,
                       dut.tg_dbg_status,
                       dut.tg_dbg_imm,
                       dut.tg_dbg_arin,
                       last_prog_adr0, last_prog_data0,
                       last_prog_adr1, last_prog_data1,
                       last_prog_adr2, last_prog_data2,
                       last_prog_adr3, last_prog_data3,
                       dut.dbg_fault_prog_adr0, dut.dbg_fault_prog_data0,
                       dut.dbg_fault_prog_adr1, dut.dbg_fault_prog_data1,
                       dut.dbg_fault_prog_adr2, dut.dbg_fault_prog_data2,
                       dut.dbg_fault_prog_adr3, dut.dbg_fault_prog_data3);
            end
            if (TRAP_EXCEPTION_MARKERS && coretest_started && !saw_fail && dut.uart_data == 8'h58) begin
                $fatal(1, "unexpected recover mismatch marker adr=0x%08x fc=%b rw=%b ipln=%b avecn=%b rec=%08x %08x %08x %08x %08x %08x %08x prog=%08x/%08x %08x/%08x %08x/%08x %08x/%08x",
                       dut.cpu_adr, dut.cpu_fc, dut.cpu_rw_n,
                       sim_ipln, sim_avecn,
                       ram_word(EXC_REC_BASE + 32'h00),
                       ram_word(EXC_REC_BASE + 32'h04),
                       ram_word(EXC_REC_BASE + 32'h08),
                       ram_word(EXC_REC_BASE + 32'h0c),
                       ram_word(EXC_REC_BASE + 32'h10),
                       ram_word(EXC_REC_BASE + 32'h1c),
                       ram_word(EXC_REC_BASE + 32'h20),
                       last_prog_adr0, last_prog_data0,
                       last_prog_adr1, last_prog_data1,
                       last_prog_adr2, last_prog_data2,
                       last_prog_adr3, last_prog_data3);
            end
            if (next_shift[13*8-1:0] == PASS_SIG) begin
                $display("\n*** CORETEST PASS detected ***");
                $finish;
            end
            if (next_shift[13*8-1:0] == FAIL_SIG) begin
                saw_fail <= 1'b1;
            end
            if (saw_fail && dut.uart_data == 8'h0a) begin
                $display("*** CORETEST FAIL detected ***");
                $fatal(1, "coretest fail adr=0x%08x fc=%b rw=%b tgpc=%08x tgtrap=%08x tgvbr=%08x rec=%08x %08x %08x %08x %08x",
                       dut.cpu_adr, dut.cpu_fc, dut.cpu_rw_n,
                       dut.tg_dbg_status,
                       dut.tg_dbg_imm,
                       dut.tg_dbg_arin,
                       ram_word(EXC_REC_BASE + 32'h00),
                       ram_word(EXC_REC_BASE + 32'h04),
                       ram_word(EXC_REC_BASE + 32'h08),
                       ram_word(EXC_REC_BASE + 32'h0c),
                       ram_word(EXC_REC_BASE + 32'h10));
            end
        end
    end

    always @(posedge dut.clk) begin
        if (!rstn || dut.rst) begin
            berr_setup_trace_left <= 0;
        end else if (DEBUG_IRQ) begin
            if (dut.uart_start &&
                (dut.uart_data == 8'h6b || dut.uart_data == 8'h6a ||
                 dut.uart_data == 8'h6c)) begin
                berr_setup_trace_left <= 512;
                $display("[%0t] BERR_SETUP_TRACE armed marker=%c pc=%08x adr=%08x",
                         $time, dut.uart_data, dut.tg_dbg_status,
                         dut.cpu_adr);
                if (dut.uart_data == 8'h6a) begin
                    $display("[%0t] RTE_REGS sp_before=%08x a6_before=%08x sp_after=%08x a6_after=%08x frame_sp=%08x",
                             $time,
                             ram_word(EXC_REC_BASE + 32'h40),
                             ram_word(EXC_REC_BASE + 32'h44),
                             ram_word(EXC_REC_BASE + 32'h48),
                             ram_word(EXC_REC_BASE + 32'h4c),
                             ram_word(EXC_REC_BASE + 32'h10));
                end
            end else if (berr_setup_trace_left > 0) begin
                berr_setup_trace_left <= berr_setup_trace_left - 1;
            end

            if (berr_setup_trace_left > 0 &&
                (dut.bus_read_stb || dut.bus_write_stb)) begin
                $display("[%0t] BERR_SETUP adr=%08x rd=%b wr=%b asn=%b rwn=%b fc=%b siz=%b be=%b dsack=%b berr=%b pc=%08x micro=%0d next=%0d state=%b setstate=%b dec=%b setopc=%b end=%b nextpass=%b wb=%b rot=%b halted=%b",
                         $time, dut.cpu_adr, dut.bus_read_stb,
                         dut.bus_write_stb, dut.cpu_as_n, dut.cpu_rw_n,
                         dut.cpu_fc, dut.cpu_siz, dut.be, dut.cpu_dsack_n,
                         sim_berrn, dut.tg_dbg_status,
                         dut.g_tg68k_enabled.tg_cpu.u_cpu.micro_state,
                         dut.g_tg68k_enabled.tg_cpu.u_cpu.next_micro_state,
                         dut.g_tg68k_enabled.tg_cpu.u_cpu.state,
                         dut.g_tg68k_enabled.tg_cpu.u_cpu.setstate,
                         dut.g_tg68k_enabled.tg_cpu.u_cpu.decodeopc,
                         dut.g_tg68k_enabled.tg_cpu.u_cpu.setopcode,
                         dut.g_tg68k_enabled.tg_cpu.u_cpu.setendopc,
                         dut.g_tg68k_enabled.tg_cpu.u_cpu.setnextpass,
                         dut.g_tg68k_enabled.tg_cpu.u_cpu.exec_write_back,
                         dut.g_tg68k_enabled.tg_cpu.u_cpu.set_rot_cnt,
                         dut.g_tg68k_enabled.tg_cpu.u_cpu.cpu_halted);
            end
            if (berr_setup_trace_left == 1) begin
                $display("[%0t] BERR_SETUP_TRACE done adr=%08x asn=%b dsack=%b berr=%b pc=%08x micro=%x",
                         $time, dut.cpu_adr, dut.cpu_as_n,
                         dut.cpu_dsack_n, sim_berrn, dut.tg_dbg_status,
                         dut.tg_dbg_imm[16:13]);
            end
        end
    end

    always @(posedge dut.clk) begin
        if (!rstn || dut.rst) begin
            pmmu_trace_armed <= 1'b0;
            pmmu_wait_cycles <= 0;
            pmmu_trace_count <= 0;
        end else if (DEBUG_PMMU) begin
            if (dut.uart_start && {shift[23:0], dut.uart_data} == "0tce") begin
                pmmu_trace_armed <= 1'b1;
                pmmu_wait_cycles <= 0;
                pmmu_trace_count <= 0;
                $display("\n[%0t] PMMU TRACE armed pc=%08x imm=%08x arin=%08x",
                         $time, dut.tg_dbg_status, dut.tg_dbg_imm, dut.tg_dbg_arin);
            end else if (pmmu_trace_armed) begin
                pmmu_wait_cycles <= pmmu_wait_cycles + 1;
                if (dut.bus_write_stb
                    && {dut.cpu_adr[31:2], 2'b00} == 32'h01ffa000) begin
                    $display("[%0t] PMMU WP TARGET WRITE adr=%08x siz=%b be=%b dout=%08x pc=%08x imm=%08x arin=%08x",
                             $time, dut.cpu_adr, dut.cpu_siz, dut.be,
                             dut.cpu_dout, dut.tg_dbg_status,
                             dut.tg_dbg_imm, dut.tg_dbg_arin);
                end
                if ((dut.bus_read_stb || dut.bus_write_stb) && pmmu_trace_count < 64) begin
                    pmmu_trace_count <= pmmu_trace_count + 1;
                    $display("[%0t] PMMU BUS %s adr=%08x fc=%b rw=%b siz=%b din=%08x dout=%08x pc=%08x imm=%08x arin=%08x",
                             $time, dut.bus_read_stb ? "RD" : "WR",
                             dut.cpu_adr, dut.cpu_fc, dut.cpu_rw_n, dut.cpu_siz,
                             dut.cpu_din, dut.cpu_dout, dut.tg_dbg_status,
                             dut.tg_dbg_imm, dut.tg_dbg_arin);
                end
                if (pmmu_wait_cycles == 50000) begin
                    $fatal(1, "PMMU walk timeout adr=%08x fc=%b rw=%b pc=%08x imm=%08x arin=%08x",
                           dut.cpu_adr, dut.cpu_fc, dut.cpu_rw_n,
                           dut.tg_dbg_status, dut.tg_dbg_imm, dut.tg_dbg_arin);
                end
            end
        end
    end

    always @(posedge dut.clk) begin
        if (DEBUG_BOOT && rstn && !dut.rst
            && (dut.bus_read_stb || dut.bus_write_stb)) begin
            debug_boot_cycles <= debug_boot_cycles + 1;
            $display("[%0t] BOOT %s adr=0x%08x fc=%b siz=%b be=%b din=0x%08x dout=0x%08x",
                     $time,
                     dut.bus_read_stb ? "RD" : "WR",
                     dut.cpu_adr,
                     dut.cpu_fc,
                     dut.cpu_siz,
                     dut.be,
                     dut.cpu_din,
                     dut.cpu_dout);
            if (debug_boot_cycles > 2000) begin
                $fatal(1, "DEBUG_BOOT cycle limit reached");
            end
        end
    end

    always @(posedge dut.clk) begin
        if (DEBUG_SCRATCH && (dut.bus_write_stb || dut.bus_read_stb)
            && dut.cpu_adr >= 32'h01ff9100 && dut.cpu_adr < 32'h01ff9200) begin
            $display("[%0t] SCR %s adr=0x%08x siz=%b be=%b dout=0x%08x din=0x%08x ram_q=0x%08x",
                     $time,
                     dut.bus_write_stb ? "WR" : "RD",
                     dut.cpu_adr,
                     dut.cpu_siz,
                     dut.be,
                     dut.cpu_dout,
                     dut.cpu_din,
                     dut.ram_q);
        end
    end

    reg [31:0] last_adr = 32'hx;
    integer stall = 0;
    always @(posedge dut.clk) begin
        if (^dut.cpu_adr === 1'bx) begin
            stall = 0;
        end else if (dut.cpu_adr === last_adr && !dut.cpu_as_n) begin
            stall = stall + 1;
        end else begin
            stall = 0;
            last_adr = dut.cpu_adr;
        end

        if (stall == 3000) begin
            $display("\n*** HANG: cpu_adr frozen at 0x%08x for 3000 CPU clocks ***", dut.cpu_adr);
            $display("  bs=%0d as_n=%b ds_n=%b rw_n=%b dsack_n=%b siz=%b fc=%b",
                     dut.bs, dut.cpu_as_n, dut.cpu_ds_n, dut.cpu_rw_n, dut.dsack_n, dut.cpu_siz, dut.cpu_fc);
            $display("  sel_rom=%b sel_ram=%b sel_uart=%b cpu_din=0x%08x ram_q=0x%08x rom_q=0x%08x",
                     dut.sel_rom, dut.sel_ram, dut.sel_uart, dut.cpu_din, dut.ram_q, dut.rom_q);
            $display("  data_en=%b dout=0x%08x waitc=%0d read_stb=%b write_stb=%b be=%b",
                     dut.cpu_data_en, dut.cpu_dout, dut.waitc, dut.bus_read_stb, dut.bus_write_stb, dut.be);
            $fatal(1);
        end
    end

    initial begin
        #3_000_000_000;
        $display("\n*** TIMEOUT: no CORETEST PASS/FAIL seen ***");
        $fatal(1);
    end
endmodule
