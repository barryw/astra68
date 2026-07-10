// Astra 68 coretest SoC simulation gate.
`timescale 1ns/1ps

module tb_coretest;
    localparam DEBUG_SCRATCH = 1'b0;
    localparam DEBUG_MOVE_11BC = 1'b0;
    localparam DEBUG_BF = 1'b0;
    localparam DEBUG_CAS = 1'b0;
    localparam DEBUG_EXC = 1'b0;
    localparam DEBUG_IRQ = 1'b0;

    reg clk25 = 0;
    reg rstn = 0;
    wire tx;
    wire [7:0] leds;
    localparam IRQ_REQ_ADDR = 32'h01ff9600;
    localparam BERR_REQ_ADDR = 32'h01ff9604;
    localparam BERR_TARGET_ADDR = 32'h01ff9608;
    localparam MOVES_FC_READ_ADDR = 32'h01ffad00;
    localparam MOVES_FC_WRITE_ADDR = 32'h01ffad04;
    localparam ATOMIC_RMC_TARGET_ADDR = 32'h01ffae00;
    localparam ATOMIC_RMC_ARM_ADDR = 32'h01ffae04;
    reg [2:0] sim_ipln = 3'b111;
    reg sim_avecn = 1'b1;
    reg sim_berrn = 1'b1;
    reg sim_berr_arm = 1'b0;
    reg atomic_rmc_arm = 1'b0;
    reg atomic_rmc_seen_read = 1'b0;
    reg atomic_rmc_seen_write = 1'b0;

    astra_soc #(.RST_MAX(16'd16)) dut (
        .clk25_mhz(clk25),
        .reset_n(rstn),
        .ftdi_rxd(tx),
        .ftdi_txd(1'b1),
        .leds(leds)
`ifdef ASTRA_SOC_SIM_IRQ
        , .sim_ipln(sim_ipln)
        , .sim_avecn(sim_avecn)
        , .sim_berrn(sim_berrn)
