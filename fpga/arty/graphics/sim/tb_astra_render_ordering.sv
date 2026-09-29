// Memory ordering of the render transport under a slow memory whose writes
// become visible only with their response: a read that passes an unanswered
// write, a completion that becomes visible before its pixels, or a record
// interleaved into an engine burst all fail here.
`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module tb_astra_render_ordering;
    parameter integer READ_LATENCY = 4;
    parameter integer WRITE_LATENCY = 1000;
    localparam [31:0] ARENA_BYTES = 32'h00400000;
    localparam [31:0] SUBMISSION_OFFSET = 32'h00000000;
    localparam [31:0] COMPLETION_OFFSET = 32'h00010000;
    localparam [31:0] DST_DESC = 32'h00020000;
    localparam [31:0] SRC_DESC = 32'h00020020;
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
        .ARENA_BASE(32'd0), .ARENA_LIMIT(ARENA_BYTES),
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

    astra_render_axi_latency_model #(
        .MEMORY_BYTES(ARENA_BYTES), .READ_LATENCY(READ_LATENCY),
        .WRITE_LATENCY(WRITE_LATENCY), .WRITE_AT_RESPONSE(1'b1),
        .FAST_ID(3), .FAST_LATENCY(4),
        .ERROR_ADDRESS(DST_DATA + 449 * W * 2 + 8)
    ) memory_i (
        .clk(clk), .reset(reset),
        .s_axi_arid(arid), .s_axi_araddr(araddr), .s_axi_arlen(arlen),
        .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rid(rid), .s_axi_rdata(rdata), .s_axi_rresp(rresp),
        .s_axi_rlast(rlast), .s_axi_rvalid(rvalid), .s_axi_rready(rready),
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

    // ---- command helpers ------------------------------------------------
    task automatic be32(input [31:0] a, input [31:0] v);
        begin
            memory_i.write_byte(a, v[31:24]);
            memory_i.write_byte(a + 1, v[23:16]);
            memory_i.write_byte(a + 2, v[15:8]);
            memory_i.write_byte(a + 3, v[7:0]);
        end
    endtask
    function automatic [31:0] rd32(input [31:0] a);
        rd32 = {memory_i.read_byte(a), memory_i.read_byte(a + 1),
                memory_i.read_byte(a + 2), memory_i.read_byte(a + 3)};
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
    localparam [31:0] VERTS = 32'h00300000;
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


    // Each retired completion's pixels are visible when the producer covers
    // it: check_after[k] names the first pixel command k wrote.
    reg [31:0] expect_address [0:1023];
    reg [15:0] expect_value [0:1023];
    reg expect_valid [0:1023];
    reg [15:0] expect_status [0:1023];
    reg [10:0] seen_producer = 11'd0;
    integer k;
    always @(posedge clk) if (!reset) begin
        while (seen_producer != completion_producer) begin
            k = seen_producer[9:0];
            if (expect_valid[k] &&
                {memory_i.read_byte(expect_address[k]),
                 memory_i.read_byte(expect_address[k] + 1)} !==
                    expect_value[k])
                $fatal(1, "completion %0d visible before its pixel %08x",
                       k, expect_address[k]);
            if ((rd32(COMPLETION_OFFSET + (k << 5) + 4) & 32'hffff) !==
                    {16'd0, expect_status[k]})
                $fatal(1, "completion %0d status %08x expected %04x", k,
                       rd32(COMPLETION_OFFSET + (k << 5) + 4),
                       expect_status[k]);
            seen_producer = seen_producer + 11'd1;
        end
    end

    task automatic expect_pixel(input [31:0] a, input [15:0] v);
        begin
            expect_address[sub[9:0]] = a; expect_value[sub[9:0]] = v;
            expect_valid[sub[9:0]] = 1'b1;
        end
    endtask

    task automatic run_all;
        integer guard;
        begin
            @(negedge clk);
            submission_producer = sub;
            guard = 0;
            while (completion_producer != sub) begin
                @(posedge clk);
                guard = guard + 1;
                if (guard > 5000000) $fatal(1, "ordering bench timeout");
            end
            @(posedge clk);
        end
    endtask

    integer n, row, col, rect_x, rect_y;
    initial begin
        for (n = 0; n < 1024; n = n + 1) begin
            expect_valid[n] = 1'b0;
            expect_status[n] = `ASTRA_RENDER_STATUS_OK;
        end
        memory_i.clear_memory(8'h00);
        surface(DST_DESC, DST_DATA, `ASTRA_RENDER_SURFACE_WRITE |
                `ASTRA_RENDER_SURFACE_READ);
        surface(SRC_DESC, SRC_DATA, `ASTRA_RENDER_SURFACE_READ);
        repeat (8) @(posedge clk);
        reset = 1'b0;
        repeat (8) @(posedge clk);

        // 1. Pixels are visible when their completion is: small fills, lines
        //    and wide fills whose bursts compete with completion records.
        for (n = 0; n < 96; n = n + 1) begin
            case (n % 3)
                0: begin
                    expect_pixel(DST_DATA + (n * 2) * W * 2 + 6, 16'h1000 + n);
                    fill(3, n * 2, 1, 1, 16'h1000 + n);
                end
                1: begin
                    expect_pixel(DST_DATA + (n * 2) * W * 2 + 20 * 2,
                                 16'hf800);
                    line(10, n * 2, 30, n * 2);
                end
                default: begin
                    expect_pixel(DST_DATA + (n * 2) * W * 2 + 600 * 2,
                                 16'h2000 + n);
                    fill(40, n * 2, 600, 2, 16'h2000 + n);
                end
            endcase
        end
        run_all;

        // 2. A copy reads a row the command just before it wrote.
        for (n = 0; n < 8; n = n + 1) begin
            fill(0, 300 + n, 16, 1, 16'h3300 + n);
            header(`ASTRA_RENDER_OP_BLIT, 0);
            be32(slot(sub) + 36, DST_DESC);
            be32(slot(sub) + 44, {16'd0, 16'(300 + n)});
            be32(slot(sub) + 48, {16'd0, 16'(400 + n)});
            be32(slot(sub) + 52, {16'd16, 16'd1});
            be32(slot(sub) + 56, {16'd16, 16'd1});
            expect_pixel(DST_DATA + (400 + n) * W * 2 + 8 * 2, 16'h3300 + n);
            sub = sub + 1;
        end
        run_all;

        // 3. A descriptor written by earlier commands of the same doorbell
        //    is read after those writes are answered.
        for (n = 0; n < 8; n = n + 1) begin
            // eight XRGB8888-sized words as pairs of RGB565 pixels
            fill(n * 2, 470, 1, 1, 16'h0000);
        end
        run_all;
        begin : desc_from_pixels
            reg [31:0] words [0:7];
            words[0] = (`ASTRA_RENDER_ABI_VERSION << 16) |
                `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
            words[1] = GENERATION; words[2] = SRC_DATA; words[3] = W * H * 2;
            words[4] = W * 2; words[5] = {16'(W), 16'(H)};
            words[6] = {8'(`ASTRA_RENDER_FORMAT_RGB565),
                        8'(`ASTRA_RENDER_SURFACE_WRITE), 16'd0};
            words[7] = 0;
            // The descriptor slot lives in destination row 470; the fills
            // below write its words, the next command uses it.
            // Written last to first, so the header words land just before
            // the command that reads them.
            for (n = 15; n >= 0; n = n - 1)
                fill(n, 470, 1, 1, n[0] ? words[n / 2][15:0] :
                                          words[n / 2][31:16]);
            header(`ASTRA_RENDER_OP_FILL, 0);
            be32(slot(sub) + 32, DST_DATA + 470 * W * 2);
            be32(slot(sub) + 48, {16'd5, 16'd5});
            be32(slot(sub) + 56, {16'd1, 16'd1});
            be32(slot(sub) + 60, 32'h00004242);
            expect_pixel(SRC_DATA + 5 * W * 2 + 10, 16'h4242);
            sub = sub + 1;
            run_all;
        end
        // 4. A failed write is charged to the command that issued it, even
        //    with later commands' writes and records in flight.
        for (n = 0; n < 12; n = n + 1) begin
            if (n == 5) expect_status[sub[9:0]] =
                `ASTRA_RENDER_STATUS_AXI_WRITE;
            else expect_pixel(DST_DATA + (450 + n) * W * 2 + 8, 16'h5500 + n);
            fill(4, n == 5 ? 449 : 450 + n, 1, 1, 16'h5500 + n);
        end
        run_all;
        if (commands_failed != 1)
            $fatal(1, "failed commands=%0d expected 1", commands_failed);
        // 5. Overlapping blended FILL_RECTS records read what the record
        //    before them wrote: rows 200.. by FILL_RECTS equal rows 240.. by
        //    sequential straight-alpha BLITs of a 1x1 ARGB color.
        be32(32'h00020040, (`ASTRA_RENDER_ABI_VERSION << 16) |
             `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES);
        be32(32'h00020044, GENERATION);
        be32(32'h00020048, 32'h00310000);
        be32(32'h0002004c, 4);
        be32(32'h00020050, 4);
        be32(32'h00020054, {16'd1, 16'd1});
        be32(32'h00020058, {8'(`ASTRA_RENDER_FORMAT_ARGB8888),
                            8'(`ASTRA_RENDER_SURFACE_READ), 16'd0});
        be32(32'h0002005c, 0);
        be32(32'h00310000, 32'h80ff4020);
        for (n = 0; n < 16 * W * 2; n = n + 1) begin
            memory_i.write_byte(DST_DATA + 200 * W * 2 + n, n * 29 + 3);
            memory_i.write_byte(DST_DATA + 240 * W * 2 + n, n * 29 + 3);
        end
        for (n = 0; n < 10; n = n + 1) begin
            be32(32'h00320000 + n * 16, {16'(n * 3), 16'(200 + n)});
            be32(32'h00320000 + n * 16 + 4, {16'd12, 16'd5});
            be32(32'h00320000 + n * 16 + 8, 0);
            be32(32'h00320000 + n * 16 + 12, 0);
            header(`ASTRA_RENDER_OP_BLIT, `ASTRA_RENDER_FLAG_BLIT_ALPHA);
            be32(slot(sub) + 36, 32'h00020040);
            be32(slot(sub) + 44, 0);
            be32(slot(sub) + 48, {16'(n * 3), 16'(240 + n)});
            be32(slot(sub) + 52, {16'd1, 16'd1});
            be32(slot(sub) + 56, {16'd12, 16'd5});
            be32(slot(sub) + 60, 32'hff000000);
            sub = sub + 1;
        end
        header(`ASTRA_RENDER_OP_FILL_RECTS, 0);
        be32(slot(sub) + 40, 32'h00320000);
        be32(slot(sub) + 44, 10);
        be32(slot(sub) + 48, `ASTRA_RENDER_FILL_RECTS_OPTION_BLEND);
        be32(slot(sub) + 60, 32'h80ff4020);
        sub = sub + 1;
        run_all;
        for (n = 0; n < 16 * W * 2; n = n + 1)
            if (memory_i.read_byte(DST_DATA + 200 * W * 2 + n) !==
                memory_i.read_byte(DST_DATA + 240 * W * 2 + n))
                $fatal(1, "blended records byte %0d = %02x, BLITs %02x", n,
                       memory_i.read_byte(DST_DATA + 200 * W * 2 + n),
                       memory_i.read_byte(DST_DATA + 240 * W * 2 + n));

        // 6. A record that overlaps an earlier, not the latest, undrained
        //    record still waits: A, a disjoint B, then C over A, repeated.
        for (n = 0; n < 16 * W * 2; n = n + 1) begin
            memory_i.write_byte(DST_DATA + 260 * W * 2 + n, n * 13 + 7);
            memory_i.write_byte(DST_DATA + 300 * W * 2 + n, n * 13 + 7);
        end
        for (n = 0; n < 12; n = n + 1) begin
            // x: A at 40k, B at 40k + 20, C at 40k + 5 (k = n / 3)
            rect_x = 40 * (n / 3) + (n % 3 == 0 ? 0 : n % 3 == 1 ? 20 : 5);
            rect_y = n % 3 == 2 ? 1 : 0;
            be32(32'h00320000 + n * 16, {16'(rect_x), 16'(260 + rect_y)});
            be32(32'h00320000 + n * 16 + 4, {16'd10, 16'd4});
            be32(32'h00320000 + n * 16 + 8, 0);
            be32(32'h00320000 + n * 16 + 12, 0);
            header(`ASTRA_RENDER_OP_BLIT, `ASTRA_RENDER_FLAG_BLIT_ALPHA);
            be32(slot(sub) + 36, 32'h00020040);
            be32(slot(sub) + 44, 0);
            be32(slot(sub) + 48, {16'(rect_x), 16'(300 + rect_y)});
            be32(slot(sub) + 52, {16'd1, 16'd1});
            be32(slot(sub) + 56, {16'd10, 16'd4});
            be32(slot(sub) + 60, 32'hff000000);
            sub = sub + 1;
        end
        header(`ASTRA_RENDER_OP_FILL_RECTS, 0);
        be32(slot(sub) + 40, 32'h00320000);
        be32(slot(sub) + 44, 12);
        be32(slot(sub) + 48, `ASTRA_RENDER_FILL_RECTS_OPTION_BLEND);
        be32(slot(sub) + 60, 32'h80ff4020);
        sub = sub + 1;
        run_all;
        for (n = 0; n < 16 * W * 2; n = n + 1)
            if (memory_i.read_byte(DST_DATA + 260 * W * 2 + n) !==
                memory_i.read_byte(DST_DATA + 300 * W * 2 + n))
                $fatal(1, "earlier-overlap records byte %0d = %02x, BLITs %02x",
                       n, memory_i.read_byte(DST_DATA + 260 * W * 2 + n),
                       memory_i.read_byte(DST_DATA + 300 * W * 2 + n));

        $display("ASTRA RENDER ORDERING PASS commands=%0d", sub);
        $finish;
    end
endmodule
`default_nettype wire
