`timescale 1ns/1ps
`default_nettype none

module tb_astra_tile_line_builder;
    localparam integer OUTPUT_WIDTH = 37;
    localparam integer AXI_ID_WIDTH = 4;
    localparam [31:0] ARENA_BASE = 32'h00001000;
    localparam [31:0] ARENA_LIMIT = 32'h00010000;
    localparam [31:0] MAP8_BASE = 32'h00001000;
    localparam [31:0] PAT8_BASE = 32'h00002000;
    localparam [31:0] MAP4_BASE = 32'h00004000;
    localparam [31:0] PAT4_BASE = 32'h00005000;

    reg build_clk = 1'b0;
    reg pixel_clk = 1'b0;
    always #2.5 build_clk = ~build_clk;
    always #6.734 pixel_clk = ~pixel_clk;

    reg build_reset = 1'b1;
    reg pixel_reset = 1'b1;
    reg start = 1'b0;
    reg [1:0] build_slot = 2'd0;
    reg [10:0] line_y = 11'd0;
    reg signed [31:0] scroll_x = 32'sd0;
    reg signed [31:0] scroll_y = 32'sd0;
    reg tile_16 = 1'b0;
    reg index_8 = 1'b1;
    reg [3:0] map_width_log2 = 4'd3;
    reg [3:0] map_height_log2 = 4'd3;
    reg wrap_x = 1'b1;
    reg wrap_y = 1'b1;
    reg transparent_enable = 1'b0;
    reg [7:0] transparent_index = 8'd0;
    reg [31:0] map_base = MAP8_BASE;
    reg [31:0] pattern_base = PAT8_BASE;
    reg [16:0] tile_count = 17'd32;
    reg [31:0] arena_base = ARENA_BASE;
    reg [31:0] arena_limit = ARENA_LIMIT;

    wire busy;
    wire done;
    wire line_complete;
    wire [1:0] completed_slot;
    wire [3:0] slot_valid;
    wire config_error;
    wire descriptor_error;
    wire fetch_error;
    wire [31:0] build_cycles;
    wire [31:0] map_read_bytes;
    wire [31:0] pattern_read_bytes;

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
    wire [3:0] pixel_palette_bank;
    wire [7:0] pixel_index;

    astra_tile_line_builder #(
        .OUTPUT_WIDTH(OUTPUT_WIDTH),
        .AXI_ID_WIDTH(AXI_ID_WIDTH)
    ) dut (
        .build_clk(build_clk),
        .build_reset(build_reset),
        .start(start),
        .build_slot(build_slot),
        .line_y(line_y),
        .scroll_x(scroll_x),
        .scroll_y(scroll_y),
        .tile_16(tile_16),
        .index_8(index_8),
        .map_width_log2(map_width_log2),
        .map_height_log2(map_height_log2),
        .wrap_x(wrap_x),
        .wrap_y(wrap_y),
        .transparent_enable(transparent_enable),
        .transparent_index(transparent_index),
        .map_base(map_base),
        .pattern_base(pattern_base),
        .tile_count(tile_count),
        .arena_base(arena_base),
        .arena_limit(arena_limit),
        .busy(busy),
        .done(done),
        .line_complete(line_complete),
        .completed_slot(completed_slot),
        .slot_valid(slot_valid),
        .config_error(config_error),
        .descriptor_error(descriptor_error),
        .fetch_error(fetch_error),
        .build_cycles(build_cycles),
        .map_read_bytes(map_read_bytes),
        .pattern_read_bytes(pattern_read_bytes),
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
        .pixel_palette_bank(pixel_palette_bank),
        .pixel_index(pixel_index)
    );

    reg [7:0] memory [0:65535];
    integer memory_index;

    task automatic write_be32(input integer address, input reg [31:0] value);
        begin
            memory[address] = value[31:24];
            memory[address + 1] = value[23:16];
            memory[address + 2] = value[15:8];
            memory[address + 3] = value[7:0];
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

    function automatic [31:0] descriptor_value(
        input integer map_x,
        input integer map_y,
        input integer width,
        input integer count
    );
        integer tile;
        integer bank;
        begin
            tile = (map_y * width + map_x) % count;
            bank = (map_x + 2 * map_y) & 15;
            descriptor_value = (tile << 16) | (bank << 12) |
                ((map_x & 1) << 11) | ((map_y & 1) << 10);
        end
    endfunction

    function automatic [7:0] index8_value(
        input integer tile,
        input integer row,
        input integer column
    );
        begin
            index8_value = (tile * 17 + row * 3 + column) & 255;
        end
    endfunction

    function automatic [3:0] index4_value(
        input integer tile,
        input integer row,
        input integer column
    );
        begin
            index4_value = (tile * 3 + row * 5 + column) & 15;
        end
    endfunction

    task automatic initialize_index8_scene;
        integer x;
        integer y;
        integer tile;
        integer row;
        integer column;
        integer address;
        begin
            for (y = 0; y < 8; y = y + 1)
                for (x = 0; x < 8; x = x + 1)
                    write_be32(MAP8_BASE + (y * 8 + x) * 4,
                               descriptor_value(x, y, 8, 32));
            for (tile = 0; tile < 32; tile = tile + 1)
                for (row = 0; row < 8; row = row + 1)
                    for (column = 0; column < 8; column = column + 1) begin
                        address = PAT8_BASE + tile * 64 + row * 8 + column;
                        memory[address] = index8_value(tile, row, column);
                    end
        end
    endtask

    task automatic initialize_index4_scene;
        integer x;
        integer y;
        integer tile;
        integer row;
        integer column;
        integer address;
        reg [7:0] packed_byte;
        begin
            for (y = 0; y < 4; y = y + 1)
                for (x = 0; x < 4; x = x + 1)
                    write_be32(MAP4_BASE + (y * 4 + x) * 4,
                               descriptor_value(x, y, 4, 16));
            for (tile = 0; tile < 16; tile = tile + 1)
                for (row = 0; row < 16; row = row + 1)
                    for (column = 0; column < 16; column = column + 2) begin
                        address = PAT4_BASE + tile * 128 + row * 8 +
                                  column / 2;
                        packed_byte = {index4_value(tile, row, column),
                                       index4_value(tile, row, column + 1)};
                        memory[address] = packed_byte;
                    end
        end
    endtask

    reg [31:0] command_address [0:1023];
    reg [7:0] command_length [0:1023];
    integer command_write = 0;
    integer command_read = 0;
    integer model_cycle = 0;
    reg response_active = 1'b0;
    reg [31:0] response_address = 32'd0;
    reg [7:0] response_length = 8'd0;
    reg [7:0] response_index = 8'd0;
    integer response_delay = 0;
    reg inject_error = 1'b0;
    reg [31:0] inject_error_address = 32'd0;

    assign m_axi_arready = model_cycle[2:0] != 3'd2 &&
                           model_cycle[3:0] != 4'd9;

    always @(posedge build_clk) begin
        model_cycle <= model_cycle + 1;

        if (m_axi_arvalid && m_axi_arready) begin
            if (m_axi_arid != {AXI_ID_WIDTH{1'b0}} ||
                m_axi_arsize != 3'b011 || m_axi_arburst != 2'b01 ||
                m_axi_araddr[2:0] != 3'b000 || m_axi_arlen > 8'd1)
                $fatal(1, "invalid AXI read request");
            command_address[command_write] <= m_axi_araddr;
            command_length[command_write] <= m_axi_arlen;
            command_write <= command_write + 1;
        end

        if (m_axi_rvalid && m_axi_rready) begin
            m_axi_rvalid <= 1'b0;
            if (m_axi_rlast) begin
                response_active <= 1'b0;
            end else begin
                response_index <= response_index + 8'd1;
                response_address <= response_address + 32'd8;
                response_delay <= 1 + (model_cycle & 1);
            end
        end

        if (!response_active && !m_axi_rvalid &&
            command_read < command_write) begin
            response_active <= 1'b1;
            response_address <= command_address[command_read];
            response_length <= command_length[command_read];
            response_index <= 8'd0;
            response_delay <= 2 + (model_cycle & 3);
            command_read <= command_read + 1;
        end else if (response_active && !m_axi_rvalid) begin
            if (response_delay != 0) begin
                response_delay <= response_delay - 1;
            end else begin
                m_axi_rdata <= read64(response_address);
                m_axi_rresp <= inject_error &&
                    response_address == inject_error_address ? 2'b10 : 2'b00;
                m_axi_rlast <= response_index == response_length;
                m_axi_rid <= {AXI_ID_WIDTH{1'b0}};
                m_axi_rvalid <= 1'b1;
            end
        end
    end

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
                    $fatal(1, "line build timed out");
            end
            if (line_complete !== expected_complete[0])
                $fatal(1, "line_complete=%0d expected=%0d",
                       line_complete, expected_complete);
            if (command_read != command_write)
                $fatal(1, "AXI model retained unread commands");
        end
    endtask

    task automatic expected_pixel(
        input integer screen_x,
        input integer test_tile_size,
        input integer test_index8,
        input integer test_map_width,
        input integer test_map_height,
        input integer test_scroll_x,
        input integer test_scroll_y,
        input integer test_line_y,
        input integer test_wrap_x,
        input integer test_wrap_y,
        input integer test_transparent_enable,
        input integer test_transparent_index,
        output integer expected_valid,
        output integer expected_bank,
        output integer expected_index
    );
        longint signed world_x;
        longint signed world_y;
        longint signed raw_x;
        longint signed raw_y;
        integer map_x;
        integer map_y;
        integer tile;
        integer source_x;
        integer source_y;
        integer flip_x;
        integer flip_y;
        begin
            world_x = $signed(test_scroll_x) + screen_x;
            world_y = $signed(test_scroll_y) + test_line_y;
            raw_x = world_x >>> (test_tile_size == 16 ? 4 : 3);
            raw_y = world_y >>> (test_tile_size == 16 ? 4 : 3);
            expected_valid = (test_wrap_x ||
                (raw_x >= 0 && raw_x < test_map_width)) &&
                (test_wrap_y ||
                (raw_y >= 0 && raw_y < test_map_height));
            map_x = raw_x & (test_map_width - 1);
            map_y = raw_y & (test_map_height - 1);
            tile = (map_y * test_map_width + map_x) %
                   (test_index8 ? 32 : 16);
            expected_bank = (map_x + 2 * map_y) & 15;
            flip_x = map_x & 1;
            flip_y = map_y & 1;
            source_x = world_x & (test_tile_size - 1);
            source_y = world_y & (test_tile_size - 1);
            if (flip_x)
                source_x = test_tile_size - 1 - source_x;
            if (flip_y)
                source_y = test_tile_size - 1 - source_y;
            if (test_index8)
                expected_index = index8_value(tile, source_y, source_x);
            else
                expected_index = index4_value(tile, source_y, source_x);
            if (test_transparent_enable &&
                expected_index == test_transparent_index)
                expected_valid = 0;
        end
    endtask

    task automatic check_line(
        input integer slot,
        input integer test_tile_size,
        input integer test_index8,
        input integer test_map_width,
        input integer test_map_height,
        input integer test_scroll_x,
        input integer test_scroll_y,
        input integer test_line_y,
        input integer test_wrap_x,
        input integer test_wrap_y,
        input integer test_transparent_enable,
        input integer test_transparent_index
    );
        integer x;
        integer want_valid;
        integer want_bank;
        integer want_index;
        integer padded_width;
        begin
            pixel_read_slot = slot[1:0];
            for (x = 0; x < OUTPUT_WIDTH; x = x + 1) begin
                @(negedge pixel_clk);
                pixel_read_x = x[10:0];
                @(posedge pixel_clk);
                #1;
                expected_pixel(x, test_tile_size, test_index8,
                    test_map_width, test_map_height, test_scroll_x,
                    test_scroll_y, test_line_y, test_wrap_x, test_wrap_y,
                    test_transparent_enable, test_transparent_index,
                    want_valid, want_bank, want_index);
                if (pixel_valid !== want_valid[0] ||
                    (want_valid &&
                     (pixel_palette_bank !== want_bank[3:0] ||
                      pixel_index !== want_index[7:0])))
                    $fatal(1,
                        "pixel %0d got v=%0d bank=%0d index=%0d expected %0d/%0d/%0d",
                        x, pixel_valid, pixel_palette_bank, pixel_index,
                        want_valid, want_bank, want_index);
            end

            padded_width = ((OUTPUT_WIDTH + 3) / 4) * 4;
            for (x = OUTPUT_WIDTH; x < padded_width; x = x + 1) begin
                @(negedge pixel_clk);
                pixel_read_x = x[10:0];
                @(posedge pixel_clk);
                #1;
                if (pixel_valid !== 1'b0)
                    $fatal(1, "padding pixel %0d was published", x);
            end
        end
    endtask

    integer phase;
    initial begin
        for (memory_index = 0; memory_index < 65536;
             memory_index = memory_index + 1)
            memory[memory_index] = 8'd0;
        initialize_index8_scene();
        initialize_index4_scene();

        repeat (5) @(posedge build_clk);
        build_reset = 1'b0;
        repeat (3) @(posedge pixel_clk);
        pixel_reset = 1'b0;

        // INDEX8/8x8: signed vertical scroll, both-axis wrap, X reflection,
        // Y reflection, transparency, AXI request stalls, and response gaps.
        build_slot = 2'd0;
        line_y = 11'd5;
        scroll_x = 32'sd3;
        scroll_y = -32'sd11;
        tile_16 = 1'b0;
        index_8 = 1'b1;
        map_width_log2 = 4'd3;
        map_height_log2 = 4'd3;
        wrap_x = 1'b1;
        wrap_y = 1'b1;
        transparent_enable = 1'b1;
        transparent_index = 8'd68;
        map_base = MAP8_BASE;
        pattern_base = PAT8_BASE;
        tile_count = 17'd32;
        launch_and_wait(1, 2000);
        if (config_error || descriptor_error || fetch_error ||
            !slot_valid[0] || completed_slot != 2'd0)
            $fatal(1, "INDEX8 status failed");
        check_line(0, 8, 1, 8, 8, 3, -11, 5, 1, 1, 1, 68);
        $display("INDEX8 wrapped line pass cycles=%0d map=%0d pattern=%0d",
                 build_cycles, map_read_bytes, pattern_read_bytes);

        // Sweep every 8x8 horizontal phase. A four-pixel output write can
        // cross a span at any lane, so each possible boundary is checked.
        transparent_enable = 1'b0;
        for (phase = 0; phase < 8; phase = phase + 1) begin
            build_slot = phase & 3;
            scroll_x = phase;
            launch_and_wait(1, 2000);
            check_line(phase & 3, 8, 1, 8, 8, phase, -11, 5,
                       1, 1, 0, 0);
        end
        $display("INDEX8 quad-boundary sweep pass");

        // INDEX4/16x16: negative unwrapped X clips the first span and the
        // remaining packed nibbles retain bank and flip semantics.
        build_slot = 2'd1;
        line_y = 11'd7;
        scroll_x = -32'sd3;
        scroll_y = 32'sd2;
        tile_16 = 1'b1;
        index_8 = 1'b0;
        map_width_log2 = 4'd2;
        map_height_log2 = 4'd2;
        wrap_x = 1'b0;
        wrap_y = 1'b0;
        transparent_enable = 1'b1;
        transparent_index = 8'd9;
        map_base = MAP4_BASE;
        pattern_base = PAT4_BASE;
        tile_count = 17'd16;
        launch_and_wait(1, 2000);
        if (config_error || descriptor_error || fetch_error ||
            !slot_valid[1])
            $fatal(1, "INDEX4 status failed");
        check_line(1, 16, 0, 4, 4, -3, 2, 7, 0, 0, 1, 9);
        $display("INDEX4 clipped line pass cycles=%0d map=%0d pattern=%0d",
                 build_cycles, map_read_bytes, pattern_read_bytes);

        // INDEX4 additionally sweeps every packed-nibble source phase.
        wrap_x = 1'b1;
        wrap_y = 1'b1;
        transparent_enable = 1'b0;
        for (phase = 0; phase < 16; phase = phase + 1) begin
            build_slot = phase & 3;
            scroll_x = phase;
            launch_and_wait(1, 2000);
            check_line(phase & 3, 16, 0, 4, 4, phase, 2, 7,
                       1, 1, 0, 0);
        end
        $display("INDEX4 quad-boundary sweep pass");

        // A reserved descriptor bit rejects the complete line and invalidates
        // the destination slot rather than publishing partial pixels.
        write_be32(MAP8_BASE, descriptor_value(0, 0, 8, 32) | 32'h1);
        build_slot = 2'd2;
        line_y = 11'd0;
        scroll_x = 32'sd0;
        scroll_y = 32'sd0;
        tile_16 = 1'b0;
        index_8 = 1'b1;
        map_width_log2 = 4'd3;
        map_height_log2 = 4'd3;
        wrap_x = 1'b1;
        wrap_y = 1'b1;
        transparent_enable = 1'b0;
        map_base = MAP8_BASE;
        pattern_base = PAT8_BASE;
        tile_count = 17'd32;
        launch_and_wait(0, 2000);
        if (!descriptor_error || config_error || fetch_error || slot_valid[2])
            $fatal(1, "malformed descriptor was not contained");
        write_be32(MAP8_BASE, descriptor_value(0, 0, 8, 32));
        $display("descriptor containment pass");

        // A failed pattern response drains outstanding AXI traffic but cannot
        // publish the partially constructed line.
        inject_error = 1'b1;
        inject_error_address = PAT8_BASE;
        build_slot = 2'd3;
        launch_and_wait(0, 2000);
        inject_error = 1'b0;
        if (!fetch_error || config_error || slot_valid[3])
            $fatal(1, "AXI response failure was not contained");
        $display("AXI error containment pass");

        // Static bounds and alignment are rejected before any AXI request.
        map_base = MAP8_BASE + 32'd4;
        build_slot = 2'd0;
        launch_and_wait(0, 20);
        if (!config_error || busy || !done)
            $fatal(1, "invalid static configuration was accepted");
        map_base = MAP8_BASE;
        $display("configuration containment pass");

        $display("ASTRA TILE LINE BUILDER PASS");
        $finish;
    end
endmodule

`default_nettype wire
