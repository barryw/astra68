`timescale 1ns/1ps
`default_nettype none

module tb_astra_framebuffer_line_builder;
    localparam integer OUTPUT_WIDTH = 257;
    localparam integer OUTPUT_HEIGHT = 12;
    localparam integer AXI_ID_WIDTH = 4;
    localparam integer MAX_BUILD_CYCLES = 1200;

    localparam [31:0] ARENA_BASE = 32'h00001000;
    localparam [31:0] ARENA_LIMIT = 32'h00040000;
    localparam [31:0] INDEX_BASE = 32'h00004000;
    localparam [31:0] RGB565_BASE = 32'h00010000;
    localparam [31:0] XRGB_BASE = 32'h00020000;
    localparam [31:0] EDGE_BASE = 32'h00031fc0;

    localparam [31:0] INDEX_PITCH = 32'd512;
    localparam [31:0] RGB565_PITCH = 32'd1024;
    localparam [31:0] XRGB_PITCH = 32'd2048;
    localparam [31:0] EDGE_PITCH = 32'd512;

    localparam [1:0] FORMAT_INDEX8 = 2'd0;
    localparam [1:0] FORMAT_RGB565 = 2'd1;
    localparam [1:0] FORMAT_XRGB8888 = 2'd2;

    reg build_clk = 1'b0;
    reg pixel_clk = 1'b0;
    always #2.5 build_clk = ~build_clk;
    always #6.734 pixel_clk = ~pixel_clk;

    reg build_reset = 1'b1;
    reg pixel_reset = 1'b1;
    reg start = 1'b0;
    reg [1:0] build_slot = 2'd0;
    reg [9:0] line_y = 10'd0;
    reg [1:0] format = FORMAT_INDEX8;
    reg [31:0] framebuffer_base = INDEX_BASE;
    reg [31:0] pitch = INDEX_PITCH;
    reg [12:0] virtual_width = 13'd512;
    reg [12:0] virtual_height = 13'd32;
    reg signed [31:0] viewport_x = 32'sd0;
    reg signed [31:0] viewport_y = 32'sd0;
    reg wrap_x = 1'b0;
    reg wrap_y = 1'b0;
    reg [31:0] arena_base = ARENA_BASE;
    reg [31:0] arena_limit = ARENA_LIMIT;

    wire busy;
    wire done;
    wire line_complete;
    wire [1:0] completed_slot;
    wire [3:0] slot_valid;
    wire config_error;
    wire fetch_error;
    wire deadline_error;
    wire [31:0] build_cycles;
    wire [31:0] read_bytes;
    wire [31:0] axi_debug_status;
    wire [31:0] axi_ar_accept_count;
    wire [31:0] axi_r_accept_count;
    wire [31:0] axi_last_ar_address;
    wire [31:0] axi_response_stall_cycles;

    wire [AXI_ID_WIDTH-1:0] m_axi_arid;
    wire [31:0] m_axi_araddr;
    wire [7:0] m_axi_arlen;
    wire [2:0] m_axi_arsize;
    wire [1:0] m_axi_arburst;
    wire [3:0] m_axi_arcache;
    wire [2:0] m_axi_arprot;
    wire [3:0] m_axi_arqos;
    wire m_axi_arvalid;
    wire m_axi_arready;
    reg [AXI_ID_WIDTH-1:0] m_axi_rid = {AXI_ID_WIDTH{1'b0}};
    reg [63:0] m_axi_rdata = 64'd0;
    reg [1:0] m_axi_rresp = 2'b00;
    reg m_axi_rlast = 1'b0;
    reg m_axi_rvalid = 1'b0;
    wire m_axi_rready;

    reg [1:0] pixel_read_slot = 2'd0;
    reg [10:0] pixel_read_x = 11'd0;
    wire pixel_valid;
    wire [31:0] pixel_value;

    astra_framebuffer_line_builder #(
        .OUTPUT_WIDTH(OUTPUT_WIDTH),
        .OUTPUT_HEIGHT(OUTPUT_HEIGHT),
        .AXI_ID_WIDTH(AXI_ID_WIDTH),
        .MAX_BUILD_CYCLES(MAX_BUILD_CYCLES)
    ) dut (
        .build_clk(build_clk),
        .build_reset(build_reset),
        .start(start),
        .build_slot(build_slot),
        .line_y(line_y),
        .format(format),
        .framebuffer_base(framebuffer_base),
        .pitch(pitch),
        .virtual_width(virtual_width),
        .virtual_height(virtual_height),
        .viewport_x(viewport_x),
        .viewport_y(viewport_y),
        .wrap_x(wrap_x),
        .wrap_y(wrap_y),
        .arena_base(arena_base),
        .arena_limit(arena_limit),
        .busy(busy),
        .done(done),
        .line_complete(line_complete),
        .completed_slot(completed_slot),
        .slot_valid(slot_valid),
        .config_error(config_error),
        .fetch_error(fetch_error),
        .deadline_error(deadline_error),
        .build_cycles(build_cycles),
        .read_bytes(read_bytes),
        .axi_debug_status(axi_debug_status),
        .axi_ar_accept_count(axi_ar_accept_count),
        .axi_r_accept_count(axi_r_accept_count),
        .axi_last_ar_address(axi_last_ar_address),
        .axi_response_stall_cycles(axi_response_stall_cycles),
        .m_axi_arid(m_axi_arid),
        .m_axi_araddr(m_axi_araddr),
        .m_axi_arlen(m_axi_arlen),
        .m_axi_arsize(m_axi_arsize),
        .m_axi_arburst(m_axi_arburst),
        .m_axi_arcache(m_axi_arcache),
        .m_axi_arprot(m_axi_arprot),
        .m_axi_arqos(m_axi_arqos),
        .m_axi_arvalid(m_axi_arvalid),
        .m_axi_arready(m_axi_arready),
        .m_axi_rid(m_axi_rid),
        .m_axi_rdata(m_axi_rdata),
        .m_axi_rresp(m_axi_rresp),
        .m_axi_rlast(m_axi_rlast),
        .m_axi_rvalid(m_axi_rvalid),
        .m_axi_rready(m_axi_rready),
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .pixel_read_slot(pixel_read_slot),
        .pixel_read_x(pixel_read_x),
        .pixel_valid(pixel_valid),
        .pixel_value(pixel_value)
    );

    always @(posedge build_clk) begin
        if (m_axi_arvalid && m_axi_arqos != 4'd0) begin
            $display("FAIL: framebuffer traffic must not starve peer masters");
            $fatal;
        end
    end

    reg [7:0] memory [0:262143];
    integer memory_index;

    function automatic [7:0] index_value(
        input integer source_x,
        input integer source_y
    );
        begin
            index_value = (source_x * 3 + source_y * 67 + 5) & 255;
        end
    endfunction

    function automatic [15:0] rgb565_value(
        input integer source_x,
        input integer source_y
    );
        begin
            rgb565_value = (source_x * 16'h0421 +
                            source_y * 16'h0123 + 16'h5a3c) & 16'hffff;
        end
    endfunction

    function automatic [7:0] red_value(
        input integer source_x,
        input integer source_y
    );
        begin
            red_value = (source_x * 5 + source_y * 7 + 8'h21) & 255;
        end
    endfunction

    function automatic [7:0] green_value(
        input integer source_x,
        input integer source_y
    );
        begin
            green_value = (source_x * 11 + source_y * 3 + 8'h43) & 255;
        end
    endfunction

    function automatic [7:0] blue_value(
        input integer source_x,
        input integer source_y
    );
        begin
            blue_value = (source_x * 13 + source_y * 17 + 8'h65) & 255;
        end
    endfunction

    task automatic initialize_surface(
        input integer surface_base,
        input integer surface_pitch,
        input integer surface_width,
        input integer surface_height,
        input integer surface_format
    );
        integer x;
        integer y;
        integer address;
        reg [15:0] rgb;
        begin
            for (y = 0; y < surface_height; y = y + 1) begin
                for (x = 0; x < surface_width; x = x + 1) begin
                    address = surface_base + y * surface_pitch;
                    case (surface_format)
                        FORMAT_INDEX8: begin
                            memory[address + x] = index_value(x, y);
                        end
                        FORMAT_RGB565: begin
                            rgb = rgb565_value(x, y);
                            memory[address + x * 2] = rgb[15:8];
                            memory[address + x * 2 + 1] = rgb[7:0];
                        end
                        default: begin
                            memory[address + x * 4] =
                                (x ^ y ^ 8'ha5) & 255;
                            memory[address + x * 4 + 1] = red_value(x, y);
                            memory[address + x * 4 + 2] = green_value(x, y);
                            memory[address + x * 4 + 3] = blue_value(x, y);
                        end
                    endcase
                end
            end
        end
    endtask

    function automatic [63:0] read64(input [31:0] address);
        integer lane;
        begin
            read64 = 64'd0;
            for (lane = 0; lane < 8; lane = lane + 1)
                read64[lane * 8 +: 8] = memory[address + lane];
        end
    endfunction

    reg [31:0] command_address [0:1023];
    reg [7:0] command_length [0:1023];
    integer command_write = 0;
    integer command_read = 0;
    integer model_cycle = 0;
    integer ar_count = 0;
    integer outstanding_count = 0;
    integer maximum_outstanding = 0;
    integer simultaneous_turnovers = 0;
    integer r_accept_count = 0;
    reg [31:0] last_ar_address = 32'd0;
    integer saw_4k_limited_burst = 0;

    reg response_active = 1'b0;
    reg [31:0] response_address = 32'd0;
    reg [7:0] response_length = 8'd0;
    reg [7:0] response_index = 8'd0;
    integer response_delay = 0;

    reg accept_requests = 1'b1;
    reg emit_responses = 1'b1;
    reg inject_bad_id = 1'b0;
    reg inject_bad_resp = 1'b0;
    reg inject_bad_last = 1'b0;
    reg force_simultaneous_turnover = 1'b0;
    reg turnover_armed = 1'b0;

    wire expected_model_last = response_index == response_length;
    wire response_final_accept = m_axi_rvalid && m_axi_rready &&
                                 expected_model_last;
    wire ordinary_arready = model_cycle[2:0] != 3'd2 &&
        model_cycle[3:0] != 4'd9;
    assign m_axi_arready = accept_requests &&
        (force_simultaneous_turnover && turnover_armed ?
            response_final_accept : ordinary_arready);

    always @(posedge build_clk) begin
        if (build_reset) begin
            command_write <= 0;
            command_read <= 0;
            model_cycle <= 0;
            ar_count <= 0;
            outstanding_count <= 0;
            maximum_outstanding <= 0;
            simultaneous_turnovers <= 0;
            r_accept_count <= 0;
            last_ar_address <= 32'd0;
            saw_4k_limited_burst <= 0;
            turnover_armed <= 1'b0;
            response_active <= 1'b0;
            response_address <= 32'd0;
            response_length <= 8'd0;
            response_index <= 8'd0;
            response_delay <= 0;
            m_axi_rid <= {AXI_ID_WIDTH{1'b0}};
            m_axi_rdata <= 64'd0;
            m_axi_rresp <= 2'b00;
            m_axi_rlast <= 1'b0;
            m_axi_rvalid <= 1'b0;
        end else begin
            model_cycle <= model_cycle + 1;

            if (!force_simultaneous_turnover)
                turnover_armed <= 1'b0;
            else if (outstanding_count == 2)
                turnover_armed <= 1'b1;

            if (outstanding_count != 0 && !m_axi_rready)
                $fatal(1, "accepted AXI read was backpressured");
            if (dut.reserved_beats > 32)
                $fatal(1, "AXI receive credits exceeded FIFO capacity: %0d",
                       dut.reserved_beats);

            if (m_axi_arvalid && m_axi_arready) begin
                if (m_axi_arid != {AXI_ID_WIDTH{1'b0}} ||
                    m_axi_arsize != 3'b011 || m_axi_arburst != 2'b01 ||
                    m_axi_araddr[2:0] != 3'b000 || m_axi_arlen > 8'd15)
                    $fatal(1, "invalid AXI read request");
                if ({1'b0, m_axi_araddr[11:0]} +
                    (({5'd0, m_axi_arlen} + 13'd1) << 3) > 13'd4096)
                    $fatal(1, "AXI request crossed 4 KiB boundary");
                if (m_axi_araddr[11:0] == 12'hfe0 && m_axi_arlen == 8'd3)
                    saw_4k_limited_burst <= 1;
                command_address[command_write] <= m_axi_araddr;
                command_length[command_write] <= m_axi_arlen;
                command_write <= command_write + 1;
                ar_count <= ar_count + 1;
                last_ar_address <= m_axi_araddr;
                if (outstanding_count + 1 > maximum_outstanding)
                    maximum_outstanding <= outstanding_count + 1;
            end

            case ({m_axi_arvalid && m_axi_arready,
                   response_final_accept})
                2'b10: outstanding_count <= outstanding_count + 1;
                2'b01: outstanding_count <= outstanding_count - 1;
                2'b11: simultaneous_turnovers <=
                    simultaneous_turnovers + 1;
                default: begin end
            endcase

            if (m_axi_rvalid && m_axi_rready) begin
                r_accept_count <= r_accept_count + 1;
                m_axi_rvalid <= 1'b0;
                if (expected_model_last) begin
                    response_active <= 1'b0;
                end else begin
                    response_index <= response_index + 8'd1;
                    response_address <= response_address + 32'd8;
                    response_delay <= 1 + (model_cycle & 1);
                end
            end

            if (!response_active && !m_axi_rvalid &&
                command_read < command_write && emit_responses) begin
                response_active <= 1'b1;
                response_address <= command_address[command_read];
                response_length <= command_length[command_read];
                response_index <= 8'd0;
                response_delay <= 12 + (model_cycle & 3);
                command_read <= command_read + 1;
            end else if (response_active && !m_axi_rvalid &&
                         emit_responses) begin
                if (response_delay != 0) begin
                    response_delay <= response_delay - 1;
                end else begin
                    m_axi_rdata <= read64(response_address);
                    m_axi_rresp <= inject_bad_resp ? 2'b10 : 2'b00;
                    m_axi_rid <= inject_bad_id ?
                        {{(AXI_ID_WIDTH-1){1'b0}}, 1'b1} :
                        {AXI_ID_WIDTH{1'b0}};
                    m_axi_rlast <= inject_bad_last && expected_model_last ?
                        1'b0 : expected_model_last;
                    m_axi_rvalid <= 1'b1;
                end
            end
        end
    end

    task automatic pulse_build_reset;
        begin
            @(negedge build_clk);
            build_reset = 1'b1;
            repeat (3) @(posedge build_clk);
            @(negedge build_clk);
            build_reset = 1'b0;
        end
    endtask

    task automatic launch_and_wait(
        input integer expected_complete,
        input integer maximum_cycles
    );
        integer cycles;
        begin
            @(negedge build_clk);
            start = 1'b1;
            @(negedge build_clk);
            start = 1'b0;
            cycles = 0;
            while (!done) begin
                @(posedge build_clk);
                #1;
                cycles = cycles + 1;
                if (cycles > maximum_cycles)
                    $fatal(1, "framebuffer line build timed out");
            end
            if (line_complete !== expected_complete[0])
                $fatal(1, "line_complete=%0d expected=%0d",
                       line_complete, expected_complete);
        end
    endtask

    task automatic expected_pixel(
        input integer screen_x,
        input integer test_format,
        input integer test_width,
        input integer test_height,
        input integer test_viewport_x,
        input integer test_viewport_y,
        input integer test_line_y,
        input integer test_wrap_x,
        input integer test_wrap_y,
        output integer expected_valid,
        output reg [31:0] expected_value
    );
        integer source_x;
        integer source_y;
        integer world_x;
        integer world_y;
        begin
            world_x = test_viewport_x + screen_x;
            world_y = test_viewport_y + test_line_y;
            expected_valid = (test_wrap_x ||
                (world_x >= 0 && world_x < test_width)) &&
                (test_wrap_y ||
                (world_y >= 0 && world_y < test_height));
            source_x = world_x;
            source_y = world_y;
            if (test_wrap_x) begin
                source_x = world_x % test_width;
                if (source_x < 0)
                    source_x = source_x + test_width;
            end
            if (test_wrap_y) begin
                source_y = world_y % test_height;
                if (source_y < 0)
                    source_y = source_y + test_height;
            end
            case (test_format)
                FORMAT_INDEX8:
                    expected_value = {24'd0, index_value(source_x, source_y)};
                FORMAT_RGB565:
                    expected_value = {16'd0, rgb565_value(source_x, source_y)};
                default:
                    expected_value = {8'hff, red_value(source_x, source_y),
                        green_value(source_x, source_y),
                        blue_value(source_x, source_y)};
            endcase
        end
    endtask

    task automatic check_line(
        input integer slot,
        input integer test_format,
        input integer test_width,
        input integer test_height,
        input integer test_viewport_x,
        input integer test_viewport_y,
        input integer test_line_y,
        input integer test_wrap_x,
        input integer test_wrap_y
    );
        integer x;
        integer want_valid;
        reg [31:0] want_value;
        begin
            pixel_read_slot = slot[1:0];
            for (x = 0; x < OUTPUT_WIDTH; x = x + 1) begin
                @(negedge pixel_clk);
                pixel_read_x = x[10:0];
                @(posedge pixel_clk);
                #1;
                expected_pixel(x, test_format, test_width, test_height,
                    test_viewport_x, test_viewport_y, test_line_y,
                    test_wrap_x, test_wrap_y, want_valid, want_value);
                if (pixel_valid !== want_valid[0] ||
                    (want_valid && pixel_value !== want_value))
                    $fatal(1,
                        "pixel %0d got valid=%0d value=%08x expected=%0d/%08x",
                        x, pixel_valid, pixel_value, want_valid, want_value);
            end
        end
    endtask

    task automatic select_surface(
        input integer test_format,
        input integer test_base,
        input integer test_pitch
    );
        begin
            format = test_format[1:0];
            framebuffer_base = test_base;
            pitch = test_pitch;
            virtual_width = 13'd512;
            virtual_height = 13'd32;
            viewport_x = 32'sd0;
            viewport_y = 32'sd0;
            line_y = 10'd0;
            wrap_x = 1'b0;
            wrap_y = 1'b0;
        end
    endtask

    integer requests_before;
    initial begin
        for (memory_index = 0; memory_index < 262144;
             memory_index = memory_index + 1)
            memory[memory_index] = 8'd0;
        initialize_surface(INDEX_BASE, INDEX_PITCH, 512, 32,
                           FORMAT_INDEX8);
        initialize_surface(RGB565_BASE, RGB565_PITCH, 512, 32,
                           FORMAT_RGB565);
        initialize_surface(XRGB_BASE, XRGB_PITCH, 512, 32,
                           FORMAT_XRGB8888);
        initialize_surface(EDGE_BASE, EDGE_PITCH, 512, 32,
                           FORMAT_INDEX8);

        repeat (5) @(posedge build_clk);
        build_reset = 1'b0;
        repeat (3) @(posedge pixel_clk);
        pixel_reset = 1'b0;

        // Unaligned byte starts prove the explicit Astra byte order for all
        // three formats, independent of AXI lane order.
        build_slot = 2'd0;
        select_surface(FORMAT_INDEX8, INDEX_BASE, INDEX_PITCH);
        viewport_x = 32'sd3;
        viewport_y = 32'sd2;
        line_y = 10'd5;
        launch_and_wait(1, MAX_BUILD_CYCLES);
        if (config_error || fetch_error || deadline_error ||
            !slot_valid[0] || completed_slot != 2'd0)
            $fatal(1, "INDEX8 status failed");
        check_line(0, FORMAT_INDEX8, 512, 32, 3, 2, 5, 0, 0);
        $display("INDEX8 byte-order pass cycles=%0d bytes=%0d",
                 build_cycles, read_bytes);

        build_slot = 2'd1;
        select_surface(FORMAT_RGB565, RGB565_BASE, RGB565_PITCH);
        viewport_x = 32'sd1;
        viewport_y = 32'sd3;
        line_y = 10'd4;
        launch_and_wait(1, MAX_BUILD_CYCLES);
        if (config_error || fetch_error || deadline_error || !slot_valid[1])
            $fatal(1, "RGB565 status failed");
        check_line(1, FORMAT_RGB565, 512, 32, 1, 3, 4, 0, 0);
        $display("RGB565 byte-order pass cycles=%0d bytes=%0d",
                 build_cycles, read_bytes);

        build_slot = 2'd2;
        select_surface(FORMAT_XRGB8888, XRGB_BASE, XRGB_PITCH);
        force_simultaneous_turnover = 1'b1;
        viewport_x = 32'sd1;
        viewport_y = 32'sd1;
        line_y = 10'd6;
        launch_and_wait(1, MAX_BUILD_CYCLES);
        if (config_error || fetch_error || deadline_error || !slot_valid[2])
            $fatal(1, "XRGB8888 status failed");
        check_line(2, FORMAT_XRGB8888, 512, 32, 1, 1, 6, 0, 0);
        if (maximum_outstanding < 2)
            $fatal(1, "AXI reader reached only %0d outstanding requests",
                   maximum_outstanding);
        if (simultaneous_turnovers == 0)
            $fatal(1, "AXI reader did not exercise simultaneous turnover");
        force_simultaneous_turnover = 1'b0;
        $display("XRGB8888 byte-order/outstanding pass cycles=%0d bytes=%0d outstanding=%0d turnovers=%0d",
                 build_cycles, read_bytes, maximum_outstanding,
                 simultaneous_turnovers);

        // Wrapping splits the line into two bounded contiguous regions and
        // wraps the row independently.
        build_slot = 2'd3;
        select_surface(FORMAT_INDEX8, INDEX_BASE, INDEX_PITCH);
        viewport_x = 32'sd400;
        viewport_y = 32'sd30;
        line_y = 10'd5;
        wrap_x = 1'b1;
        wrap_y = 1'b1;
        launch_and_wait(1, MAX_BUILD_CYCLES);
        if (config_error || fetch_error || deadline_error || !slot_valid[3])
            $fatal(1, "wrapped status failed");
        check_line(3, FORMAT_INDEX8, 512, 32, 400, 30, 5, 1, 1);
        $display("independent XY wrap pass cycles=%0d bytes=%0d",
                 build_cycles, read_bytes);

        // Nonwrapped coordinates outside the surface produce invalid pixels,
        // while the in-range portion remains exact.
        build_slot = 2'd0;
        select_surface(FORMAT_RGB565, RGB565_BASE, RGB565_PITCH);
        viewport_x = -32'sd11;
        viewport_y = 32'sd4;
        line_y = 10'd2;
        launch_and_wait(1, MAX_BUILD_CYCLES);
        check_line(0, FORMAT_RGB565, 512, 32, -11, 4, 2, 0, 0);

        build_slot = 2'd1;
        viewport_x = 32'sd500;
        launch_and_wait(1, MAX_BUILD_CYCLES);
        check_line(1, FORMAT_RGB565, 512, 32, 500, 4, 2, 0, 0);

        build_slot = 2'd2;
        viewport_x = 32'sd0;
        viewport_y = -32'sd4;
        line_y = 10'd2;
        launch_and_wait(1, MAX_BUILD_CYCLES);
        check_line(2, FORMAT_RGB565, 512, 32, 0, -4, 2, 0, 0);
        $display("nonwrapped clipping pass");

        // A source beginning 32 bytes before a 4 KiB boundary must split the
        // first request at that boundary, never cross it.
        pulse_build_reset();
        build_slot = 2'd0;
        select_surface(FORMAT_INDEX8, EDGE_BASE, EDGE_PITCH);
        viewport_x = 32'sd33;
        launch_and_wait(1, MAX_BUILD_CYCLES);
        if (!saw_4k_limited_burst)
            $fatal(1, "4 KiB burst split was not observed");
        check_line(0, FORMAT_INDEX8, 512, 32, 33, 0, 0, 0, 0);
        $display("4 KiB burst-boundary pass requests=%0d", ar_count);

        // Each malformed AXI response invalidates the destination line while
        // draining all accepted traffic.
        build_slot = 2'd1;
        select_surface(FORMAT_INDEX8, INDEX_BASE, INDEX_PITCH);
        inject_bad_resp = 1'b1;
        launch_and_wait(0, MAX_BUILD_CYCLES);
        inject_bad_resp = 1'b0;
        if (!fetch_error || config_error || deadline_error || slot_valid[1])
            $fatal(1, "RRESP failure was not contained");

        build_slot = 2'd2;
        inject_bad_id = 1'b1;
        launch_and_wait(0, MAX_BUILD_CYCLES);
        inject_bad_id = 1'b0;
        if (!fetch_error || config_error || deadline_error || slot_valid[2])
            $fatal(1, "RID failure was not contained");

        build_slot = 2'd3;
        inject_bad_last = 1'b1;
        launch_and_wait(0, MAX_BUILD_CYCLES);
        inject_bad_last = 1'b0;
        if (!fetch_error || config_error || deadline_error || slot_valid[3])
            $fatal(1, "RLAST failure was not contained");
        $display("AXI response containment pass");

        // Static format, alignment, pitch, and arena errors are rejected
        // before issuing any memory request.
        requests_before = ar_count;
        format = 2'd3;
        build_slot = 2'd0;
        launch_and_wait(0, 20);
        if (!config_error || ar_count != requests_before)
            $fatal(1, "reserved format was accepted");

        select_surface(FORMAT_INDEX8, INDEX_BASE + 1, INDEX_PITCH);
        launch_and_wait(0, 20);
        if (!config_error || ar_count != requests_before)
            $fatal(1, "unaligned base was accepted");

        select_surface(FORMAT_RGB565, RGB565_BASE, 32'd512);
        launch_and_wait(0, 20);
        if (!config_error || ar_count != requests_before)
            $fatal(1, "short pitch was accepted");

        select_surface(FORMAT_XRGB8888, XRGB_BASE, XRGB_PITCH);
        arena_limit = XRGB_BASE + 32'd64;
        launch_and_wait(0, 20);
        arena_limit = ARENA_LIMIT;
        if (!config_error || ar_count != requests_before)
            $fatal(1, "arena overflow was accepted");
        $display("configuration containment pass");

        // A deadline stops new requests but must drain every accepted AXI read
        // before reporting completion or allowing another line to start.
        pulse_build_reset();
        select_surface(FORMAT_XRGB8888, XRGB_BASE, XRGB_PITCH);
        build_slot = 2'd0;
        emit_responses = 1'b0;
        @(negedge build_clk);
        start = 1'b1;
        @(negedge build_clk);
        start = 1'b0;
        wait (deadline_error);
        #1;
        if (!busy || done || outstanding_count == 0)
            $fatal(1,
                "deadline abandoned accepted reads busy=%0d done=%0d outstanding=%0d",
                busy, done, outstanding_count);
        if (!axi_debug_status[29] || !axi_debug_status[11] ||
            axi_debug_status[15:12] == 4'd0 ||
            axi_response_stall_cycles == 32'd0)
            $fatal(1, "AXI deadline diagnostics did not capture the stall");
        emit_responses = 1'b1;
        wait (done);
        #1;
        if (!fetch_error || !deadline_error || config_error || slot_valid[0])
            $fatal(1, "deadline failure was not contained");
        if (outstanding_count != 0 || m_axi_rvalid)
            $fatal(1, "deadline left stale AXI traffic outstanding=%0d rvalid=%0d",
                outstanding_count, m_axi_rvalid);

        build_slot = 2'd1;
        launch_and_wait(1, MAX_BUILD_CYCLES);
        if (config_error || fetch_error || deadline_error || !slot_valid[1])
            $fatal(1, "framebuffer reader was not reusable after deadline");
        if (axi_ar_accept_count !== ar_count ||
            axi_r_accept_count !== r_accept_count ||
            axi_last_ar_address !== last_ar_address ||
            axi_response_stall_cycles !== 32'd0)
            $fatal(1, "AXI diagnostics do not match accepted traffic");
        $display("deadline drain/reuse pass");

        $display("ASTRA FRAMEBUFFER LINE BUILDER PASS");
        $finish;
    end
endmodule

`default_nettype wire
