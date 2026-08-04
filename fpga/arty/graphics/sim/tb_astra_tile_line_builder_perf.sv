`timescale 1ns/1ps
`default_nettype none

module tb_astra_tile_line_builder_perf;
    localparam integer OUTPUT_WIDTH = 1280;
    localparam integer LINE_CYCLES_200MHZ = 4444;
    localparam [31:0] ARENA_BASE = 32'h10000000;
    localparam [31:0] MAP_BASE = 32'h10000000;
    localparam [31:0] PATTERN_BASE = 32'h10200000;
    localparam [31:0] ARENA_LIMIT = 32'h11000000;

    reg build_clk = 1'b0;
    reg pixel_clk = 1'b0;
    always #2.5 build_clk = ~build_clk;
    always #6.734 pixel_clk = ~pixel_clk;
    reg build_reset = 1'b1;
    reg pixel_reset = 1'b1;
    reg start = 1'b0;

    wire busy;
    wire done;
    wire line_complete;
    wire [3:0] slot_valid;
    wire config_error;
    wire descriptor_error;
    wire fetch_error;
    wire [31:0] build_cycles;
    wire [31:0] map_read_bytes;
    wire [31:0] pattern_read_bytes;
    wire [3:0] arid;
    wire [31:0] araddr;
    wire [7:0] arlen;
    wire [2:0] arsize;
    wire [1:0] arburst;
    wire arvalid;
    wire arready;
    reg [3:0] rid = 4'd0;
    reg [63:0] rdata = 64'd0;
    reg [1:0] rresp = 2'b00;
    reg rlast = 1'b0;
    reg rvalid = 1'b0;
    wire rready;
    reg [1:0] pixel_read_slot = 2'd0;
    reg [10:0] pixel_read_x = 11'd0;
    wire pixel_valid;
    wire [3:0] pixel_palette_bank;
    wire [7:0] pixel_index;

    astra_tile_line_builder #(
        .OUTPUT_WIDTH(OUTPUT_WIDTH),
        .AXI_ID_WIDTH(4)
    ) dut (
        .build_clk(build_clk),
        .build_reset(build_reset),
        .start(start),
        .build_slot(2'd0),
        .line_y(11'd719),
        .scroll_x(32'sd1),
        .scroll_y(-32'sd37),
        .tile_16(1'b0),
        .index_8(1'b1),
        .map_width_log2(4'd9),
        .map_height_log2(4'd9),
        .wrap_x(1'b1),
        .wrap_y(1'b1),
        .transparent_enable(1'b0),
        .transparent_index(8'd0),
        .map_base(MAP_BASE),
        .pattern_base(PATTERN_BASE),
        .tile_count(17'h10000),
        .arena_base(ARENA_BASE),
        .arena_limit(ARENA_LIMIT),
        .busy(busy),
        .done(done),
        .line_complete(line_complete),
        .completed_slot(),
        .slot_valid(slot_valid),
        .config_error(config_error),
        .descriptor_error(descriptor_error),
        .fetch_error(fetch_error),
        .build_cycles(build_cycles),
        .map_read_bytes(map_read_bytes),
        .pattern_read_bytes(pattern_read_bytes),
        .m_axi_arid(arid),
        .m_axi_araddr(araddr),
        .m_axi_arlen(arlen),
        .m_axi_arsize(arsize),
        .m_axi_arburst(arburst),
        .m_axi_arcache(),
        .m_axi_arprot(),
        .m_axi_arqos(),
        .m_axi_arvalid(arvalid),
        .m_axi_arready(arready),
        .m_axi_rid(rid),
        .m_axi_rdata(rdata),
        .m_axi_rresp(rresp),
        .m_axi_rlast(rlast),
        .m_axi_rvalid(rvalid),
        .m_axi_rready(rready),
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .pixel_read_slot(pixel_read_slot),
        .pixel_read_x(pixel_read_x),
        .pixel_valid(pixel_valid),
        .pixel_palette_bank(pixel_palette_bank),
        .pixel_index(pixel_index)
    );

    function automatic [31:0] map_entry(input integer entry_index);
        integer tile;
        integer map_x;
        integer map_y;
        begin
            tile = entry_index & 16'hffff;
            map_x = entry_index & 511;
            map_y = (entry_index >> 9) & 511;
            map_entry = (tile << 16) |
                        (((map_x + 2 * map_y) & 15) << 12);
        end
    endfunction

    function automatic [7:0] memory_byte(input [31:0] address);
        integer offset;
        integer entry_index;
        integer byte_index;
        integer tile;
        integer row;
        integer column;
        reg [31:0] entry;
        begin
            memory_byte = 8'd0;
            if (address >= MAP_BASE && address < MAP_BASE + 32'h00100000) begin
                offset = address - MAP_BASE;
                entry_index = offset >> 2;
                byte_index = offset & 3;
                entry = map_entry(entry_index);
                case (byte_index)
                    0: memory_byte = entry[31:24];
                    1: memory_byte = entry[23:16];
                    2: memory_byte = entry[15:8];
                    default: memory_byte = entry[7:0];
                endcase
            end else if (address >= PATTERN_BASE &&
                         address < PATTERN_BASE + 32'h00400000) begin
                offset = address - PATTERN_BASE;
                tile = offset >> 6;
                row = (offset >> 3) & 7;
                column = offset & 7;
                memory_byte = (tile * 17 + row * 3 + column) & 255;
            end
        end
    endfunction

    function automatic [63:0] memory_word(input [31:0] address);
        integer lane;
        begin
            memory_word = 64'd0;
            for (lane = 0; lane < 8; lane = lane + 1)
                memory_word[lane * 8 +: 8] = memory_byte(address + lane);
        end
    endfunction

    reg [31:0] command_address [0:1023];
    reg [7:0] command_length [0:1023];
    integer command_write = 0;
    integer command_read = 0;
    integer response_beat = 0;
    integer response_delay = 24;
    integer model_cycle = 0;

    // This profile permits three of every four address cycles. Ordered read
    // data has a 24-cycle cold latency and then sustains one beat every two
    // fabric clocks. Empty periods re-arm the cold latency.
    assign arready = model_cycle[1:0] != 2'd1;

    always @(posedge build_clk) begin
        model_cycle <= model_cycle + 1;

        if (arvalid && arready) begin
            if (arid != 4'd0 || arsize != 3'b011 ||
                arburst != 2'b01 || araddr[2:0] != 3'd0)
                $fatal(1, "invalid performance AXI request");
            command_address[command_write] <= araddr;
            command_length[command_write] <= arlen;
            command_write <= command_write + 1;
        end

        if (rvalid && rready) begin
            rvalid <= 1'b0;
            if (rlast) begin
                command_read <= command_read + 1;
                response_beat <= 0;
                if (command_read + 1 >= command_write)
                    response_delay <= 24;
            end else begin
                response_beat <= response_beat + 1;
            end
        end else if (!rvalid && command_read < command_write) begin
            if (response_delay != 0) begin
                response_delay <= response_delay - 1;
            end else begin
                rdata <= memory_word(command_address[command_read] +
                                     response_beat * 8);
                rlast <= response_beat == command_length[command_read];
                rresp <= 2'b00;
                rid <= 4'd0;
                rvalid <= 1'b1;
            end
        end
    end

    task automatic check_pixel(
        input integer x,
        input integer expected_bank,
        input integer expected_index
    );
        begin
            @(negedge pixel_clk);
            pixel_read_x = x[10:0];
            @(posedge pixel_clk);
            #1;
            if (!pixel_valid || pixel_palette_bank != expected_bank[3:0] ||
                pixel_index != expected_index[7:0])
                $fatal(1, "sample pixel %0d mismatch", x);
        end
    endtask

    integer cycles;
    integer world_y;
    integer map_y;
    integer first_tile;
    integer last_world_x;
    integer last_map_x;
    integer last_tile;
    initial begin
        repeat (5) @(posedge build_clk);
        build_reset = 1'b0;
        repeat (3) @(posedge pixel_clk);
        pixel_reset = 1'b0;
        @(negedge build_clk);
        start = 1'b1;
        @(negedge build_clk);
        start = 1'b0;

        cycles = 0;
        while (!done) begin
            @(posedge build_clk);
            #1;
            cycles = cycles + 1;
            if (cycles > 5000)
                $fatal(1, "performance line timed out");
        end

        if (!line_complete || !slot_valid[0] || config_error ||
            descriptor_error || fetch_error)
            $fatal(1, "performance line did not complete cleanly");
        if (build_cycles > LINE_CYCLES_200MHZ)
            $fatal(1, "line took %0d clocks, budget is %0d",
                   build_cycles, LINE_CYCLES_200MHZ);
        if (map_read_bytes != 32'd1288 || pattern_read_bytes != 32'd1288)
            $fatal(1, "unexpected traffic map=%0d pattern=%0d",
                   map_read_bytes, pattern_read_bytes);

        world_y = -37 + 719;
        map_y = (world_y >>> 3) & 511;
        first_tile = (map_y * 512) & 16'hffff;
        check_pixel(0, (2 * map_y) & 15,
                    (first_tile * 17 + (world_y & 7) * 3 + 1) & 255);
        last_world_x = 1 + OUTPUT_WIDTH - 1;
        last_map_x = (last_world_x >>> 3) & 511;
        last_tile = (map_y * 512 + last_map_x) & 16'hffff;
        check_pixel(OUTPUT_WIDTH - 1, (last_map_x + 2 * map_y) & 15,
                    (last_tile * 17 + (world_y & 7) * 3 +
                     (last_world_x & 7)) & 255);

        $display("ASTRA TILE LINE PERFORMANCE PASS cycles=%0d deadline=%0d map=%0d pattern=%0d",
                 build_cycles, LINE_CYCLES_200MHZ,
                 map_read_bytes, pattern_read_bytes);
        $finish;
    end
endmodule

`default_nettype wire
