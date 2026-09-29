`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

// Replays texture_vectors.c cases (generated from the bit-exact reference
// model) through the texture engine, the shared pixel writer and the AXI
// memory model, then compares every destination and the whole memory.
module tb_astra_render_texture;
    localparam integer MEMORY_BYTES = 1 << 20;
    localparam integer CASE_WORDS = 24;
    localparam integer MAX_CASES = 512;

    reg clk = 1'b0;
    always #2.5 clk = ~clk;
    reg reset = 1'b1;
    reg start = 1'b0;
    reg abort = 1'b0;
    reg stall_mode = 1'b0;
    reg inject_read_error = 1'b0;
    reg inject_write_error = 1'b0;
    reg [31:0] stall_counter = 32'd0;
    // Long write stalls outlast triangle setup, exposing any destination
    // read that does not wait for earlier writes.
    wire stall_writes = stall_mode && stall_counter[11:10] != 2'b00;

    reg signed [15:0] clip_left, clip_top, clip_right, clip_bottom;
    reg [3:0] options;
    reg textured;
    reg [31:0] vertex_offset;
    reg [12:0] triangle_count;
    reg [31:0] destination_offset, destination_pitch;
    reg [15:0] destination_width, destination_height;
    reg [7:0] destination_format;
    reg [31:0] source_offset, source_pitch, palette_offset;
    reg [15:0] source_width, source_height;
    reg [7:0] source_format;

    wire busy, done;
    wire [15:0] status;
    wire [31:0] fault_detail, completed_pixels;
    wire writer_start, writer_abort, writer_flush, writer_barrier;
    wire writer_flush_ready, writer_busy, writer_done, writer_aborted;
    wire writer_error, writer_barrier_ready, writer_barrier_done;
    wire [31:0] writer_fault;
    wire pixel_valid, pixel_ready;
    wire [31:0] pixel_address, pixel_value;
    wire [7:0] pixel_format;

    wire [5:0] arid, rid, awid, bid;
    wire [31:0] araddr, awaddr;
    wire [7:0] arlen, awlen;
    wire [2:0] arsize, awsize;
    wire [1:0] arburst, awburst, rresp, bresp;
    wire [3:0] arcache, arqos, awcache, awqos;
    wire [2:0] arprot, awprot;
    wire arvalid, arready, rlast, rvalid, rready;
    wire [63:0] rdata, wdata;
    wire awvalid, awready, wlast, wvalid, wready, bvalid, bready;
    wire [7:0] wstrb;
    wire [31:0] reads, writes;

    always @(posedge clk)
        stall_counter <= stall_counter + 32'd1;

    astra_render_texture dut (
        .clk(clk), .reset(reset), .start(start), .abort(abort),
        .arena_base(32'd0), .clip_left(clip_left), .clip_top(clip_top),
        .clip_right(clip_right), .clip_bottom(clip_bottom),
        .options(options), .textured(textured),
        .vertex_offset(vertex_offset), .triangle_count(triangle_count),
        .destination_data_offset(destination_offset),
        .destination_pitch(destination_pitch),
        .destination_width(destination_width),
        .destination_height(destination_height),
        .destination_format(destination_format),
        .source_data_offset(source_offset), .source_pitch(source_pitch),
        .source_width(source_width), .source_height(source_height),
        .source_format(source_format),
        .source_palette_offset(palette_offset),
        .busy(busy), .done(done), .status(status),
        .fault_detail(fault_detail), .completed_pixels(completed_pixels),
        .writer_start(writer_start), .writer_abort(writer_abort),
        .writer_flush(writer_flush), .writer_flush_ready(writer_flush_ready),
        .writer_barrier(writer_barrier),
        .writer_barrier_ready(writer_barrier_ready),
        .writer_barrier_done(writer_barrier_done),
        .writer_done(writer_done), .writer_aborted(writer_aborted),
        .writer_error(writer_error), .writer_fault_detail(writer_fault),
        .pixel_valid(pixel_valid), .pixel_ready(pixel_ready),
        .pixel_address(pixel_address), .pixel_format(pixel_format),
        .pixel_value(pixel_value), .m_axi_arid(arid), .m_axi_araddr(araddr),
        .m_axi_arlen(arlen), .m_axi_arsize(arsize),
        .m_axi_arburst(arburst), .m_axi_arcache(arcache),
        .m_axi_arprot(arprot), .m_axi_arqos(arqos),
        .m_axi_arvalid(arvalid), .m_axi_arready(arready), .m_axi_rid(rid),
        .m_axi_rdata(rdata), .m_axi_rresp(rresp), .m_axi_rlast(rlast),
        .m_axi_rvalid(rvalid), .m_axi_rready(rready));

    astra_render_pixel_writer writer (
        .clk(clk), .reset(reset), .start(writer_start), .abort(writer_abort),
        .flush(writer_flush), .flush_ready(writer_flush_ready),
        .barrier(writer_barrier), .barrier_ready(writer_barrier_ready),
        .barrier_done(writer_barrier_done),
        .pixel_valid(pixel_valid), .pixel_ready(pixel_ready),
        .pixel_address(pixel_address), .pixel_format(pixel_format),
        .pixel_value(pixel_value), .busy(writer_busy), .done(writer_done),
        .aborted(writer_aborted), .write_error(writer_error),
        .fault_detail(writer_fault), .pixels_accepted(), .bytes_written(),
        .m_axi_awid(awid), .m_axi_awaddr(awaddr), .m_axi_awlen(awlen),
        .m_axi_awsize(awsize), .m_axi_awburst(awburst),
        .m_axi_awcache(awcache), .m_axi_awprot(awprot), .m_axi_awqos(awqos),
        .m_axi_awvalid(awvalid), .m_axi_awready(awready), .m_axi_wdata(wdata),
        .m_axi_wstrb(wstrb), .m_axi_wlast(wlast), .m_axi_wvalid(wvalid),
        .m_axi_wready(wready), .m_axi_bid(bid), .m_axi_bresp(bresp),
        .m_axi_bvalid(bvalid), .m_axi_bready(bready));

    astra_render_axi_memory_model #(.MEMORY_BYTES(MEMORY_BYTES)) memory (
        .clk(clk), .reset(reset), .stall_reads(1'b0),
        .stall_writes(stall_writes),
        .inject_read_error(inject_read_error),
        .inject_write_error(inject_write_error),
        .s_axi_arid(arid), .s_axi_araddr(araddr), .s_axi_arlen(arlen),
        .s_axi_arsize(arsize), .s_axi_arburst(arburst),
        .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rid(rid), .s_axi_rdata(rdata), .s_axi_rresp(rresp),
        .s_axi_rlast(rlast), .s_axi_rvalid(rvalid), .s_axi_rready(rready),
        .s_axi_awid(awid), .s_axi_awaddr(awaddr), .s_axi_awlen(awlen),
        .s_axi_awsize(awsize), .s_axi_awburst(awburst),
        .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(wstrb), .s_axi_wlast(wlast),
        .s_axi_wvalid(wvalid), .s_axi_wready(wready), .s_axi_bid(bid),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid), .s_axi_bready(bready),
        .read_transactions(reads), .write_transactions(writes));

    reg [7:0] expected [0:MEMORY_BYTES-1];
    reg [31:0] case_words [0:MAX_CASES*CASE_WORDS];
    reg [7:0] saved [0:1023];
    integer case_count, case_index, byte_index, timeout, mismatches;
    integer total_cycles, total_pixels, begin_cycle, before_writes;
    reg [31:0] w [0:CASE_WORDS-1];

    task automatic run_command(output integer cycles);
        begin
            @(posedge clk); start <= 1'b1;
            @(posedge clk); start <= 1'b0;
            cycles = 0;
            while (!done && cycles < 5000000) begin
                @(posedge clk); cycles = cycles + 1;
            end
            if (!done)
                $fatal(1, "texture case %0d timed out state=%0d",
                       case_index, dut.state);
            @(posedge clk);
        end
    endtask

    task automatic check_region(input [31:0] base, input [31:0] bytes);
        integer offset;
        begin
            for (offset = 0; offset < bytes; offset = offset + 1)
                if (memory.memory[base + offset] !== expected[base + offset]) begin
                    $display("case %0d: byte %08x = %02x, model %02x (pixel x=%0d y=%0d)",
                             case_index, base + offset,
                             memory.memory[base + offset],
                             expected[base + offset],
                             (offset % w[1]) / (w[3] == `ASTRA_RENDER_FORMAT_RGB565 ? 2 : 4),
                             offset / w[1]);
                    mismatches = mismatches + 1;
                    if (mismatches > 20)
                        $fatal(1, "texture pixel mismatches");
                end
        end
    endtask

    integer word_index, cycles;
    initial begin
        clip_left = 0; clip_top = 0; clip_right = 0; clip_bottom = 0;
        options = 0; textured = 0; vertex_offset = 0; triangle_count = 1;
        destination_offset = 0; destination_pitch = 0;
        destination_width = 0; destination_height = 0;
        destination_format = 0; source_offset = 0; source_pitch = 0;
        source_width = 0; source_height = 0; source_format = 0;
        palette_offset = 0;
        memory.clear_memory(8'd0);
        for (byte_index = 0; byte_index < MEMORY_BYTES;
             byte_index = byte_index + 1)
            expected[byte_index] = 8'd0;
        $readmemh("texture_memory.hex",
                  memory.memory);
        $readmemh("texture_expected.hex",
                  expected);
        $readmemh("texture_cases.hex",
                  case_words);
        case_count = case_words[0];
        if (case_count <= 0 || case_count > MAX_CASES)
            $fatal(1, "texture vectors missing: run from the vector directory");
        mismatches = 0;
        total_cycles = 0;
        total_pixels = 0;
        repeat (4) @(posedge clk);
        reset <= 1'b0;
        repeat (2) @(posedge clk);

        for (case_index = 0; case_index < case_count;
             case_index = case_index + 1) begin
            for (word_index = 0; word_index < CASE_WORDS;
                 word_index = word_index + 1)
                w[word_index] = case_words[1 + case_index * CASE_WORDS +
                                           word_index];
            destination_offset = w[0];
            destination_pitch = w[1];
            destination_width = w[2][31:16];
            destination_height = w[2][15:0];
            destination_format = w[3][7:0];
            source_offset = w[4];
            source_pitch = w[5];
            source_width = w[6][31:16];
            source_height = w[6][15:0];
            source_format = w[7][7:0];
            palette_offset = w[8];
            options = w[9][3:0];
            textured = w[10][0];
            vertex_offset = w[11];
            triangle_count = w[12][12:0];
            clip_left = w[13][31:16];
            clip_top = w[13][15:0];
            clip_right = w[14][31:16];
            clip_bottom = w[14][15:0];
            stall_mode = w[18][0];
            before_writes = writes;
            run_command(cycles);
            if (status != w[15][15:0])
                $fatal(1, "case %0d: status %0d fault %08x, model %0d",
                       case_index, status, fault_detail, w[15]);
            if (completed_pixels != w[16])
                $fatal(1, "case %0d: %0d pixels, model %0d", case_index,
                       completed_pixels, w[16]);
            if (status != `ASTRA_RENDER_STATUS_OK && writes != before_writes)
                $fatal(1, "case %0d: rejected command wrote memory",
                       case_index);
            check_region(w[0], w[17]);
            if (mismatches != 0)
                $fatal(1, "case %0d: destination differs from the model",
                       case_index);
            if ($test$plusargs("texture_perf"))
                $display("perf case=%0d dst=%0d src=%0d textured=%0d options=%0h triangles=%0d stall=%0d pixels=%0d cycles=%0d",
                         case_index, w[3], w[7], w[10], w[9], w[12], w[18],
                         completed_pixels, cycles);
            total_cycles = total_cycles + cycles;
            total_pixels = total_pixels + completed_pixels;
        end
        stall_mode = 1'b0;

        // Nothing outside the destinations changed.
        check_region(0, MEMORY_BYTES);
        if (mismatches != 0)
            $fatal(1, "memory outside destinations differs");

        // Abort mid-command: the engine stops, reports RESET and idles.
        w[0] = case_words[1 + 0 * CASE_WORDS];
        destination_offset = w[0];
        destination_format = `ASTRA_RENDER_FORMAT_XRGB8888;
        destination_pitch = case_words[2];
        destination_width = case_words[3] >> 16;
        destination_height = case_words[3] & 16'hffff;
        vertex_offset = case_words[12];
        triangle_count = 2;
        textured = 0;
        options = `ASTRA_RENDER_TRIANGLE_OPTION_BLEND_ADD;
        clip_left = 0; clip_top = 0; clip_right = 100; clip_bottom = 100;
        @(posedge clk); start <= 1'b1;
        @(posedge clk); start <= 1'b0;
        wait (dut.state == 7'd32); // first covered pixel
        @(posedge clk); abort <= 1'b1;
        @(posedge clk); abort <= 1'b0;
        timeout = 0;
        while (!done && timeout < 10000) begin
            @(posedge clk); timeout = timeout + 1;
        end
        if (!done || status != `ASTRA_RENDER_STATUS_RESET || busy)
            $fatal(1, "abort status=%0d done=%0d", status, done);
        // As the command processor does, reset the aborted engine (and here
        // the memory model's abandoned read burst).
        reset <= 1'b1;
        repeat (4) @(posedge clk);
        reset <= 1'b0;
        repeat (2) @(posedge clk);

        // A read error during the prepass fails before any write.
        before_writes = writes;
        inject_read_error = 1'b1;
        run_command(cycles);
        inject_read_error = 1'b0;
        if (status != `ASTRA_RENDER_STATUS_AXI_READ || writes != before_writes)
            $fatal(1, "read error status=%0d writes=%0d->%0d", status,
                   before_writes, writes);

        $display("texture: %0d cases, %0d pixels, %0d cycles (%0d.%02d cycles/pixel)",
                 case_count, total_pixels, total_cycles,
                 total_cycles / total_pixels,
                 (total_cycles * 100 / total_pixels) % 100);
        $display("PASS astra_render_texture");
        $finish;
    end
endmodule

`default_nettype wire
