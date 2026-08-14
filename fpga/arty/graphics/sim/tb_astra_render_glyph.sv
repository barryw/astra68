`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module tb_astra_render_glyph;
    localparam [31:0] DESC = 32'h00000100;
    localparam [31:0] DST = 32'h00001000;
    localparam [31:0] SRC = 32'h00002000;
    localparam [31:0] PAL = 32'h00003000;

    reg clk = 1'b0;
    always #2.5 clk = ~clk;
    reg reset = 1'b1;
    reg start = 1'b0;
    reg abort = 1'b0;
    reg stall_reads = 1'b0;
    reg stall_writes = 1'b0;
    reg inject_read_error = 1'b0;
    reg inject_write_error = 1'b0;
    reg force_descriptor_gap = 1'b0;
    reg [2:0] read_gap_cycles = 3'd0;
    reg signed [15:0] clip_left = 0, clip_top = 0;
    reg signed [15:0] clip_right = 16, clip_bottom = 16;
    reg [15:0] command_flags = 0;
    reg [31:0] foreground = 0, background = 0;
    reg [7:0] transparent_index = 0;
    reg [12:0] descriptor_count = 1;
    reg [31:0] destination_pitch = 32;
    reg [7:0] destination_format = `ASTRA_RENDER_FORMAT_RGB565;
    reg [2:0] destination_bpp = 2;
    reg [31:0] source_pitch = 2;
    reg [7:0] source_format = `ASTRA_RENDER_FORMAT_MASK1;
    reg [31:0] source_palette_offset = 0;
    wire busy, done;
    wire [15:0] status;
    wire [31:0] fault_detail, completed_pixels;
    wire writer_start, writer_abort, writer_flush;
    wire writer_flush_ready, writer_busy, writer_done, writer_aborted;
    wire writer_error;
    wire [31:0] writer_fault;
    wire pixel_valid, pixel_ready;
    wire [31:0] pixel_address, pixel_value;
    wire [7:0] pixel_format;

    wire [5:0] arid, rid, memory_rid, awid, bid;
    wire [31:0] araddr, awaddr;
    wire [7:0] arlen, awlen;
    wire [2:0] arsize, awsize;
    wire [1:0] arburst, awburst, rresp, memory_rresp, bresp;
    wire [3:0] arcache, arqos, awcache, awqos;
    wire [2:0] arprot, awprot;
    wire arvalid, arready, rlast, memory_rlast, rvalid, memory_rvalid, rready;
    wire [63:0] rdata, memory_rdata, wdata;
    wire awvalid, awready, wlast, wvalid, wready, bvalid, bready;
    wire [7:0] wstrb;
    wire [31:0] reads, writes;
    integer test_stage = 0;

    assign rid = memory_rid;
    assign rdata = memory_rdata;
    assign rresp = memory_rresp;
    assign rlast = memory_rlast;
    assign rvalid = memory_rvalid && read_gap_cycles == 3'd0;

    always @(posedge clk) begin
        if (reset) begin
            read_gap_cycles <= 3'd0;
        end else if (read_gap_cycles != 3'd0) begin
            read_gap_cycles <= read_gap_cycles - 3'd1;
        end else if (force_descriptor_gap && rvalid && rready && !rlast) begin
            read_gap_cycles <= 3'd3;
        end
    end

    astra_render_glyph dut (
        .clk(clk), .reset(reset), .start(start), .abort(abort),
        .arena_base(32'd0), .clip_left(clip_left), .clip_top(clip_top),
        .clip_right(clip_right), .clip_bottom(clip_bottom),
        .command_flags(command_flags), .foreground(foreground),
        .background(background), .transparent_index(transparent_index),
        .descriptor_offset(DESC), .descriptor_count(descriptor_count),
        .destination_data_offset(DST), .destination_pitch(destination_pitch),
        .destination_width(16'd16), .destination_height(16'd16),
        .destination_format(destination_format),
        .destination_bytes_per_pixel(destination_bpp),
        .source_data_offset(SRC), .source_data_bytes(256),
        .source_pitch(source_pitch), .source_width(16'd16),
        .source_height(16'd16),
        .source_format(source_format),
        .source_palette_offset(source_palette_offset),
        .busy(busy), .done(done), .status(status),
        .fault_detail(fault_detail), .completed_pixels(completed_pixels),
        .writer_start(writer_start), .writer_abort(writer_abort),
        .writer_flush(writer_flush), .writer_flush_ready(writer_flush_ready),
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
        .barrier(1'b0), .barrier_ready(), .barrier_done(),
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

    astra_render_axi_memory_model #(.MEMORY_BYTES(65536)) memory (
        .clk(clk), .reset(reset), .stall_reads(stall_reads),
        .stall_writes(stall_writes),
        .inject_read_error(inject_read_error),
        .inject_write_error(inject_write_error),
        .s_axi_arid(arid), .s_axi_araddr(araddr), .s_axi_arlen(arlen),
        .s_axi_arsize(arsize), .s_axi_arburst(arburst),
        .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rid(memory_rid), .s_axi_rdata(memory_rdata),
        .s_axi_rresp(memory_rresp), .s_axi_rlast(memory_rlast),
        .s_axi_rvalid(memory_rvalid),
        .s_axi_rready(rready && read_gap_cycles == 3'd0), .s_axi_awid(awid),
        .s_axi_awaddr(awaddr), .s_axi_awlen(awlen), .s_axi_awsize(awsize),
        .s_axi_awburst(awburst), .s_axi_awvalid(awvalid),
        .s_axi_awready(awready), .s_axi_wdata(wdata), .s_axi_wstrb(wstrb),
        .s_axi_wlast(wlast), .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bid(bid), .s_axi_bresp(bresp), .s_axi_bvalid(bvalid),
        .s_axi_bready(bready), .read_transactions(reads),
        .write_transactions(writes));

    task automatic put32(input [31:0] address, input [31:0] value);
        begin
            memory.write_byte(address + 0, value[31:24]);
            memory.write_byte(address + 1, value[23:16]);
            memory.write_byte(address + 2, value[15:8]);
            memory.write_byte(address + 3, value[7:0]);
        end
    endtask

    task automatic descriptor(input [15:0] width, input [15:0] height);
        begin
            put32(DESC + 0, 0);
            put32(DESC + 4, 0);
            put32(DESC + 8, 0);
            put32(DESC + 12, {width, height});
        end
    endtask

    task automatic positioned_descriptor(
        input [31:0] source_offset,
        input [15:0] source_x,
        input [15:0] source_y,
        input signed [15:0] destination_x,
        input signed [15:0] destination_y,
        input [15:0] width,
        input [15:0] height
    );
        begin
            put32(DESC + 0, source_offset);
            put32(DESC + 4, {source_x, source_y});
            put32(DESC + 8, {destination_x, destination_y});
            put32(DESC + 12, {width, height});
        end
    endtask

    task automatic launch_status(
        input [15:0] expected_status,
        input [31:0] expected_pixels
    );
        integer timeout;
        begin
            @(posedge clk); start <= 1'b1;
            @(posedge clk); start <= 1'b0;
            timeout = 0;
            while (!done && timeout < 20000) begin
                @(posedge clk); timeout = timeout + 1;
            end
            if (!done) $fatal(1, "glyph command timed out stage=%0d state=%0d",
                              test_stage, dut.state);
            if (status != expected_status)
                $fatal(1, "glyph status=%0d expected=%0d fault=%08x",
                       status, expected_status, fault_detail);
            if (completed_pixels != expected_pixels)
                $fatal(1, "glyph pixels=%0d expected=%0d",
                       completed_pixels, expected_pixels);
            @(posedge clk);
        end
    endtask

    task automatic launch(input [31:0] expected_pixels);
        begin
            launch_status(`ASTRA_RENDER_STATUS_OK, expected_pixels);
        end
    endtask

    task automatic expect16(input [31:0] address, input [15:0] value);
        reg [15:0] actual;
        begin
            actual = {memory.read_byte(address), memory.read_byte(address + 1)};
            if (actual !== value)
                $fatal(1, "pixel @%x=%04x expected=%04x", address, actual, value);
        end
    endtask

    initial begin
        repeat (5) @(posedge clk);
        reset <= 1'b0;
        memory.clear_memory(8'd0);

        // Source-format classification is command state, not per-pixel work.
        // Capture it at admission so format decode cannot sit on the sample
        // classification path for every glyph pixel.
        source_format = `ASTRA_RENDER_FORMAT_A4;
        @(posedge clk); start <= 1'b1;
        @(posedge clk); start <= 1'b0;
        #1;
        if (dut.source_apply_kind_q != dut.SOURCE_APPLY_A4)
            $fatal(1, "glyph source kind was not captured at start");
        reset <= 1'b1;
        repeat (2) @(posedge clk);
        reset <= 1'b0;

        // MASK1: foreground then transparent.
        test_stage = 1;
        descriptor(2, 1);
        source_format = `ASTRA_RENDER_FORMAT_MASK1;
        source_pitch = 2;
        foreground = 32'h0000f800;
        memory.write_byte(SRC, 8'b10000000);
        launch(1);
        expect16(DST, 16'hf800);
        expect16(DST + 2, 16'h0000);

        // MASK1 opaque background selects a second canonical destination
        // color without requiring software to expand the mask.
        command_flags = `ASTRA_RENDER_GLYPH_FLAG_OPAQUE_BACKGROUND;
        background = 32'h0000001f;
        memory.write_byte(DST, 0);
        memory.write_byte(DST + 1, 0);
        memory.write_byte(DST + 2, 0);
        memory.write_byte(DST + 3, 0);
        launch(2);
        expect16(DST, 16'hf800);
        expect16(DST + 2, 16'h001f);
        command_flags = 0;

        // Signed positioning and clip intersection suppress the off-screen
        // pixel while preserving source progression for the visible pixel.
        positioned_descriptor(0, 0, 0, -16'sd1, 16'sd2, 2, 1);
        memory.write_byte(SRC, 8'b11000000);
        memory.write_byte(DST + 2 * 32, 0);
        memory.write_byte(DST + 2 * 32 + 1, 0);
        launch(1);
        expect16(DST + 2 * 32, 16'hf800);

        // A4: full white and exact half-coverage native RGB565.
        descriptor(2, 1);
        source_format = `ASTRA_RENDER_FORMAT_A4;
        source_pitch = 8;
        foreground = 32'h0000ffff;
        memory.write_byte(DST, 0);
        memory.write_byte(DST + 1, 0);
        memory.write_byte(DST + 2, 0);
        memory.write_byte(DST + 3, 0);
        memory.write_byte(SRC, 8'hf8);
        launch(2);
        expect16(DST, 16'hffff);
        expect16(DST + 2, 16'h8c51);

        // A8: full white and round-to-nearest 128/255 coverage.
        descriptor(2, 1);
        source_format = `ASTRA_RENDER_FORMAT_A8;
        source_pitch = 16;
        foreground = 32'h0000ffff;
        memory.write_byte(DST, 0);
        memory.write_byte(DST + 1, 0);
        memory.write_byte(DST + 2, 0);
        memory.write_byte(DST + 3, 0);
        memory.write_byte(SRC, 8'hff);
        memory.write_byte(SRC + 1, 8'h80);
        launch(2);
        expect16(DST, 16'hffff);
        expect16(DST + 2, 16'h8410);

        // The same A8 coverage path writes XRGB8888 in canonical byte order.
        destination_format = `ASTRA_RENDER_FORMAT_XRGB8888;
        destination_bpp = 4;
        destination_pitch = 64;
        foreground = 32'hffff0000;
        descriptor(1, 1);
        memory.write_byte(SRC, 8'h80);
        put32(DST, 32'hff0000ff);
        launch(1);
        if ({memory.read_byte(DST), memory.read_byte(DST + 1),
             memory.read_byte(DST + 2), memory.read_byte(DST + 3)} !==
            32'hff80007f)
            $fatal(1, "A8 XRGB result=%08x expected=ff80007f",
                   {memory.read_byte(DST), memory.read_byte(DST + 1),
                    memory.read_byte(DST + 2), memory.read_byte(DST + 3)});
        destination_format = `ASTRA_RENDER_FORMAT_RGB565;
        destination_bpp = 2;
        destination_pitch = 32;

        // INDEX4 exact palette expansion and transparent suppression.
        descriptor(2, 1);
        source_format = `ASTRA_RENDER_FORMAT_INDEX4;
        source_pitch = 8;
        source_palette_offset = PAL;
        transparent_index = 2;
        memory.write_byte(SRC, 8'h12);
        put32(PAL + 4, 32'hffff0000);
        memory.write_byte(DST + 2, 8'h84);
        memory.write_byte(DST + 3, 8'h10);
        launch(1);
        expect16(DST, 16'hf800);
        expect16(DST + 2, 16'h8410);

        // INDEX8 straight-alpha red over blue.
        descriptor(1, 1);
        source_format = `ASTRA_RENDER_FORMAT_INDEX8;
        source_pitch = 16;
        transparent_index = 0;
        memory.write_byte(SRC, 8'h03);
        put32(PAL + 12, 32'h80ff0000);
        memory.write_byte(DST, 8'h00);
        memory.write_byte(DST + 1, 8'h1f);
        launch(1);
        expect16(DST, 16'h800f);

        // Indexed destinations receive exact INDEX4 samples and suppress the
        // selected transparent index without an alpha quantization path.
        descriptor(2, 1);
        destination_format = `ASTRA_RENDER_FORMAT_INDEX8;
        destination_bpp = 1;
        destination_pitch = 16;
        source_format = `ASTRA_RENDER_FORMAT_INDEX4;
        source_pitch = 8;
        transparent_index = 2;
        memory.write_byte(SRC, 8'h12);
        memory.write_byte(DST, 8'h00);
        memory.write_byte(DST + 1, 8'h55);
        launch(1);
        if (memory.read_byte(DST) !== 8'h01 ||
            memory.read_byte(DST + 1) !== 8'h55)
            $fatal(1, "INDEX4 to INDEX8 output mismatch");

        // Real HP AXI may insert idle cycles between the two descriptor
        // beats. Only accepted R-channel transfers may update beat storage.
        test_stage = 9;
        destination_format = `ASTRA_RENDER_FORMAT_RGB565;
        destination_bpp = 2;
        destination_pitch = 32;
        source_format = `ASTRA_RENDER_FORMAT_MASK1;
        source_pitch = 2;
        source_palette_offset = 0;
        descriptor(1, 1);
        memory.write_byte(SRC, 8'h80);
        force_descriptor_gap = 1'b1;
        launch(1);
        force_descriptor_gap = 1'b0;

        // AFNT owns its AXI read failure reporting rather than relying on a
        // transport-level command-fetch test as indirect evidence.
        test_stage = 10;
        destination_format = `ASTRA_RENDER_FORMAT_RGB565;
        destination_bpp = 2;
        destination_pitch = 32;
        source_format = `ASTRA_RENDER_FORMAT_MASK1;
        source_pitch = 2;
        source_palette_offset = 0;
        descriptor(1, 1);
        memory.write_byte(SRC, 8'h80);
        inject_read_error = 1'b1;
        launch_status(`ASTRA_RENDER_STATUS_AXI_READ, 0);
        inject_read_error = 1'b0;

        // Destination response failures are returned after the accepted
        // pixel count, and the engine remains reusable afterward.
        test_stage = 11;
        inject_write_error = 1'b1;
        launch_status(`ASTRA_RENDER_STATUS_AXI_WRITE, 1);
        inject_write_error = 1'b0;
        test_stage = 12;
        launch(1);

        // Cancellation during descriptor prepass completes as RESET without
        // starting the writer or retaining an unchecked descriptor.
        test_stage = 13;
        @(posedge clk); start <= 1'b1;
        @(posedge clk); start <= 1'b0;
        wait (busy);
        @(posedge clk); abort <= 1'b1;
        @(posedge clk); abort <= 1'b0;
        wait (done);
        if (status != `ASTRA_RENDER_STATUS_RESET ||
            completed_pixels != 0)
            $fatal(1, "glyph abort status=%0d pixels=%0d",
                   status, completed_pixels);
        @(posedge clk);

        $display("AFNT glyph renderer PASS reads=%0d writes=%0d", reads, writes);
        $finish;
    end
endmodule

`default_nettype wire
