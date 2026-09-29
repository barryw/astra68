`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module tb_astra_render_blitter;
    reg clk = 1'b0;
    reg reset = 1'b1;
    always #2.5 clk = ~clk;

    reg start = 1'b0;
    reg abort = 1'b0;
    reg [15:0] opcode = `ASTRA_RENDER_OP_FILL;
    reg signed [15:0] clip_left = 16'sd0;
    reg signed [15:0] clip_top = 16'sd0;
    reg signed [15:0] clip_right = 16'sd8;
    reg signed [15:0] clip_bottom = 16'sd8;
    reg signed [15:0] source_x = 16'sd0;
    reg signed [15:0] source_y = 16'sd0;
    reg signed [15:0] destination_x = 16'sd0;
    reg signed [15:0] destination_y = 16'sd0;
    reg [15:0] source_width = 16'd0;
    reg [15:0] source_height = 16'd0;
    reg [15:0] destination_width = 16'd0;
    reg [15:0] destination_height = 16'd0;
    reg [15:0] command_flags = 16'd0;
    reg [31:0] options = 32'd0;
    reg same_surface = 1'b0;
    reg [31:0] destination_data_offset = 32'h1000;
    reg [31:0] destination_pitch = 32'd16;
    reg [15:0] destination_surface_width = 16'd8;
    reg [15:0] destination_surface_height = 16'd8;
    reg [7:0] destination_format = `ASTRA_RENDER_FORMAT_INDEX8;
    reg [2:0] destination_bpp = 3'd1;
    reg [31:0] source_data_offset = 32'h4000;
    reg [31:0] source_pitch = 32'd16;
    reg [15:0] source_surface_width = 16'd8;
    reg [15:0] source_surface_height = 16'd8;
    reg [7:0] source_format = `ASTRA_RENDER_FORMAT_INDEX8;
    reg [2:0] source_bpp = 3'd1;
    reg [31:0] source_palette_offset = 32'd0;
    reg [31:0] auxiliary_data_offset = 32'd0;
    reg [31:0] auxiliary_pitch = 32'd0;
    reg [15:0] auxiliary_surface_width = 16'd0;
    reg [15:0] auxiliary_surface_height = 16'd0;
    wire busy;
    wire done;
    wire [15:0] status;
    wire [31:0] fault_detail;
    wire [31:0] completed_pixels;

    wire writer_start;
    wire writer_abort;
    wire writer_flush;
    wire writer_flush_ready;
    wire writer_busy;
    wire writer_done;
    wire writer_aborted;
    wire writer_error;
    wire [31:0] writer_fault_detail;
    wire writer_pixel_valid;
    wire writer_pixel_ready;
    wire [31:0] writer_pixel_address;
    wire [7:0] writer_pixel_format;
    wire [31:0] writer_pixel_value;

    wire [5:0] read_arid;
    wire [31:0] read_araddr;
    wire [7:0] read_arlen;
    wire [2:0] read_arsize;
    wire [1:0] read_arburst;
    wire [3:0] read_arcache;
    wire [2:0] read_arprot;
    wire [3:0] read_arqos;
    wire read_arvalid;
    wire read_arready;
    wire [5:0] read_rid;
    wire [63:0] read_rdata;
    wire [1:0] read_rresp;
    wire read_rlast;
    wire read_rvalid;
    wire read_rready;

    wire copy_write_active;
    wire [5:0] copy_awid;
    wire [31:0] copy_awaddr;
    wire [7:0] copy_awlen;
    wire [2:0] copy_awsize;
    wire [1:0] copy_awburst;
    wire [3:0] copy_awcache;
    wire [2:0] copy_awprot;
    wire [3:0] copy_awqos;
    wire copy_awvalid;
    wire [63:0] copy_wdata;
    wire [7:0] copy_wstrb;
    wire copy_wlast;
    wire copy_wvalid;
    wire copy_bready;

    wire [5:0] write_awid;
    wire [31:0] write_awaddr;
    wire [7:0] write_awlen;
    wire [2:0] write_awsize;
    wire [1:0] write_awburst;
    wire [3:0] write_awcache;
    wire [2:0] write_awprot;
    wire [3:0] write_awqos;
    wire write_awvalid;
    wire write_awready;
    wire [63:0] write_wdata;
    wire [7:0] write_wstrb;
    wire write_wlast;
    wire write_wvalid;
    wire write_wready;
    wire [5:0] write_bid;
    wire [1:0] write_bresp;
    wire write_bvalid;
    wire write_bready;

    reg stall_reads = 1'b0;
    reg stall_writes = 1'b0;
    reg inject_read_error = 1'b0;
    reg inject_write_error = 1'b0;
    wire [31:0] read_transactions;
    wire [31:0] write_transactions;

    astra_render_blitter #(
        .AXI_ID(6'd1),
        .WRITE_ID(6'd6)
    ) blitter_i (
        .clk(clk), .reset(reset), .start(start), .abort(abort), .fill_blend(1'b0),
        .is_fill(opcode == `ASTRA_RENDER_OP_FILL),
        .is_blit(opcode == `ASTRA_RENDER_OP_BLIT), .arena_base(32'd0),
        .clip_left(clip_left), .clip_top(clip_top),
        .clip_right(clip_right), .clip_bottom(clip_bottom),
        .source_x(source_x), .source_y(source_y),
        .destination_x(destination_x), .destination_y(destination_y),
        .source_width(source_width), .source_height(source_height),
        .destination_width(destination_width),
        .destination_height(destination_height),
        .command_flags(command_flags), .options(options),
        .same_surface(same_surface),
        .destination_data_offset(destination_data_offset),
        .destination_pitch(destination_pitch),
        .destination_surface_width(destination_surface_width),
        .destination_surface_height(destination_surface_height),
        .destination_format(destination_format),
        .destination_bytes_per_pixel(destination_bpp),
        .source_data_offset(source_data_offset),
        .source_pitch(source_pitch),
        .source_surface_width(source_surface_width),
        .source_surface_height(source_surface_height),
        .source_format(source_format),
        .source_bytes_per_pixel(source_bpp),
        .source_palette_offset(source_palette_offset),
        .auxiliary_data_offset(auxiliary_data_offset),
        .auxiliary_pitch(auxiliary_pitch),
        .auxiliary_surface_width(auxiliary_surface_width),
        .auxiliary_surface_height(auxiliary_surface_height),
        .busy(busy), .done(done), .status(status),
        .fault_detail(fault_detail), .completed_pixels(completed_pixels),
        .writer_start(writer_start), .writer_abort(writer_abort),
        .writer_flush(writer_flush), .writer_flush_ready(writer_flush_ready),
        .writer_busy(writer_busy), .writer_done(writer_done),
        .writer_aborted(writer_aborted), .writer_error(writer_error),
        .writer_fault_detail(writer_fault_detail),
        .pixel_valid(writer_pixel_valid), .pixel_ready(writer_pixel_ready),
        .pixel_address(writer_pixel_address),
        .pixel_format(writer_pixel_format), .pixel_value(writer_pixel_value),
        .m_axi_arid(read_arid), .m_axi_araddr(read_araddr),
        .m_axi_arlen(read_arlen), .m_axi_arsize(read_arsize),
        .m_axi_arburst(read_arburst), .m_axi_arcache(read_arcache),
        .m_axi_arprot(read_arprot), .m_axi_arqos(read_arqos),
        .m_axi_arvalid(read_arvalid), .m_axi_arready(read_arready),
        .m_axi_rid(read_rid), .m_axi_rdata(read_rdata),
        .m_axi_rresp(read_rresp), .m_axi_rlast(read_rlast),
        .m_axi_rvalid(read_rvalid), .m_axi_rready(read_rready),
        .copy_write_active(copy_write_active),
        .m_axi_awid(copy_awid), .m_axi_awaddr(copy_awaddr),
        .m_axi_awlen(copy_awlen), .m_axi_awsize(copy_awsize),
        .m_axi_awburst(copy_awburst), .m_axi_awcache(copy_awcache),
        .m_axi_awprot(copy_awprot), .m_axi_awqos(copy_awqos),
        .m_axi_awvalid(copy_awvalid),
        .m_axi_awready(copy_write_active && write_awready),
        .m_axi_wdata(copy_wdata), .m_axi_wstrb(copy_wstrb),
        .m_axi_wlast(copy_wlast), .m_axi_wvalid(copy_wvalid),
        .m_axi_wready(copy_write_active && write_wready),
        .m_axi_bid(write_bid), .m_axi_bresp(write_bresp),
        .m_axi_bvalid(copy_write_active && write_bvalid),
        .m_axi_bready(copy_bready)
    );

    astra_render_pixel_writer writer_i (
        .clk(clk), .reset(reset), .start(writer_start),
        .abort(writer_abort), .flush(writer_flush),
        .flush_ready(writer_flush_ready), .barrier(1'b0),
        .barrier_ready(), .barrier_done(), .pixel_valid(writer_pixel_valid),
        .pixel_ready(writer_pixel_ready),
        .pixel_address(writer_pixel_address),
        .pixel_format(writer_pixel_format), .pixel_value(writer_pixel_value),
        .busy(writer_busy), .done(writer_done), .aborted(writer_aborted),
        .write_error(writer_error), .fault_detail(writer_fault_detail),
        .pixels_accepted(), .bytes_written(), .m_axi_awid(write_awid),
        .m_axi_awaddr(write_awaddr), .m_axi_awlen(write_awlen),
        .m_axi_awsize(write_awsize), .m_axi_awburst(write_awburst),
        .m_axi_awcache(write_awcache), .m_axi_awprot(write_awprot),
        .m_axi_awqos(write_awqos), .m_axi_awvalid(write_awvalid),
        .m_axi_awready(!copy_write_active && write_awready),
        .m_axi_wdata(write_wdata),
        .m_axi_wstrb(write_wstrb), .m_axi_wlast(write_wlast),
        .m_axi_wvalid(write_wvalid),
        .m_axi_wready(!copy_write_active && write_wready),
        .m_axi_bid(write_bid), .m_axi_bresp(write_bresp),
        .m_axi_bvalid(!copy_write_active && write_bvalid),
        .m_axi_bready(write_bready)
    );

    astra_render_axi_memory_model #(
        .MEMORY_BYTES(4194304)
    ) memory_i (
        .clk(clk), .reset(reset), .stall_reads(stall_reads),
        .stall_writes(stall_writes),
        .inject_read_error(inject_read_error),
        .inject_write_error(inject_write_error),
        .s_axi_arid(read_arid), .s_axi_araddr(read_araddr),
        .s_axi_arlen(read_arlen), .s_axi_arsize(read_arsize),
        .s_axi_arburst(read_arburst), .s_axi_arvalid(read_arvalid),
        .s_axi_arready(read_arready), .s_axi_rid(read_rid),
        .s_axi_rdata(read_rdata), .s_axi_rresp(read_rresp),
        .s_axi_rlast(read_rlast), .s_axi_rvalid(read_rvalid),
        .s_axi_rready(read_rready),
        .s_axi_awid(copy_write_active ? copy_awid : write_awid),
        .s_axi_awaddr(copy_write_active ? copy_awaddr : write_awaddr),
        .s_axi_awlen(copy_write_active ? copy_awlen : write_awlen),
        .s_axi_awsize(copy_write_active ? copy_awsize : write_awsize),
        .s_axi_awburst(copy_write_active ? copy_awburst : write_awburst),
        .s_axi_awvalid(copy_write_active ? copy_awvalid : write_awvalid),
        .s_axi_awready(write_awready),
        .s_axi_wdata(copy_write_active ? copy_wdata : write_wdata),
        .s_axi_wstrb(copy_write_active ? copy_wstrb : write_wstrb),
        .s_axi_wlast(copy_write_active ? copy_wlast : write_wlast),
        .s_axi_wvalid(copy_write_active ? copy_wvalid : write_wvalid),
        .s_axi_wready(write_wready), .s_axi_bid(write_bid),
        .s_axi_bresp(write_bresp), .s_axi_bvalid(write_bvalid),
        .s_axi_bready(copy_write_active ? copy_bready : write_bready),
        .read_transactions(read_transactions),
        .write_transactions(write_transactions)
    );

    integer last_elapsed;
    integer abort_case;

    task automatic launch;
        integer elapsed;
        begin
            @(negedge clk);
            start = 1'b1;
            @(negedge clk);
            start = 1'b0;
            elapsed = 0;
            while (!done) begin
                @(posedge clk);
                elapsed = elapsed + 1;
                if (elapsed > 6000000)
                    $fatal(1, "blitter timeout state=%0d writer_busy=%0d fifo=%0d issue=%0d outstanding=%0d aw=%0d w=%0d b=%0d active=%0d",
                           blitter_i.state, writer_busy, writer_i.fifo_count,
                           writer_i.issue_valid, writer_i.outstanding_count,
                           memory_i.aw_count, memory_i.w_count,
                           memory_i.b_count, memory_i.write_active);
            end
            last_elapsed = elapsed;
            @(negedge clk);
        end
    endtask

    task automatic set_index_surface(
        input [31:0] source_offset,
        input [31:0] destination_offset,
        input [15:0] width,
        input [15:0] height,
        input [31:0] pitch_value
    );
        begin
            source_data_offset = source_offset;
            destination_data_offset = destination_offset;
            source_pitch = pitch_value;
            destination_pitch = pitch_value;
            source_surface_width = width;
            destination_surface_width = width;
            source_surface_height = height;
            destination_surface_height = height;
            source_format = `ASTRA_RENDER_FORMAT_INDEX8;
            destination_format = `ASTRA_RENDER_FORMAT_INDEX8;
            source_bpp = 3'd1;
            destination_bpp = 3'd1;
        end
    endtask

    task automatic expect_byte(input [31:0] address, input [7:0] expected);
        reg [7:0] actual;
        begin
            actual = memory_i.read_byte(address);
            if (actual !== expected)
                $fatal(1, "memory[%08x]=%02x expected=%02x",
                       address, actual, expected);
        end
    endtask

    function automatic [7:0] expected_rop8(
        input [3:0] rop,
        input [7:0] source_pixel,
        input [7:0] destination_pixel
    );
        begin
            expected_rop8 =
                ({8{rop[0]}} & ~source_pixel & ~destination_pixel) |
                ({8{rop[1]}} & ~source_pixel &  destination_pixel) |
                ({8{rop[2]}} &  source_pixel & ~destination_pixel) |
                ({8{rop[3]}} &  source_pixel &  destination_pixel);
        end
    endfunction

    function automatic [15:0] screen_pixel(input integer x, input integer y);
        begin
            screen_pixel = (x * 16'h0421 + y * 16'h1f3d +
                            (x >> 8) * 16'h0101) ^ (x << 7) ^ (y << 11);
        end
    endfunction


    // ---- pixel-stream reference (expand, straight-alpha source-over,
    // pack), the blitter's per-pixel equations ---------------------------
    function automatic [7:0] ref_m(input [7:0] x, input [7:0] y);
        reg [16:0] a;
        begin
            a = x * y + 17'd128;
            ref_m = (a + (a >> 8)) >> 8;
        end
    endfunction
    function automatic [7:0] ref_sat(input [8:0] v);
        ref_sat = v[8] ? 8'hff : v[7:0];
    endfunction
    function automatic [31:0] ref_expand(input [7:0] f, input [31:0] v);
        begin
            if (f == `ASTRA_RENDER_FORMAT_RGB565)
                ref_expand = {8'hff, v[15:11], v[15:13], v[10:5], v[10:9],
                              v[4:0], v[4:2]};
            else if (f == `ASTRA_RENDER_FORMAT_XRGB8888)
                ref_expand = {8'hff, v[23:0]};
            else
                ref_expand = v;
        end
    endfunction
    function automatic [31:0] ref_pack(input [7:0] f, input [31:0] c);
        begin
            if (f == `ASTRA_RENDER_FORMAT_RGB565)
                ref_pack = {16'd0, c[23:19], c[15:10], c[7:3]};
            else if (f == `ASTRA_RENDER_FORMAT_XRGB8888)
                ref_pack = {8'hff, c[23:0]};
            else
                ref_pack = c;
        end
    endfunction
    function automatic [31:0] ref_blit(input [7:0] sf, input [7:0] df,
                                       input blend, input [7:0] opacity,
                                       input [31:0] sv, input [31:0] dv);
        reg [31:0] sc, dc, r;
        reg [7:0] a;
        begin
            sc = ref_expand(sf, sv);
            dc = ref_expand(df, dv);
            if (!blend) begin
                r = sc;
            end else begin
                a = ref_m(sc[31:24], opacity);
                r[31:24] = ref_sat(a + ref_m(dc[31:24], 8'hff - a));
                r[23:16] = ref_sat(ref_m(sc[23:16], a) +
                                   ref_m(dc[23:16], 8'hff - a));
                r[15:8] = ref_sat(ref_m(sc[15:8], a) +
                                  ref_m(dc[15:8], 8'hff - a));
                r[7:0] = ref_sat(ref_m(sc[7:0], a) +
                                 ref_m(dc[7:0], 8'hff - a));
            end
            ref_blit = ref_pack(df, r);
        end
    endfunction
    function automatic [31:0] mem_pixel(input [31:0] address,
                                        input [2:0] bpp);
        begin
            mem_pixel = bpp == 3'd2 ?
                {16'd0, memory_i.read_byte(address),
                 memory_i.read_byte(address + 1)} :
                {memory_i.read_byte(address), memory_i.read_byte(address + 1),
                 memory_i.read_byte(address + 2),
                 memory_i.read_byte(address + 3)};
        end
    endfunction
    reg [31:0] px_rng = 32'h2545f491;
    function automatic [31:0] px_rand(input [31:0] modulus);
        begin
            px_rng = px_rng ^ (px_rng << 13);
            px_rng = px_rng ^ (px_rng >> 17);
            px_rng = px_rng ^ (px_rng << 5);
            px_rand = px_rng % modulus;
        end
    endfunction
    reg [7:0] px_before [0:131071];
    reg [7:0] px_formats [0:2];
    integer px_stall_mode = 0;
    always @(negedge clk) begin
        if (px_stall_mode != 0) begin
            stall_writes = px_rand(4) == 0;
            stall_reads = px_rand(5) == 0;
        end
    end

    // One random direct-color BLIT (plain or BLIT_ALPHA) of a sw x sh
    // source onto w x h destination pixels, checked byte for byte against the
    // reference over the whole destination surface. Nearest-neighbour
    // sampling uses the blitter's Q24 step, ceil((sw << 24) / w).
    // serial selects a 4-mod-8 pitch, which keeps the per-pixel path; a
    // source wider than its destination keeps it too.
    task automatic pixel_case(input [7:0] sf, input [7:0] df,
                              input blend, input [7:0] opacity,
                              input [1:0] alpha_kind, input integer w,
                              input integer h, input serial,
                              input clipped, input integer sw,
                              input integer sh);
        integer sbpp, dbpp, ssw, ssh, dsw, dsh, i, j, k, dxi, dyi, expected_pixels;
        integer sxi, syi, spitch, dpitch, soff, doff, x0, y0, x1, y1;
        reg [31:0] value, want;
        reg [7:0] want_byte;
        reg in_rect;
        reg [63:0] step_x, step_y, sample_x, sample_y;
        begin
            step_x = (({32'd0, sw} << 24) + w - 1) / w;
            step_y = (({32'd0, sh} << 24) + h - 1) / h;
            sbpp = sf == `ASTRA_RENDER_FORMAT_RGB565 ? 2 : 4;
            dbpp = df == `ASTRA_RENDER_FORMAT_RGB565 ? 2 : 4;
            sxi = px_rand(9); syi = px_rand(3);
            dxi = px_rand(9); dyi = px_rand(3);
            ssw = sxi + sw + px_rand(5); ssh = syi + sh + px_rand(2);
            dsw = dxi + w + px_rand(5); dsh = dyi + h + px_rand(2);
            spitch = ((ssw * sbpp + 7) / 8 + px_rand(40)) * 8 +
                     (serial ? 4 : 0);
            dpitch = ((dsw * dbpp + 7) / 8 + px_rand(40)) * 8 +
                     (serial ? 4 : 0);
            soff = 32'h00010000 + px_rand(2048) * sbpp;
            doff = 32'h00100000 + px_rand(2048) * dbpp;
            if (dpitch * dsh + 16 > 131072)
                $fatal(1, "pixel case destination too large");
            for (k = 0; k < spitch * ssh; k = k + 1)
                memory_i.write_byte(soff + k, px_rand(256));
            if (sf == `ASTRA_RENDER_FORMAT_ARGB8888)
                for (k = 0; k < spitch * ssh; k = k + 4)
                    memory_i.write_byte(soff + k,
                        alpha_kind == 0 ? 8'hff :
                        alpha_kind == 1 ? px_rand(256) :
                        alpha_kind == 2 ? (px_rand(9) == 0 ? px_rand(256) : 8'hff) :
                        // Only the first and last columns translucent.
                        ((k % spitch) / 4 == sxi ||
                         (k % spitch) / 4 == sxi + sw - 1 ? 8'h40 : 8'hff));
            for (k = 0; k < dpitch * dsh + 16; k = k + 1) begin
                value = px_rand(256);
                memory_i.write_byte(doff - 8 + k, value[7:0]);
                px_before[k] = value[7:0];
            end
            opcode = `ASTRA_RENDER_OP_BLIT;
            source_data_offset = soff; destination_data_offset = doff;
            source_pitch = spitch; destination_pitch = dpitch;
            source_surface_width = ssw; source_surface_height = ssh;
            destination_surface_width = dsw; destination_surface_height = dsh;
            source_format = sf; destination_format = df;
            source_bpp = sbpp; destination_bpp = dbpp;
            source_x = sxi; source_y = syi;
            destination_x = dxi; destination_y = dyi;
            source_width = sw; source_height = sh;
            destination_width = w; destination_height = h;
            x0 = 0; y0 = 0; x1 = dsw; y1 = dsh;
            if (clipped) begin
                x0 = dxi + px_rand(w); y0 = dyi + px_rand(h);
                x1 = x0 + 1 + px_rand(dsw - x0); y1 = y0 + 1 + px_rand(dsh - y0);
            end
            clip_left = x0; clip_top = y0; clip_right = x1; clip_bottom = y1;
            same_surface = 1'b0;
            source_palette_offset = 32'd0;
            auxiliary_data_offset = 32'd0; auxiliary_pitch = 32'd0;
            auxiliary_surface_width = 16'd0; auxiliary_surface_height = 16'd0;
            command_flags = blend ? `ASTRA_RENDER_FLAG_BLIT_ALPHA : 16'd0;
            options = blend ? {opacity, 24'd0} : 32'd0;
            launch();
            expected_pixels = 0;
            for (j = 0; j < dsh; j = j + 1)
                for (i = 0; i < dsw; i = i + 1) begin
                    in_rect = i >= dxi && i < dxi + w && j >= dyi && j < dyi + h &&
                             i >= x0 && i < x1 && j >= y0 && j < y1;
                    if (in_rect) begin
                        expected_pixels = expected_pixels + 1;
                        value = 0;
                        for (k = 0; k < dbpp; k = k + 1)
                            value = {value[23:0],
                                     px_before[8 + j * dpitch + i * dbpp + k]};
                        sample_x = ((i - dxi) * step_x) >> 24;
                        sample_y = ((j - dyi) * step_y) >> 24;
                        want = ref_blit(sf, df, blend, opacity,
                            mem_pixel(soff + (syi + sample_y) * spitch +
                                      (sxi + sample_x) * sbpp, sbpp),
                            value);
                    end
                    for (k = 0; k < dbpp; k = k + 1) begin
                        want_byte = in_rect ? want >> (8 * (dbpp - 1 - k)) :
                            px_before[8 + j * dpitch + i * dbpp + k];
                        if (memory_i.read_byte(doff + j * dpitch + i * dbpp + k)
                            !== want_byte)
                            $fatal(1, "pixel case %0d->%0d blend=%0d op=%02x serial=%0d %0dx%0d->%0dx%0d: (%0d,%0d) byte %0d = %02x expected %02x",
                                   sf, df, blend, opacity, serial, sw, sh, w, h, i, j, k,
                                   memory_i.read_byte(doff + j * dpitch + i * dbpp + k),
                                   want_byte);
                    end
                end
            // Bytes around the surface rows are untouched.
            for (k = 0; k < 8; k = k + 1)
                if (memory_i.read_byte(doff - 8 + k) !== px_before[k])
                    $fatal(1, "pixel case wrote before the surface");
            for (j = 0; j < dsh; j = j + 1)
                for (k = dsw * dbpp; k < dpitch; k = k + 1)
                    if (memory_i.read_byte(doff + j * dpitch + k) !==
                        px_before[8 + j * dpitch + k])
                        $fatal(1, "pixel case wrote row padding");
            if (status != `ASTRA_RENDER_STATUS_OK ||
                completed_pixels != expected_pixels)
                $fatal(1, "pixel case status=%0d pixels=%0d expected %0d",
                       status, completed_pixels, expected_pixels);
            if (blitter_i.fast_copy_q !== (!serial && sw <= w) ||
                blitter_i.pixel_stream_q !== (sw <= w))
                $fatal(1, "pixel case took the wrong path fast=%0d stream=%0d",
                       blitter_i.fast_copy_q, blitter_i.pixel_stream_q);
        end
    endtask

    integer row;
    integer column;
    integer before_reads;
    integer before_writes;
    integer rop_index;
    integer source_lane;
    integer destination_lane;
    reg [7:0] expected;
    integer px_case, px_s, px_d, px_w, px_h, px_sw, px_sh;
    reg px_blend;
    initial begin
        repeat (6) @(posedge clk);
        reset = 1'b0;
        memory_i.clear_memory(8'ha5);

        // Signed destination coordinates and the command clip both trim this
        // fill to the exact 3x3 rectangle (1,1)..(3,3).
        set_index_surface(32'h4000, 32'h1000, 16'd8, 16'd8, 32'd16);
        opcode = `ASTRA_RENDER_OP_FILL;
        clip_left = 16'sd1;
        clip_top = 16'sd1;
        clip_right = 16'sd7;
        clip_bottom = 16'sd7;
        destination_x = -16'sd2;
        destination_y = -16'sd1;
        destination_width = 16'd6;
        destination_height = 16'd5;
        source_width = 16'd0;
        source_height = 16'd0;
        options = 32'h0000005a;
        same_surface = 1'b0;
        launch();
        if (status != `ASTRA_RENDER_STATUS_OK || completed_pixels != 32'd9)
            $fatal(1, "clipped fill status=%0d pixels=%0d",
                   status, completed_pixels);
        for (row = 0; row < 8; row = row + 1)
            for (column = 0; column < 8; column = column + 1) begin
                expected = row >= 1 && row < 4 && column >= 1 && column < 4 ?
                    8'h5a : 8'ha5;
                expect_byte(32'h1000 + row * 16 + column, expected);
            end

        // RGB565 and direct-color fills prove canonical big-endian bytes.
        destination_data_offset = 32'h2000;
        destination_pitch = 32'd16;
        destination_surface_width = 16'd4;
        destination_surface_height = 16'd4;
        destination_format = `ASTRA_RENDER_FORMAT_RGB565;
        destination_bpp = 3'd2;
        clip_left = 16'sd0;
        clip_top = 16'sd0;
        clip_right = 16'sd4;
        clip_bottom = 16'sd4;
        destination_x = 16'sd1;
        destination_y = 16'sd1;
        destination_width = 16'd2;
        destination_height = 16'd2;
        options = 32'h00001234;
        launch();
        expect_byte(32'h2012, 8'h12);
        expect_byte(32'h2013, 8'h34);
        expect_byte(32'h2024, 8'h12);
        expect_byte(32'h2025, 8'h34);

        destination_data_offset = 32'h3000;
        destination_format = `ASTRA_RENDER_FORMAT_XRGB8888;
        destination_bpp = 3'd4;
        destination_x = 16'sd0;
        destination_y = 16'sd0;
        destination_width = 16'd1;
        destination_height = 16'd1;
        options = 32'hff112233;
        launch();
        expect_byte(32'h3000, 8'hff);
        expect_byte(32'h3001, 8'h11);
        expect_byte(32'h3002, 8'h22);
        expect_byte(32'h3003, 8'h33);

        // Copy clipping adjusts source and destination together.
        memory_i.clear_memory(8'hee);
        set_index_surface(32'h4000, 32'h5000, 16'd8, 16'd4, 32'd16);
        for (row = 0; row < 4; row = row + 1)
            for (column = 0; column < 8; column = column + 1)
                memory_i.write_byte(32'h4000 + row * 16 + column,
                                    row * 16 + column);
        opcode = `ASTRA_RENDER_OP_BLIT;
        source_x = 16'sd1;
        source_y = 16'sd1;
        destination_x = 16'sd2;
        destination_y = 16'sd1;
        source_width = 16'd4;
        source_height = 16'd2;
        destination_width = 16'd4;
        destination_height = 16'd2;
        clip_left = 16'sd3;
        clip_top = 16'sd1;
        clip_right = 16'sd6;
        clip_bottom = 16'sd3;
        options = 32'd0;
        same_surface = 1'b0;
        launch();
        if (status != `ASTRA_RENDER_STATUS_OK || completed_pixels != 32'd6)
            $fatal(1, "copy status=%0d pixels=%0d", status, completed_pixels);
        for (row = 0; row < 2; row = row + 1)
            for (column = 0; column < 3; column = column + 1)
                expect_byte(32'h5000 + (row + 1) * 16 + column + 3,
                            (row + 1) * 16 + column + 2);

        // Window movement is dominated by unscaled same-format RGB565 copy.
        // Keep that hardware hot path below the interactive frame budget.
        memory_i.clear_memory(8'hee);
        source_data_offset = 32'h1000;
        destination_data_offset = 32'h4000;
        source_pitch = 32'd128;
        destination_pitch = 32'd128;
        source_surface_width = 16'd64;
        source_surface_height = 16'd16;
        destination_surface_width = 16'd64;
        destination_surface_height = 16'd16;
        source_format = `ASTRA_RENDER_FORMAT_RGB565;
        destination_format = `ASTRA_RENDER_FORMAT_RGB565;
        source_bpp = 3'd2;
        destination_bpp = 3'd2;
        source_x = 16'sd0;
        source_y = 16'sd0;
        destination_x = 16'sd0;
        destination_y = 16'sd0;
        source_width = 16'd64;
        source_height = 16'd16;
        destination_width = 16'd64;
        destination_height = 16'd16;
        clip_left = 16'sd0;
        clip_top = 16'sd0;
        clip_right = 16'sd64;
        clip_bottom = 16'sd16;
        for (row = 0; row < 16; row = row + 1)
            for (column = 0; column < 128; column = column + 1)
                memory_i.write_byte(32'h1000 + row * 128 + column,
                                    row + column);
        before_reads = read_transactions;
        before_writes = write_transactions;
        launch();
        if (status != `ASTRA_RENDER_STATUS_OK ||
            completed_pixels != 32'd1024 || last_elapsed > 1500 ||
            read_transactions - before_reads != 16 ||
            write_transactions - before_writes != 16)
            $fatal(1,
                   "identity RGB565 status=%0d pixels=%0d cycles=%0d/1500 reads=%0d writes=%0d",
                   status, completed_pixels, last_elapsed,
                   read_transactions - before_reads,
                   write_transactions - before_writes);
        $display("identity RGB565 cycles=%0d/1500 bursts=%0d",
                 last_elapsed, read_transactions - before_reads);
        for (row = 0; row < 16; row = row + 1)
            for (column = 0; column < 128; column = column + 1)
                expect_byte(32'h4000 + row * 128 + column,
                            row + column);

        // Every possible source/destination byte-lane pair must realign
        // exactly while preserving bytes outside the copied rectangle.
        memory_i.clear_memory(8'hee);
        set_index_surface(32'h8000, 32'ha000, 16'd37, 16'd1, 32'd64);
        source_x = 16'sd0;
        source_y = 16'sd0;
        destination_x = 16'sd0;
        destination_y = 16'sd0;
        source_width = 16'd37;
        source_height = 16'd1;
        destination_width = 16'd37;
        destination_height = 16'd1;
        clip_right = 16'sd37;
        clip_bottom = 16'sd1;
        for (source_lane = 0; source_lane < 8;
             source_lane = source_lane + 1)
            for (destination_lane = 0; destination_lane < 8;
                 destination_lane = destination_lane + 1) begin
                source_data_offset = 32'h8000 + source_lane;
                destination_data_offset = 32'ha000 + destination_lane;
                for (column = 0; column < 64; column = column + 1)
                    memory_i.write_byte(32'ha000 + column, 8'hee);
                for (column = 0; column < 37; column = column + 1)
                    memory_i.write_byte(32'h8000 + source_lane + column,
                                        source_lane * 8 + column);
                launch();
                if (status != `ASTRA_RENDER_STATUS_OK ||
                    completed_pixels != 32'd37)
                    $fatal(1, "lane %0d->%0d status=%0d pixels=%0d",
                           source_lane, destination_lane, status,
                           completed_pixels);
                if (destination_lane != 0)
                    expect_byte(32'h9fff + destination_lane, 8'hee);
                for (column = 0; column < 37; column = column + 1)
                    expect_byte(32'ha000 + destination_lane + column,
                                source_lane * 8 + column);
                expect_byte(32'ha000 + destination_lane + 37, 8'hee);
            end

        // Exact compositor cache update: an 816-pixel RGB565 client is
        // copied two pixels into an 820-pixel decorated window.  The four-byte
        // lane shift must use the burst mover, preserve the frame bytes, and
        // remain comfortably in_rect one 60 Hz frame.
        memory_i.clear_memory(8'hee);
        source_data_offset = 32'h00010000;
        destination_data_offset = 32'h00100000;
        source_pitch = 32'd1632;
        destination_pitch = 32'd1640;
        source_surface_width = 16'd816;
        source_surface_height = 16'd440;
        destination_surface_width = 16'd820;
        destination_surface_height = 16'd440;
        source_format = `ASTRA_RENDER_FORMAT_RGB565;
        destination_format = `ASTRA_RENDER_FORMAT_RGB565;
        source_bpp = 3'd2;
        destination_bpp = 3'd2;
        source_x = 16'sd0;
        source_y = 16'sd0;
        destination_x = 16'sd2;
        destination_y = 16'sd0;
        source_width = 16'd816;
        source_height = 16'd440;
        destination_width = 16'd816;
        destination_height = 16'd440;
        clip_left = 16'sd0;
        clip_top = 16'sd0;
        clip_right = 16'sd820;
        clip_bottom = 16'sd440;
        same_surface = 1'b0;
        for (row = 0; row < 440; row = row + 1)
            for (column = 0; column < 1632; column = column + 1)
                memory_i.write_byte(32'h00010000 + row * 1632 + column,
                                    row + column);
        before_reads = read_transactions;
        before_writes = write_transactions;
        launch();
        if (status != `ASTRA_RENDER_STATUS_OK ||
            completed_pixels != 32'd359040 || last_elapsed > 450000 ||
            read_transactions - before_reads != 3321 ||
            write_transactions - before_writes != 3321)
            $fatal(1,
                   "offset RGB565 status=%0d pixels=%0d cycles=%0d/450000 reads=%0d writes=%0d",
                   status, completed_pixels, last_elapsed,
                   read_transactions - before_reads,
                   write_transactions - before_writes);
        $display("offset RGB565 cycles=%0d bursts=%0d",
                 last_elapsed, read_transactions - before_reads);
        for (row = 0; row < 440; row = row + 1) begin
            for (column = 0; column < 4; column = column + 1)
                expect_byte(32'h00100000 + row * 1640 + column, 8'hee);
            for (column = 0; column < 1632; column = column + 1)
                expect_byte(32'h00100004 + row * 1640 + column,
                            row + column);
            for (column = 1636; column < 1640; column = column + 1)
                expect_byte(32'h00100000 + row * 1640 + column, 8'hee);
        end

        // An unaligned row that straddles a 4KiB boundary must preserve the
        // neighboring bytes and split both AXI bursts at that boundary.
        memory_i.clear_memory(8'hee);
        set_index_surface(32'h0ffb, 32'h4ffb, 16'd32, 16'd2, 32'd4096);
        source_x = 16'sd0;
        source_y = 16'sd0;
        destination_x = 16'sd0;
        destination_y = 16'sd0;
        source_width = 16'd32;
        source_height = 16'd2;
        destination_width = 16'd32;
        destination_height = 16'd2;
        clip_right = 16'sd32;
        clip_bottom = 16'sd2;
        for (row = 0; row < 2; row = row + 1)
            for (column = 0; column < 32; column = column + 1)
                memory_i.write_byte(32'h0ffb + row * 4096 + column,
                                    row * 64 + column);
        before_reads = read_transactions;
        before_writes = write_transactions;
        launch();
        if (status != `ASTRA_RENDER_STATUS_OK ||
            completed_pixels != 32'd64 ||
            read_transactions - before_reads != 4 ||
            write_transactions - before_writes != 4)
            $fatal(1, "4KiB split status=%0d pixels=%0d reads=%0d writes=%0d",
                   status, completed_pixels,
                   read_transactions - before_reads,
                   write_transactions - before_writes);
        for (row = 0; row < 2; row = row + 1) begin
            expect_byte(32'h4ffa + row * 4096, 8'hee);
            for (column = 0; column < 32; column = column + 1)
                expect_byte(32'h4ffb + row * 4096 + column,
                            row * 64 + column);
            expect_byte(32'h501b + row * 4096, 8'hee);
        end

        // Exact desktop presentation from the captured broken batch: copy a
        // separate 1280x644 RGB565 surface into scanout at y=34.  The pattern
        // is unique across the 512-pixel boundary seen on HDMI.
        memory_i.clear_memory(8'hee);
        source_data_offset = 32'h10000;
        destination_data_offset = 32'h200000;
        source_pitch = 32'd2560;
        destination_pitch = 32'd2560;
        source_format = `ASTRA_RENDER_FORMAT_RGB565;
        destination_format = `ASTRA_RENDER_FORMAT_RGB565;
        source_bpp = 3'd2;
        destination_bpp = 3'd2;
        source_surface_width = 16'd1280;
        source_surface_height = 16'd644;
        destination_surface_width = 16'd1280;
        destination_surface_height = 16'd720;
        source_y = 16'sd0;
        destination_y = 16'sd34;
        source_width = 16'd1280;
        source_height = 16'd644;
        destination_width = 16'd1280;
        destination_height = 16'd644;
        clip_right = 16'sd1280;
        clip_bottom = 16'sd720;
        same_surface = 1'b0;
        for (row = 0; row < 644; row = row + 1)
            for (column = 0; column < 1280; column = column + 1) begin
                memory_i.write_byte(32'h10000 + row * 2560 + column * 2,
                                    screen_pixel(column, row) >> 8);
                memory_i.write_byte(32'h10001 + row * 2560 + column * 2,
                                    screen_pixel(column, row));
            end
        before_reads = read_transactions;
        before_writes = write_transactions;
        launch();
        if (status != `ASTRA_RENDER_STATUS_OK ||
            completed_pixels != 32'd824320 || last_elapsed > 900000 ||
            read_transactions - before_reads != 7728 ||
            write_transactions - before_writes != 7728)
            $fatal(1,
                   "desktop RGB565 status=%0d pixels=%0d cycles=%0d/900000 reads=%0d writes=%0d",
                   status, completed_pixels, last_elapsed,
                   read_transactions - before_reads,
                   write_transactions - before_writes);
        $display("desktop RGB565 cycles=%0d/900000 bursts=%0d",
                 last_elapsed, read_transactions - before_reads);
        for (row = 0; row < 720; row = row + 1)
            for (column = 0; column < 1280; column = column + 1) begin
                expect_byte(32'h200000 + row * 2560 + column * 2,
                            row >= 34 && row < 678 ?
                                screen_pixel(column, row - 34) >> 8 : 8'hee);
                expect_byte(32'h200001 + row * 2560 + column * 2,
                            row >= 34 && row < 678 ?
                                screen_pixel(column, row - 34) : 8'hee);
            end

        // Scaling is sampled in command space before reflection, so clipping
        // cannot change which source texel belongs to a destination pixel.
        memory_i.clear_memory(8'hee);
        set_index_surface(32'h4000, 32'h5000, 16'd8, 16'd6, 32'd16);
        destination_surface_width = 16'd12;
        destination_surface_height = 16'd10;
        for (row = 0; row < 6; row = row + 1)
            for (column = 0; column < 8; column = column + 1)
                memory_i.write_byte(32'h4000 + row * 16 + column,
                                    row * 16 + column);
        source_x = 16'sd1;
        source_y = 16'sd1;
        destination_x = -16'sd1;
        destination_y = 16'sd1;
        source_width = 16'd4;
        source_height = 16'd3;
        destination_width = 16'd8;
        destination_height = 16'd6;
        clip_left = 16'sd1;
        clip_top = 16'sd2;
        clip_right = 16'sd6;
        clip_bottom = 16'sd6;
        command_flags = `ASTRA_RENDER_FLAG_BLIT_REFLECT_X |
                        `ASTRA_RENDER_FLAG_BLIT_REFLECT_Y;
        launch();
        if (status != `ASTRA_RENDER_STATUS_OK || completed_pixels != 32'd20)
            $fatal(1, "scaled reflection status=%0d pixels=%0d",
                   status, completed_pixels);
        for (row = 2; row < 6; row = row + 1)
            for (column = 1; column < 6; column = column + 1) begin
                expected = (3 - (((row - 1) * 3) / 6)) * 16 +
                           (4 - (((column + 1) * 4) / 8));
                expect_byte(32'h5000 + row * 16 + column, expected);
            end
        expect_byte(32'h5000 + 2 * 16, 8'hee);
        expect_byte(32'h5000 + 2 * 16 + 6, 8'hee);
        command_flags = 16'd0;

        // Downscaling uses the same floor(offset * source / destination)
        // contract and must not sample beyond the declared source rectangle.
        destination_surface_width = 16'd4;
        destination_surface_height = 16'd3;
        source_x = 16'sd1;
        source_y = 16'sd1;
        destination_x = 16'sd0;
        destination_y = 16'sd0;
        source_width = 16'd6;
        source_height = 16'd4;
        destination_width = 16'd3;
        destination_height = 16'd2;
        clip_left = 16'sd0;
        clip_top = 16'sd0;
        clip_right = 16'sd4;
        clip_bottom = 16'sd3;
        launch();
        if (status != `ASTRA_RENDER_STATUS_OK || completed_pixels != 32'd6)
            $fatal(1, "downscale status=%0d pixels=%0d",
                   status, completed_pixels);
        for (row = 0; row < 2; row = row + 1)
            for (column = 0; column < 3; column = column + 1) begin
                expected = (1 + row * 2) * 16 + (1 + column * 2);
                expect_byte(32'h5000 + row * 16 + column, expected);
            end

        // Keyed pixels consume source work but never reach the destination
        // reader or writer.
        memory_i.clear_memory(8'haa);
        set_index_surface(32'h4000, 32'h5000, 16'd8, 16'd2, 32'd16);
        source_x = 16'sd0;
        source_y = 16'sd0;
        destination_x = 16'sd0;
        destination_y = 16'sd0;
        source_width = 16'd4;
        source_height = 16'd1;
        destination_width = 16'd4;
        destination_height = 16'd1;
        clip_left = 16'sd0;
        clip_top = 16'sd0;
        clip_right = 16'sd8;
        clip_bottom = 16'sd2;
        memory_i.write_byte(32'h4000, 8'h01);
        memory_i.write_byte(32'h4001, 8'h02);
        memory_i.write_byte(32'h4002, 8'h01);
        memory_i.write_byte(32'h4003, 8'h03);
        command_flags = `ASTRA_RENDER_FLAG_BLIT_SOURCE_KEY;
        options = 32'h00000001;
        launch();
        if (status != `ASTRA_RENDER_STATUS_OK || completed_pixels != 32'd2)
            $fatal(1, "source key status=%0d pixels=%0d",
                   status, completed_pixels);
        expect_byte(32'h5000, 8'haa);
        expect_byte(32'h5001, 8'h02);
        expect_byte(32'h5002, 8'haa);
        expect_byte(32'h5003, 8'h03);

        // The ROP nibble is an exact {S,D} truth table. Exercise every one
        // against the same source/destination pair.
        source_width = 16'd1;
        destination_width = 16'd1;
        memory_i.write_byte(32'h4000, 8'h5a);
        options = 32'd0;
        for (rop_index = 0; rop_index < 16; rop_index = rop_index + 1) begin
            memory_i.write_byte(32'h5000, 8'ha5);
            command_flags = `ASTRA_RENDER_FLAG_BLIT_ROP_ENABLE |
                            (rop_index <<
                             `ASTRA_RENDER_FLAG_BLIT_ROP_SHIFT);
            launch();
            if (status != `ASTRA_RENDER_STATUS_OK ||
                completed_pixels != 32'd1)
                $fatal(1, "ROP %0d status=%0d pixels=%0d",
                       rop_index, status, completed_pixels);
            expect_byte(32'h5000,
                        expected_rop8(rop_index[3:0], 8'h5a, 8'ha5));
        end
        command_flags = 16'd0;

        // An INDEX8 palette is an immutable source-descriptor attachment in
        // DDR. Palette expansion composes with constant-opacity source-over.
        destination_surface_width = 16'd1;
        destination_surface_height = 16'd1;
        destination_format = `ASTRA_RENDER_FORMAT_XRGB8888;
        destination_bpp = 3'd4;
        source_width = 16'd1;
        source_height = 16'd1;
        destination_width = 16'd1;
        destination_height = 16'd1;
        source_palette_offset = 32'h00006000;
        auxiliary_data_offset = 32'h00007000;
        auxiliary_pitch = 32'd1;
        auxiliary_surface_width = 16'd8;
        auxiliary_surface_height = 16'd1;
        memory_i.write_byte(32'h4000, 8'h02);
        memory_i.write_byte(32'h6008, 8'h80);
        memory_i.write_byte(32'h6009, 8'h80);
        memory_i.write_byte(32'h600a, 8'h00);
        memory_i.write_byte(32'h600b, 8'h00);
        memory_i.write_byte(32'h5000, 8'hff);
        memory_i.write_byte(32'h5001, 8'h00);
        memory_i.write_byte(32'h5002, 8'h00);
        memory_i.write_byte(32'h5003, 8'hff);
        memory_i.write_byte(32'h7000, 8'h80);
        command_flags = `ASTRA_RENDER_FLAG_BLIT_PALETTE |
                        `ASTRA_RENDER_FLAG_BLIT_ALPHA |
                        `ASTRA_RENDER_FLAG_BLIT_MASK1;
        options = 32'h80000000;
        launch();
        // Straight alpha: a' = m(0x80, 0x80) = 0x40, r = m(0x80, a') = 0x20.
        expect_byte(32'h5000, 8'hff);
        expect_byte(32'h5001, 8'h20);
        expect_byte(32'h5002, 8'h00);
        expect_byte(32'h5003, 8'hbf);
        memory_i.write_byte(32'h5000, 8'hff);
        memory_i.write_byte(32'h5001, 8'h00);
        memory_i.write_byte(32'h5002, 8'h00);
        memory_i.write_byte(32'h5003, 8'hff);
        memory_i.write_byte(32'h7000, 8'h00);
        launch();
        if (status != `ASTRA_RENDER_STATUS_OK || completed_pixels != 32'd0)
            $fatal(1, "masked pixel status=%0d pixels=%0d",
                   status, completed_pixels);
        expect_byte(32'h5000, 8'hff);
        expect_byte(32'h5001, 8'h00);
        expect_byte(32'h5002, 8'h00);
        expect_byte(32'h5003, 8'hff);

        // MASK1 follows the registered, scaled, reflected source coordinate.
        // Source bits 0 and 2 are enabled; a reflected 2x blit therefore
        // writes destination columns 2-3 and 6-7 only.
        memory_i.clear_memory(8'haa);
        set_index_surface(32'h4000, 32'h5000, 16'd8, 16'd1, 32'd16);
        source_width = 16'd4;
        source_height = 16'd1;
        destination_width = 16'd8;
        destination_height = 16'd1;
        destination_surface_width = 16'd8;
        destination_surface_height = 16'd1;
        source_palette_offset = 32'd0;
        auxiliary_data_offset = 32'h00007000;
        auxiliary_pitch = 32'd1;
        auxiliary_surface_width = 16'd8;
        auxiliary_surface_height = 16'd1;
        memory_i.write_byte(32'h4000, 8'h01);
        memory_i.write_byte(32'h4001, 8'h02);
        memory_i.write_byte(32'h4002, 8'h03);
        memory_i.write_byte(32'h4003, 8'h04);
        memory_i.write_byte(32'h7000, 8'ha0);
        command_flags = `ASTRA_RENDER_FLAG_BLIT_REFLECT_X |
                        `ASTRA_RENDER_FLAG_BLIT_MASK1;
        options = 32'd0;
        launch();
        if (status != `ASTRA_RENDER_STATUS_OK || completed_pixels != 32'd4)
            $fatal(1, "scaled reflected mask status=%0d pixels=%0d",
                   status, completed_pixels);
        expect_byte(32'h5000, 8'haa);
        expect_byte(32'h5001, 8'haa);
        expect_byte(32'h5002, 8'h03);
        expect_byte(32'h5003, 8'h03);
        expect_byte(32'h5004, 8'haa);
        expect_byte(32'h5005, 8'haa);
        expect_byte(32'h5006, 8'h01);
        expect_byte(32'h5007, 8'h01);

        // MASK1 pitch must cover every declared auxiliary-surface column.
        auxiliary_surface_width = 16'd9;
        auxiliary_pitch = 32'd1;
        launch();
        if (status != `ASTRA_RENDER_STATUS_BAD_RANGE ||
            fault_detail != 32'h00010001 || completed_pixels != 32'd0)
            $fatal(1,
                   "undersized mask pitch status=%0d fault=%08x pixels=%0d",
                   status, fault_detail, completed_pixels);

        source_palette_offset = 32'd0;
        auxiliary_data_offset = 32'd0;
        auxiliary_pitch = 32'd0;
        auxiliary_surface_width = 16'd0;
        auxiliary_surface_height = 16'd0;
        command_flags = 16'd0;
        options = 32'd0;

        // Same-row right shift must traverse backwards like memmove.
        set_index_surface(32'h6000, 32'h6000, 16'd16, 16'd4, 32'd16);
        for (column = 0; column < 16; column = column + 1)
            memory_i.write_byte(32'h6000 + column, column);
        source_x = 16'sd0;
        source_y = 16'sd0;
        destination_x = 16'sd2;
        destination_y = 16'sd0;
        source_width = 16'd12;
        source_height = 16'd1;
        destination_width = 16'd12;
        destination_height = 16'd1;
        clip_left = 16'sd0;
        clip_top = 16'sd0;
        clip_right = 16'sd16;
        clip_bottom = 16'sd4;
        same_surface = 1'b1;
        launch();
        expect_byte(32'h6000, 8'd0);
        expect_byte(32'h6001, 8'd1);
        for (column = 0; column < 12; column = column + 1)
            expect_byte(32'h6002 + column, column);
        expect_byte(32'h600e, 8'd14);
        expect_byte(32'h600f, 8'd15);

        // Vertical overlap uses bottom-up traversal.
        set_index_surface(32'h6100, 32'h6100, 16'd8, 16'd4, 32'd8);
        for (row = 0; row < 4; row = row + 1)
            for (column = 0; column < 8; column = column + 1)
                memory_i.write_byte(32'h6100 + row * 8 + column,
                                    row * 16 + column);
        source_x = 16'sd0;
        source_y = 16'sd0;
        destination_x = 16'sd0;
        destination_y = 16'sd1;
        source_width = 16'd8;
        source_height = 16'd3;
        destination_width = 16'd8;
        destination_height = 16'd3;
        clip_right = 16'sd8;
        clip_bottom = 16'sd4;
        launch();
        for (row = 1; row < 4; row = row + 1)
            for (column = 0; column < 8; column = column + 1)
                expect_byte(32'h6100 + row * 8 + column,
                            (row - 1) * 16 + column);

        // Direct-color conversion expands through canonical ARGB and packs
        // back to the destination format without host-endian dependence.
        memory_i.clear_memory(8'hee);
        source_data_offset = 32'h2000;
        destination_data_offset = 32'h3000;
        source_pitch = 32'd8;
        destination_pitch = 32'd16;
        source_surface_width = 16'd2;
        source_surface_height = 16'd1;
        destination_surface_width = 16'd2;
        destination_surface_height = 16'd1;
        source_format = `ASTRA_RENDER_FORMAT_RGB565;
        source_bpp = 3'd2;
        destination_format = `ASTRA_RENDER_FORMAT_XRGB8888;
        destination_bpp = 3'd4;
        source_x = 16'sd0;
        source_y = 16'sd0;
        destination_x = 16'sd0;
        destination_y = 16'sd0;
        source_width = 16'd2;
        source_height = 16'd1;
        destination_width = 16'd2;
        destination_height = 16'd1;
        clip_left = 16'sd0;
        clip_top = 16'sd0;
        clip_right = 16'sd2;
        clip_bottom = 16'sd1;
        same_surface = 1'b0;
        memory_i.write_byte(32'h2000, 8'hf8);
        memory_i.write_byte(32'h2001, 8'h00);
        memory_i.write_byte(32'h2002, 8'h07);
        memory_i.write_byte(32'h2003, 8'he0);
        launch();
        expect_byte(32'h3000, 8'hff);
        expect_byte(32'h3001, 8'hff);
        expect_byte(32'h3002, 8'h00);
        expect_byte(32'h3003, 8'h00);
        expect_byte(32'h3004, 8'hff);
        expect_byte(32'h3005, 8'h00);
        expect_byte(32'h3006, 8'hff);
        expect_byte(32'h3007, 8'h00);

        source_data_offset = 32'h2400;
        destination_data_offset = 32'h3400;
        source_pitch = 32'd8;
        destination_pitch = 32'd8;
        source_surface_width = 16'd1;
        destination_surface_width = 16'd1;
        source_format = `ASTRA_RENDER_FORMAT_ARGB8888;
        source_bpp = 3'd4;
        destination_format = `ASTRA_RENDER_FORMAT_RGB565;
        destination_bpp = 3'd2;
        source_width = 16'd1;
        destination_width = 16'd1;
        memory_i.write_byte(32'h2400, 8'h80);
        memory_i.write_byte(32'h2401, 8'h40);
        memory_i.write_byte(32'h2402, 8'h20);
        memory_i.write_byte(32'h2403, 8'h10);
        launch();
        expect_byte(32'h3400, 8'h41);
        expect_byte(32'h3401, 8'h02);

        // ARGB sources are straight alpha (the BLEND equation of
        // docs/TEXTURE_ENGINE.md): opacity scales alpha, a' = m(a, opacity),
        // then out = m(src, a') + m(dst, 255 - a').
        destination_data_offset = 32'h3800;
        destination_format = `ASTRA_RENDER_FORMAT_XRGB8888;
        destination_bpp = 3'd4;
        memory_i.write_byte(32'h2400, 8'h80);
        memory_i.write_byte(32'h2401, 8'h80);
        memory_i.write_byte(32'h2402, 8'h00);
        memory_i.write_byte(32'h2403, 8'h00);
        memory_i.write_byte(32'h3800, 8'hff);
        memory_i.write_byte(32'h3801, 8'h00);
        memory_i.write_byte(32'h3802, 8'h00);
        memory_i.write_byte(32'h3803, 8'hff);
        command_flags = `ASTRA_RENDER_FLAG_BLIT_ALPHA;
        options = 32'h80000000;
        launch();
        expect_byte(32'h3800, 8'hff);
        expect_byte(32'h3801, 8'h20);
        expect_byte(32'h3802, 8'h00);
        expect_byte(32'h3803, 8'hbf);
        command_flags = 16'd0;
        options = 32'd0;

        // An abort that lands on the cycle an address handshake completes
        // must still finish that burst: an abandoned AR leaves R beats for
        // the next reader, an abandoned AW leaves the memory waiting for W.
        for (abort_case = 0; abort_case < 2; abort_case = abort_case + 1) begin
            source_data_offset = 32'h1000;
            destination_data_offset = 32'h9000;
            source_pitch = 32'd128;
            destination_pitch = 32'd128;
            source_surface_width = 16'd64;
            source_surface_height = 16'd16;
            destination_surface_width = 16'd64;
            destination_surface_height = 16'd16;
            source_format = `ASTRA_RENDER_FORMAT_RGB565;
            destination_format = `ASTRA_RENDER_FORMAT_RGB565;
            source_bpp = 3'd2;
            destination_bpp = 3'd2;
            source_x = 16'sd0;
            source_y = 16'sd0;
            destination_x = 16'sd0;
            destination_y = 16'sd0;
            source_width = 16'd64;
            source_height = 16'd16;
            destination_width = 16'd64;
            destination_height = 16'd16;
            clip_left = 16'sd0;
            clip_top = 16'sd0;
            clip_right = 16'sd64;
            clip_bottom = 16'sd16;
            same_surface = 1'b0;
            opcode = `ASTRA_RENDER_OP_BLIT;
            command_flags = 16'd0;
            options = 32'd0;
            fork
                launch();
                begin
                    @(negedge clk);
                    while (!(copy_write_active &&
                             (abort_case == 0 ?
                                 read_arvalid && read_arready :
                                 copy_awvalid && write_awready)))
                        @(negedge clk);
                    abort = 1'b1;
                    @(negedge clk);
                    abort = 1'b0;
                end
            join
            if (status != `ASTRA_RENDER_STATUS_RESET)
                $fatal(1, "abort on %0s handshake status=%0d",
                       abort_case == 0 ? "AR" : "AW", status);
            // The bus is clean: an ordinary copy still lands exactly.
            destination_data_offset = 32'ha000;
            launch();
            if (status != `ASTRA_RENDER_STATUS_OK)
                $fatal(1, "copy after abort status=%0d", status);
            for (row = 0; row < 16; row = row + 1)
                for (column = 0; column < 128; column = column + 1)
                    expect_byte(32'ha000 + row * 128 + column,
                                memory_i.read_byte(32'h1000 + row * 128 +
                                                   column));
        end

        // Read failure is contained and never reaches the destination writer.
        set_index_surface(32'h4000, 32'h5000, 16'd8, 16'd4, 32'd16);
        source_x = 16'sd0;
        source_y = 16'sd0;
        destination_x = 16'sd0;
        destination_y = 16'sd0;
        source_width = 16'd4;
        source_height = 16'd1;
        destination_width = 16'd4;
        destination_height = 16'd1;
        same_surface = 1'b0;
        before_writes = write_transactions;
        inject_read_error = 1'b1;
        launch();
        inject_read_error = 1'b0;
        if (status != `ASTRA_RENDER_STATUS_AXI_READ ||
            write_transactions != before_writes)
            $fatal(1, "read failure containment status=%0d writes=%0d->%0d",
                   status, before_writes, write_transactions);

        // Unsupported conversion fails before source or destination DMA.
        before_reads = read_transactions;
        before_writes = write_transactions;
        source_format = `ASTRA_RENDER_FORMAT_RGB565;
        source_bpp = 3'd2;
        launch();
        if (status != `ASTRA_RENDER_STATUS_UNSUPPORTED ||
            read_transactions != before_reads ||
            write_transactions != before_writes)
            $fatal(1, "unsupported operation performed DMA");

        // A write response failure completes with a precise engine status.
        opcode = `ASTRA_RENDER_OP_FILL;
        destination_format = `ASTRA_RENDER_FORMAT_INDEX8;
        destination_bpp = 3'd1;
        destination_width = 16'd2;
        destination_height = 16'd1;
        options = 32'h00000077;
        inject_write_error = 1'b1;
        launch();
        inject_write_error = 1'b0;
        if (status != `ASTRA_RENDER_STATUS_AXI_WRITE)
            $fatal(1, "write failure status=%0d detail=%08x",
                   status, fault_detail);


        // Pixel-stream BLITs: every direct-color conversion, plain and
        // BLIT_ALPHA, over random lanes, pitches, clips and alpha, with
        // random read and write backpressure; the 4-mod-8 pitch keeps the
        // per-pixel path, so both paths meet the same reference.
        px_formats[0] = `ASTRA_RENDER_FORMAT_RGB565;
        px_formats[1] = `ASTRA_RENDER_FORMAT_XRGB8888;
        px_formats[2] = `ASTRA_RENDER_FORMAT_ARGB8888;
        inject_read_error = 1'b0;
        inject_write_error = 1'b0;
        for (px_case = 0; px_case < 90; px_case = px_case + 1) begin
            px_s = px_case % 3;
            px_d = (px_case / 3) % 3;
            px_blend = (px_case / 9) % 2;
            if (!px_blend && px_s == px_d)
                px_d = (px_d + 1) % 3;
            px_stall_mode = px_case % 4 == 3;
            px_w = px_case % 7 == 0 ? 1 + px_rand(3) : 1 + px_rand(300);
            px_h = 1 + px_rand(4);
            pixel_case(px_formats[px_s], px_formats[px_d], px_blend,
                px_case % 5 == 0 ? 8'hff : px_case % 5 == 1 ? 8'h00 :
                    px_rand(256), px_case % 4,
                px_w, px_h, 1'b0, px_case % 6 == 5, px_w, px_h);
            px_stall_mode = 0;
            stall_reads = 1'b0;
            stall_writes = 1'b0;
            if (px_case % 5 == 2) begin
                px_w = 1 + px_rand(40);
                px_h = 1 + px_rand(3);
                pixel_case(px_formats[px_s], px_formats[px_d], px_blend,
                    px_rand(256), px_case % 4, px_w, px_h, 1'b1, 1'b0,
                    px_w, px_h);
            end
        end

        // Opaque chunks skip the destination read; a translucent first or
        // last pixel on either lane of a partial beat must not.
        for (px_case = 0; px_case < 24; px_case = px_case + 1)
        begin
            px_w = 1 + px_case % 5;
            px_h = 1 + px_rand(2);
            pixel_case(`ASTRA_RENDER_FORMAT_ARGB8888,
                px_formats[px_case % 3], 1'b1, 8'hff, 2'd3,
                px_w, px_h, 1'b0, 1'b0, px_w, px_h);
        end

        // Scaled pixel-stream BLITs: horizontal upscales (and equal widths
        // with a vertical scale) against the same reference, with random
        // stalls; the serial path (4-mod-8 pitch, or a horizontal
        // downscale) must agree with it.
        for (px_case = 0; px_case < 72; px_case = px_case + 1) begin
            px_s = px_case % 3;
            px_d = (px_case / 3) % 3;
            px_blend = (px_case / 9) % 2;
            if (!px_blend && px_s == px_d)
                px_d = (px_d + 1) % 3;
            px_stall_mode = px_case % 4 == 3;
            px_w = px_case % 8 == 0 ? 1 + px_rand(4) : 1 + px_rand(300);
            px_h = 1 + px_rand(6);
            px_sw = px_case % 11 == 0 ? px_w : 1 + px_rand(px_w);
            px_sh = px_case % 5 == 0 ? 1 + px_rand(12) : 1 + px_rand(px_h);
            pixel_case(px_formats[px_s], px_formats[px_d], px_blend,
                px_rand(256), px_case % 4, px_w, px_h, 1'b0,
                px_case % 6 == 5, px_sw, px_sh);
            px_stall_mode = 0;
            stall_reads = 1'b0;
            stall_writes = 1'b0;
            if (px_case % 6 == 1) begin
                px_w = 1 + px_rand(40);
                px_sw = 1 + px_rand(px_w);
                pixel_case(px_formats[px_s], px_formats[px_d], px_blend,
                    px_rand(256), px_case % 4, px_w, px_h, 1'b1, 1'b0,
                    px_sw, px_sh);
                pixel_case(px_formats[px_s], px_formats[px_d], px_blend,
                    px_rand(256), px_case % 4, px_sw, px_h, 1'b0, 1'b0,
                    px_w, px_sh);
            end
        end
        $display("PASS astra_render_blitter");
        $finish;
    end
endmodule

`default_nettype wire
