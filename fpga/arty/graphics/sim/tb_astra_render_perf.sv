// Render engine cycle accounting under a DDR-like latency model.
// Not part of the functional suite: it prints where the cycles go.
// Run with fpga/arty/graphics/perf/run_perf.sh.
`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module tb_astra_render_perf;
    parameter integer READ_LATENCY = 25;
    parameter integer WRITE_LATENCY = 25;
    parameter integer HOST_READ_LATENCY = 60;
    localparam [31:0] ARENA_BYTES = 32'h00800000;
    // The host aperture follows the Media RAM model: arena offsets
    // [ARENA_BYTES, 2 * ARENA_BYTES) read HOST_PHYS memory once +host=1
    // programs the router. Batch data (ring, descriptors, ARGB source,
    // vertices, records) then moves there, as a DE25 batch does.
    localparam [31:0] HOST_PHYS = 32'h80000000;
    reg [31:0] host_shift = 32'd0;
    wire [31:0] SUBMISSION_OFFSET = 32'h00000000 + host_shift;
    localparam [31:0] COMPLETION_OFFSET = 32'h00010000;
    wire [31:0] DST_DESC = 32'h00020000 + host_shift;
    wire [31:0] SRC_DESC = 32'h00020020 + host_shift;
    wire [31:0] ARGB_DESC = 32'h00020040 + host_shift;
    wire [31:0] ARGB_DATA = 32'h00400000 + host_shift;
    localparam [31:0] DST_DATA = 32'h00100000;
    localparam [31:0] SRC_DATA = 32'h00200000;
    localparam [31:0] GENERATION = 32'h12345678;
    localparam integer W = 640, H = 480;

    reg clk = 1'b0;
    reg reset = 1'b1;
    always #2.5 clk = ~clk;

    reg [10:0] submission_producer = 11'd0;
    wire [10:0] submission_consumer, completion_producer;
    wire busy, completion_irq, engine_reset_active, configuration_fault;
    wire [31:0] retired_fence, commands_submitted, commands_completed,
        commands_failed, backpressure_cycles, timeout_count, reset_count,
        last_fault_detail;
    wire [5:0] arid, rid, awid, bid;
    wire [31:0] araddr, awaddr;
    wire [7:0] arlen, awlen, wstrb;
    wire [2:0] arsize, awsize, arprot, awprot;
    wire [1:0] arburst, awburst, rresp, bresp;
    wire [3:0] arcache, arqos, awcache, awqos;
    wire arvalid, arready, rlast, rvalid, rready, awvalid, awready;
    wire [63:0] rdata, wdata;
    wire wlast, wvalid, wready, bvalid, bready;
    wire [31:0] read_transactions, write_transactions, read_beats,
        write_beats;

    astra_render_command_processor #(
        .ARENA_BASE(32'd0), .ARENA_LIMIT(ARENA_BYTES * 2),
        .CYCLES_PER_US(165), .RESET_HOLD_CYCLES(4)
    ) dut (
        .clk(clk), .reset(reset), .enable(1'b1),
        .queue_rebase(1'b0), .soft_reset(1'b0),
        .submission_ring_offset(SUBMISSION_OFFSET),
        .submission_producer(submission_producer),
        .submission_consumer(submission_consumer),
        .completion_ring_offset(COMPLETION_OFFSET),
        .completion_producer(completion_producer),
        .completion_consumer(completion_producer),
        .resource_generation(GENERATION),
        .protected0_valid(1'b0), .protected0_offset(32'd0),
        .protected0_bytes(32'd0), .protected1_valid(1'b0),
        .protected1_offset(32'd0), .protected1_bytes(32'd0),
        .busy(busy), .completion_irq(completion_irq),
        .engine_reset_active(engine_reset_active),
        .configuration_fault(configuration_fault),
        .retired_fence(retired_fence),
        .commands_submitted(commands_submitted),
        .commands_completed(commands_completed),
        .commands_failed(commands_failed),
        .backpressure_cycles(backpressure_cycles),
        .timeout_count(timeout_count), .reset_count(reset_count),
        .last_fault_detail(last_fault_detail),
        .m_axi_arid(arid), .m_axi_araddr(araddr), .m_axi_arlen(arlen),
        .m_axi_arsize(arsize), .m_axi_arburst(arburst),
        .m_axi_arcache(arcache), .m_axi_arprot(arprot),
        .m_axi_arqos(arqos), .m_axi_arvalid(arvalid),
        .m_axi_arready(arready), .m_axi_rid(rid), .m_axi_rdata(rdata),
        .m_axi_rresp(rresp), .m_axi_rlast(rlast),
        .m_axi_rvalid(rvalid), .m_axi_rready(rready),
        .m_axi_awid(awid), .m_axi_awaddr(awaddr), .m_axi_awlen(awlen),
        .m_axi_awsize(awsize), .m_axi_awburst(awburst),
        .m_axi_awcache(awcache), .m_axi_awprot(awprot),
        .m_axi_awqos(awqos), .m_axi_awvalid(awvalid),
        .m_axi_awready(awready), .m_axi_wdata(wdata),
        .m_axi_wstrb(wstrb), .m_axi_wlast(wlast),
        .m_axi_wvalid(wvalid), .m_axi_wready(wready),
        .m_axi_bid(bid), .m_axi_bresp(bresp), .m_axi_bvalid(bvalid),
        .m_axi_bready(bready)
    );

    wire [5:0] m_arid, m_rid, h_arid, h_rid;
    wire [31:0] m_araddr, h_araddr;
    wire [7:0] m_arlen, h_arlen;
    wire [1:0] m_rresp, h_rresp;
    wire m_arvalid, m_arready, m_rlast, m_rvalid, m_rready;
    wire h_arvalid, h_arready, h_rlast, h_rvalid, h_rready;
    wire [63:0] m_rdata, h_rdata;
    astra_render_host_reads #(.ARENA_BASE(32'd0), .AXI_ID_WIDTH(6)) host_reads_i (
        .clk(clk), .reset(reset),
        .host_base(host_shift != 0 ? HOST_PHYS : 32'd0),
        .s_arid(arid), .s_araddr(araddr), .s_arlen(arlen),
        .s_arsize(arsize), .s_arburst(arburst), .s_arcache(arcache),
        .s_arprot(arprot), .s_arvalid(arvalid), .s_arready(arready),
        .s_rid(rid), .s_rdata(rdata), .s_rresp(rresp), .s_rlast(rlast),
        .s_rvalid(rvalid), .s_rready(rready),
        .m_arid(m_arid), .m_araddr(m_araddr), .m_arlen(m_arlen),
        .m_arsize(), .m_arburst(), .m_arcache(), .m_arprot(),
        .m_arvalid(m_arvalid), .m_arready(m_arready), .m_rid(m_rid),
        .m_rdata(m_rdata), .m_rresp(m_rresp), .m_rlast(m_rlast),
        .m_rvalid(m_rvalid), .m_rready(m_rready),
        .h_arid(h_arid), .h_araddr(h_araddr), .h_arlen(h_arlen),
        .h_arsize(), .h_arburst(), .h_arcache(), .h_arprot(),
        .h_arvalid(h_arvalid), .h_arready(h_arready), .h_rid(h_rid),
        .h_rdata(h_rdata), .h_rresp(h_rresp), .h_rlast(h_rlast),
        .h_rvalid(h_rvalid), .h_rready(h_rready)
    );
    astra_render_axi_latency_model #(
        .MEMORY_BYTES(ARENA_BYTES), .BASE_ADDRESS(HOST_PHYS),
        .READ_LATENCY(HOST_READ_LATENCY)
    ) host_i (
        .clk(clk), .reset(reset),
        .s_axi_arid(h_arid), .s_axi_araddr(h_araddr), .s_axi_arlen(h_arlen),
        .s_axi_arvalid(h_arvalid), .s_axi_arready(h_arready),
        .s_axi_rid(h_rid), .s_axi_rdata(h_rdata), .s_axi_rresp(h_rresp),
        .s_axi_rlast(h_rlast), .s_axi_rvalid(h_rvalid),
        .s_axi_rready(h_rready),
        .s_axi_awid(6'd0), .s_axi_awaddr(32'd0), .s_axi_awlen(8'd0),
        .s_axi_awvalid(1'b0), .s_axi_awready(),
        .s_axi_wdata(64'd0), .s_axi_wstrb(8'd0), .s_axi_wlast(1'b0),
        .s_axi_wvalid(1'b0), .s_axi_wready(), .s_axi_bid(), .s_axi_bresp(),
        .s_axi_bvalid(), .s_axi_bready(1'b1),
        .read_transactions(), .write_transactions(), .read_beats(),
        .write_beats()
    );

    astra_render_axi_latency_model #(
        .MEMORY_BYTES(ARENA_BYTES), .READ_LATENCY(READ_LATENCY),
        .WRITE_LATENCY(WRITE_LATENCY)
    ) memory_i (
        .clk(clk), .reset(reset),
        .s_axi_arid(m_arid), .s_axi_araddr(m_araddr), .s_axi_arlen(m_arlen),
        .s_axi_arvalid(m_arvalid), .s_axi_arready(m_arready),
        .s_axi_rid(m_rid), .s_axi_rdata(m_rdata), .s_axi_rresp(m_rresp),
        .s_axi_rlast(m_rlast), .s_axi_rvalid(m_rvalid),
        .s_axi_rready(m_rready),
        .s_axi_awid(awid), .s_axi_awaddr(awaddr), .s_axi_awlen(awlen),
        .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(wstrb), .s_axi_wlast(wlast),
        .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bid(bid), .s_axi_bresp(bresp), .s_axi_bvalid(bvalid),
        .s_axi_bready(bready),
        .read_transactions(read_transactions),
        .write_transactions(write_transactions),
        .read_beats(read_beats), .write_beats(write_beats)
    );

    // ---- accounting -------------------------------------------------
    reg measuring = 1'b0;
    integer cp_hist [0:63];
    integer bl_hist [0:127];
    integer fc_hist [0:31];
    integer geo_hist [0:24];
    integer tx_hist [0:127];
    integer cycles, ar_busy, r_busy, aw_busy, w_busy, b_wait;
    integer i, geo_index;
    task automatic clear_hist;
        begin
            for (i = 0; i < 64; i = i + 1) cp_hist[i] = 0;
            for (i = 0; i < 128; i = i + 1) bl_hist[i] = 0;
            for (i = 0; i < 32; i = i + 1) fc_hist[i] = 0;
            for (i = 0; i < 25; i = i + 1) geo_hist[i] = 0;
            for (i = 0; i < 128; i = i + 1) tx_hist[i] = 0;
            cycles = 0; ar_busy = 0; r_busy = 0; aw_busy = 0; w_busy = 0;
        end
    endtask
    always @(posedge clk) if (measuring) begin
        cycles = cycles + 1;
        cp_hist[dut.state] = cp_hist[dut.state] + 1;
        if (dut.command_dispatched_q) begin
            bl_hist[dut.blitter_i.state] = bl_hist[dut.blitter_i.state] + 1;
            fc_hist[{dut.blitter_i.fast_copy_i.write_state,
                     dut.blitter_i.fast_copy_i.plan_state[2:0]}] =
                fc_hist[{dut.blitter_i.fast_copy_i.write_state,
                         dut.blitter_i.fast_copy_i.plan_state[2:0]}] + 1;
            geo_index = 24;
            for (i = 0; i < 24; i = i + 1)
                if (dut.geometry_i.state[i]) geo_index = i;
            geo_hist[geo_index] = geo_hist[geo_index] + 1;
            tx_hist[dut.texture_i.state] = tx_hist[dut.texture_i.state] + 1;
        end
        if (arvalid && arready) ar_busy = ar_busy + 1;
        if (rvalid && rready) r_busy = r_busy + 1;
        if (awvalid && awready) aw_busy = aw_busy + 1;
        if (wvalid && wready) w_busy = w_busy + 1;
    end

    task automatic report(input [8*24-1:0] name, input integer units);
        begin
            $display("== %0s: %0d cycles, %0d commands/px -> %0d.%02d cycles each",
                     name, cycles, units, cycles / units,
                     (cycles % units) * 100 / units);
            $display("   AR %0d  R beats %0d  AW %0d  W beats %0d",
                     ar_busy, r_busy, aw_busy, w_busy);
            $write("   CP states:");
            for (i = 0; i < 64; i = i + 1)
                if (cp_hist[i] != 0) $write(" %0d:%0d", i, cp_hist[i]);
            $write("\n   blitter (dispatched):");
            for (i = 0; i < 128; i = i + 1)
                if (bl_hist[i] != 0) $write(" %0d:%0d", i, bl_hist[i]);
            $write("\n   fast copy (dispatched):");
            for (i = 0; i < 32; i = i + 1)
                if (fc_hist[i] != 0) $write(" %0d:%0d", i, fc_hist[i]);
            $write("\n   texture (dispatched):");
            for (i = 1; i < 128; i = i + 1)
                if (tx_hist[i] != 0) $write(" %0d:%0d", i, tx_hist[i]);
            $write("\n   geometry bit (dispatched, 24=none):");
            for (i = 0; i < 25; i = i + 1)
                if (geo_hist[i] != 0) $write(" %0d:%0d", i, geo_hist[i]);
            $write("\n");
        end
    endtask

    reg [5:0] prev_state = 0;
    always @(posedge clk) begin
        if (dut.state == 6'd22 && prev_state != 6'd22)
            $display("FATAL entered from %0d at %0t bid=%0d bresp=%0d rlast=%0d", prev_state, $time, bid, bresp, rlast);
        prev_state <= dut.state;
        if (0) begin
            if (awvalid && awready) $display("%0t AW id=%0d a=%h len=%0d st=%0d wb=%0d", $time, awid, awaddr, awlen, dut.state, dut.writer_busy);
            if (wvalid && wready) $display("%0t W last=%0d st=%0d wb=%0d", $time, wlast, dut.state, dut.writer_busy);
            if (bvalid && bready) $display("%0t B id=%0d st=%0d wb=%0d", $time, bid, dut.state, dut.writer_busy);
            if (bvalid && !bready) $display("%0t B pending id=%0d st=%0d wb=%0d", $time, bid, dut.state, dut.writer_busy);
        end
    end
    // ---- command helpers ------------------------------------------------
    // Batch data at or above ARENA_BYTES is host memory (writes are only
    // ever issued to Media RAM, so the engine never writes there).
    task automatic be32(input [31:0] a, input [31:0] v);
        integer k;
        begin
            for (k = 0; k < 4; k = k + 1)
                if (a >= ARENA_BYTES)
                    host_i.write_byte(HOST_PHYS + a - ARENA_BYTES + k,
                                      v[31 - k * 8 -: 8]);
                else
                    memory_i.write_byte(a + k, v[31 - k * 8 -: 8]);
        end
    endtask
    function automatic [31:0] rd32(input [31:0] a);
        integer k;
        for (k = 0; k < 4; k = k + 1)
            rd32[31 - k * 8 -: 8] = a >= ARENA_BYTES ?
                host_i.read_byte(HOST_PHYS + a - ARENA_BYTES + k) :
                memory_i.read_byte(a + k);
    endfunction
    task automatic surface(input [31:0] d, input [31:0] data,
                           input [7:0] flags);
        begin
            be32(d, (`ASTRA_RENDER_ABI_VERSION << 16) |
                 `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES);
            be32(d + 4, GENERATION);
            be32(d + 8, data);
            be32(d + 12, W * H * 2);
            be32(d + 16, W * 2);
            be32(d + 20, {16'(W), 16'(H)});
            be32(d + 24, {8'(`ASTRA_RENDER_FORMAT_RGB565), flags, 16'd0});
            be32(d + 28, 0);
        end
    endtask

    integer seq = 0;
    reg [10:0] sub = 0;
    task automatic surface_argb(input [31:0] d, input [31:0] data);
        begin
            be32(d, (`ASTRA_RENDER_ABI_VERSION << 16) |
                 `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES);
            be32(d + 4, GENERATION);
            be32(d + 8, data);
            be32(d + 12, W * H * 4);
            be32(d + 16, W * 4);
            be32(d + 20, {16'(W), 16'(H)});
            be32(d + 24, {8'(`ASTRA_RENDER_FORMAT_ARGB8888),
                          8'(`ASTRA_RENDER_SURFACE_READ), 16'd0});
            be32(d + 28, 0);
        end
    endtask

    // Reference BLIT (docs/TEXTURE_ENGINE.md section 7): straight-alpha
    // source-over into RGB565, m(x, y) = round(x * y / 255).
    function automatic [7:0] m255(input [7:0] x, input [7:0] y);
        reg [16:0] a;
        begin
            a = x * y + 17'd128;
            m255 = (a + (a >> 8)) >> 8;
        end
    endfunction
    function automatic [7:0] sat8(input [8:0] v);
        sat8 = v[8] ? 8'hff : v[7:0];
    endfunction
    function automatic [15:0] ref_argb_565(input [31:0] s, input [15:0] d,
                                           input blend, input [7:0] opacity);
        reg [7:0] a, dr, dg, db, r, g, b;
        begin
            dr = {d[15:11], d[15:13]}; dg = {d[10:5], d[10:9]};
            db = {d[4:0], d[4:2]};
            if (!blend) begin
                r = s[23:16]; g = s[15:8]; b = s[7:0];
            end else begin
                a = m255(s[31:24], opacity);
                r = sat8(m255(s[23:16], a) + m255(dr, ~a));
                g = sat8(m255(s[15:8], a) + m255(dg, ~a));
                b = sat8(m255(s[7:0], a) + m255(db, ~a));
            end
            ref_argb_565 = {r[7:3], g[7:2], b[7:3]};
        end
    endfunction
    // One W x blit_rows ARGB8888 -> RGB565 BLIT (all H rows unless
    // +blit_rows=N) from a sw x sh source (the same extent when zero,
    // nearest-neighbour otherwise), NONE or BLEND at opacity 255, and a
    // check of every 13th pixel against the reference.
    reg [15:0] blit_before [0:W*H-1];
    integer blit_rows = H;
    task automatic blit_argb(input [8*24-1:0] name, input blend,
                             input [1:0] alpha_kind, input integer sw,
                             input integer sh);
        integer k, sk;
        reg [31:0] s;
        reg [15:0] expect_px, got;
        reg [63:0] step_x, step_y;
        begin
            if (sw == 0) begin
                sw = W;
                sh = blit_rows;
            end
            step_x = (({32'd0, sw} << 24) + W - 1) / W;
            step_y = (({32'd0, sh} << 24) + blit_rows - 1) / blit_rows;
            for (k = 0; k < W * H; k = k + 1) begin
                s = k * 32'h9e3779b1;
                s[31:24] = alpha_kind == 0 ? 8'hff :
                    alpha_kind == 1 ? s[31:24] : (k % 7 == 0 ? s[31:24] : 8'hff);
                be32(ARGB_DATA + k * 4, s);
                memory_i.write_byte(DST_DATA + k * 2, 8'(k * 5));
                memory_i.write_byte(DST_DATA + k * 2 + 1, 8'(k * 11 + 1));
                blit_before[k] = {8'(k * 5), 8'(k * 11 + 1)};
            end
            header(`ASTRA_RENDER_OP_BLIT,
                   blend ? `ASTRA_RENDER_FLAG_BLIT_ALPHA : 16'd0);
            be32(slot(sub) + 36, ARGB_DESC);
            be32(slot(sub) + 44, 0);
            be32(slot(sub) + 48, 0);
            be32(slot(sub) + 52, {16'(sw), 16'(sh)});
            be32(slot(sub) + 56, {16'(W), 16'(blit_rows)});
            be32(slot(sub) + 60, blend ? 32'hff000000 : 32'd0);
            sub = sub + 1;
            run(name, W * blit_rows);
            for (k = 0; k < W * blit_rows; k = k + 13) begin
                sk = (((k / W) * step_y) >> 24) * W +
                     (((k % W) * step_x) >> 24);
                expect_px = ref_argb_565(rd32(ARGB_DATA + sk * 4),
                                         blit_before[k], blend, 8'hff);
                got = {memory_i.read_byte(DST_DATA + k * 2),
                       memory_i.read_byte(DST_DATA + k * 2 + 1)};
                if (got !== expect_px)
                    $fatal(1, "%0s pixel %0d = %04x expected %04x", name, k,
                           got, expect_px);
            end
        end
    endtask

    function automatic [31:0] slot(input [10:0] p);
        slot = SUBMISSION_OFFSET + ({21'd0, p[9:0]} << 6);
    endfunction
    task automatic header(input [15:0] op, input [15:0] flags);
        integer k;
        begin
            for (k = 0; k < 16; k = k + 1) be32(slot(sub) + k * 4, 0);
            seq = seq + 1;
            be32(slot(sub), (`ASTRA_RENDER_ABI_VERSION << 16) |
                 `ASTRA_RENDER_COMMAND_BYTES);
            be32(slot(sub) + 4, {op, flags});
            be32(slot(sub) + 8, seq);
            be32(slot(sub) + 12, GENERATION);
            be32(slot(sub) + 16, 32'd400000);
            be32(slot(sub) + 24, 0);
            be32(slot(sub) + 28, {16'(W), 16'(H)});
            be32(slot(sub) + 32, DST_DESC);
        end
    endtask
    task automatic fill(input [15:0] x, input [15:0] y, input [15:0] w,
                        input [15:0] h, input [31:0] color);
        begin
            header(`ASTRA_RENDER_OP_FILL, 0);
            be32(slot(sub) + 48, {x, y});
            be32(slot(sub) + 56, {w, h});
            be32(slot(sub) + 60, color);
            sub = sub + 1;
        end
    endtask
    task automatic copy(input [15:0] w, input [15:0] h);
        begin
            header(`ASTRA_RENDER_OP_BLIT, 0);
            be32(slot(sub) + 36, SRC_DESC);
            be32(slot(sub) + 44, 0);
            be32(slot(sub) + 48, 0);
            be32(slot(sub) + 52, {w, h});
            be32(slot(sub) + 56, {w, h});
            sub = sub + 1;
        end
    endtask
    task automatic line(input [15:0] x0, input [15:0] y0, input [15:0] x1,
                        input [15:0] y1);
        begin
            header(`ASTRA_RENDER_OP_LINE, 0);
            be32(slot(sub) + 44, {x0, y0});
            be32(slot(sub) + 48, {x1, y1});
            be32(slot(sub) + 60, 32'h0000f800);
            sub = sub + 1;
        end
    endtask

    // One untextured TRIANGLES command: n axis-aligned w x h quads, each as
    // (x,y)-(x+w,y)-(x,y+h) and (x+w,y)-(x+w,y+h)-(x,y+h), on a 20-pixel
    // grid so no two quads touch.
    wire [31:0] VERTS = 32'h00300000 + host_shift;
    task automatic vertex(input [31:0] a, input integer px, input integer py,
                          input [31:0] color);
        begin
            be32(a, px * 256); be32(a + 4, py * 256);
            be32(a + 8, 0); be32(a + 12, 0); be32(a + 16, color);
            be32(a + 20, 0); be32(a + 24, 0); be32(a + 28, 0);
        end
    endtask
    task automatic quads(input integer n, input integer w, input integer h,
                         input [2:0] blend, input [31:0] color);
        integer k, qx, qy;
        reg [31:0] a;
        begin
            for (k = 0; k < n; k = k + 1) begin
                qx = (k % 16) * 20 + 2; qy = (k / 16) * 20 + 2;
                a = VERTS + k * 192;
                vertex(a, qx, qy, color);
                vertex(a + 32, qx + w, qy, color);
                vertex(a + 64, qx, qy + h, color);
                vertex(a + 96, qx + w, qy, color);
                vertex(a + 128, qx + w, qy + h, color);
                vertex(a + 160, qx, qy + h, color);
            end
            header(`ASTRA_RENDER_OP_TRIANGLES, 0);
            be32(slot(sub) + 36, 0);
            be32(slot(sub) + 40, VERTS);
            be32(slot(sub) + 44, n * 2);
            be32(slot(sub) + 48, {29'd0, blend});
            sub = sub + 1;
        end
    endtask
    // Every pixel of every quad, and a one-pixel border, after an ADD draw
    // on black: inside must hold exactly one ADD, outside must stay black.
    task automatic check_quads(input integer n, input integer w,
                               input integer h, input [15:0] single);
        integer k, qx, qy, px, py;
        reg [15:0] got;
        begin
            for (k = 0; k < n; k = k + 1) begin
                qx = (k % 16) * 20 + 2; qy = (k / 16) * 20 + 2;
                for (py = qy - 1; py <= qy + h; py = py + 1)
                    for (px = qx - 1; px <= qx + w; px = px + 1) begin
                        got = {memory_i.read_byte(DST_DATA + py * W * 2 + px * 2),
                               memory_i.read_byte(DST_DATA + py * W * 2 + px * 2 + 1)};
                        if (px >= qx && px < qx + w && py >= qy && py < qy + h) begin
                            if (got !== single)
                                $fatal(1, "quad %0dx%0d pixel (%0d,%0d)=%04x expected single cover %04x",
                                       w, h, px - qx, py - qy, got, single);
                        end else if (got !== 16'h0000)
                            $fatal(1, "quad %0dx%0d outside pixel (%0d,%0d)=%04x",
                                   w, h, px - qx, py - qy, got);
                    end
            end
        end
    endtask

    task automatic run(input [8*24-1:0] name, input integer units);
        integer guard;
        begin
            @(negedge clk);
            clear_hist;
            measuring = 1'b1;
            submission_producer = sub;
            guard = 0;
            while (completion_producer != sub) begin
                @(posedge clk);
                guard = guard + 1;
                if (guard > 50000000) $fatal(1, "timeout %0s cp=%0d sub=%0d/%0d comp=%0d failed=%0d detail=%08x bl=%0d st=%0d", name, completion_producer, submission_consumer, sub, completion_producer, commands_failed, last_fault_detail, dut.blitter_i.state, dut.state);
            end
            measuring = 1'b0;
            if (commands_failed != 0)
                $fatal(1, "%0s: command failed detail=%08x status word=%08x",
                       name, last_fault_detail,
                       rd32(COMPLETION_OFFSET +
                            ({21'd0, (sub - 11'd1) & 11'h3ff} << 5) + 4));
            report(name, units);
        end
    endtask

    // Deterministic LCG standing in for testdraw2's rand().
    reg [31:0] lcg = 32'd12345;
    function automatic integer rnd(input integer modulus);
        begin
            lcg = lcg * 32'd1103515245 + 32'd12345;
            rnd = (lcg >> 1) % modulus;
        end
    endfunction

    // One upstream SDL testdraw2 frame as the Astra SDL renderer lowers it
    // with BLENDMODE_NONE: a clear FILL, 25 FILL rects, 100 LINEs (4 fixed,
    // 96 random in [-W,W)x[-H,H)), 400 FILL 1x1 points.
    integer frame_commands;
    task automatic testdraw2_frame;
        integer k, rw, rh, rx, ry, x1, y1, x2, y2;
        begin
            frame_commands = 0;
            fill(0, 0, W, H, 32'ha514);
            frame_commands = frame_commands + 1;
            for (k = 0; k < 25; k = k + 1) begin
                rw = rnd(H / 2); rh = rnd(H / 2);
                rx = (rnd(W * 2) - W) - rw / 2;
                ry = (rnd(H * 2) - H) - rh / 2;
                if (rw > 0 && rh > 0 && rx < W && ry < H &&
                    rx + rw > 0 && ry + rh > 0) begin
                    fill(rx, ry, rw, rh, k);
                    frame_commands = frame_commands + 1;
                end
            end
            line(0, 0, W - 1, H - 1);
            line(0, H - 1, W - 1, 0);
            line(0, H / 2, W - 1, H / 2);
            line(W / 2, 0, W / 2, H - 1);
            frame_commands = frame_commands + 4;
            for (k = 4; k < 100; k = k + 1) begin
                x1 = rnd(W * 2) - W; x2 = rnd(W * 2) - W;
                y1 = rnd(H * 2) - H; y2 = rnd(H * 2) - H;
                line(x1, y1, x2, y2);
                frame_commands = frame_commands + 1;
            end
            for (k = 0; k < 400; k = k + 1) begin
                fill(rnd(W), rnd(H), 1, 1, k);
                frame_commands = frame_commands + 1;
            end
        end
    endtask

    // One FILL_RECTS command over n records written at RECORDS.
    wire [31:0] RECORDS = 32'h00340000 + host_shift;
    integer record_count = 0;
    task automatic record(input [15:0] x, input [15:0] y, input [15:0] w,
                          input [15:0] h, input [31:0] color);
        begin
            be32(RECORDS + record_count * 16, {x, y});
            be32(RECORDS + record_count * 16 + 4, {w, h});
            be32(RECORDS + record_count * 16 + 8, color);
            be32(RECORDS + record_count * 16 + 12, 0);
            record_count = record_count + 1;
        end
    endtask
    task automatic fill_rects(input [31:0] first, input integer n);
        begin
            header(`ASTRA_RENDER_OP_FILL_RECTS, 0);
            be32(slot(sub) + 40, RECORDS + first * 16);
            be32(slot(sub) + 44, n);
            be32(slot(sub) + 48, 1);
            sub = sub + 1;
        end
    endtask

    task automatic lines_command(input [31:0] first, input integer n);
        begin
            header(`ASTRA_RENDER_OP_LINES, 0);
            be32(slot(sub) + 40, RECORDS + first * 16);
            be32(slot(sub) + 44, n);
            be32(slot(sub) + 48, 1);
            sub = sub + 1;
        end
    endtask

    // testdraw2 with everything batched: FILL_RECTS rectangles, LINES
    // segments, FILL_RECTS points.
    task automatic testdraw2_frame_lines;
        integer k, rw, rh, rx, ry, x1, y1, x2, y2, first;
        begin
            frame_commands = 0;
            record_count = 0;
            fill(0, 0, W, H, 32'ha514);
            frame_commands = frame_commands + 1;
            first = record_count;
            for (k = 0; k < 25; k = k + 1) begin
                rw = rnd(H / 2); rh = rnd(H / 2);
                rx = (rnd(W * 2) - W) - rw / 2;
                ry = (rnd(H * 2) - H) - rh / 2;
                if (rw > 0 && rh > 0 && rx < W && ry < H &&
                    rx + rw > 0 && ry + rh > 0)
                    record(rx, ry, rw, rh, k);
            end
            fill_rects(first, record_count - first);
            first = record_count;
            record(0, 0, W - 1, H - 1, 32'hf800);
            record(0, H - 1, W - 1, 0, 32'hf800);
            record(0, H / 2, W - 1, H / 2, 32'hf800);
            record(W / 2, 0, W / 2, H - 1, 32'hf800);
            for (k = 4; k < 100; k = k + 1) begin
                x1 = rnd(W * 2) - W; x2 = rnd(W * 2) - W;
                y1 = rnd(H * 2) - H; y2 = rnd(H * 2) - H;
                record(x1, y1, x2, y2, 32'hf800);
            end
            lines_command(first, 100);
            first = record_count;
            for (k = 0; k < 400; k = k + 1)
                record(rnd(W), rnd(H), 1, 1, k);
            fill_rects(first, 400);
            frame_commands = frame_commands + 3;
        end
    endtask

    // testdraw2 with its points and rectangles batched as FILL_RECTS.
    task automatic testdraw2_frame_batched;
        integer k, rw, rh, rx, ry, x1, y1, x2, y2, first;
        begin
            frame_commands = 0;
            record_count = 0;
            fill(0, 0, W, H, 32'ha514);
            frame_commands = frame_commands + 1;
            first = record_count;
            for (k = 0; k < 25; k = k + 1) begin
                rw = rnd(H / 2); rh = rnd(H / 2);
                rx = (rnd(W * 2) - W) - rw / 2;
                ry = (rnd(H * 2) - H) - rh / 2;
                if (rw > 0 && rh > 0 && rx < W && ry < H &&
                    rx + rw > 0 && ry + rh > 0)
                    record(rx, ry, rw, rh, k);
            end
            fill_rects(first, record_count - first);
            frame_commands = frame_commands + 1;
            line(0, 0, W - 1, H - 1);
            line(0, H - 1, W - 1, 0);
            line(0, H / 2, W - 1, H / 2);
            line(W / 2, 0, W / 2, H - 1);
            frame_commands = frame_commands + 4;
            for (k = 4; k < 100; k = k + 1) begin
                x1 = rnd(W * 2) - W; x2 = rnd(W * 2) - W;
                y1 = rnd(H * 2) - H; y2 = rnd(H * 2) - H;
                line(x1, y1, x2, y2);
                frame_commands = frame_commands + 1;
            end
            first = record_count;
            for (k = 0; k < 400; k = k + 1)
                record(rnd(W), rnd(H), 1, 1, k);
            fill_rects(first, 400);
            frame_commands = frame_commands + 1;
        end
    endtask

    integer n, x, y, pix;
    reg [31:0] cases;
    reg [15:0] quad_single;
    initial begin
        if (!$value$plusargs("cases=%h", cases)) cases = 32'h3f;
        if ($test$plusargs("host")) host_shift = ARENA_BYTES;
        if (!$value$plusargs("blit_rows=%d", blit_rows)) blit_rows = H;
        memory_i.clear_memory(8'h00);
        surface(DST_DESC, DST_DATA, `ASTRA_RENDER_SURFACE_WRITE |
                `ASTRA_RENDER_SURFACE_READ);
        surface(SRC_DESC, SRC_DATA, `ASTRA_RENDER_SURFACE_READ);
        surface_argb(ARGB_DESC, ARGB_DATA);
        for (n = 0; n < W * H * 2; n = n + 1)
            memory_i.write_byte(SRC_DATA + n, n * 7 + 3);
        repeat (8) @(posedge clk);
        reset = 1'b0;
        repeat (8) @(posedge clk);
        $display("latency read=%0d write=%0d host read=%0d cycles, batch in %0s",
                 READ_LATENCY, WRITE_LATENCY, HOST_READ_LATENCY,
                 host_shift != 0 ? "host aperture" : "Media RAM");

        if (cases[0]) begin
        for (n = 0; n < 512; n = n + 1)
            fill((n * 37) % W, (n * 13) % H, 1, 1, n);
        run("512 x fill 1x1", 512);
        end

        if (cases[1]) begin
        for (n = 0; n < 512; n = n + 1)
            fill((n * 37) % (W - 8), (n * 13) % (H - 8), 8, 8, n);
        run("512 x fill 8x8", 512);
        end

        if (cases[2]) begin
        for (n = 0; n < 512; n = n + 1) begin
            x = (n * 37) % (W - 16); y = (n * 13) % (H - 16);
            line(x, y, x + 10, y + 5);
        end
        run("512 x line 11px", 512);
        end

        if (cases[3]) begin
        fill(0, 0, W, H, 32'h1234);
        run("fill 640x480", W * H);
        for (pix = 0; pix < W * H; pix = pix + 997)
            if (memory_i.read_byte(DST_DATA + pix * 2) !== 8'h12 ||
                memory_i.read_byte(DST_DATA + pix * 2 + 1) !== 8'h34)
                $fatal(1, "fill wrong at %0d", pix);
        end

        if (cases[4]) begin
        copy(W, H);
        run("copy 640x480", W * H);
        for (pix = 0; pix < W * H * 2; pix = pix + 991)
            if (memory_i.read_byte(DST_DATA + pix) !== 8'(pix * 7 + 3))
                $fatal(1, "copy wrong at byte %0d", pix);
        end
        if (cases[5]) begin
            testdraw2_frame;
            run("testdraw2 frame", frame_commands);
        end
        // One TRIANGLES command of 128 quads versus 128 FILL commands.
        if (cases[6]) begin
            quads(128, 1, 1, 0, 32'hff808080);
            run("tri 128 quads 1x1", 128);
            if (cases[8]) $finish;
            quads(128, 8, 8, 0, 32'hff808080);
            run("tri 128 quads 8x8", 128);
            quads(128, 11, 1, 0, 32'hff808080);
            run("tri 128 quads 11x1", 128);
            for (n = 0; n < 128; n = n + 1)
                fill((n % 16) * 20 + 2, (n / 16) * 20 + 2, 1, 1, n);
            run("fill x128 1x1", 128);
            for (n = 0; n < 128; n = n + 1)
                fill((n % 16) * 20 + 2, (n / 16) * 20 + 2, 8, 8, n);
            run("fill x128 8x8", 128);
            for (n = 0; n < 128; n = n + 1)
                fill((n % 16) * 20 + 2, (n / 16) * 20 + 2, 11, 1, n);
            run("fill x128 11x1", 128);
        end
        // FILL_RECTS: records per command, versus one FILL each (case 1/2).
        if (cases[9]) begin
            record_count = 0;
            for (n = 0; n < 512; n = n + 1)
                record((n * 37) % W, (n * 13) % H, 1, 1, n);
            fill_rects(0, 512);
            run("fill_rects 512 x 1x1", 512);
            record_count = 0;
            for (n = 0; n < 512; n = n + 1)
                record((n * 37) % (W - 8), (n * 13) % (H - 8), 8, 8, n);
            fill_rects(0, 512);
            run("fill_rects 512 x 8x8", 512);
        end
        if (cases[10]) begin
            lcg = 32'd12345;
            testdraw2_frame_batched;
            run("testdraw2 frame batched", frame_commands);
        end
        if (cases[11]) begin
            lcg = 32'd12345;
            testdraw2_frame_lines;
            run("testdraw2 frame all batched", frame_commands);
            record_count = 0;
            for (n = 0; n < 512; n = n + 1) begin
                x = (n * 37) % (W - 16); y = (n * 13) % (H - 16);
                record(x, y, x + 10, y + 5, 32'hf800);
            end
            lines_command(0, 512);
            run("lines 512 x 11px", 512);
        end
        // Blended FILL_RECTS: 512 translucent 8x8 records (each waits for
        // the previous record's writes) and one full-surface record.
        if (cases[12]) begin
            record_count = 0;
            for (n = 0; n < 512; n = n + 1)
                record((n * 37) % (W - 8), (n * 13) % (H - 8), 8, 8, 0);
            fill_rects(0, 512);
            be32(slot(sub - 1) + 48, 2);
            be32(slot(sub - 1) + 60, 32'h80ff4020);
            run("blend rects 512 x 8x8", 512 * 64);
            record_count = 0;
            record(0, 0, W, H, 0);
            fill_rects(0, 1);
            be32(slot(sub - 1) + 48, 2);
            be32(slot(sub - 1) + 60, 32'h80ff4020);
            run("blend rect 640x480", W * H);
        end
        // Format-converting BLITs: ARGB8888 -> RGB565, NONE and BLEND
        // (opaque, every seventh pixel translucent, every pixel random).
        if (cases[13]) begin
            blit_argb("blit argb>565 none", 1'b0, 2'd0, 0, 0);
            blit_argb("blit argb>565 blend opq", 1'b1, 2'd0, 0, 0);
            blit_argb("blit argb>565 blend 1/7", 1'b1, 2'd2, 0, 0);
            blit_argb("blit argb>565 blend rnd", 1'b1, 2'd1, 0, 0);
        end
        // Scaled BLITs as testscale draws its background: 408x167 ARGB8888
        // onto the whole 640x480 RGB565 surface.
        if (cases[14]) begin
            blit_argb("scaled 408x167 none", 1'b0, 2'd0, 408, 167);
            blit_argb("scaled 408x167 blend", 1'b1, 2'd1, 408, 167);
        end
        // Coverage: ADD on black, each covered pixel exactly once.
        if (cases[7]) begin
            fill(0, 0, W, H, 0);
            run("clear", 1);
            quads(128, 8, 8, 2, 32'hff101010);
            run("tri add 8x8", 128);
            // The reference single-cover value: an interior pixel.
            quad_single = {memory_i.read_byte(DST_DATA + 3 * W * 2 + 3 * 2),
                           memory_i.read_byte(DST_DATA + 3 * W * 2 + 3 * 2 + 1)};
            $display("   single ADD cover = %04x", quad_single);
            if (quad_single == 16'h0000) $fatal(1, "ADD wrote nothing");
            check_quads(128, 8, 8, quad_single);
            fill(0, 0, W, H, 0);
            run("clear", 1);
            quads(128, 1, 1, 2, 32'hff101010);
            run("tri add 1x1", 128);
            check_quads(128, 1, 1, quad_single);
            fill(0, 0, W, H, 0);
            run("clear", 1);
            quads(128, 11, 1, 2, 32'hff101010);
            run("tri add 11x1", 128);
            check_quads(128, 11, 1, quad_single);
            fill(0, 0, W, H, 0);
            run("clear", 1);
            quads(128, 7, 3, 2, 32'hff101010);
            run("tri add 7x3", 128);
            check_quads(128, 7, 3, quad_single);
            $display("   quad coverage exact: 1x1 8x8 11x1 7x3");
        end

        $display("PERF DONE");
        $finish;
    end
endmodule
`default_nettype wire
