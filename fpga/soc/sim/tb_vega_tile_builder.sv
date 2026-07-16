// Tile map, scroll, flip, transparency, wrap, and line-buffer tests.
`timescale 1ns/1ps

module tb_vega_tile_builder;
    localparam [24:0] T0_MAP = 25'h0001000;
    localparam [24:0] T0_SET = 25'h0002000;
    localparam [24:0] T1_MAP = 25'h0004000;
    localparam [24:0] T1_SET = 25'h0005000;

    reg mem_clk = 1'b0;
    reg pixel_clk = 1'b0;
    always #6.666 mem_clk = ~mem_clk;
    always #18.5 pixel_clk = ~pixel_clk;
    reg rst = 1'b1;

    reg start = 1'b0;
    reg build_bank = 1'b0;
    reg [9:0] line_y = 10'd0;
    reg [9:0] line_width = 10'd0;
    reg [31:0] tile0_ctrl = 32'd0;
    reg [24:0] tile0_map = 25'd0;
    reg [24:0] tile0_set = 25'd0;
    reg [7:0] tile0_size = 8'd0;
    reg [31:0] tile0_scroll = 32'd0;
    reg [31:0] tile1_ctrl = 32'd0;
    reg [24:0] tile1_map = 25'd0;
    reg [24:0] tile1_set = 25'd0;
    reg [7:0] tile1_size = 8'd0;
    reg [31:0] tile1_scroll = 32'd0;
    wire busy;
    wire done;
    wire config_error;

    wire mem_lock;
    wire mem_valid;
    wire mem_ready;
    wire [24:0] mem_addr;
    wire mem_rsp_valid;
    wire [31:0] mem_rdata;
    reg pending = 1'b0;
    reg [24:0] pending_addr = 25'd0;
    reg rsp_valid = 1'b0;
    reg [31:0] rsp_data = 32'd0;
    integer request_count = 0;

    reg display_bank = 1'b0;
    reg [9:0] pixel_x = 10'd0;
    wire [17:0] tile0_pair;
    wire [17:0] tile1_pair;

    vega_tile_builder dut (
        .mem_clk(mem_clk), .mem_rst(rst), .start(start),
        .build_bank(build_bank), .line_y(line_y), .line_width(line_width),
        .tile0_ctrl(tile0_ctrl), .tile0_map(tile0_map),
        .tile0_set(tile0_set), .tile0_size(tile0_size),
        .tile0_scroll(tile0_scroll), .tile1_ctrl(tile1_ctrl),
        .tile1_map(tile1_map), .tile1_set(tile1_set),
        .tile1_size(tile1_size), .tile1_scroll(tile1_scroll),
        .busy(busy), .done(done), .config_error(config_error),
        .mem_lock(mem_lock), .mem_valid(mem_valid), .mem_ready(mem_ready),
        .mem_write(), .mem_addr(mem_addr), .mem_be(), .mem_wdata(),
        .mem_rsp_valid(mem_rsp_valid), .mem_rdata(mem_rdata),
        .pixel_clk(pixel_clk), .display_bank(display_bank),
        .pixel_x(pixel_x), .tile0_pair(tile0_pair),
        .tile1_pair(tile1_pair)
    );

    function automatic [15:0] map_entry(
        input integer layer,
        input integer entry_index
    );
        integer width;
        integer tx;
        integer ty;
        integer tile;
        integer bank;
        reg flip_x;
        reg flip_y;
        begin
            width = layer == 0 ? 4 : 2;
            tx = entry_index % width;
            ty = entry_index / width;
            tile = ty * width + tx;
            bank = layer == 0 ? (tx + 1) & 7 : (ty * 2 + tx + 4) & 7;
            flip_x = tx == 1;
            flip_y = ty == 1;
            map_entry = tile[9:0] | (bank[2:0] << 10) |
                        (flip_x ? 16'h4000 : 16'd0) |
                        (flip_y ? 16'h8000 : 16'd0);
        end
    endfunction

    function automatic [3:0] pattern_nibble(
        input integer layer,
        input integer tile,
        input integer row,
        input integer column
    );
        integer value;
        begin
            value = layer == 0 ? tile + row + column :
                                 tile * 2 + row + column;
            pattern_nibble = value[3:0];
        end
    endfunction

    function automatic [31:0] pattern_word(
        input integer layer,
        input integer tile,
        input integer row,
        input integer word_index
    );
        integer lane;
        reg [31:0] value;
        begin
            value = 32'd0;
            for (lane = 0; lane < 8; lane = lane + 1)
                value[31 - lane * 4 -: 4] =
                    pattern_nibble(layer, tile, row, word_index * 8 + lane);
            pattern_word = value;
        end
    endfunction

    function automatic [31:0] memory_value(input [24:0] address);
        integer offset;
        integer entry_index;
        integer tile;
        integer row;
        integer word_index;
        begin
            memory_value = 32'd0;
            if (address >= T0_MAP && address < T0_MAP + 25'h100) begin
                offset = address - T0_MAP;
                entry_index = offset / 2;
                memory_value = {map_entry(0, entry_index),
                                map_entry(0, entry_index + 1)};
            end else if (address >= T1_MAP && address < T1_MAP + 25'h100) begin
                offset = address - T1_MAP;
                entry_index = offset / 2;
                memory_value = {map_entry(1, entry_index),
                                map_entry(1, entry_index + 1)};
            end else if (address >= T0_SET && address < T0_SET + 25'h1000) begin
                offset = address - T0_SET;
                tile = offset / 32;
                row = (offset % 32) / 4;
                memory_value = pattern_word(0, tile, row, 0);
            end else if (address >= T1_SET && address < T1_SET + 25'h1000) begin
                offset = address - T1_SET;
                tile = offset / 128;
                row = (offset % 128) / 8;
                word_index = ((offset % 128) % 8) / 4;
                memory_value = pattern_word(1, tile, row, word_index);
            end
        end
    endfunction

    assign mem_ready = !pending;
    assign mem_rsp_valid = rsp_valid;
    assign mem_rdata = rsp_data;

    always @(posedge mem_clk) begin
        rsp_valid <= 1'b0;
        if (pending) begin
            rsp_valid <= 1'b1;
            rsp_data <= memory_value(pending_addr);
            pending <= 1'b0;
        end
        if (mem_valid && mem_ready) begin
            if (!mem_lock)
                $fatal(1, "tile request accepted without lock");
            pending_addr <= mem_addr;
            pending <= 1'b1;
            request_count <= request_count + 1;
        end
    end

    function automatic [8:0] expected_descriptor(
        input integer layer,
        input integer screen_x
    );
        integer tile_size;
        integer map_width;
        integer map_height;
        integer scroll_x;
        integer scroll_y;
        integer world_x;
        integer world_y_value;
        integer tx;
        integer ty;
        integer source_x;
        integer source_y;
        reg [15:0] entry;
        reg [3:0] nibble;
        reg opaque;
        begin
            tile_size = layer == 0 ? 8 : 16;
            map_width = layer == 0 ? 4 : 2;
            map_height = 2;
            scroll_x = layer == 0 ? 3 : 11;
            scroll_y = layer == 0 ? 1 : 5;
            world_x = screen_x + scroll_x;
            world_y_value = 2 + scroll_y;
            tx = (world_x / tile_size) & (map_width - 1);
            ty = (world_y_value / tile_size) & (map_height - 1);
            entry = map_entry(layer, ty * map_width + tx);
            source_x = world_x % tile_size;
            source_y = world_y_value % tile_size;
            if (entry[14]) source_x = tile_size - 1 - source_x;
            if (entry[15]) source_y = tile_size - 1 - source_y;
            nibble = pattern_nibble(layer, entry[9:0], source_y, source_x);
            opaque = nibble != 4'd0;
            expected_descriptor = {opaque, 1'b0, entry[12:10], nibble};
        end
    endfunction

    task automatic start_line(input bank);
        begin
            @(negedge mem_clk);
            build_bank = bank;
            start = 1'b1;
            @(negedge mem_clk);
            start = 1'b0;
        end
    endtask

    task automatic check_pixel(input integer x);
        reg [8:0] actual0;
        reg [8:0] actual1;
        reg [8:0] expected0;
        reg [8:0] expected1;
        begin
            pixel_x = x[9:0];
            repeat (2) @(posedge pixel_clk);
            #1;
            actual0 = x[0] ? tile0_pair[8:0] : tile0_pair[17:9];
            actual1 = x[0] ? tile1_pair[8:0] : tile1_pair[17:9];
            expected0 = expected_descriptor(0, x);
            expected1 = expected_descriptor(1, x);
            if (actual0 !== expected0)
                $fatal(1, "tile0 x=%0d got=%03x expected=%03x",
                       x, actual0, expected0);
            if (actual1 !== expected1)
                $fatal(1, "tile1 x=%0d got=%03x expected=%03x",
                       x, actual1, expected1);
        end
    endtask

    task automatic build_and_sample_tile0(
        input bank,
        output reg [8:0] descriptor
    );
        integer build_timeout;
        begin
            start_line(bank);
            build_timeout = 0;
            while (!done) begin
                @(posedge mem_clk);
                build_timeout = build_timeout + 1;
                if (build_timeout > 4000)
                    $fatal(1, "tile wrap test timeout state=%0d", dut.state);
            end
            if (config_error)
                $fatal(1, "tile wrap test reported configuration error");
            display_bank = bank;
            pixel_x = 10'd0;
            repeat (2) @(posedge pixel_clk);
            #1 descriptor = tile0_pair[17:9];
        end
    endtask

    integer x;
    integer timeout;
    integer full_width_cycles;
    integer full_width_requests;
    reg [8:0] wrap_descriptor;

    initial begin
        repeat (6) @(posedge mem_clk);
        rst = 1'b0;

        tile0_ctrl = 32'h0000000b; // enable, transparency, wrap, 8x8
        tile0_map = T0_MAP;
        tile0_set = T0_SET;
        tile0_size = 8'h12;        // 4x2 map
        tile0_scroll = {16'd1, 16'd3};
        tile1_ctrl = 32'h0000001f; // enable, transparency, 16x16, wrap, above
        tile1_map = T1_MAP;
        tile1_set = T1_SET;
        tile1_size = 8'h11;        // 2x2 map
        tile1_scroll = {16'd5, 16'd11};
        line_y = 10'd2;
        line_width = 10'd40;
        repeat (5) @(posedge mem_clk);

        start_line(1'b1);
        timeout = 0;
        while (!done) begin
            @(posedge mem_clk);
            timeout = timeout + 1;
            if (timeout > 4000)
                $fatal(1, "tile builder timeout state=%0d", dut.state);
        end
        if (busy || config_error)
            $fatal(1, "tile builder status busy=%b error=%b", busy, config_error);
        if (request_count > 48)
            $fatal(1, "tile builder request budget exceeded: %0d", request_count);

        display_bank = 1'b1;
        for (x = 0; x < 40; x = x + 1)
            check_pixel(x);

        // Wrapping is independently controllable on each map axis.
        tile1_ctrl = 32'd0;
        tile0_size = 8'h12;
        tile0_scroll = {16'd0, 16'hfff8};
        tile0_ctrl = 32'h0000000b; // visible, transparent, wrap X only
        line_y = 10'd2;
        line_width = 10'd8;
        repeat (4) @(posedge mem_clk);
        build_and_sample_tile0(1'b0, wrap_descriptor);
        if (wrap_descriptor !== 9'h145)
            $fatal(1, "independent X wrap mismatch %03x", wrap_descriptor);
        tile0_ctrl = 32'h00000003;
        build_and_sample_tile0(1'b1, wrap_descriptor);
        if (wrap_descriptor !== 9'd0)
            $fatal(1, "disabled X wrap leaked a pixel %03x", wrap_descriptor);

        tile0_scroll = {16'hfff8, 16'd0};
        tile0_ctrl = 32'h00000023; // visible, transparent, wrap Y only
        build_and_sample_tile0(1'b0, wrap_descriptor);
        if (wrap_descriptor !== 9'h119)
            $fatal(1, "independent Y wrap mismatch %03x", wrap_descriptor);
        tile0_ctrl = 32'h00000003;
        build_and_sample_tile0(1'b1, wrap_descriptor);
        if (wrap_descriptor !== 9'd0)
            $fatal(1, "disabled Y wrap leaked a pixel %03x", wrap_descriptor);

        // Two enabled, independently scrolled layers at the maximum logical
        // width must leave room for framebuffer and sprite traffic inside one
        // 75 MHz memory-clock scanline budget.
        tile0_ctrl = 32'h0000000b;
        tile0_map = T0_MAP;
        tile0_set = T0_SET;
        tile0_size = 8'h12;
        tile0_scroll = {16'd1, 16'd3};
        tile1_ctrl = 32'h0000001f;
        tile1_map = T1_MAP;
        tile1_set = T1_SET;
        tile1_size = 8'h11;
        tile1_scroll = {16'd5, 16'd11};
        line_y = 10'd2;
        line_width = 10'd720;
        repeat (4) @(posedge mem_clk);
        full_width_requests = request_count;
        start_line(1'b0);
        full_width_cycles = 0;
        while (!done) begin
            @(posedge mem_clk);
            full_width_cycles = full_width_cycles + 1;
            if (full_width_cycles > 1500)
                $fatal(1, "full-width tile deadline missed state=%0d",
                       dut.state);
        end
        full_width_requests = request_count - full_width_requests;
        if (config_error)
            $fatal(1, "full-width tile build reported configuration error");
        display_bank = 1'b0;
        check_pixel(0);
        check_pixel(319);
        check_pixel(719);

        // A bad map alignment reports an error and publishes a cleared line.
        tile0_map = T0_MAP + 25'd2;
        tile1_ctrl = 32'd0;
        repeat (4) @(posedge mem_clk);
        start_line(1'b0);
        wait (done);
        if (!config_error)
            $fatal(1, "misaligned tile map did not report config error");
        display_bank = 1'b0;
        pixel_x = 10'd0;
        repeat (2) @(posedge pixel_clk);
        #1;
        if (tile0_pair !== 18'd0 || tile1_pair !== 18'd0)
            $fatal(1, "invalid-config line was not cleared");

        // The complete configured map and tileset must fit in the 32 MiB
        // SDRAM aperture. Address arithmetic is never allowed to wrap.
        tile0_map = 25'h1ff0000;
        tile0_set = T0_SET;
        tile0_size = 8'haa;
        repeat (4) @(posedge mem_clk);
        start_line(1'b1);
        wait (done);
        if (!config_error)
            $fatal(1, "overflowing tile map did not report config error");

        tile0_map = T0_MAP;
        tile0_set = 25'h1fff000;
        tile0_size = 8'd0;
        repeat (4) @(posedge mem_clk);
        start_line(1'b0);
        wait (done);
        if (!config_error)
            $fatal(1, "overflowing tileset did not report config error");

        $display("VEGA TILE PASS requests=%0d cycles=%0d full_width=%0d/%0d",
                 request_count, timeout, full_width_cycles,
                 full_width_requests);
        $finish;
    end
endmodule
