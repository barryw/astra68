// Register, scanout, page-flip, mode-map, and underrun tests for Vega.
`timescale 1ns/1ps

module tb_vega_video;
    localparam [24:0] PAGE0 = 25'h0100000;
    localparam [24:0] PAGE1 = 25'h0200000;
    localparam [24:0] INDEX_PAGE = 25'h0300000;
    localparam [24:0] TILE_MAP = 25'h0001000;
    localparam [24:0] TILE_SET = 25'h0002000;
    localparam [24:0] SPR_BASE = 25'h0003000;
    localparam [23:0] POST_RGB = 24'hdeadc0;
    localparam [23:0] BACKDROP = 24'h123456;
    localparam [23:0] TILE_RGB = 24'hc04080;
    localparam [23:0] SPR_RGB = 24'h20a0e0;
    localparam [23:0] INDEX_RGB = 24'hd08020;
    localparam [15:0] COLORKEY = 16'h07e0;
    localparam [7:0] INDEX_COLORKEY = 8'h7e;

    reg cpu_clk = 1'b0;
    reg mem_clk = 1'b0;
    reg pixel_clk = 1'b0;
    always #40 cpu_clk = ~cpu_clk;
    always #6.666 mem_clk = ~mem_clk;
    always #18.5 pixel_clk = ~pixel_clk;

    reg cpu_rst = 1'b1;
    reg mem_rst = 1'b1;
    reg pixel_rst = 1'b1;

    reg cpu_write_stb = 1'b0;
    reg cpu_read_stb = 1'b0;
    reg [15:0] cpu_addr = 16'd0;
    reg [3:0] cpu_be = 4'd0;
    reg [31:0] cpu_wdata = 32'd0;
    wire [31:0] cpu_rdata;
    wire cpu_irq;

    wire mem_lock;
    wire mem_valid;
    wire mem_ready;
    wire mem_write;
    wire [24:0] mem_addr;
    wire [3:0] mem_be;
    wire [31:0] mem_wdata;
    reg mem_rsp_valid = 1'b0;
    reg [31:0] mem_rdata = 32'd0;
    reg stall_mem = 1'b0;
    reg response_pending = 1'b0;
    reg [24:0] response_addr = 25'd0;

    reg [9:0] pixel_x = 10'd0;
    reg [9:0] pixel_y = 10'd0;
    integer frame_count = 0;
    wire [23:0] rgb;
    wire graphics_active;
    wire [9:0] beam_x;
    wire [9:0] beam_y;

    vega_video dut (
        .cpu_clk(cpu_clk), .cpu_rst(cpu_rst),
        .cpu_write_stb(cpu_write_stb), .cpu_read_stb(cpu_read_stb),
        .cpu_addr(cpu_addr), .cpu_be(cpu_be), .cpu_wdata(cpu_wdata),
        .cpu_rdata(cpu_rdata), .cpu_irq(cpu_irq), .display_ready(1'b1),
        .cop_write_stb(1'b0), .cop_addr(16'd0), .cop_wdata(32'd0),
        .mem_clk(mem_clk), .mem_rst(mem_rst), .mem_lock(mem_lock),
        .mem_valid(mem_valid), .mem_ready(mem_ready), .mem_write(mem_write),
        .mem_addr(mem_addr), .mem_be(mem_be), .mem_wdata(mem_wdata),
        .mem_rsp_valid(mem_rsp_valid), .mem_rdata(mem_rdata),
        .pixel_clk(pixel_clk), .pixel_rst(pixel_rst),
        .pixel_x(pixel_x), .pixel_y(pixel_y), .post_rgb(POST_RGB),
        .rgb(rgb), .graphics_active(graphics_active),
        .beam_x(beam_x), .beam_y(beam_y)
    );

    // Coordinates change on the falling edge so the DUT and scoreboard see a
    // complete pixel interval around every rising edge.
    always @(negedge pixel_clk) begin
        if (pixel_rst) begin
            pixel_x <= 10'd0;
            pixel_y <= 10'd0;
            frame_count <= 0;
        end else if (pixel_x == 10'd857) begin
            pixel_x <= 10'd0;
            if (pixel_y == 10'd524) begin
                pixel_y <= 10'd0;
                frame_count <= frame_count + 1;
            end else begin
                pixel_y <= pixel_y + 10'd1;
            end
        end else begin
            pixel_x <= pixel_x + 10'd1;
        end
    end

    function automatic [15:0] test_pixel(
        input integer page,
        input integer row,
        input integer column
    );
        reg [4:0] red;
        reg [5:0] green;
        reg [4:0] blue;
        begin
            if (column == 30) begin
                test_pixel = COLORKEY;
            end else begin
                red = page == 0 ? 5'h05 : 5'h1a;
                green = row[5:0];
                blue = column[4:0];
                test_pixel = {red, green, blue};
            end
        end
    endfunction

    function automatic [23:0] expand_rgb565(input [15:0] value);
        begin
            expand_rgb565 = {
                value[15:11], value[15:13],
                value[10:5], value[10:9],
                value[4:0], value[4:2]
            };
        end
    endfunction

    function automatic [7:0] indexed_pixel(
        input integer row,
        input integer column
    );
        begin
            indexed_pixel = column == 30 ? INDEX_COLORKEY : 8'h2a;
        end
    endfunction

    function automatic [31:0] memory_word(input [24:0] address);
        integer page;
        integer offset;
        integer row;
        integer word_index;
        begin
            if (address >= TILE_MAP && address < TILE_MAP + 25'h100) begin
                memory_word = 32'h04000400; // tile 0, palette bank 1
            end else if (address >= TILE_SET &&
                         address < TILE_SET + 25'h100) begin
                memory_word = 32'h33333333;
            end else if (address >= SPR_BASE &&
                         address < SPR_BASE + 25'h100) begin
                memory_word = 32'h55555555;
            end else if (address >= INDEX_PAGE) begin
                offset = address - INDEX_PAGE;
                row = offset / 720;
                word_index = (offset % 720) / 4;
                memory_word = {
                    indexed_pixel(row, word_index * 4),
                    indexed_pixel(row, word_index * 4 + 1),
                    indexed_pixel(row, word_index * 4 + 2),
                    indexed_pixel(row, word_index * 4 + 3)
                };
            end else if (address >= PAGE1) begin
                page = 1;
                offset = address - PAGE1;
                row = offset / 1440;
                word_index = (offset % 1440) / 4;
                memory_word = {
                    test_pixel(page, row, word_index * 2),
                    test_pixel(page, row, word_index * 2 + 1)
                };
            end else if (address >= PAGE0) begin
                page = 0;
                offset = address - PAGE0;
                row = offset / 1440;
                word_index = (offset % 1440) / 4;
                memory_word = {
                    test_pixel(page, row, word_index * 2),
                    test_pixel(page, row, word_index * 2 + 1)
                };
            end else begin
                memory_word = 32'd0;
            end
        end
    endfunction

    assign mem_ready = !stall_mem && !response_pending;

    always @(posedge mem_clk) begin
        mem_rsp_valid <= 1'b0;
        if (response_pending) begin
            mem_rsp_valid <= 1'b1;
            mem_rdata <= memory_word(response_addr);
            response_pending <= 1'b0;
        end
        if (mem_valid && mem_ready) begin
            if (mem_write || mem_be != 4'b1111 || mem_wdata != 32'd0)
                $fatal(1, "invalid framebuffer memory request");
            response_addr <= mem_addr;
            response_pending <= 1'b1;
        end
        if (mem_valid && !mem_lock)
            $fatal(1, "framebuffer request issued without lock");
        if (!mem_rst && dut.request_enqueue && !dut.any_client_mem_lock)
            $fatal(1, "Vega enqueue occurred without a client lock");
    end

    task automatic reg_write(input [15:0] address, input [31:0] value);
        begin
            @(negedge cpu_clk);
            cpu_addr = address;
            cpu_be = 4'b1111;
            cpu_wdata = value;
            cpu_write_stb = 1'b1;
            @(negedge cpu_clk);
            cpu_write_stb = 1'b0;
            cpu_be = 4'd0;
            cpu_wdata = 32'd0;
        end
    endtask

    task automatic reg_read(input [15:0] address, output [31:0] value);
        begin
            @(negedge cpu_clk);
            cpu_addr = address;
            cpu_read_stb = 1'b1;
            @(posedge cpu_clk);
            #1 value = cpu_rdata;
            @(negedge cpu_clk);
            cpu_read_stb = 1'b0;
        end
    endtask

    task automatic wait_pixel(input integer y, input integer x);
        integer timeout;
        begin : wait_loop
            timeout = 0;
            while (pixel_y != y || pixel_x != x) begin
                @(posedge pixel_clk);
                timeout = timeout + 1;
                if (timeout > 500000)
                    $fatal(1, "pixel wait timeout y=%0d x=%0d", y, x);
            end
            #1;
        end
    endtask

    task automatic wait_frame(input integer wanted_frame);
        integer timeout;
        begin
            timeout = 0;
            while (frame_count < wanted_frame) begin
                @(posedge pixel_clk);
                timeout = timeout + 1;
                if (timeout > 1000000)
                    $fatal(1, "frame wait timeout wanted=%0d got=%0d",
                           wanted_frame, frame_count);
            end
        end
    endtask

    task automatic expect_rgb(
        input integer y,
        input integer x,
        input [23:0] expected,
        input [255:0] label
    );
        begin
            wait_pixel(y, x);
            if (rgb !== expected)
                $fatal(1, "%0s rgb mismatch at (%0d,%0d): got=%06x expected=%06x fmt=%0d word=%08x index=%02x palette=%08x base=%07x valid=%b",
                       label, x, y, rgb, expected, dut.fb_format_pixel,
                       dut.framebuffer_word_pixel,
                       dut.framebuffer_index_d1,
                       dut.palette_framebuffer_q,
                       dut.reg_fb_base_active,
                       dut.display_line_valid_pixel);
        end
    endtask

    reg [31:0] value;
    integer active_frame;

    initial begin
        repeat (8) @(posedge mem_clk);
        cpu_rst = 1'b0;
        mem_rst = 1'b0;
        pixel_rst = 1'b0;

        reg_read(16'h0000, value);
        if (value !== 32'h56454741)
            $fatal(1, "Vega ID mismatch %08x", value);
        reg_read(16'h0004, value);
        if (value !== 32'h00030000)
            $fatal(1, "Vega version mismatch %08x", value);
        reg_read(16'h001c, value);
        if (value !== 32'h0000003f)
            $fatal(1, "Vega capabilities mismatch %08x", value);
        reg_read(16'h0028, value);
        if (value !== {16'd480, 16'd720})
            $fatal(1, "Vega active size mismatch %08x", value);

        reg_write(16'h040c, 32'h00abcdef);
        cpu_addr = 16'h040c;
        repeat (2) @(posedge cpu_clk);
        #1;
        if (cpu_rdata !== 32'h00abcdef)
            $fatal(1, "palette readback mismatch %08x", cpu_rdata);

        reg_write(16'h0030, {8'd0, BACKDROP});
        reg_write(16'h0040, {7'd0, PAGE0});
        reg_write(16'h0044, 32'd1440);
        reg_write(16'h0048, 32'd0);
        reg_write(16'h004c, {16'd0, COLORKEY});
        reg_write(16'h0008, 32'h0000001b);
        repeat (6) @(posedge mem_clk);
        if (dut.sprite_budget_effective_mem !== 16'd512)
            $fatal(1, "RGB565 sprite budget limit mismatch %0d",
                   dut.sprite_budget_effective_mem);

        reg_read(16'h000c, value);
        if (!value[2] || !value[4])
            $fatal(1, "pending/ready status mismatch %08x", value);

        wait_frame(1);
        active_frame = frame_count;
        expect_rgb(0, 20, expand_rgb565(test_pixel(0, 0, 18)),
                   "framebuffer");
        expect_rgb(0, 32, BACKDROP, "colorkey");
        expect_rgb(1, 20, expand_rgb565(test_pixel(0, 1, 18)),
                   "next scanline");

        reg_read(16'h000c, value);
        if (value[2] || value[6] || !value[4])
            $fatal(1, "post-flip status mismatch %08x", value);
        reg_write(16'h000c, 32'h00000020);
        repeat (4) @(posedge cpu_clk);
        reg_read(16'h000c, value);
        if (value[5])
            $fatal(1, "underrun did not clear on stable scanout %08x", value);

        reg_write(16'h0024, 32'd3);
        reg_write(16'h0014, 32'hffffffff);
        reg_write(16'h0010, 32'h00000003);
        reg_write(16'h0040, {7'd0, PAGE1});
        reg_read(16'h000c, value);
        if (!value[2])
            $fatal(1, "page flip did not become pending %08x", value);

        wait_frame(active_frame + 1);
        expect_rgb(0, 20, expand_rgb565(test_pixel(1, 0, 18)),
                   "page flip line zero");
        wait_pixel(3, 100);
        repeat (4) @(posedge cpu_clk);
        reg_read(16'h0014, value);
        if ((value & 32'h3) != 32'h3 || !cpu_irq)
            $fatal(1, "vblank/raster IRQ mismatch stat=%08x irq=%b",
                   value, cpu_irq);

        // Change modes during vblank, then verify centering and 2x mapping.
        wait_pixel(480, 100);
        reg_write(16'h0018, 32'd2);
        wait_frame(active_frame + 2);
        expect_rgb(0, 30, BACKDROP, "320 mode left border");
        expect_rgb(0, 42, expand_rgb565(test_pixel(1, 0, 0)),
                   "320 mode first pixel");
        expect_rgb(0, 44, expand_rgb565(test_pixel(1, 0, 1)),
                   "320 mode horizontal scale");
        expect_rgb(2, 42, expand_rgb565(test_pixel(1, 1, 0)),
                   "320 mode vertical scale");

        // Block the request for logical line 2 until after its retirement
        // point. Vega must report the missed deadline, then recover.
        reg_write(16'h000c, 32'h00000020);
        wait_pixel(2, 800);
        stall_mem = 1'b1;
        wait_pixel(4, 10);
        stall_mem = 1'b0;
        repeat (8) @(posedge cpu_clk);
        reg_read(16'h000c, value);
        if (!value[5])
            $fatal(1, "forced scanline underrun was not reported %08x", value);

        // Exercise the complete tile path: MMIO configuration, SDRAM map and
        // pattern reads, palette lookup, and ordering around the framebuffer.
        wait_pixel(480, 100);
        reg_write(16'h0018, 32'd0);
        reg_write(16'h044c, {8'd0, TILE_RGB});
        reg_write(16'h0080, 32'h00000009); // tile0 enable, wrap, below FB
        reg_write(16'h0084, {7'd0, TILE_MAP});
        reg_write(16'h0088, {7'd0, TILE_SET});
        reg_write(16'h008c, 32'd0);        // one 8x8 tile, repeated
        reg_write(16'h0090, 32'd0);
        reg_write(16'h0008, 32'h0000001b);
        active_frame = frame_count + 1;
        wait_frame(active_frame);
        expect_rgb(0, 20, expand_rgb565(test_pixel(1, 0, 18)),
                   "tile below opaque framebuffer");
        expect_rgb(0, 32, TILE_RGB, "tile below framebuffer colorkey");

        wait_pixel(480, 100);
        reg_write(16'h0080, 32'h00000019); // move tile0 above FB
        active_frame = frame_count + 1;
        wait_frame(active_frame);
        expect_rgb(0, 20, TILE_RGB, "tile above opaque framebuffer");

        wait_pixel(480, 100);
        reg_write(16'h0008, 32'h00000011); // display + backdrop, no FB fetch
        reg_write(16'h0804, 32'd2048);
        repeat (6) @(posedge mem_clk);
        if (dut.sprite_budget_effective_mem !== 16'd1024)
            $fatal(1, "framebuffer-disabled sprite budget mismatch %0d",
                   dut.sprite_budget_effective_mem);
        active_frame = frame_count + 1;
        wait_frame(active_frame);
        expect_rgb(0, 20, TILE_RGB, "tile-only scanout");

        // Two overlapping behind sprites exercise table MMIO, priority,
        // collision reporting, and visibility through the framebuffer key.
        wait_pixel(480, 100);
        reg_write(16'h0080, 32'd0);
        reg_write(16'h0494, {8'd0, SPR_RGB});
        reg_write(16'h0800, 32'd1);
        reg_write(16'h0804, 32'd64);
        reg_write(16'h1000, 32'h00002333);
        reg_write(16'h1004, 32'h00000010);
        reg_write(16'h1008, 32'h00010010);
        reg_write(16'h100c, {7'd0, SPR_BASE});
        reg_write(16'h1010, 32'd8);
        reg_write(16'h1020, 32'h00002233);
        reg_write(16'h1024, 32'h00000018);
        reg_write(16'h1028, 32'h00010010);
        reg_write(16'h102c, {7'd0, SPR_BASE});
        reg_write(16'h1030, 32'd8);
        reg_write(16'h0014, 32'hffffffff);
        reg_write(16'h0008, 32'h0000001f);
        active_frame = frame_count + 1;
        wait_frame(active_frame);
        expect_rgb(0, 20, expand_rgb565(test_pixel(1, 0, 18)),
                   "behind sprite hidden by framebuffer");
        expect_rgb(0, 32, SPR_RGB, "behind sprite through colorkey");
        repeat (8) @(posedge cpu_clk);
        reg_read(16'h0808, value);
        if ((value & 32'h3) != 32'h3)
            $fatal(1, "sprite collision bitmap mismatch %08x", value);
        reg_read(16'h0014, value);
        if (!value[2])
            $fatal(1, "sprite collision IRQ missing %08x", value);

        // A front sprite overrides an opaque framebuffer pixel.
        wait_pixel(480, 100);
        reg_write(16'h1000, 32'h00002323);
        reg_write(16'h1020, 32'd0);
        active_frame = frame_count + 1;
        wait_frame(active_frame);
        expect_rgb(0, 20, SPR_RGB, "front sprite over framebuffer");

        // INDEX8 scanout uses the same global palette and byte-exact
        // colorkey path while halving framebuffer bandwidth.
        wait_pixel(480, 100);
        reg_write(16'h04a8, {8'd0, INDEX_RGB});
        reg_write(16'h0800, 32'd0);
        reg_write(16'h0040, {7'd0, INDEX_PAGE});
        reg_write(16'h0044, 32'd720);
        reg_write(16'h0048, 32'd1);
        reg_write(16'h004c, {24'd0, INDEX_COLORKEY});
        reg_write(16'h0008, 32'h0000001b);
        reg_write(16'h0804, 32'd2048);
        repeat (6) @(posedge mem_clk);
        if (dut.sprite_budget_effective_mem !== 16'd1024)
            $fatal(1, "INDEX8 sprite budget limit mismatch %0d",
                   dut.sprite_budget_effective_mem);
        reg_write(16'h0804, 32'd64);
        // This base write occurs after the current vblank edge, so it retires
        // at the following edge and becomes visible one frame later.
        active_frame = frame_count + 2;
        wait_frame(active_frame);
        expect_rgb(0, 20, INDEX_RGB, "INDEX8 framebuffer");
        expect_rgb(0, 32, BACKDROP, "INDEX8 colorkey");

        reg_write(16'h0008, 32'd0);
        repeat (6) @(posedge pixel_clk);
        #1;
        if (graphics_active || rgb !== POST_RGB)
            $fatal(1, "POST fallback mismatch active=%b rgb=%06x",
                   graphics_active, rgb);
        if (beam_x !== pixel_x || beam_y !== pixel_y)
            $fatal(1, "beam output mismatch");

        // Malformed wide addresses and pitches must fail closed. Silently
        // truncating these values would turn a software bug into unrelated
        // SDRAM reads after the next page flip.
        reg_write(16'h0040, 32'h80001000);
        reg_write(16'h0044, 32'd720);
        reg_write(16'h0048, 32'd1);
        reg_write(16'h0008, 32'h00000003);
        active_frame = frame_count + 1;
        wait_frame(active_frame);
        repeat (4) @(posedge cpu_clk);
        reg_read(16'h000c, value);
        if (!value[6])
            $fatal(1, "high framebuffer base was not rejected %08x", value);

        reg_write(16'h0040, {7'd0, INDEX_PAGE});
        active_frame = frame_count + 1;
        wait_frame(active_frame);
        reg_write(16'h0044, 32'h00010000);
        reg_read(16'h000c, value);
        if (!value[6])
            $fatal(1, "high framebuffer pitch was not rejected %08x", value);
        reg_write(16'h0044, 32'd716);
        reg_read(16'h000c, value);
        if (!value[6])
            $fatal(1, "short framebuffer pitch was not rejected %08x", value);
        reg_write(16'h0044, 32'd720);
        reg_read(16'h000c, value);
        if (value[6])
            $fatal(1, "valid framebuffer configuration stayed rejected %08x",
                   value);

        reg_write(16'h0008, 32'h00000001);
        reg_write(16'h0084, 32'h80001000);
        reg_write(16'h0088, {7'd0, TILE_SET});
        reg_write(16'h008c, 32'd0);
        reg_write(16'h0080, 32'h00000009);
        reg_read(16'h000c, value);
        if (!value[6])
            $fatal(1, "high tile address was not rejected %08x", value);
        reg_write(16'h0080, 32'd0);
        reg_write(16'h0008, 32'd0);

        $display("VEGA VIDEO PASS frames=%0d", frame_count);
        $finish;
    end
endmodule
