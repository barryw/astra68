`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module tb_astra_render_flood;
    reg clk = 1'b0;
    reg reset = 1'b1;
    always #2.5 clk = ~clk;

    reg start = 1'b0;
    reg abort = 1'b0;
    wire busy, done;
    wire [15:0] status;
    wire [31:0] fault_detail, completed_pixels;
    wire writer_start, writer_abort, writer_flush, writer_flush_ready;
    wire writer_barrier, writer_barrier_ready, writer_barrier_done;
    wire writer_busy, writer_done, writer_aborted, writer_error;
    wire [31:0] writer_fault;
    wire pixel_valid, pixel_ready;
    wire [31:0] pixel_address, pixel_value;
    wire [7:0] pixel_format;

    wire [5:0] arid, awid, bid, rid;
    wire [31:0] araddr, awaddr;
    wire [7:0] arlen, awlen;
    wire [2:0] arsize, awsize;
    wire [1:0] arburst, awburst, rresp, bresp;
    wire [3:0] arcache, arqos, awcache, awqos;
    wire [2:0] arprot, awprot;
    wire arvalid, arready, rlast, rvalid, rready;
    wire [63:0] rdata, wdata;
    wire [7:0] wstrb;
    wire wlast, wvalid, wready, awvalid, awready, bvalid, bready;
    reg stall_reads = 1'b0;
    reg stall_writes = 1'b0;
    reg [31:0] cycle = 32'd0;
    reg [31:0] workspace_bytes = 32'd256;
    localparam [7:0] FORMAT_INDEX8 = `ASTRA_RENDER_FORMAT_INDEX8;

    astra_render_flood flood_i (
        .clk(clk), .reset(reset), .start(start), .abort(abort),
        .arena_base(32'd0), .clip_left(16'sd1), .clip_top(16'sd1),
        .clip_right(16'sd15), .clip_bottom(16'sd11),
        .seed_x(16'sd3), .seed_y(16'sd3), .replacement(32'd7),
        .destination_data_offset(32'h00002000),
        .destination_pitch(32'd16), .destination_width(16'd16),
        .destination_height(16'd12),
        .destination_format(FORMAT_INDEX8),
        .destination_bytes_per_pixel(3'd1),
        .workspace_data_offset(32'h00001000),
        .workspace_data_bytes(workspace_bytes), .busy(busy), .done(done),
        .status(status), .fault_detail(fault_detail),
        .completed_pixels(completed_pixels), .writer_start(writer_start),
        .writer_abort(writer_abort), .writer_flush(writer_flush),
        .writer_flush_ready(writer_flush_ready),
        .writer_barrier(writer_barrier),
        .writer_barrier_ready(writer_barrier_ready),
        .writer_barrier_done(writer_barrier_done), .writer_done(writer_done),
        .writer_aborted(writer_aborted), .writer_error(writer_error),
        .writer_fault_detail(writer_fault), .pixel_valid(pixel_valid),
        .pixel_ready(pixel_ready), .pixel_address(pixel_address),
        .pixel_format(pixel_format), .pixel_value(pixel_value),
        .m_axi_arid(arid), .m_axi_araddr(araddr), .m_axi_arlen(arlen),
        .m_axi_arsize(arsize), .m_axi_arburst(arburst),
        .m_axi_arcache(arcache), .m_axi_arprot(arprot),
        .m_axi_arqos(arqos), .m_axi_arvalid(arvalid),
        .m_axi_arready(arready), .m_axi_rid(rid), .m_axi_rdata(rdata),
        .m_axi_rresp(rresp), .m_axi_rlast(rlast), .m_axi_rvalid(rvalid),
        .m_axi_rready(rready)
    );

    astra_render_pixel_writer writer_i (
        .clk(clk), .reset(reset), .start(writer_start), .abort(writer_abort),
        .flush(writer_flush), .flush_ready(writer_flush_ready),
        .barrier(writer_barrier), .barrier_ready(writer_barrier_ready),
        .barrier_done(writer_barrier_done), .pixel_valid(pixel_valid),
        .pixel_ready(pixel_ready), .pixel_address(pixel_address),
        .pixel_format(pixel_format), .pixel_value(pixel_value),
        .busy(writer_busy), .done(writer_done), .aborted(writer_aborted),
        .write_error(writer_error), .fault_detail(writer_fault),
        .pixels_accepted(), .bytes_written(), .m_axi_awid(awid),
        .m_axi_awaddr(awaddr), .m_axi_awlen(awlen),
        .m_axi_awsize(awsize), .m_axi_awburst(awburst),
        .m_axi_awcache(awcache), .m_axi_awprot(awprot),
        .m_axi_awqos(awqos), .m_axi_awvalid(awvalid),
        .m_axi_awready(awready), .m_axi_wdata(wdata),
        .m_axi_wstrb(wstrb), .m_axi_wlast(wlast),
        .m_axi_wvalid(wvalid), .m_axi_wready(wready), .m_axi_bid(bid),
        .m_axi_bresp(bresp), .m_axi_bvalid(bvalid), .m_axi_bready(bready)
    );

    astra_render_axi_memory_model #(.MEMORY_BYTES(65536)) memory_i (
        .clk(clk), .reset(reset), .stall_reads(stall_reads),
        .stall_writes(stall_writes), .inject_read_error(1'b0),
        .inject_write_error(1'b0), .s_axi_arid(arid),
        .s_axi_araddr(araddr), .s_axi_arlen(arlen),
        .s_axi_arsize(arsize), .s_axi_arburst(arburst),
        .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rid(rid), .s_axi_rdata(rdata), .s_axi_rresp(rresp),
        .s_axi_rlast(rlast), .s_axi_rvalid(rvalid),
        .s_axi_rready(rready), .s_axi_awid(awid),
        .s_axi_awaddr(awaddr), .s_axi_awlen(awlen),
        .s_axi_awsize(awsize), .s_axi_awburst(awburst),
        .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(wstrb), .s_axi_wlast(wlast),
        .s_axi_wvalid(wvalid), .s_axi_wready(wready), .s_axi_bid(bid),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid), .s_axi_bready(bready),
        .read_transactions(), .write_transactions()
    );

    always @(posedge clk) begin
        cycle <= cycle + 32'd1;
        stall_reads <= cycle[4:0] == 5'd9;
        stall_writes <= cycle[5:0] == 6'd27;
        if (cycle == 32'd1000000)
            $fatal(1, "flood test timeout");
    end

    integer x, y;
    integer expected_count;
    integer normal_pixels;
    initial begin
        memory_i.clear_memory(8'd0);
        // A 9x7 connected region of value 1, with a three-pixel hole.
        for (y = 2; y <= 8; y = y + 1)
            for (x = 2; x <= 10; x = x + 1)
                memory_i.write_byte(32'h2000 + y * 16 + x, 8'd1);
        memory_i.write_byte(32'h2000 + 4 * 16 + 5, 8'd2);
        memory_i.write_byte(32'h2000 + 4 * 16 + 6, 8'd2);
        memory_i.write_byte(32'h2000 + 5 * 16 + 5, 8'd2);
        repeat (8) @(posedge clk);
        reset = 1'b0;
        @(negedge clk);
        start = 1'b1;
        @(negedge clk);
        start = 1'b0;
        wait (done);
        expected_count = 60;
        if (status != `ASTRA_RENDER_STATUS_OK ||
            completed_pixels != expected_count)
            $fatal(1, "flood status=%0d detail=%08x pixels=%0d expected=%0d",
                   status, fault_detail, completed_pixels, expected_count);
        normal_pixels = completed_pixels;
        for (y = 0; y < 12; y = y + 1)
            for (x = 0; x < 16; x = x + 1) begin
                if (x >= 2 && x <= 10 && y >= 2 && y <= 8 &&
                    !((x == 5 && y == 4) || (x == 6 && y == 4) ||
                      (x == 5 && y == 5))) begin
                    if (memory_i.read_byte(32'h2000 + y * 16 + x) != 8'd7)
                        $fatal(1, "connected pixel not filled x=%0d y=%0d", x, y);
                end else if (memory_i.read_byte(32'h2000 + y * 16 + x) == 8'd7) begin
                    $fatal(1, "flood escaped region x=%0d y=%0d", x, y);
                end
            end
        // The same topology needs more than one pending seed. A one-entry
        // workspace must fail explicitly rather than escaping its bound.
        for (y = 2; y <= 8; y = y + 1)
            for (x = 2; x <= 10; x = x + 1)
                memory_i.write_byte(32'h2000 + y * 16 + x, 8'd1);
        memory_i.write_byte(32'h2000 + 4 * 16 + 5, 8'd2);
        memory_i.write_byte(32'h2000 + 4 * 16 + 6, 8'd2);
        memory_i.write_byte(32'h2000 + 5 * 16 + 5, 8'd2);
        for (x = 4; x < 8; x = x + 1)
            memory_i.write_byte(32'h1000 + x, 8'ha5);
        workspace_bytes = 32'd4;
        @(negedge clk);
        start = 1'b1;
        @(negedge clk);
        start = 1'b0;
        wait (done);
        if (status != `ASTRA_RENDER_STATUS_WORK_OVERFLOW)
            $fatal(1, "bounded workspace did not overflow status=%0d detail=%08x",
                   status, fault_detail);
        for (x = 4; x < 8; x = x + 1)
            if (memory_i.read_byte(32'h1000 + x) != 8'ha5)
                $fatal(1, "workspace overflow wrote byte %0d", x);
        $display("ASTRA RENDER FLOOD PASS normal_pixels=%0d overflow_pixels=%0d",
                 normal_pixels, completed_pixels);
        $finish;
    end
endmodule

`default_nettype wire