`endif
    );

    always #4 clk25 = ~clk25;

    initial begin
        rstn = 0;
        repeat (40) @(posedge clk25);
        rstn = 1;
    end

    always @(posedge dut.clk) begin
        if (!rstn || dut.rst) begin
            sim_ipln <= 3'b111;
            sim_avecn <= 1'b1;
            sim_berrn <= 1'b1;
            sim_berr_arm <= 1'b0;
        end else if (dut.bus_write_stb && dut.cpu_adr == IRQ_REQ_ADDR) begin
            if (dut.cpu_dout[2:0] == 3'd0) begin
                sim_ipln <= 3'b111;
                sim_avecn <= 1'b1;
            end else begin
                sim_ipln <= ~dut.cpu_dout[2:0];
                sim_avecn <= 1'b0;
            end
        end else begin
            if (dut.cpu_as_n) begin
                sim_berrn <= 1'b1;
            end
            if (dut.bus_write_stb && dut.cpu_adr == BERR_REQ_ADDR) begin
                sim_berr_arm <= dut.cpu_dout[0];
            end
            if (sim_berr_arm && !dut.cpu_as_n && dut.cpu_adr == BERR_TARGET_ADDR) begin
                sim_berrn <= 1'b0;
                sim_berr_arm <= 1'b0;
            end
        end
    end

    always @(posedge dut.clk) begin
        if (rstn && !dut.rst) begin
            if (dut.bus_read_stb && dut.cpu_adr == MOVES_FC_READ_ADDR
                && dut.cpu_fc !== 3'b010) begin
                $fatal(1, "MOVES SFC probe expected FC=010, got %b", dut.cpu_fc);
            end
            if (dut.bus_write_stb && dut.cpu_adr == MOVES_FC_WRITE_ADDR
                && dut.cpu_fc !== 3'b001) begin
                $fatal(1, "MOVES DFC probe expected FC=001, got %b", dut.cpu_fc);
            end
        end
    end

    always @(posedge dut.clk) begin
        if (!rstn || dut.rst) begin
            atomic_rmc_arm <= 1'b0;
            atomic_rmc_seen_read <= 1'b0;
            atomic_rmc_seen_write <= 1'b0;
        end else begin
            if (atomic_rmc_arm && dut.bus_read_stb
                && dut.cpu_adr == ATOMIC_RMC_TARGET_ADDR) begin
                atomic_rmc_seen_read <= 1'b1;
                if (dut.cpu.u_cpu.rmc !== 1'b1) begin
                    $fatal(1, "CAS RMC probe expected read RMC=1");
                end
            end
            if (atomic_rmc_arm && dut.bus_write_stb
                && dut.cpu_adr == ATOMIC_RMC_TARGET_ADDR) begin
                atomic_rmc_seen_write <= 1'b1;
                if (dut.cpu.u_cpu.rmc !== 1'b1) begin
                    $fatal(1, "CAS RMC probe expected write RMC=1");
                end
            end
            if (dut.bus_write_stb && dut.cpu_adr == ATOMIC_RMC_ARM_ADDR) begin
                if (dut.cpu_dout[0]) begin
                    atomic_rmc_arm <= 1'b1;
                    atomic_rmc_seen_read <= 1'b0;
                    atomic_rmc_seen_write <= 1'b0;
                end else begin
                    if (!atomic_rmc_seen_read) begin
                        $fatal(1, "CAS RMC probe did not observe read cycle");
                    end
                    if (!atomic_rmc_seen_write) begin
                        $fatal(1, "CAS RMC probe did not observe write cycle");
                    end
                    atomic_rmc_arm <= 1'b0;
                end
            end
        end
    end

    integer irq_trace_left = 0;
    reg [4:0] last_ex_state = 5'hxx;
    always @(posedge dut.clk) begin
        if (DEBUG_IRQ && dut.bus_write_stb && dut.cpu_adr == IRQ_REQ_ADDR) begin
            irq_trace_left <= 5000;
            $display("[%0t] IRQ_REQ dout=0x%08x sr=%04x ipln=%b avecn=%b",
                     $time, dut.cpu_dout, dut.cpu.u_cpu.i_alu.status_reg,
                     sim_ipln, sim_avecn);
        end else if (irq_trace_left > 0) begin
            irq_trace_left <= irq_trace_left - 1;
        end

        if (DEBUG_IRQ && irq_trace_left > 0
            && dut.cpu.u_cpu.i_exc_handler.ex_state !== last_ex_state) begin
            last_ex_state <= dut.cpu.u_cpu.i_exc_handler.ex_state;
            $display("[%0t] IRQ_EX state=%0d next=%0d exc=%0d ex_p_int=%b irq=%b pend=%b sr=%04x ipln=%b avecn=%b",
                     $time,
                     dut.cpu.u_cpu.i_exc_handler.ex_state,
                     dut.cpu.u_cpu.i_exc_handler.next_ex_state,
                     dut.cpu.u_cpu.i_exc_handler.exception,
                     dut.cpu.u_cpu.i_exc_handler.ex_p_int,
                     dut.cpu.u_cpu.i_exc_handler.irq,
                     dut.cpu.u_cpu.i_exc_handler.irq_pend_i,
                     dut.cpu.u_cpu.i_alu.status_reg,
                     sim_ipln,
                     sim_avecn);
        end

        if (DEBUG_IRQ && irq_trace_left > 0
            && (dut.bus_read_stb || dut.bus_write_stb || dut.cpu_fc == 3'b111
                || (dut.cpu_adr >= 32'h01ff9560 && dut.cpu_adr < 32'h01ff9578)
                || (dut.cpu_adr >= 32'h01ff9270 && dut.cpu_adr < 32'h01ff9290))) begin
            $display("[%0t] IRQ_BUS adr=0x%08x rd=%b wr=%b asn=%b rwn=%b fc=%b siz=%b din=0x%08x dout=0x%08x rdy=%b valid=%b sr=%04x",
                     $time,
                     dut.cpu_adr,
                     dut.bus_read_stb,
                     dut.bus_write_stb,
                     dut.cpu_as_n,
                     dut.cpu_rw_n,
                     dut.cpu_fc,
                     dut.cpu_siz,
                     dut.cpu_din,
                     dut.cpu_dout,
                     dut.cpu.u_cpu.data_rdy,
                     dut.cpu.u_cpu.data_valid,
                     dut.cpu.u_cpu.i_alu.status_reg);
        end

        if (DEBUG_IRQ && irq_trace_left > 0
            && (dut.cpu.u_cpu.pc_load || dut.cpu.u_cpu.pc_load_exh
                || dut.cpu.u_cpu.i_exc_handler.ex_state == 5'd8
                || dut.cpu.u_cpu.i_exc_handler.ex_state == 5'd11)) begin
            $display("[%0t] IRQ_PC pc=0x%08x pc_load=%b pc_load_exh=%b ar_in_1=0x%08x data_to_core=0x%08x data_in=0x%08x state=%0d next=%0d",
                     $time,
                     dut.cpu.u_cpu.pc,
                     dut.cpu.u_cpu.pc_load,
                     dut.cpu.u_cpu.pc_load_exh,
                     dut.cpu.u_cpu.ar_in_1,
                     dut.cpu.u_cpu.data_to_core,
                     dut.cpu.u_cpu.data_in,
                     dut.cpu.u_cpu.i_exc_handler.ex_state,
                     dut.cpu.u_cpu.i_exc_handler.next_ex_state);
        end
    end

    reg [127:0] shift = 128'd0;
    reg [127:0] next_shift;
    reg saw_fail = 1'b0;
    localparam [13*8-1:0] PASS_SIG = "CORETEST PASS";
    localparam [13*8-1:0] FAIL_SIG = "CORETEST FAIL";

    always @(posedge dut.clk) begin
        if (dut.uart_start) begin
            $write("%c", dut.uart_data);
            next_shift = {shift[119:0], dut.uart_data};
            shift <= next_shift;
            if (next_shift[13*8-1:0] == PASS_SIG) begin
                $display("\n*** CORETEST PASS detected ***");
                $finish;
            end
            if (next_shift[13*8-1:0] == FAIL_SIG) begin
                saw_fail <= 1'b1;
            end
            if (saw_fail && dut.uart_data == 8'h0a) begin
                $display("*** CORETEST FAIL detected ***");
                $fatal(1);
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

    always @(posedge dut.clk) begin
        if (DEBUG_EXC && (dut.bus_write_stb || dut.bus_read_stb)
            && dut.cpu_adr >= 32'h01ff9270 && dut.cpu_adr < 32'h01ff9290) begin
            $display("[%0t] EXC %s adr=0x%08x siz=%b be=%b dout=0x%08x din=0x%08x ram_q=0x%08x sr=%04x biw0=%04x fc=%b",
                     $time,
                     dut.bus_write_stb ? "WR" : "RD",
                     dut.cpu_adr,
                     dut.cpu_siz,
                     dut.be,
                     dut.cpu_dout,
                     dut.cpu_din,
                     dut.ram_q,
                     dut.cpu.u_cpu.i_alu.status_reg,
                     dut.cpu.u_cpu.biw_0,
                     dut.cpu_fc);
        end
    end

    reg trace_bf = 1'b0;
    integer trace_bf_left = 0;
    wire trace_bf_hit =
        (dut.cpu.u_cpu.biw_0 == 16'he9f9) ||
        (dut.cpu.u_cpu.biw_0 == 16'heff9) ||
        (dut.bus_read_stb && dut.cpu_adr >= 32'hffe012d8 && dut.cpu_adr < 32'hffe012e8) ||
        (dut.bus_read_stb && dut.cpu_adr >= 32'hffe01300 && dut.cpu_adr < 32'hffe01320) ||
        (dut.bus_read_stb && dut.cpu_adr == 32'h01ff9230) ||
        (dut.bus_read_stb && dut.cpu_adr == 32'h01ff9231) ||
        (dut.bus_read_stb && dut.cpu_adr == 32'h01ff9232) ||
        (dut.bus_write_stb && dut.cpu_adr == 32'h01ff9230) ||
        (dut.bus_write_stb && dut.cpu_adr == 32'h01ff9231) ||
        (dut.bus_write_stb && dut.cpu_adr == 32'h01ff9232) ||
        (dut.bus_write_stb && dut.cpu_adr == 32'h01ff9234);

    always @(posedge dut.clk) begin
        if (DEBUG_BF && trace_bf_hit) begin
            trace_bf <= 1'b1;
            trace_bf_left <= 120;
        end else if (trace_bf_left > 0) begin
            trace_bf_left <= trace_bf_left - 1;
        end else begin
            trace_bf <= 1'b0;
        end

        if (DEBUG_BF && (trace_bf || trace_bf_hit)) begin
            $display("[%0t] BF adr=0x%08x rd=%b wr=%b siz=%b din=0x%08x dout=0x%08x ram_q=0x%08x fs=%0d nfs=%0d ex=%0d nex=%0d op=%0h biw0=%04x biw1=%04x alu_i=%b req=%b ack=%b bsy=%b bf_bytes=%0d bf_ctl_hilo=%b hilo=%b bf_off=%0d bf_w=%0d lower=%0d upper=%0d op2=0x%08x op3=0x%08x bf_data=0x%010x bf_res=0x%010x alu_res=0x%016x",
                     $time,
                     dut.cpu_adr,
                     dut.bus_read_stb,
                     dut.bus_write_stb,
                     dut.cpu_siz,
                     dut.cpu_din,
                     dut.cpu_dout,
                     dut.ram_q,
                     dut.cpu.u_cpu.i_control.fetch_state,
                     dut.cpu.u_cpu.i_control.next_fetch_state,
                     dut.cpu.u_cpu.i_control.exec_wb_state,
                     dut.cpu.u_cpu.i_control.next_exec_wb_state,
                     dut.cpu.u_cpu.op,
                     dut.cpu.u_cpu.biw_0,
                     dut.cpu.u_cpu.biw_1,
                     dut.cpu.u_cpu.i_control.alu_init,
                     dut.cpu.u_cpu.alu_req,
                     dut.cpu.u_cpu.i_control.alu_ack,
                     dut.cpu.u_cpu.alu_bsy,
                     dut.cpu.u_cpu.i_control.bf_bytes,
                     dut.cpu.u_cpu.i_control.bf_hilon,
                     dut.cpu.u_cpu.hilon,
                     dut.cpu.u_cpu.bf_offset,
                     dut.cpu.u_cpu.bf_width,
                     dut.cpu.u_cpu.i_alu.bf_lower_bnd,
                     dut.cpu.u_cpu.i_alu.bf_upper_bnd,
                     dut.cpu.u_cpu.i_alu.op2,
                     dut.cpu.u_cpu.i_alu.op3,
                     dut.cpu.u_cpu.i_alu.bf_data_in,
                     dut.cpu.u_cpu.i_alu.result_bitfield,
                     dut.cpu.u_cpu.i_alu.result);
        end
    end

    reg trace_cas = 1'b0;
    integer trace_cas_left = 0;
    wire trace_cas_hit =
        (dut.cpu.u_cpu.biw_0 == 16'h0ed0) ||
        (dut.bus_read_stb && dut.cpu_adr >= 32'hffe015d0 && dut.cpu_adr < 32'hffe01670) ||
        (dut.bus_write_stb && dut.cpu_adr >= 32'h01ff9260 && dut.cpu_adr < 32'h01ff9268);

    always @(posedge dut.clk) begin
        if (DEBUG_CAS && trace_cas_hit) begin
            trace_cas <= 1'b1;
            trace_cas_left <= 180;
        end else if (trace_cas_left > 0) begin
            trace_cas_left <= trace_cas_left - 1;
        end else begin
            trace_cas <= 1'b0;
        end

        if (DEBUG_CAS && (trace_cas || trace_cas_hit)) begin
            $display("[%0t] CAS adr=0x%08x rd=%b wr=%b siz=%b be=%b din=0x%08x dout=0x%08x ram_q=0x%08x fs=%0d nfs=%0d ex=%0d nex=%0d op=%0h opwb=%0h biw0=%04x biw1=%04x alu_i=%b req=%b ack=%b bsy=%b cc=%b cond=%b xnzvc=%05b sr=%04x op1=0x%08x op2=0x%08x intop=0x%08x result=0x%016x",
                     $time,
                     dut.cpu_adr,
                     dut.bus_read_stb,
                     dut.bus_write_stb,
                     dut.cpu_siz,
                     dut.be,
                     dut.cpu_din,
                     dut.cpu_dout,
                     dut.ram_q,
                     dut.cpu.u_cpu.i_control.fetch_state,
                     dut.cpu.u_cpu.i_control.next_fetch_state,
                     dut.cpu.u_cpu.i_control.exec_wb_state,
                     dut.cpu.u_cpu.i_control.next_exec_wb_state,
                     dut.cpu.u_cpu.op,
                     dut.cpu.u_cpu.i_control.op_wb_i,
                     dut.cpu.u_cpu.biw_0,
                     dut.cpu.u_cpu.biw_1,
                     dut.cpu.u_cpu.i_control.alu_init,
                     dut.cpu.u_cpu.alu_req,
                     dut.cpu.u_cpu.i_control.alu_ack,
                     dut.cpu.u_cpu.alu_bsy,
                     dut.cpu.u_cpu.i_control.cc_updt,
                     dut.cpu.u_cpu.alu_cond,
                     dut.cpu.u_cpu.i_alu.xnzvc,
                     dut.cpu.u_cpu.i_alu.status_reg,
                     dut.cpu.u_cpu.i_alu.op1,
                     dut.cpu.u_cpu.i_alu.op2,
                     dut.cpu.u_cpu.i_alu.result_intop,
                     dut.cpu.u_cpu.i_alu.result);
        end
    end

    reg trace_ctrl = 1'b0;
    integer trace_left = 0;
    wire trace_move_11bc =
        (dut.cpu.u_cpu.biw_0 == 16'h11bc) ||
        (dut.cpu.u_cpu.i_control.biw_0_wb == 12'h1bc) ||
        (dut.bus_read_stb && dut.cpu_adr >= 32'hffe0078c && dut.cpu_adr < 32'hffe00798);

    always @(posedge dut.clk) begin
        if (DEBUG_MOVE_11BC && trace_move_11bc) begin
            trace_ctrl <= 1'b1;
            trace_left <= 220;
        end else if (trace_left > 0) begin
            trace_left <= trace_left - 1;
        end else begin
            trace_ctrl <= 1'b0;
        end

        if (DEBUG_MOVE_11BC && (trace_ctrl || trace_move_11bc)) begin
            $display("[%0t] M11BC adr=0x%08x fs=%0d nfs=%0d exec=%0d nexec=%0d op=%0h opwb=%0h biw0=%04x biw0wb=%03x phase2=%b alu_i=%b alu_req=%b alu_ack=%b alu_bsy=%b data_wr=%b data_wr_i=%b rd=%b wr=%b rdy=%b valid=%b adr_eff=0x%08x adr_wb=0x%08x mark=%b inuse=%b mode=%0d sel=%0d ext=%04x disp=%08x fmt=%b d32h=%b d32l=%b abs_h=%b abs_l=%b",
                     $time,
                     dut.cpu_adr,
                     dut.cpu.u_cpu.i_control.fetch_state,
                     dut.cpu.u_cpu.i_control.next_fetch_state,
                     dut.cpu.u_cpu.i_control.exec_wb_state,
                     dut.cpu.u_cpu.i_control.next_exec_wb_state,
                     dut.cpu.u_cpu.op,
                     dut.cpu.u_cpu.i_control.op_wb_i,
                     dut.cpu.u_cpu.biw_0,
                     dut.cpu.u_cpu.i_control.biw_0_wb,
                     dut.cpu.u_cpu.i_control.phase2,
                     dut.cpu.u_cpu.i_control.alu_init,
                     dut.cpu.u_cpu.alu_req,
                     dut.cpu.u_cpu.i_control.alu_ack,
                     dut.cpu.u_cpu.alu_bsy,
                     dut.cpu.u_cpu.i_control.data_wr,
                     dut.cpu.u_cpu.i_control.data_wr_i,
                     dut.cpu.u_cpu.i_control.read_cycle,
                     dut.cpu.u_cpu.i_control.write_cycle,
                     dut.cpu.u_cpu.data_rdy,
                     dut.cpu.u_cpu.data_valid,
                     dut.cpu.u_cpu.adr_eff,
                     dut.cpu.u_cpu.adr_eff_wb,
                     dut.cpu.u_cpu.adr_mark_used,
                     dut.cpu.u_cpu.adr_in_use,
                     dut.cpu.u_cpu.adr_mode,
                     dut.cpu.u_cpu.amode_sel,
                     dut.cpu.u_cpu.ext_word,
                     dut.cpu.u_cpu.displacement,
                     dut.cpu.u_cpu.store_adr_format,
                     dut.cpu.u_cpu.store_d32_hi,
                     dut.cpu.u_cpu.store_d32_lo,
                     dut.cpu.u_cpu.store_abs_hi,
                     dut.cpu.u_cpu.store_abs_lo);
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
        #800_000_000;
        $display("\n*** TIMEOUT: no CORETEST PASS/FAIL seen ***");
        $fatal(1);
    end
endmodule
