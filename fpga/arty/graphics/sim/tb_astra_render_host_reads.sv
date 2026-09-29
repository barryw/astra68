// Host-aperture read router: routing, address translation, base == 0
// passthrough, and response order across ports with different latencies,
// random R backpressure and a bounded model queue.
`timescale 1ns/1ps
`default_nettype none

module tb_astra_render_host_reads;
    localparam [31:0] ARENA = 32'h40000000;
    localparam [31:0] WINDOW = ARENA + 32'h00800000;
    localparam [31:0] MEDIA_BASE = WINDOW - 32'h8000;
    localparam [31:0] HOST_BASE = 32'h8123_4000;

    reg clk = 1'b0;
    reg reset = 1'b1;
    always #5 clk = ~clk;

    reg [31:0] host_base = 32'd0;
    reg [2:0] s_arid;
    reg [31:0] s_araddr;
    reg [7:0] s_arlen;
    reg s_arvalid = 1'b0;
    wire s_arready;
    wire [2:0] s_rid;
    wire [63:0] s_rdata;
    wire [1:0] s_rresp;
    wire s_rlast, s_rvalid;
    reg s_rready = 1'b0;

    wire [2:0] m_arid, h_arid, m_rid, h_rid;
    wire [31:0] m_araddr, h_araddr;
    wire [7:0] m_arlen, h_arlen;
    wire [2:0] m_arsize, h_arsize, m_arprot, h_arprot;
    wire [1:0] m_arburst, h_arburst, m_rresp, h_rresp;
    wire [3:0] m_arcache, h_arcache;
    wire m_arvalid, m_arready, h_arvalid, h_arready;
    wire [63:0] m_rdata, h_rdata;
    wire m_rlast, m_rvalid, m_rready, h_rlast, h_rvalid, h_rready;

    astra_render_host_reads #(.ARENA_BASE(ARENA), .AXI_ID_WIDTH(3)) dut (
        .clk(clk), .reset(reset), .host_base(host_base),
        .s_arid(s_arid), .s_araddr(s_araddr), .s_arlen(s_arlen),
        .s_arsize(3'd3), .s_arburst(2'b01), .s_arcache(4'b0011),
        .s_arprot(3'd0), .s_arvalid(s_arvalid), .s_arready(s_arready),
        .s_rid(s_rid), .s_rdata(s_rdata), .s_rresp(s_rresp),
        .s_rlast(s_rlast), .s_rvalid(s_rvalid), .s_rready(s_rready),
        .m_arid(m_arid), .m_araddr(m_araddr), .m_arlen(m_arlen),
        .m_arsize(m_arsize), .m_arburst(m_arburst), .m_arcache(m_arcache),
        .m_arprot(m_arprot), .m_arvalid(m_arvalid), .m_arready(m_arready),
        .m_rid(m_rid), .m_rdata(m_rdata), .m_rresp(m_rresp),
        .m_rlast(m_rlast), .m_rvalid(m_rvalid), .m_rready(m_rready),
        .h_arid(h_arid), .h_araddr(h_araddr), .h_arlen(h_arlen),
        .h_arsize(h_arsize), .h_arburst(h_arburst), .h_arcache(h_arcache),
        .h_arprot(h_arprot), .h_arvalid(h_arvalid), .h_arready(h_arready),
        .h_rid(h_rid), .h_rdata(h_rdata), .h_rresp(h_rresp),
        .h_rlast(h_rlast), .h_rvalid(h_rvalid), .h_rready(h_rready)
    );

    // Media RAM spans the window's lower edge; host memory is the window.
    astra_render_axi_latency_model #(
        .AXI_ID_WIDTH(3), .MEMORY_BYTES(65536), .BASE_ADDRESS(MEDIA_BASE),
        .READ_LATENCY(25), .MAX_READS(4)
    ) media (
        .clk(clk), .reset(reset),
        .s_axi_arid(m_arid), .s_axi_araddr(m_araddr), .s_axi_arlen(m_arlen),
        .s_axi_arvalid(m_arvalid), .s_axi_arready(m_arready),
        .s_axi_rid(m_rid), .s_axi_rdata(m_rdata), .s_axi_rresp(m_rresp),
        .s_axi_rlast(m_rlast), .s_axi_rvalid(m_rvalid),
        .s_axi_rready(m_rready),
        .s_axi_awid(3'd0), .s_axi_awaddr(32'd0), .s_axi_awlen(8'd0),
        .s_axi_awvalid(1'b0), .s_axi_awready(),
        .s_axi_wdata(64'd0), .s_axi_wstrb(8'd0), .s_axi_wlast(1'b0),
        .s_axi_wvalid(1'b0), .s_axi_wready(), .s_axi_bid(), .s_axi_bresp(),
        .s_axi_bvalid(), .s_axi_bready(1'b1),
        .read_transactions(), .write_transactions(), .read_beats(),
        .write_beats()
    );
    astra_render_axi_latency_model #(
        .AXI_ID_WIDTH(3), .MEMORY_BYTES(32768), .BASE_ADDRESS(HOST_BASE),
        .READ_LATENCY(60), .MAX_READS(3), .LOST_READ_BEATS(128)
    ) host (
        .clk(clk), .reset(reset),
        .s_axi_arid(h_arid), .s_axi_araddr(h_araddr), .s_axi_arlen(h_arlen),
        .s_axi_arvalid(h_arvalid), .s_axi_arready(h_arready),
        .s_axi_rid(h_rid), .s_axi_rdata(h_rdata), .s_axi_rresp(h_rresp),
        .s_axi_rlast(h_rlast), .s_axi_rvalid(h_rvalid),
        .s_axi_rready(h_rready),
        .s_axi_awid(3'd0), .s_axi_awaddr(32'd0), .s_axi_awlen(8'd0),
        .s_axi_awvalid(1'b0), .s_axi_awready(),
        .s_axi_wdata(64'd0), .s_axi_wstrb(8'd0), .s_axi_wlast(1'b0),
        .s_axi_wvalid(1'b0), .s_axi_wready(), .s_axi_bid(), .s_axi_bresp(),
        .s_axi_bvalid(), .s_axi_bready(1'b1),
        .read_transactions(), .write_transactions(), .read_beats(),
        .write_beats()
    );

    function automatic [7:0] media_byte(input [31:0] a);
        media_byte = a[7:0] ^ a[15:8] ^ 8'h5a;
    endfunction
    function automatic [7:0] host_byte(input [31:0] a);
        host_byte = a[7:0] + a[14:8] + 8'hc3;
    endfunction

    // Expected beats in issue order.
    localparam integer QUEUE = 8192;
    reg [63:0] expect_data [0:QUEUE-1];
    reg [2:0]  expect_id [0:QUEUE-1];
    reg        expect_last [0:QUEUE-1];
    integer expect_head = 0, expect_tail = 0;
    integer errors = 0, beats = 0;
    integer host_bursts = 0, media_bursts = 0, limit_seen = 0;
    integer overlap = 0;

    task automatic push_burst(input [31:0] address, input [7:0] len,
                              input [2:0] id);
        integer beat, lane;
        reg [31:0] a;
        reg use_host;
        begin
            use_host = host_base != 0 && address >= WINDOW &&
                       address < WINDOW + 32'h00800000;
            for (beat = 0; beat <= len; beat = beat + 1) begin
                for (lane = 0; lane < 8; lane = lane + 1) begin
                    a = address + beat * 8 + lane;
                    expect_data[expect_tail % QUEUE][lane*8 +: 8] =
                        use_host ? host_byte(a - WINDOW + HOST_BASE) :
                                   media_byte(a);
                end
                expect_id[expect_tail % QUEUE] = id;
                expect_last[expect_tail % QUEUE] = beat == len;
                expect_tail = expect_tail + 1;
            end
        end
    endtask

    always @(posedge clk) begin
        if (!reset) begin
            if (m_arvalid && m_arready) begin
                media_bursts = media_bursts + 1;
                if (m_araddr == WINDOW + 32'h00800000)
                    limit_seen = limit_seen + 1;
                else if (m_araddr < MEDIA_BASE ||
                         m_araddr >= MEDIA_BASE + 65536) begin
                    $display("FAIL media address %08x", m_araddr);
                    errors = errors + 1;
                end
            end
            if (h_arvalid && h_arready) begin
                host_bursts = host_bursts + 1;
                // The DE25 F2SDRAM path loses narrow bursts of 136+ beats;
                // the router must split host bursts into 32-beat pieces.
                if (h_arlen > 8'd31) begin
                    $display("FAIL host burst of %0d beats", h_arlen + 1);
                    errors = errors + 1;
                end
                if (h_araddr < HOST_BASE || h_araddr >= HOST_BASE + 32768)
                    begin
                    $display("FAIL host address %08x", h_araddr);
                    errors = errors + 1;
                end
            end
            // Both ports hold responses at once: reads overlap.
            if (m_rvalid && h_rvalid) overlap = overlap + 1;
            if (s_rvalid && s_rready) begin
                if (expect_head == expect_tail) begin
                    $display("FAIL unexpected beat");
                    errors = errors + 1;
                end else begin
                    if (s_rdata !== expect_data[expect_head % QUEUE] ||
                        s_rid !== expect_id[expect_head % QUEUE] ||
                        s_rlast !== expect_last[expect_head % QUEUE] ||
                        s_rresp !== 2'b00) begin
                        $display("FAIL beat %0d: %016x id %0d last %0d, expected %016x id %0d last %0d",
                                 expect_head, s_rdata, s_rid, s_rlast,
                                 expect_data[expect_head % QUEUE],
                                 expect_id[expect_head % QUEUE],
                                 expect_last[expect_head % QUEUE]);
                        errors = errors + 1;
                    end
                    expect_head = expect_head + 1;
                    beats = beats + 1;
                end
            end
            s_rready <= ($random & 3) != 0;
        end
    end

    // Random bursts: page-bounded, half in the window.
    task automatic run_random(input integer count, input integer window_pct);
        integer n, len, page_left;
        reg [31:0] address;
        begin
            for (n = 0; n < count; n = n + 1) begin
                len = ($unsigned($random) % 4) == 0 ?
                    $unsigned($random) % 256 : $unsigned($random) % 32;
                if (($unsigned($random) % 100) < window_pct)
                    address = WINDOW + (($unsigned($random) % 4096) & ~7);
                else
                    address = MEDIA_BASE + (($unsigned($random) % 32768) & ~7);
                page_left = (4096 - (address & 12'hfff)) / 8;
                if (len >= page_left) len = page_left - 1;
                s_arid <= $random;
                s_araddr <= address;
                s_arlen <= len[7:0];
                s_arvalid <= 1'b1;
                @(posedge clk);
                while (!s_arready) @(posedge clk);
                push_burst(address, len[7:0], s_arid);
                s_arvalid <= 1'b0;
                if ($random & 1) @(posedge clk);
            end
        end
    endtask

    task automatic drain;
        integer t;
        begin
            for (t = 0; t < 100000 && expect_head != expect_tail; t = t + 1)
                @(posedge clk);
            if (expect_head != expect_tail) begin
                $display("FAIL %0d beats missing", expect_tail - expect_head);
                errors = errors + 1;
            end
        end
    endtask

    integer i;
    integer before_host;
    initial begin
        #5_000_000;
        $display("FAIL watchdog: router stalled");
        $fatal(1);
    end
    initial begin
        for (i = 0; i < 65536; i = i + 1)
            media.write_byte(MEDIA_BASE + i, media_byte(MEDIA_BASE + i));
        for (i = 0; i < 32768; i = i + 1)
            host.write_byte(HOST_BASE + i, host_byte(HOST_BASE + i));
        repeat (4) @(posedge clk);
        reset <= 1'b0;
        @(posedge clk);

        // Aperture off: window reads go to Media RAM unchanged.
        run_random(200, 50);
        drain;
        if (host_bursts != 0) begin
            $display("FAIL base 0 reached host port");
            errors = errors + 1;
        end

        // Aperture on, mixed (every switch drains the other port).
        host_base = HOST_BASE;
        run_random(600, 50);
        drain;
        // Host-only streams keep several bursts in flight.
        before_host = host_bursts;
        run_random(300, 100);
        drain;
        if (host_bursts - before_host < 300) begin
            $display("FAIL host-only bursts %0d", host_bursts - before_host);
            errors = errors + 1;
        end

        // A full 256-beat ring prefetch from a page start, as the desktop's
        // first batch issues it, arrives whole with one RLAST.
        before_host = host_bursts;
        s_arid <= 3'd0;
        s_araddr <= WINDOW + 32'h1000;
        s_arlen <= 8'd255;
        s_arvalid <= 1'b1;
        @(posedge clk);
        while (!s_arready) @(posedge clk);
        push_burst(WINDOW + 32'h1000, 8'd255, 3'd0);
        s_arvalid <= 1'b0;
        drain;
        if (host_bursts - before_host != 8) begin
            $display("FAIL 256-beat burst issued as %0d pieces",
                     host_bursts - before_host);
            errors = errors + 1;
        end

        // The first byte past the window stays in Media RAM (outside the
        // model, so its data is X on both sides of the comparison).
        s_arid <= 3'd1;
        s_araddr <= WINDOW + 32'h00800000;
        s_arlen <= 8'd0;
        s_arvalid <= 1'b1;
        @(posedge clk);
        while (!s_arready) @(posedge clk);
        expect_data[expect_tail % QUEUE] = 64'bx;
        expect_id[expect_tail % QUEUE] = 3'd1;
        expect_last[expect_tail % QUEUE] = 1'b1;
        expect_tail = expect_tail + 1;
        s_arvalid <= 1'b0;
        drain;
        if (limit_seen != 1) begin
            $display("FAIL window limit read not on Media RAM");
            errors = errors + 1;
        end

        if (overlap == 0) begin
            $display("FAIL ports never overlapped");
            errors = errors + 1;
        end
        if (errors == 0 && beats > 0 && media_bursts > 0)
            $display("tb_astra_render_host_reads PASS (%0d beats, %0d media, %0d host bursts)",
                     beats, media_bursts, host_bursts);
        else begin
            $display("tb_astra_render_host_reads FAIL (%0d errors)", errors);
            $fatal(1);
        end
        $finish;
    end
endmodule

`default_nettype wire
