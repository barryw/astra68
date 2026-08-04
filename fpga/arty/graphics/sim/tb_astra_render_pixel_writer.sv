`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module tb_astra_render_pixel_writer;
    reg clk = 1'b0;
    reg reset = 1'b1;
    always #2.5 clk = ~clk;

    reg start = 1'b0;
    reg abort = 1'b0;
    reg flush = 1'b0;
    wire flush_ready;
    reg barrier = 1'b0;
    wire barrier_ready;
    wire barrier_done;
    reg pixel_valid = 1'b0;
    wire pixel_ready;
    reg [31:0] pixel_address = 32'd0;
    reg [7:0] pixel_format = 8'd0;
    reg [31:0] pixel_value = 32'd0;
    wire busy;
    wire done;
    wire aborted;
    wire write_error;
    wire [31:0] fault_detail;
    wire [31:0] pixels_accepted;
    wire [31:0] bytes_written;

    wire [5:0] awid;
    wire [31:0] awaddr;
    wire [7:0] awlen;
    wire [2:0] awsize;
    wire [1:0] awburst;
    wire [3:0] awcache;
    wire [2:0] awprot;
    wire [3:0] awqos;
    wire awvalid;
    wire awready;
    wire [63:0] wdata;
    wire [7:0] wstrb;
    wire wlast;
    wire wvalid;
    wire wready;
    wire [5:0] bid = 6'd0;
    wire [1:0] bresp;
    wire bvalid;
    wire bready;

    astra_render_pixel_writer dut (
        .clk(clk), .reset(reset), .start(start), .abort(abort),
        .flush(flush), .flush_ready(flush_ready),
        .barrier(barrier), .barrier_ready(barrier_ready),
        .barrier_done(barrier_done),
        .pixel_valid(pixel_valid), .pixel_ready(pixel_ready),
        .pixel_address(pixel_address), .pixel_format(pixel_format),
        .pixel_value(pixel_value), .busy(busy), .done(done),
        .aborted(aborted), .write_error(write_error),
        .fault_detail(fault_detail), .pixels_accepted(pixels_accepted),
        .bytes_written(bytes_written), .m_axi_awid(awid),
        .m_axi_awaddr(awaddr), .m_axi_awlen(awlen),
        .m_axi_awsize(awsize), .m_axi_awburst(awburst),
        .m_axi_awcache(awcache), .m_axi_awprot(awprot),
        .m_axi_awqos(awqos), .m_axi_awvalid(awvalid),
        .m_axi_awready(awready), .m_axi_wdata(wdata),
        .m_axi_wstrb(wstrb), .m_axi_wlast(wlast),
        .m_axi_wvalid(wvalid), .m_axi_wready(wready),
        .m_axi_bid(bid), .m_axi_bresp(bresp),
        .m_axi_bvalid(bvalid), .m_axi_bready(bready)
    );

    reg [7:0] memory [0:4095];
    reg [31:0] aw_fifo [0:63];
    reg [63:0] wdata_fifo [0:63];
    reg [7:0] wstrb_fifo [0:63];
    reg [1:0] bresp_fifo [0:63];
    reg [5:0] aw_write_pointer = 6'd0;
    reg [5:0] aw_read_pointer = 6'd0;
    reg [6:0] aw_count = 7'd0;
    reg [5:0] w_write_pointer = 6'd0;
    reg [5:0] w_read_pointer = 6'd0;
    reg [6:0] w_count = 7'd0;
    reg [5:0] b_write_pointer = 6'd0;
    reg [5:0] b_read_pointer = 6'd0;
    reg [6:0] b_count = 7'd0;
    reg inject_error = 1'b0;
    reg stall_writes = 1'b0;
    reg [31:0] cycle = 32'd0;
    integer max_outstanding = 0;
    reg saw_full_release_bubble = 1'b0;
    integer lane;

    assign awready = !stall_writes && (cycle[0] || cycle[3]);
    assign wready = !stall_writes && (cycle[1] || !cycle[2]);
    wire consume_pair = aw_count != 0 && w_count != 0;
    assign bvalid = b_count != 0 && cycle[2:0] == 3'b111;
    assign bresp = bresp_fifo[b_read_pointer];
    wire b_accept = bvalid && bready;
    wire aw_accept = awvalid && awready;
    wire w_accept = wvalid && wready;

    always @(posedge clk) begin
        cycle <= cycle + 32'd1;
        if (dut.outstanding_count > max_outstanding)
            max_outstanding <= dut.outstanding_count;
        if (dut.fifo_count > 16)
            $fatal(1, "writer FIFO overflowed count=%0d", dut.fifo_count);
        if (busy && dut.fifo_count == 16 && dut.issue_complete) begin
            if (dut.fifo_push)
                $fatal(1, "full FIFO used combinational AXI completion credit");
            saw_full_release_bubble <= 1'b1;
        end

        if (aw_accept) begin
            if (awlen != 8'd0 || awsize != 3'b011 ||
                awburst != 2'b01 || awid != 6'd0)
                $fatal(1, "invalid AXI AW metadata");
            aw_fifo[aw_write_pointer] <= awaddr;
            aw_write_pointer <= aw_write_pointer + 6'd1;
        end
        if (w_accept) begin
            if (!wlast)
                $fatal(1, "single-beat write missing WLAST");
            wdata_fifo[w_write_pointer] <= wdata;
            wstrb_fifo[w_write_pointer] <= wstrb;
            w_write_pointer <= w_write_pointer + 6'd1;
        end

        if (consume_pair) begin
            for (lane = 0; lane < 8; lane = lane + 1)
                if (wstrb_fifo[w_read_pointer][lane])
                    memory[aw_fifo[aw_read_pointer] + lane] <=
                        wdata_fifo[w_read_pointer][lane * 8 +: 8];
            aw_read_pointer <= aw_read_pointer + 6'd1;
            w_read_pointer <= w_read_pointer + 6'd1;
            bresp_fifo[b_write_pointer] <= inject_error ? 2'b10 : 2'b00;
            b_write_pointer <= b_write_pointer + 6'd1;
            inject_error <= 1'b0;
        end

        case ({aw_accept, consume_pair})
            2'b10: aw_count <= aw_count + 7'd1;
            2'b01: aw_count <= aw_count - 7'd1;
            default: begin end
        endcase
        case ({w_accept, consume_pair})
            2'b10: w_count <= w_count + 7'd1;
            2'b01: w_count <= w_count - 7'd1;
            default: begin end
        endcase
        case ({consume_pair, b_accept})
            2'b10: b_count <= b_count + 7'd1;
            2'b01: b_count <= b_count - 7'd1;
            default: begin end
        endcase
        if (b_accept)
            b_read_pointer <= b_read_pointer + 6'd1;
    end

    task automatic begin_stream;
        begin
            @(negedge clk);
            start = 1'b1;
            @(negedge clk);
            start = 1'b0;
            wait (busy);
        end
    endtask

    task automatic send_pixel(
        input [31:0] address,
        input [7:0] format,
        input [31:0] value
    );
        begin
            @(negedge clk);
            pixel_address = address;
            pixel_format = format;
            pixel_value = value;
            pixel_valid = 1'b1;
            @(posedge clk);
            while (!pixel_ready) @(posedge clk);
            @(negedge clk);
            pixel_valid = 1'b0;
        end
    endtask

    task automatic end_stream;
        begin
            @(negedge clk);
            flush = 1'b1;
            @(posedge clk);
            while (!flush_ready) @(posedge clk);
            @(negedge clk);
            flush = 1'b0;
            wait (done);
            @(negedge clk);
        end
    endtask

    task automatic drain_barrier;
        begin
            @(negedge clk);
            barrier = 1'b1;
            @(posedge clk);
            while (!barrier_ready) @(posedge clk);
            @(negedge clk);
            barrier = 1'b0;
            wait (barrier_done);
            if (!busy || done)
                $fatal(1, "barrier terminated the active writer session");
            @(negedge clk);
        end
    endtask

    task automatic expect_byte(
        input integer address,
        input [7:0] expected
    );
        begin
            if (memory[address] !== expected)
                $fatal(1, "memory[%0d]=%02x expected=%02x",
                       address, memory[address], expected);
        end
    endtask

    integer i;
    initial begin
        for (i = 0; i < 4096; i = i + 1)
            memory[i] = 8'ha5;
        repeat (6) @(posedge clk);
        reset = 1'b0;

        begin_stream();
        for (i = 0; i < 10; i = i + 1)
            send_pixel(32'h00000100 + i,
                       `ASTRA_RENDER_FORMAT_INDEX8, i);
        send_pixel(32'h00000105, `ASTRA_RENDER_FORMAT_INDEX8, 32'hee);
        send_pixel(32'h00000112, `ASTRA_RENDER_FORMAT_RGB565, 32'h1234);
        send_pixel(32'h00000114, `ASTRA_RENDER_FORMAT_RGB565, 32'habcd);
        send_pixel(32'h00000120, `ASTRA_RENDER_FORMAT_XRGB8888,
                   32'hff102030);
        send_pixel(32'h00000124, `ASTRA_RENDER_FORMAT_ARGB8888,
                   32'h80405060);
        end_stream();

        for (i = 0; i < 10; i = i + 1)
            expect_byte(16'h0100 + i, i == 5 ? 8'hee : i[7:0]);
        expect_byte(16'h010f, 8'ha5);
        expect_byte(16'h0112, 8'h12);
        expect_byte(16'h0113, 8'h34);
        expect_byte(16'h0114, 8'hab);
        expect_byte(16'h0115, 8'hcd);
        expect_byte(16'h0120, 8'hff);
        expect_byte(16'h0121, 8'h10);
        expect_byte(16'h0122, 8'h20);
        expect_byte(16'h0123, 8'h30);
        expect_byte(16'h0124, 8'h80);
        expect_byte(16'h0125, 8'h40);
        expect_byte(16'h0126, 8'h50);
        expect_byte(16'h0127, 8'h60);
        expect_byte(16'h0128, 8'ha5);
        if (pixels_accepted != 32'd15 || bytes_written != 32'd23 ||
            write_error || aborted)
            $fatal(1, "writer counters/status mismatch p=%0d b=%0d e=%0d a=%0d",
                   pixels_accepted, bytes_written, write_error, aborted);
        if (max_outstanding < 2)
            $fatal(1, "writer never exercised multiple outstanding writes");

        begin_stream();
        send_pixel(32'h00000500, `ASTRA_RENDER_FORMAT_XRGB8888,
                   32'h12345678);
        drain_barrier();
        expect_byte(16'h0500, 8'h12);
        expect_byte(16'h0501, 8'h34);
        expect_byte(16'h0502, 8'h56);
        expect_byte(16'h0503, 8'h78);
        send_pixel(32'h00000504, `ASTRA_RENDER_FORMAT_XRGB8888,
                   32'h90abcdef);
        end_stream();
        expect_byte(16'h0504, 8'h90);
        expect_byte(16'h0505, 8'hab);
        expect_byte(16'h0506, 8'hcd);
        expect_byte(16'h0507, 8'hef);

        // Fill the write queue under downstream backpressure, then release it.
        // This proves that a completion observed while full frees registered
        // capacity on the next cycle without overflow or dropped pixels.
        begin_stream();
        stall_writes = 1'b1;
        fork
            begin
                for (i = 0; i < 32; i = i + 1)
                    send_pixel(32'h00000400 + (i * 8),
                               `ASTRA_RENDER_FORMAT_INDEX8, 32'h40 + i);
            end
            begin
                wait (dut.fifo_count == 16);
                repeat (4) @(posedge clk);
                stall_writes = 1'b0;
            end
        join
        end_stream();
        for (i = 0; i < 32; i = i + 1)
            expect_byte(16'h0400 + (i * 8), 8'h40 + i[7:0]);
        if (!saw_full_release_bubble || pixels_accepted != 32'd32 ||
            bytes_written != 32'd32 || write_error || aborted)
            $fatal(1, "full/release mismatch seen=%0d p=%0d b=%0d e=%0d a=%0d",
                   saw_full_release_bubble, pixels_accepted, bytes_written,
                   write_error, aborted);

        begin_stream();
        inject_error = 1'b1;
        send_pixel(32'h00000200, `ASTRA_RENDER_FORMAT_INDEX8, 32'h5a);
        end_stream();
        if (!write_error || fault_detail[31:16] != 16'h0002)
            $fatal(1, "SLVERR was not reported detail=%08x", fault_detail);

        begin_stream();
        for (i = 0; i < 24; i = i + 1)
            send_pixel(32'h00000300 + i,
                       `ASTRA_RENDER_FORMAT_INDEX8, 32'h80 + i);
        @(negedge clk);
        abort = 1'b1;
        @(negedge clk);
        abort = 1'b0;
        wait (done);
        if (!aborted || busy)
            $fatal(1, "abort did not drain and terminate");

        $display("PASS astra_render_pixel_writer");
        $finish;
    end
endmodule

`default_nettype wire
