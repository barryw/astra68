// Register, scanout, page-flip, mode-map, and underrun tests for Vega.
`timescale 1ns/1ps

module tb_vega_video;
    localparam [24:0] PAGE0 = 25'h0100000;
    localparam [24:0] PAGE1 = 25'h0200000;
    localparam [24:0] INDEX_PAGE = 25'h0300000;
    localparam [24:0] SCROLL_PAGE = 25'h0400000;
    localparam [24:0] INDEX_SCROLL_PAGE = 25'h0500000;
    localparam integer SCROLL_WIDTH = 724;
    localparam integer SCROLL_HEIGHT = 484;
    localparam integer SCROLL_PITCH = 1448;
    localparam [24:0] SPR_BASE = 25'h0003000;
    localparam [23:0] POST_RGB = 24'hdeadc0;
    localparam [23:0] BACKDROP = 24'h123456;
    localparam [23:0] OVERLAP_BACKDROP = 24'h0badc0;
    localparam [23:0] SPR_RGB = 24'h20a0e0;
    localparam [23:0] INDEX_RGB = 24'hd08020;
    localparam [23:0] INDEX_SCROLL_BOTTOM_RGB = 24'h35b8f0;
    localparam [23:0] INDEX_SCROLL_TOP_RGB = 24'hf05098;
    localparam [15:0] COLORKEY = 16'h07e0;
    localparam [7:0] INDEX_COLORKEY = 8'h7e;

    reg cpu_clk = 1'b0;
    reg mem_clk = 1'b0;
    reg pixel_clk = 1'b0;
    always #40 cpu_clk = ~cpu_clk;
    always #8.333 mem_clk = ~mem_clk;
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
    reg present_writers_idle = 1'b1;
    reg [31:0] blitter_completed_fence = 32'd0;
    reg [31:0] draw_completed_fence = 32'd0;
    wire front_guard_valid;
    wire [24:0] front_guard_start;
    wire [25:0] front_guard_end;
    wire pending_guard_valid;
    wire [24:0] pending_guard_start;
    wire [25:0] pending_guard_end;
    reg cop_write_stb = 1'b0;
    reg [15:0] cop_addr = 16'd0;
    reg [31:0] cop_wdata = 32'd0;

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
    integer scene_generation = 0;
    wire [23:0] rgb;
    wire graphics_active;
    wire [9:0] beam_x;
    wire [9:0] beam_y;

    vega_video dut (
        .cpu_clk(cpu_clk), .cpu_rst(cpu_rst),
        .cpu_write_stb(cpu_write_stb), .cpu_read_stb(cpu_read_stb),
        .cpu_addr(cpu_addr), .cpu_be(cpu_be), .cpu_wdata(cpu_wdata),
        .cpu_rdata(cpu_rdata), .cpu_irq(cpu_irq), .display_ready(1'b1),
        .present_writers_idle(present_writers_idle),
        .blitter_completed_fence(blitter_completed_fence),
        .draw_completed_fence(draw_completed_fence),
        .front_guard_valid(front_guard_valid),
        .front_guard_start(front_guard_start),
        .front_guard_end(front_guard_end),
        .pending_guard_valid(pending_guard_valid),
        .pending_guard_start(pending_guard_start),
        .pending_guard_end(pending_guard_end),
        .cop_write_stb(cop_write_stb), .cop_addr(cop_addr),
        .cop_wdata(cop_wdata),
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

    function automatic [15:0] scroll_pixel(
        input integer row,
        input integer column
    );
        reg [4:0] red;
        reg [5:0] green;
        reg [4:0] blue;
        begin
            red = column[8:4];
            green = row[5:0];
            blue = column[4:0];
            scroll_pixel = {red, green, blue};
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

    function automatic [7:0] indexed_scroll_pixel(
        input integer row,
        input integer column
    );
        begin
            indexed_scroll_pixel = (row * 3 + column) & 8'hff;
        end
    endfunction

    function automatic [31:0] memory_word(input [24:0] address);
        integer page;
        integer offset;
        integer row;
        integer word_index;
        begin
            if (address >= INDEX_SCROLL_PAGE) begin
                offset = address - INDEX_SCROLL_PAGE;
                row = offset / 724;
                word_index = (offset % 724) / 4;
                memory_word = {
                    indexed_scroll_pixel(row, word_index * 4),
                    indexed_scroll_pixel(row, word_index * 4 + 1),
                    indexed_scroll_pixel(row, word_index * 4 + 2),
                    indexed_scroll_pixel(row, word_index * 4 + 3)
                };
            end else if (address >= SPR_BASE &&
                         address < SPR_BASE + 25'h100) begin
                memory_word = 32'h55555555;
            end else if (address >= SCROLL_PAGE) begin
                offset = address - SCROLL_PAGE;
                row = offset / SCROLL_PITCH;
                word_index = (offset % SCROLL_PITCH) / 4;
                memory_word = {
                    scroll_pixel(row, word_index * 2),
                    scroll_pixel(row, word_index * 2 + 1)
                };
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

    task automatic cop_write(input [15:0] address, input [31:0] value);
        begin
            @(negedge cpu_clk);
            cop_addr = address;
            cop_wdata = value;
            cop_write_stb = 1'b1;
            @(negedge cpu_clk);
            cop_write_stb = 1'b0;
            cop_addr = 16'd0;
            cop_wdata = 32'd0;
        end
    endtask

    task automatic wait_scene_editable;
        integer timeout;
        begin
            timeout = 0;
            while (dut.scene_locked) begin
                @(posedge cpu_clk);
                timeout = timeout + 1;
                if (timeout > 4096)
                    $fatal(1, "scene edit lock timeout state=%0d pending=%b",
                           dut.scene_copy_state, dut.present_pending_cpu);
            end
        end
    endtask

    task automatic wait_baseline_copy_active;
        integer timeout;
        begin
            timeout = 0;
            while (!dut.scene_copy_busy || dut.scene_copy_present) begin
                @(posedge cpu_clk);
                timeout = timeout + 1;
                if (timeout > 500000)
                    $fatal(1, "baseline scene-copy timeout state=%0d present=%b",
                           dut.scene_copy_state, dut.scene_copy_present);
            end
        end
    endtask

    task automatic present_scene;
        integer timeout;
        begin
            scene_generation = scene_generation + 1;
            reg_write(16'h0034, scene_generation);
            reg_write(16'h0050, 32'd1);
            if (dut.shadow_ctrl[0] && dut.shadow_ctrl[1] &&
                !dut.shadow_fb_static_config_error_cpu &&
                (!dut.present_validate_busy_cpu || !pending_guard_valid ||
                 pending_guard_end != 26'h2000000))
                $fatal(1, "framebuffer submit was not guarded during validation");
            timeout = 0;
            while (dut.present_validate_busy_cpu) begin
                @(posedge cpu_clk);
                timeout = timeout + 1;
                if (timeout > 32)
                    $fatal(1, "present validation timeout");
            end
        end
    endtask

    task automatic expect_present_rejected(input [255:0] label);
        reg [31:0] status;
        begin
            present_scene();
            reg_read(16'h0054, status);
            if (!status[3] || status[0])
                $fatal(1, "%0s was not rejected status=%08x", label, status);
            reg_write(16'h0054, 32'h00000008);
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
        if (value !== 32'h00050000)
            $fatal(1, "Vega version mismatch %08x", value);
        reg_read(16'h001c, value);
        if (value !== 32'h00000077)
            $fatal(1, "Vega capabilities mismatch %08x", value);
        reg_read(16'h0028, value);
        if (value !== {16'd480, 16'd720})
            $fatal(1, "Vega active size mismatch %08x", value);

        // Palette indices share the low MMIO address bits with scalar
        // controls. Aperture qualification must prevent those writes from
        // enabling IRQs or accidentally submitting a scene.
        reg_write(16'h0410, 32'h00000007);
        reg_read(16'h0010, value);
        if (value !== 32'd0)
            $fatal(1, "palette write aliased IRQ enable %08x", value);
        reg_write(16'h0450, 32'h00000001);
        reg_read(16'h0054, value);
        if (value[3:0] !== 4'd0)
            $fatal(1, "palette write aliased present control %08x", value);

        // TG68K may retain byte-lane address bits on a longword transfer.
        // Scalar decoding uses the complete word address, not a literal
        // byte address and not the aperture-ambiguous low index alone.
        reg_write(16'h0013, 32'h00000007);
        reg_read(16'h0010, value);
        if (value !== 32'h00000007)
            $fatal(1, "lane-addressed IRQ enable was ignored %08x", value);
        reg_write(16'h0017, 32'hffffffff);
        reg_write(16'h0013, 32'd0);

        // A normal vblank restore copies the committed baseline into the
        // active bank. It must not lock the independent shadow scene. Submit
        // while that copy is in flight to reproduce the hardware boot phase.
        wait_baseline_copy_active();
        if (dut.scene_locked)
            $fatal(1, "baseline restore incorrectly locked shadow scene");
        reg_write(16'h0030, {8'd0, OVERLAP_BACKDROP});
        reg_write(16'h040c, 32'h00abcdef);
        reg_write(16'h1000, 32'h13579bdf);
        if (!dut.scene_copy_busy || dut.scene_copy_present)
            $fatal(1, "baseline restore ended before overlap submission");
        present_scene();
        reg_read(16'h0054, value);
        if (!value[0] || value[3] || value[4])
            $fatal(1, "baseline-overlap present rejected status=%08x", value);
        active_frame = frame_count + 2;
        wait_frame(active_frame);
        wait_scene_editable();
        reg_read(16'h0054, value);
        if (value[0] || !value[2] || value[3] || value[4])
            $fatal(1, "baseline-overlap present failed status=%08x", value);
        reg_read(16'h0058, value);
        if (value !== scene_generation)
            $fatal(1, "baseline-overlap generation mismatch %08x", value);

        cpu_addr = 16'h040c;
        repeat (2) @(posedge cpu_clk);
        #1;
        if (cpu_rdata !== 32'h00abcdef)
            $fatal(1, "palette readback mismatch %08x", cpu_rdata);
        reg_read(16'h1000, value);
        if (value !== 32'h13579bdf)
            $fatal(1, "sprite-table readback mismatch %08x", value);

        reg_write(16'h0030, {8'd0, BACKDROP});
        reg_write(16'h0040, {7'd0, PAGE0});
        reg_write(16'h0044, 32'd1440);
        reg_write(16'h0048, 32'd0);
        reg_write(16'h004c, {16'd0, COLORKEY});
        reg_write(16'h0008, 32'h0000001b);
        present_scene();

        if (front_guard_valid || !pending_guard_valid ||
            pending_guard_start != PAGE0 ||
            pending_guard_end != {1'b0, PAGE0} + 26'd691200)
            $fatal(1, "pending framebuffer guard mismatch");

        reg_read(16'h000c, value);
        if (!value[2] || !value[4])
            $fatal(1, "pending/ready status mismatch %08x", value);

        active_frame = frame_count + 1;
        wait_frame(active_frame);
        if (!front_guard_valid || front_guard_start != PAGE0 ||
            front_guard_end != {1'b0, PAGE0} + 26'd691200 ||
            pending_guard_valid)
            $fatal(1, "active framebuffer guard mismatch");
        active_frame = frame_count;
        repeat (6) @(posedge mem_clk);
        if (dut.sprite_budget_effective_mem !== 16'd512)
            $fatal(1, "RGB565 sprite budget limit mismatch %0d",
                   dut.sprite_budget_effective_mem);
        expect_rgb(0, 20, expand_rgb565(test_pixel(0, 0, 18)),
                   "framebuffer");
        expect_rgb(0, 32, BACKDROP, "colorkey");
        expect_rgb(1, 20, expand_rgb565(test_pixel(0, 1, 18)),
                   "next scanline");

        // Copper changes active raster state only. CPU readback remains the
        // canonical shadow, and vblank restores the committed baseline.
        cop_write(16'h0030, 32'h00654321);
        expect_rgb(1, 32, 24'h654321, "copper active backdrop");
        reg_read(16'h0030, value);
        if (value !== {8'd0, BACKDROP})
            $fatal(1, "copper mutated shadow backdrop %08x", value);
        active_frame = frame_count + 1;
        wait_frame(active_frame);
        expect_rgb(0, 32, BACKDROP, "vblank restored backdrop");

        // A present remains queued until both independent render fences have
        // reached their submitted values. Locked shadow writes are rejected.
        wait_scene_editable();
        reg_write(16'h0038, 32'd5);
        reg_write(16'h003c, 32'd7);
        present_scene();
        reg_write(16'h0030, 32'h00fedcba);
        reg_read(16'h0054, value);
        if (!value[0] || !value[4])
            $fatal(1, "pending scene did not lock shadow %08x", value);
        reg_write(16'h0054, 32'h00000010);
        active_frame = frame_count + 1;
        wait_frame(active_frame);
        reg_read(16'h0054, value);
        if (!value[0] || value[2])
            $fatal(1, "unreached fences promoted scene %08x", value);
        @(negedge cpu_clk);
        draw_completed_fence = 32'd5;
        active_frame = frame_count + 1;
        wait_frame(active_frame);
        reg_read(16'h0054, value);
        if (!value[0] || value[2])
            $fatal(1, "draw-only fence promoted scene %08x", value);
        @(negedge cpu_clk);
        blitter_completed_fence = 32'd7;
        present_writers_idle = 1'b0;
        active_frame = frame_count + 1;
        wait_frame(active_frame);
        reg_read(16'h0054, value);
        if (!value[0] || !value[6] || value[2])
            $fatal(1, "active writer did not hold present %08x", value);
        present_writers_idle = 1'b1;
        active_frame = frame_count + 1;
        wait_frame(active_frame);
        wait_scene_editable();
        reg_read(16'h0054, value);
        if (value[0] || !value[2])
            $fatal(1, "completed fences did not promote scene %08x", value);
        reg_read(16'h0058, value);
        if (value !== scene_generation)
            $fatal(1, "completed generation mismatch %08x", value);
        reg_write(16'h0038, 32'd0);
        reg_write(16'h003c, 32'd0);
        active_frame = frame_count;

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
        present_scene();
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
        wait_scene_editable();
        reg_write(16'h0018, 32'd2);
        present_scene();
        active_frame = frame_count + 2;
        wait_frame(active_frame);
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

        // A virtual framebuffer wraps both axes at pixel granularity. X=723
        // starts in the low half of the final RGB565 word, then continues at
        // column zero without exposing row padding.
        wait_pixel(480, 100);
        wait_scene_editable();
        reg_write(16'h0018, 32'd0);
        reg_write(16'h0040, {7'd0, SCROLL_PAGE});
        reg_write(16'h0044, SCROLL_PITCH);
        reg_write(16'h0048, 32'd0);
        reg_write(16'h0068, {16'd483, 16'd723});
        reg_write(16'h006c, {16'd484, 16'd724});
        reg_write(16'h0070, 32'd3);
        reg_write(16'h0008, 32'h0000001b);
        present_scene();
        active_frame = frame_count + 2;
        wait_frame(active_frame);
        expect_rgb(0, 20, expand_rgb565(scroll_pixel(483, 17)),
                   "horizontal framebuffer wrap");
        expect_rgb(1, 20, expand_rgb565(scroll_pixel(0, 17)),
                   "vertical framebuffer wrap");
        if (front_guard_start != SCROLL_PAGE ||
            front_guard_end != {1'b0, SCROLL_PAGE} + 26'd700832)
            $fatal(1, "virtual framebuffer guard mismatch");
        reg_read(16'h0068, value);
        if (value !== {16'd483, 16'd723})
            $fatal(1, "framebuffer view readback mismatch %08x", value);

        // Copper changes only the active viewport. It affects later scanlines
        // whose builds have not started and vblank restores the baseline.
        cop_write(16'h0068, {16'd10, 16'd8});
        reg_read(16'h0068, value);
        if (value !== {16'd483, 16'd723})
            $fatal(1, "copper mutated shadow framebuffer view %08x", value);
        expect_rgb(12, 20, expand_rgb565(scroll_pixel(22, 26)),
                   "copper framebuffer scroll");
        active_frame = frame_count + 1;
        wait_frame(active_frame);
        expect_rgb(0, 20, expand_rgb565(scroll_pixel(483, 17)),
                   "vblank restored framebuffer scroll");

        // Restore a conventional page before exercising sprite composition.
        wait_pixel(480, 100);
        wait_scene_editable();
        reg_write(16'h0040, {7'd0, PAGE1});
        reg_write(16'h0044, 32'd1440);
        reg_write(16'h0068, 32'd0);
        reg_write(16'h006c, 32'd0);
        reg_write(16'h0070, 32'd0);
        reg_write(16'h0008, 32'h0000001b);
        present_scene();
        active_frame = frame_count + 2;
        wait_frame(active_frame);
        expect_rgb(0, 20, expand_rgb565(test_pixel(1, 0, 18)),
                   "restored conventional framebuffer");

        // Two overlapping behind sprites exercise table MMIO, priority,
        // collision reporting, and visibility through the framebuffer key.
        wait_pixel(480, 100);
        wait_scene_editable();
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
        present_scene();
        active_frame = frame_count + 2;
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
        wait_scene_editable();
        reg_write(16'h1000, 32'h00002323);
        reg_write(16'h1020, 32'd0);
        present_scene();
        active_frame = frame_count + 2;
        wait_frame(active_frame);
        expect_rgb(0, 20, SPR_RGB, "front sprite over framebuffer");

        // INDEX8 scanout uses the same global palette and byte-exact
        // colorkey path while halving framebuffer bandwidth.
        wait_pixel(480, 100);
        wait_scene_editable();
        reg_write(16'h04a8, {8'd0, INDEX_RGB});
        reg_write(16'h0800, 32'd0);
        reg_write(16'h0040, {7'd0, INDEX_PAGE});
        reg_write(16'h0044, 32'd720);
        reg_write(16'h0048, 32'd1);
        reg_write(16'h004c, {24'd0, INDEX_COLORKEY});
        reg_write(16'h0008, 32'h0000001b);
        reg_write(16'h0804, 32'd2048);
        reg_write(16'h0804, 32'd64);
        present_scene();
        // This base write occurs after the current vblank edge, so it retires
        // at the following edge and becomes visible one frame later.
        active_frame = frame_count + 2;
        wait_frame(active_frame);
        repeat (6) @(posedge mem_clk);
        if (dut.sprite_budget_effective_mem !== 16'd64)
            $fatal(1, "INDEX8 sprite budget mismatch %0d",
                   dut.sprite_budget_effective_mem);
        expect_rgb(0, 20, INDEX_RGB, "INDEX8 framebuffer");
        expect_rgb(0, 32, BACKDROP, "INDEX8 colorkey");

        // INDEX8 starts at byte phase three and wraps both axes. At displayed
        // x=20 the two-stage compositor is presenting source x=18, which wraps
        // (723 + 18) from virtual width 724 to source column 17.
        wait_pixel(480, 100);
        wait_scene_editable();
        reg_write(16'h06e8, {8'd0, INDEX_SCROLL_BOTTOM_RGB}); // index BA
        reg_write(16'h0444, {8'd0, INDEX_SCROLL_TOP_RGB});    // index 11
        reg_write(16'h0040, {7'd0, INDEX_SCROLL_PAGE});
        reg_write(16'h0044, 32'd724);
        reg_write(16'h0068, {16'd483, 16'd723});
        reg_write(16'h006c, {16'd484, 16'd724});
        reg_write(16'h0070, 32'd3);
        present_scene();
        active_frame = frame_count + 2;
        wait_frame(active_frame);
        expect_rgb(0, 20, INDEX_SCROLL_BOTTOM_RGB,
                   "INDEX8 unaligned horizontal wrap");
        expect_rgb(1, 20, INDEX_SCROLL_TOP_RGB,
                   "INDEX8 vertical wrap");

        wait_pixel(480, 100);
        wait_scene_editable();
        reg_write(16'h0040, {7'd0, INDEX_PAGE});
        reg_write(16'h0044, 32'd720);
        reg_write(16'h0068, 32'd0);
        reg_write(16'h006c, 32'd0);
        reg_write(16'h0070, 32'd0);
        reg_write(16'h0008, 32'd0);
        present_scene();
        active_frame = frame_count + 2;
        wait_frame(active_frame);
        repeat (6) @(posedge pixel_clk);
        #1;
        if (graphics_active || rgb !== POST_RGB)
            $fatal(1, "POST fallback mismatch active=%b rgb=%06x",
                   graphics_active, rgb);
        if (beam_x !== pixel_x || beam_y !== pixel_y)
            $fatal(1, "beam output mismatch");

        // Malformed shadows fail before promotion. The active disabled scene
        // remains untouched instead of briefly exposing an invalid frame.
        reg_write(16'h0040, 32'h80001000);
        reg_write(16'h0044, 32'd720);
        reg_write(16'h0048, 32'd1);
        reg_write(16'h0008, 32'h00000003);
        expect_present_rejected("high framebuffer base");
        if (dut.reg_ctrl != 5'd0 || dut.reg_fb_base_active != INDEX_PAGE)
            $fatal(1, "rejected framebuffer scene changed active state");

        reg_write(16'h0040, {7'd0, INDEX_PAGE});
        reg_write(16'h0044, 32'h00010000);
        expect_present_rejected("high framebuffer pitch");
        reg_write(16'h0044, 32'd716);
        expect_present_rejected("short framebuffer pitch");
        reg_write(16'h0044, 32'd720);
        reg_write(16'h0040, 32'h01faba00);
        present_scene();
        reg_read(16'h0054, value);
        if (!value[0] || value[3])
            $fatal(1, "valid framebuffer scene was rejected %08x", value);
        if (!pending_guard_valid ||
            pending_guard_start != 25'h1faba00 ||
            pending_guard_end != 26'h2000000)
            $fatal(1, "exact-limit pending guard mismatch %07x..%08x",
                   pending_guard_start, pending_guard_end);
        active_frame = frame_count + 1;
        wait_frame(active_frame);
        wait_scene_editable();
        if (!front_guard_valid || front_guard_start != 25'h1faba00 ||
            front_guard_end != 26'h2000000)
            $fatal(1, "exact-limit active guard mismatch %07x..%08x",
                   front_guard_start, front_guard_end);

        reg_write(16'h0040, 32'h01faba04);
        expect_present_rejected("framebuffer endpoint beyond SDRAM");
        reg_write(16'h0040, {7'd0, INDEX_PAGE});

        reg_write(16'h006c, {16'd480, 16'd723});
        expect_present_rejected("unaligned RGB565 virtual width");
        reg_write(16'h006c, {16'd480, 16'd724});
        reg_write(16'h0068, {16'd0, 16'd5});
        reg_write(16'h0070, 32'd0);
        expect_present_rejected("non-wrapping viewport overflow");
        reg_write(16'h0068, 32'd0);
        reg_write(16'h006c, 32'd0);
        reg_write(16'h0008, 32'd0);
        present_scene();

        $display("VEGA VIDEO PASS frames=%0d", frame_count);
        $finish;
    end
endmodule
