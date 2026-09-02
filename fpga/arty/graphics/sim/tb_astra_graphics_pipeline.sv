`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module tb_astra_graphics_pipeline #(
    parameter integer OUTPUT_WIDTH = 64,
    parameter integer OUTPUT_HEIGHT = 4,
    parameter integer TOTAL_WIDTH = 128,
    parameter integer TOTAL_HEIGHT = 48,
    parameter [31:0] FRAMEBUFFER_PITCH = OUTPUT_WIDTH * 2
);
    localparam [2:0] COPPER_OP_END = 3'd0;
    localparam [2:0] COPPER_OP_MOVE = 3'd1;
    localparam [2:0] COPPER_OP_WAIT = 3'd2;
    localparam [2:0] COPPER_OP_IRQ = 3'd4;
    localparam [2:0] COPPER_OP_DISPATCH = 3'd6;
    // Keep vblank longer than concurrent scene activation and the 4,352-entry
    // active-palette baseline restore. Production 720p provides about 667 us;
    // this reduced mode provides about 76 us at the same clock ratio.
    localparam integer AXI_ID_WIDTH = 6;
    localparam [31:0] ARENA_BASE = 32'h00001000;
    localparam [31:0] ARENA_LIMIT = 32'h00101000;
    localparam [31:0] FRAMEBUFFER_BASE = 32'h00001000;
    localparam integer FRAME_MEMORY_BYTES = 65536;
    localparam [31:0] SPRITE_BASE = 32'h00008000;
    localparam [31:0] SPRITE_ARGB = 32'h80ff4000;
    localparam [31:0] RENDER_SUBMISSION_OFFSET = 32'h00010000;
    localparam [31:0] RENDER_COMPLETION_OFFSET = 32'h00020000;
    localparam [31:0] RENDER_DESCRIPTOR_OFFSET = 32'h00030000;
    localparam [31:0] RENDER_DATA_OFFSET = 32'h00031000;
    localparam [31:0] RENDER_GENERATION = 32'h00000017;
    localparam integer FRAME_TIMEOUT_CYCLES =
        100000 + TOTAL_WIDTH * TOTAL_HEIGHT * 4;

    reg build_clk = 1'b0;
    reg pixel_clk = 1'b0;
    always #2.5 build_clk = ~build_clk;
    always #6.734 pixel_clk = ~pixel_clk;

    reg build_reset = 1'b1;
    reg pixel_reset = 1'b1;
    reg [10:0] pixel_x = 11'd0;
    reg [9:0] pixel_y = 10'd0;
    wire pixel_output_valid;
    wire [23:0] pixel_output_rgb;

    wire [31:0] active_generation;
    wire [31:0] lines_built;
    wire [31:0] lines_failed;
    wire [31:0] scheduler_overruns;
    wire [31:0] pixel_underruns;
    wire [31:0] commit_errors;
    wire [31:0] commit_deferrals;
    wire scene_active;
    wire render_interrupt;

    reg [31:0] s_axi_awaddr = 32'd0;
    reg [2:0] s_axi_awprot = 3'd0;
    reg s_axi_awvalid = 1'b0;
    wire s_axi_awready;
    reg [31:0] s_axi_wdata = 32'd0;
    reg [3:0] s_axi_wstrb = 4'd0;
    reg s_axi_wvalid = 1'b0;
    wire s_axi_wready;
    wire [1:0] s_axi_bresp;
    wire s_axi_bvalid;
    reg s_axi_bready = 1'b0;
    reg [31:0] s_axi_araddr = 32'd0;
    reg [2:0] s_axi_arprot = 3'd0;
    reg s_axi_arvalid = 1'b0;
    wire s_axi_arready;
    wire [31:0] s_axi_rdata;
    wire [1:0] s_axi_rresp;
    wire s_axi_rvalid;
    reg s_axi_rready = 1'b0;

    wire [AXI_ID_WIDTH-1:0] fb_axi_arid;
    wire [31:0] fb_axi_araddr;
    wire [7:0] fb_axi_arlen;
    wire [2:0] fb_axi_arsize;
    wire [1:0] fb_axi_arburst;
    wire [3:0] fb_axi_arcache;
    wire [2:0] fb_axi_arprot;
    wire [3:0] fb_axi_arqos;
    wire fb_axi_arvalid;
    wire fb_axi_arready;
    reg [AXI_ID_WIDTH-1:0] fb_axi_rid = {AXI_ID_WIDTH{1'b0}};
    reg [63:0] fb_axi_rdata = 64'd0;
    reg [1:0] fb_axi_rresp = 2'b00;
    reg fb_axi_rlast = 1'b0;
    reg fb_axi_rvalid = 1'b0;
    wire fb_axi_rready;

    wire [AXI_ID_WIDTH-1:0] tile0_axi_arid;
    wire [31:0] tile0_axi_araddr;
    wire [7:0] tile0_axi_arlen;
    wire [2:0] tile0_axi_arsize;
    wire [1:0] tile0_axi_arburst;
    wire [3:0] tile0_axi_arcache;
    wire [2:0] tile0_axi_arprot;
    wire [3:0] tile0_axi_arqos;
    wire tile0_axi_arvalid;
    wire tile0_axi_arready = 1'b1;
    wire [AXI_ID_WIDTH-1:0] tile0_axi_rid = {AXI_ID_WIDTH{1'b0}};
    wire [63:0] tile0_axi_rdata = 64'd0;
    wire [1:0] tile0_axi_rresp = 2'b00;
    wire tile0_axi_rlast = 1'b0;
    wire tile0_axi_rvalid = 1'b0;
    wire tile0_axi_rready;

    wire [AXI_ID_WIDTH-1:0] tile1_axi_arid;
    wire [31:0] tile1_axi_araddr;
    wire [7:0] tile1_axi_arlen;
    wire [2:0] tile1_axi_arsize;
    wire [1:0] tile1_axi_arburst;
    wire [3:0] tile1_axi_arcache;
    wire [2:0] tile1_axi_arprot;
    wire [3:0] tile1_axi_arqos;
    wire tile1_axi_arvalid;
    wire tile1_axi_arready = 1'b1;
    wire [AXI_ID_WIDTH-1:0] tile1_axi_rid = {AXI_ID_WIDTH{1'b0}};
    wire [63:0] tile1_axi_rdata = 64'd0;
    wire [1:0] tile1_axi_rresp = 2'b00;
    wire tile1_axi_rlast = 1'b0;
    wire tile1_axi_rvalid = 1'b0;
    wire tile1_axi_rready;

    wire [AXI_ID_WIDTH-1:0] sprite_axi_arid;
    wire [31:0] sprite_axi_araddr;
    wire [7:0] sprite_axi_arlen;
    wire [2:0] sprite_axi_arsize;
    wire [1:0] sprite_axi_arburst;
    wire [3:0] sprite_axi_arcache;
    wire [2:0] sprite_axi_arprot;
    wire [3:0] sprite_axi_arqos;
    wire sprite_axi_arvalid;
    wire sprite_axi_arready;
    wire [AXI_ID_WIDTH-1:0] sprite_axi_rid =
        {AXI_ID_WIDTH{1'b0}};
    reg [63:0] sprite_axi_rdata = 64'd0;
    wire [1:0] sprite_axi_rresp = 2'b00;
    wire sprite_axi_rlast;
    wire sprite_axi_rvalid;
    wire sprite_axi_rready;
    reg sprite_response_active = 1'b0;
    reg [31:0] sprite_response_address = 32'd0;
    reg [4:0] sprite_response_beats = 5'd0;

    wire [AXI_ID_WIDTH-1:0] render_axi_arid;
    wire [31:0] render_axi_araddr;
    wire [7:0] render_axi_arlen;
    wire [2:0] render_axi_arsize;
    wire [1:0] render_axi_arburst;
    wire [3:0] render_axi_arcache;
    wire [2:0] render_axi_arprot;
    wire [3:0] render_axi_arqos;
    wire render_axi_arvalid;
    wire render_axi_arready;
    wire [AXI_ID_WIDTH-1:0] render_axi_rid;
    wire [63:0] render_axi_rdata;
    wire [1:0] render_axi_rresp;
    wire render_axi_rlast;
    wire render_axi_rvalid;
    wire render_axi_rready;
    wire [AXI_ID_WIDTH-1:0] render_axi_awid;
    wire [31:0] render_axi_awaddr;
    wire [7:0] render_axi_awlen;
    wire [2:0] render_axi_awsize;
    wire [1:0] render_axi_awburst;
    wire [3:0] render_axi_awcache;
    wire [2:0] render_axi_awprot;
    wire [3:0] render_axi_awqos;
    wire render_axi_awvalid;
    wire render_axi_awready;
    wire [63:0] render_axi_wdata;
    wire [7:0] render_axi_wstrb;
    wire render_axi_wlast;
    wire render_axi_wvalid;
    wire render_axi_wready;
    wire [AXI_ID_WIDTH-1:0] render_axi_bid;
    wire [1:0] render_axi_bresp;
    wire render_axi_bvalid;
    wire render_axi_bready;
    wire [31:0] render_read_transactions;
    wire [31:0] render_write_transactions;

    astra_graphics_pipeline #(
        .ARENA_BASE(ARENA_BASE),
        .ARENA_LIMIT(ARENA_LIMIT),
        .OUTPUT_WIDTH(OUTPUT_WIDTH),
        .OUTPUT_HEIGHT(OUTPUT_HEIGHT),
        .TOTAL_WIDTH(TOTAL_WIDTH),
        .TOTAL_HEIGHT(TOTAL_HEIGHT),
        .OUTPUT_PREFETCH(37),
        .AXI_ID_WIDTH(AXI_ID_WIDTH),
.BOOT_FONT_HEX("assets/fonts/astra_8x16.hex")
    ) dut (.*);

    astra_render_axi_memory_model #(
        .AXI_ID_WIDTH(AXI_ID_WIDTH),
        .MEMORY_BYTES(262144),
        .BASE_ADDRESS(ARENA_BASE)
    ) render_memory_i (
        .clk(build_clk),
        .reset(build_reset),
        .stall_reads(1'b0),
        .stall_writes(1'b0),
        .inject_read_error(1'b0),
        .inject_write_error(1'b0),
        .s_axi_arid(render_axi_arid),
        .s_axi_araddr(render_axi_araddr),
        .s_axi_arlen(render_axi_arlen),
        .s_axi_arsize(render_axi_arsize),
        .s_axi_arburst(render_axi_arburst),
        .s_axi_arvalid(render_axi_arvalid),
        .s_axi_arready(render_axi_arready),
        .s_axi_rid(render_axi_rid),
        .s_axi_rdata(render_axi_rdata),
        .s_axi_rresp(render_axi_rresp),
        .s_axi_rlast(render_axi_rlast),
        .s_axi_rvalid(render_axi_rvalid),
        .s_axi_rready(render_axi_rready),
        .s_axi_awid(render_axi_awid),
        .s_axi_awaddr(render_axi_awaddr),
        .s_axi_awlen(render_axi_awlen),
        .s_axi_awsize(render_axi_awsize),
        .s_axi_awburst(render_axi_awburst),
        .s_axi_awvalid(render_axi_awvalid),
        .s_axi_awready(render_axi_awready),
        .s_axi_wdata(render_axi_wdata),
        .s_axi_wstrb(render_axi_wstrb),
        .s_axi_wlast(render_axi_wlast),
        .s_axi_wvalid(render_axi_wvalid),
        .s_axi_wready(render_axi_wready),
        .s_axi_bid(render_axi_bid),
        .s_axi_bresp(render_axi_bresp),
        .s_axi_bvalid(render_axi_bvalid),
        .s_axi_bready(render_axi_bready),
        .read_transactions(render_read_transactions),
        .write_transactions(render_write_transactions)
    );

    always @(posedge pixel_clk) begin
        if (pixel_reset) begin
            pixel_x <= 11'd0;
            pixel_y <= 10'd0;
        end else if (pixel_x == TOTAL_WIDTH - 1) begin
            pixel_x <= 11'd0;
            pixel_y <= pixel_y == TOTAL_HEIGHT - 1 ?
                10'd0 : pixel_y + 10'd1;
        end else begin
            pixel_x <= pixel_x + 11'd1;
        end
    end

    reg [7:0] memory [0:FRAME_MEMORY_BYTES-1];

    function automatic [15:0] source_rgb565(
        input integer x,
        input integer y
    );
        reg [31:0] value;
        begin
            value = x * 32'h0421 + y * 32'h1f3d +
                    (x >> 8) * 32'h0101;
            source_rgb565 = value ^ (x << 7) ^ (y << 11);
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

    function automatic [7:0] divide_255_round(input [16:0] numerator);
        reg [17:0] adjusted;
        begin
            adjusted = {1'b0, numerator} + 18'd128;
            divide_255_round = (adjusted + (adjusted >> 8)) >> 8;
        end
    endfunction

    function automatic [23:0] blend_straight_over_opaque(
        input [23:0] destination,
        input [31:0] source
    );
        reg [7:0] alpha;
        reg [7:0] premult_red;
        reg [7:0] premult_green;
        reg [7:0] premult_blue;
        reg [16:0] numerator;
        begin
            alpha = source[31:24];
            premult_red = divide_255_round(source[23:16] * alpha);
            premult_green = divide_255_round(source[15:8] * alpha);
            premult_blue = divide_255_round(source[7:0] * alpha);
            numerator = premult_red * 8'd255 +
                        destination[23:16] * (8'd255 - alpha);
            blend_straight_over_opaque[23:16] =
                divide_255_round(numerator);
            numerator = premult_green * 8'd255 +
                        destination[15:8] * (8'd255 - alpha);
            blend_straight_over_opaque[15:8] =
                divide_255_round(numerator);
            numerator = premult_blue * 8'd255 +
                        destination[7:0] * (8'd255 - alpha);
            blend_straight_over_opaque[7:0] =
                divide_255_round(numerator);
        end
    endfunction

    function automatic [63:0] read64(input [31:0] address);
        integer lane;
        begin
            read64 = 64'd0;
            for (lane = 0; lane < 8; lane = lane + 1)
                read64[lane * 8 +: 8] =
                    memory[address - ARENA_BASE + lane];
        end
    endfunction

    assign sprite_axi_arready = !sprite_response_active;
    assign sprite_axi_rvalid = sprite_response_active;
    assign sprite_axi_rlast = sprite_response_beats == 5'd1;
    always @* sprite_axi_rdata = read64(sprite_response_address);

    always @(posedge build_clk) begin
        if (build_reset) begin
            sprite_response_active <= 1'b0;
            sprite_response_address <= 32'd0;
            sprite_response_beats <= 5'd0;
        end else if (sprite_axi_arvalid && sprite_axi_arready) begin
            if (sprite_axi_arsize != 3'b011 ||
                sprite_axi_arburst != 2'b01 ||
                sprite_axi_araddr[2:0] != 3'b000 ||
                sprite_axi_araddr < ARENA_BASE ||
                sprite_axi_araddr + ((sprite_axi_arlen + 1) << 3) >
                    ARENA_LIMIT)
                $fatal(1, "invalid integrated sprite AXI request");
            sprite_response_active <= 1'b1;
            sprite_response_address <= sprite_axi_araddr;
            sprite_response_beats <= sprite_axi_arlen + 5'd1;
        end else if (sprite_axi_rvalid && sprite_axi_rready) begin
            if (sprite_response_beats == 5'd1) begin
                sprite_response_active <= 1'b0;
                sprite_response_beats <= 5'd0;
            end else begin
                sprite_response_address <= sprite_response_address + 32'd8;
                sprite_response_beats <= sprite_response_beats - 5'd1;
            end
        end
    end

    reg [31:0] command_address [0:31];
    reg [7:0] command_length [0:31];
    reg [4:0] command_write = 5'd0;
    reg [4:0] command_read = 5'd0;
    reg [5:0] command_count = 6'd0;
    reg response_active = 1'b0;
    reg [31:0] response_address = 32'd0;
    reg [7:0] response_length = 8'd0;
    reg [7:0] response_index = 8'd0;
    integer response_delay = 0;

    wire command_push = fb_axi_arvalid && fb_axi_arready;
    wire command_pop = !response_active && !fb_axi_rvalid &&
        command_count != 0;
    assign fb_axi_arready = command_count != 32;

    always @(posedge build_clk) begin
        if (build_reset) begin
            command_write <= 5'd0;
            command_read <= 5'd0;
            command_count <= 6'd0;
            response_active <= 1'b0;
            response_address <= 32'd0;
            response_length <= 8'd0;
            response_index <= 8'd0;
            response_delay <= 0;
            fb_axi_rid <= {AXI_ID_WIDTH{1'b0}};
            fb_axi_rdata <= 64'd0;
            fb_axi_rresp <= 2'b00;
            fb_axi_rlast <= 1'b0;
            fb_axi_rvalid <= 1'b0;
        end else begin
            if (command_push) begin
                if (fb_axi_arsize != 3'b011 ||
                    fb_axi_arburst != 2'b01 ||
                    fb_axi_arlen > 8'd15 ||
                    fb_axi_araddr[2:0] != 3'b000 ||
                    fb_axi_araddr < ARENA_BASE ||
                    fb_axi_araddr + ((fb_axi_arlen + 1) << 3) > ARENA_LIMIT)
                    $fatal(1, "invalid integrated framebuffer AXI request");
                command_address[command_write] <= fb_axi_araddr;
                command_length[command_write] <= fb_axi_arlen;
                command_write <= command_write + 5'd1;
            end

            case ({command_push, command_pop})
                2'b10: command_count <= command_count + 6'd1;
                2'b01: command_count <= command_count - 6'd1;
                default: begin end
            endcase

            if (fb_axi_rvalid && fb_axi_rready) begin
                fb_axi_rvalid <= 1'b0;
                if (response_index == response_length) begin
                    response_active <= 1'b0;
                end else begin
                    response_index <= response_index + 8'd1;
                    response_address <= response_address + 32'd8;
                    response_delay <= 1;
                end
            end

            if (command_pop) begin
                response_active <= 1'b1;
                response_address <= command_address[command_read];
                response_length <= command_length[command_read];
                response_index <= 8'd0;
                response_delay <= 2;
                command_read <= command_read + 5'd1;
            end else if (response_active && !fb_axi_rvalid) begin
                if (response_delay != 0) begin
                    response_delay <= response_delay - 1;
                end else begin
                    fb_axi_rid <= {AXI_ID_WIDTH{1'b0}};
                    fb_axi_rdata <= read64(response_address);
                    fb_axi_rresp <= 2'b00;
                    fb_axi_rlast <= response_index == response_length;
                    fb_axi_rvalid <= 1'b1;
                end
            end
        end
    end

    always @(posedge build_clk) begin
        if (!build_reset) begin
            if (render_axi_arvalid && render_axi_arready &&
                render_axi_arlen > 8'd15)
                $fatal(1, "renderer read exceeds AXI3 burst limit");
            if (render_axi_awvalid && render_axi_awready &&
                render_axi_awlen > 8'd15)
                $fatal(1, "renderer write exceeds AXI3 burst limit");
        end
    end

    task automatic send_aw(input [31:0] address);
        begin
            @(negedge build_clk);
            s_axi_awaddr = address;
            s_axi_awvalid = 1'b1;
            while (!s_axi_awready)
                @(negedge build_clk);
            @(posedge build_clk);
            @(negedge build_clk);
            s_axi_awvalid = 1'b0;
        end
    endtask

    task automatic send_w(input [31:0] data);
        begin
            @(negedge build_clk);
            s_axi_wdata = data;
            s_axi_wstrb = 4'hf;
            s_axi_wvalid = 1'b1;
            while (!s_axi_wready)
                @(negedge build_clk);
            @(posedge build_clk);
            @(negedge build_clk);
            s_axi_wvalid = 1'b0;
        end
    endtask

    task automatic axi_write(input [31:0] address, input [31:0] data);
        integer cycles;
        begin
            send_aw(address);
            send_w(data);
            cycles = 0;
            while (!s_axi_bvalid) begin
                @(posedge build_clk);
                #1;
                cycles = cycles + 1;
                if (cycles > 20000)
                    $fatal(1, "integrated AXI-Lite response timed out");
            end
            if (s_axi_bresp != 2'b00)
                $fatal(1, "integrated AXI-Lite write failed addr=%08x resp=%b",
                       address, s_axi_bresp);
            @(negedge build_clk);
            s_axi_bready = 1'b1;
            @(posedge build_clk);
            @(negedge build_clk);
            s_axi_bready = 1'b0;
        end
    endtask

    task automatic axi_read(
        input [31:0] address,
        output [31:0] data
    );
        integer cycles;
        begin
            @(negedge build_clk);
            s_axi_araddr = address;
            s_axi_arvalid = 1'b1;
            while (!s_axi_arready)
                @(negedge build_clk);
            @(posedge build_clk);
            @(negedge build_clk);
            s_axi_arvalid = 1'b0;
            cycles = 0;
            while (!s_axi_rvalid) begin
                @(posedge build_clk);
                #1;
                cycles = cycles + 1;
                if (cycles > 20000)
                    $fatal(1, "integrated AXI-Lite read timed out");
            end
            if (s_axi_rresp != 2'b00)
                $fatal(1, "integrated AXI-Lite read failed addr=%08x resp=%b",
                       address, s_axi_rresp);
            data = s_axi_rdata;
            @(negedge build_clk);
            s_axi_rready = 1'b1;
            @(posedge build_clk);
            @(negedge build_clk);
            s_axi_rready = 1'b0;
        end
    endtask

    function automatic [31:0] copper_ins0(
        input [2:0] opcode,
        input [15:0] argument
    );
        copper_ins0 = {opcode, 13'd0, argument};
    endfunction

    function automatic [31:0] copper_beam0(
        input [2:0] opcode,
        input [9:0] y
    );
        copper_beam0 = {opcode, 19'd0, y};
    endfunction

    task automatic write_copper_instruction(
        input [11:0] index,
        input [31:0] word0,
        input [31:0] word1
    );
        begin
            axi_write(32'h00008000 + index * 8, word0);
            axi_write(32'h00008004 + index * 8, word1);
        end
    endtask

    task automatic prepare_copper_line_effect;
        integer cycles;
        reg [31:0] status;
        begin
            write_copper_instruction(12'd0,
                copper_beam0(COPPER_OP_WAIT, 10'd1), 32'd0);
            write_copper_instruction(12'd1,
                copper_ins0(COPPER_OP_MOVE, 16'h0058),
                {16'd0, source_rgb565(32, 2)});
            write_copper_instruction(12'd2,
                copper_ins0(COPPER_OP_MOVE, 16'h0054), 32'h00000020);
            write_copper_instruction(12'd3,
                copper_beam0(COPPER_OP_WAIT, 10'd2), 32'd32);
            write_copper_instruction(12'd4,
                copper_ins0(COPPER_OP_MOVE, 16'h0018), 32'h00112233);
            write_copper_instruction(12'd5,
                copper_ins0(COPPER_OP_MOVE, 16'h0180), 32'd0);
            write_copper_instruction(12'd6,
                copper_ins0(COPPER_OP_MOVE, 16'h0044),
                FRAMEBUFFER_PITCH);
            write_copper_instruction(12'd7,
                copper_ins0(COPPER_OP_DISPATCH, 16'd3), 32'd0);
            write_copper_instruction(12'd8,
                copper_ins0(COPPER_OP_IRQ, 16'hbeef), 32'd0);
            write_copper_instruction(12'd9,
                copper_ins0(COPPER_OP_END, 16'd0), 32'd0);
            axi_write(32'h00004010, 32'd10 << 16);
            axi_write(32'h00004014, 32'd1);
            cycles = 0;
            status = 32'd0;
            while (!status[4] && cycles < 10000) begin
                axi_read(32'h0000400c, status);
                cycles = cycles + 1;
            end
            if (!status[4])
                $fatal(1, "integrated copper validation timed out status=%08x state=%0d index=%0d remaining=%0d w0=%08x w1=%08x",
                       status,
                       dut.copper_control_i.copper_i.validate_state,
                       dut.copper_control_i.copper_i.validate_index_q,
                       dut.copper_control_i.copper_i.validate_remaining_q,
                       dut.copper_control_i.copper_i.validate_w0_q,
                       dut.copper_control_i.copper_i.validate_w1_q);
            axi_write(32'h00004008, 32'd3);
        end
    endtask

    task automatic prepare_copper_dispatch_command;
        integer word_index;
        reg [31:0] source_address;
        reg [31:0] destination_address;
        reg [31:0] dispatch_entry;
        begin
            source_address = ARENA_BASE + RENDER_SUBMISSION_OFFSET;
            destination_address = source_address +
                `ASTRA_RENDER_COMMAND_BYTES;
            for (word_index = 0; word_index < 16;
                 word_index = word_index + 1)
                render_write_be32(destination_address + word_index * 4,
                    render_read_be32(source_address + word_index * 4));
            render_write_be32(destination_address + 32'd8, 32'd2);
            // ID 3 publishes the second command slot. The table is immutable
            // while copper is enabled.
            axi_write(32'h00004030, 32'd3);
            axi_write(32'h00004034, 32'h80000002);
            axi_read(32'h00004034, dispatch_entry);
            if (dispatch_entry != 32'h80000002)
                $fatal(1, "copper dispatch table readback failed %08x",
                       dispatch_entry);
        end
    endtask

    task automatic render_write_be32(
        input [31:0] address,
        input [31:0] value
    );
        begin
            render_memory_i.write_byte(address, value[31:24]);
            render_memory_i.write_byte(address + 32'd1, value[23:16]);
            render_memory_i.write_byte(address + 32'd2, value[15:8]);
            render_memory_i.write_byte(address + 32'd3, value[7:0]);
        end
    endtask

    function automatic [31:0] render_read_be32(input [31:0] address);
        begin
            render_read_be32 = {
                render_memory_i.read_byte(address),
                render_memory_i.read_byte(address + 32'd1),
                render_memory_i.read_byte(address + 32'd2),
                render_memory_i.read_byte(address + 32'd3)
            };
        end
    endfunction

    task automatic prepare_render_fill;
        integer word_index;
        reg [31:0] command_base;
        reg [31:0] descriptor_base;
        begin
            command_base = ARENA_BASE + RENDER_SUBMISSION_OFFSET;
            descriptor_base = ARENA_BASE + RENDER_DESCRIPTOR_OFFSET;
            for (word_index = 0; word_index < 16;
                 word_index = word_index + 1)
                render_write_be32(command_base + word_index * 4, 32'd0);
            render_write_be32(command_base,
                (`ASTRA_RENDER_ABI_VERSION << 16) |
                `ASTRA_RENDER_COMMAND_BYTES);
            render_write_be32(command_base + 32'd4,
                `ASTRA_RENDER_OP_FILL << 16);
            render_write_be32(command_base + 32'd8, 32'd1);
            render_write_be32(command_base + 32'd12, RENDER_GENERATION);
            render_write_be32(command_base + 32'd16, 32'd1000);
            render_write_be32(command_base + 32'd24, 32'd0);
            render_write_be32(command_base + 32'd28,
                {16'd16, 16'd16});
            render_write_be32(command_base + 32'd32,
                RENDER_DESCRIPTOR_OFFSET);
            render_write_be32(command_base + 32'd48,
                {16'd2, 16'd3});
            render_write_be32(command_base + 32'd56,
                {16'd4, 16'd3});
            render_write_be32(command_base + 32'd60, 32'h0000005a);

            render_write_be32(descriptor_base,
                (`ASTRA_RENDER_ABI_VERSION << 16) |
                `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES);
            render_write_be32(descriptor_base + 32'd4,
                RENDER_GENERATION);
            render_write_be32(descriptor_base + 32'd8,
                RENDER_DATA_OFFSET);
            render_write_be32(descriptor_base + 32'd12, 32'd256);
            render_write_be32(descriptor_base + 32'd16, 32'd16);
            render_write_be32(descriptor_base + 32'd20,
                {16'd16, 16'd16});
            render_write_be32(descriptor_base + 32'd24,
                {8'd0, 8'd2, 16'd0});
            render_write_be32(descriptor_base + 32'd28, 32'd0);
        end
    endtask

    task automatic test_render_fill;
        integer cycles;
        integer row;
        integer column;
        reg [31:0] value;
        begin
            prepare_render_fill();
            axi_write(32'h00000204, RENDER_SUBMISSION_OFFSET);
            axi_write(32'h00000210, RENDER_COMPLETION_OFFSET);
            axi_write(32'h0000021c, RENDER_GENERATION);
            axi_write(32'h00000200, 32'h00000002);
            axi_write(32'h00000200, 32'h00000001);
            axi_write(32'h00000208, 32'h00000001);

            cycles = 0;
            value = 32'd0;
            while (value != 32'd1 && cycles < 200000) begin
                axi_read(32'h00000214, value);
                cycles = cycles + 1;
            end
            if (value != 32'd1)
                $fatal(1, "integrated renderer completion timed out");
            if (render_read_be32(ARENA_BASE + RENDER_COMPLETION_OFFSET) !==
                ((`ASTRA_RENDER_ABI_VERSION << 16) |
                 `ASTRA_RENDER_COMPLETION_BYTES))
                $fatal(1, "integrated renderer completion header invalid");
            if (render_read_be32(ARENA_BASE + RENDER_COMPLETION_OFFSET + 4) !==
                {16'd1, 16'd0})
                $fatal(1, "integrated renderer completion status invalid");
            if (render_read_be32(ARENA_BASE + RENDER_COMPLETION_OFFSET + 12) !==
                32'd12)
                $fatal(1, "integrated renderer pixel count invalid");
            for (row = 0; row < 16; row = row + 1)
                for (column = 0; column < 16; column = column + 1)
                    if (render_memory_i.read_byte(
                            ARENA_BASE + RENDER_DATA_OFFSET + row * 16 +
                            column) !==
                        ((row >= 3 && row < 6 && column >= 2 && column < 6) ?
                         8'h5a : 8'ha5))
                        $fatal(1,
                            "integrated renderer byte mismatch x=%0d y=%0d",
                            column, row);
            axi_read(32'h00000224, value);
            if (value != 32'd1 || !render_interrupt)
                $fatal(1, "integrated renderer fence/interrupt invalid");
            axi_write(32'h00000218, 32'h00000001);
            axi_write(32'h00000244, 32'h00000001);
            if (render_interrupt)
                $fatal(1, "integrated renderer interrupt did not clear");
            $display("integrated render command/fill pass reads=%0d writes=%0d",
                     render_read_transactions, render_write_transactions);
        end
    endtask

    task automatic write_sprite_descriptor_word(
        input [2:0] word_index,
        input [31:0] value
    );
        begin
            axi_write(32'h00000184, {21'd0, word_index, 8'd0});
            axi_write(32'h00000188, value);
        end
    endtask

    task automatic write_sprite_palette(
        input [3:0] bank,
        input [7:0] index,
        input [31:0] value
    );
        begin
            axi_write(32'h0000018c, {20'd0, bank, index});
            axi_write(32'h00000190, value);
        end
    endtask

    task automatic wait_generation_and_lines(
        input [31:0] generation,
        input [31:0] minimum_lines
    );
        integer cycles;
        begin
            cycles = 0;
            while ((active_generation != generation ||
                    lines_built < minimum_lines) &&
                   cycles < FRAME_TIMEOUT_CYCLES) begin
                @(posedge build_clk);
                cycles = cycles + 1;
            end
            if (active_generation != generation ||
                lines_built < minimum_lines)
                $fatal(1,
                    "generation wait timed out expected=%0d actual=%0d lines=%0d",
                    generation, active_generation, lines_built);
        end
    endtask

    reg check_frame = 1'b0;
    reg [31:0] underruns_before_update = 32'd0;
    integer checked_pixels = 0;
    reg [15:0] expected565;
    reg [23:0] expected_rgb;
    always @(posedge pixel_clk) begin
        if (!pixel_reset) begin
            if (pixel_x == TOTAL_WIDTH - 1 &&
                pixel_y == TOTAL_HEIGHT - 1 &&
                active_generation == 32'd2 && lines_built >= 32'd8 &&
                dut.copper_structural_state_i.candidates_accepted != 32'd0 &&
                dut.render_commands_completed >= 32'd2)
                check_frame <= 1'b1;

            if (check_frame && pixel_x < OUTPUT_WIDTH &&
                pixel_y < OUTPUT_HEIGHT) begin
                expected565 = source_rgb565(pixel_x, pixel_y);
                expected_rgb = expand_rgb565(expected565);
                if (pixel_x == 32 && pixel_y == 2)
                    expected_rgb = 24'h112233;
                if (pixel_x >= 24 && pixel_x < 32 &&
                    pixel_y >= 1 && pixel_y < 3)
                    expected_rgb = blend_straight_over_opaque(
                        expected_rgb, SPRITE_ARGB);
                if (!pixel_output_valid)
                    $fatal(1, "missing pixel x=%0d y=%0d",
                           pixel_x, pixel_y);
                if (pixel_output_rgb !== expected_rgb)
                    $fatal(1,
                        "pixel mismatch x=%0d y=%0d got=%06x expected=%06x underruns=%0d read_slot=%0d valid=%b tags=%0d,%0d,%0d,%0d copper=%0d/%0d/%0d sprite_build=%0d visual2=%0d",
                        pixel_x, pixel_y, pixel_output_rgb, expected_rgb,
                        pixel_underruns, dut.pixel_read_slot,
                        dut.pixel_slot_valid, dut.pixel_slot_tag0,
                        dut.pixel_slot_tag1, dut.pixel_slot_tag2,
                        dut.pixel_slot_tag3, dut.copper_enabled,
                        dut.copper_running, dut.copper_waiting,
                        dut.sprite_enable_build,
                        dut.line_visual_slot2[54]);
                checked_pixels <= checked_pixels + 1;
                if (pixel_x == OUTPUT_WIDTH - 1 &&
                    pixel_y == OUTPUT_HEIGHT - 1) begin
                    if (lines_failed != 0 || scheduler_overruns != 0 ||
                        commit_errors != 0 ||
                        pixel_underruns != underruns_before_update)
                        $fatal(1,
                            "pipeline diagnostics failed=%0d overruns=%0d commit_errors=%0d underruns=%0d baseline=%0d deferrals=%0d",
                            lines_failed, scheduler_overruns, commit_errors,
                            pixel_underruns, underruns_before_update,
                            commit_deferrals);
                    if (!dut.copper_interrupt ||
                        dut.copper_irq_sources != 16'hbeef)
                        $fatal(1,
                            "integrated copper IRQ missing pending=%0d sources=%04x",
                            dut.copper_interrupt, dut.copper_irq_sources);
                    if (dut.copper_faulted)
                        $fatal(1, "integrated copper faulted after replay");
                    if (dut.copper_structural_state_i.candidates_accepted ==
                            32'd0 ||
                        dut.framebuffer_pitch_build != FRAMEBUFFER_PITCH)
                        $fatal(1,
                            "integrated next-vblank copper state missing accepted=%0d pitch=%0d",
                            dut.copper_structural_state_i.candidates_accepted,
                            dut.framebuffer_pitch_build);
                    if (dut.render_retired_fence != 32'd2 ||
                        dut.render_commands_completed < 32'd2)
                        $fatal(1,
                            "integrated copper dispatch missing fence=%0d completed=%0d",
                            dut.render_retired_fence,
                            dut.render_commands_completed);
                    $display(
                        "ASTRA GRAPHICS PIPELINE PASS pixels=%0d lines=%0d underruns=%0d deferrals=%0d",
                        checked_pixels + 1, lines_built,
                        pixel_underruns, commit_deferrals);
                    if (OUTPUT_WIDTH == 1280)
                        $display(
                            "ASTRA SCREEN OFFSET PASS pixels=%0d width=%0d height=%0d",
                            checked_pixels + 1, OUTPUT_WIDTH, OUTPUT_HEIGHT);
                    $finish;
                end
            end
        end
    end

    integer x;
    integer y;
    integer address;
    reg [15:0] source;
    initial begin
        if (OUTPUT_WIDTH >= 1280 &&
            source_rgb565(0, 0) == source_rgb565(640, 0))
            $fatal(1, "screen-offset oracle repeats at 640 pixels");
        for (address = 0; address < FRAME_MEMORY_BYTES;
             address = address + 1)
            memory[address] = 8'd0;
        render_memory_i.clear_memory(8'ha5);
        for (y = 0; y < OUTPUT_HEIGHT; y = y + 1) begin
            for (x = 0; x < OUTPUT_WIDTH; x = x + 1) begin
                source = source_rgb565(x, y);
                address = y * FRAMEBUFFER_PITCH + x * 2;
                memory[address] = source[15:8];
                memory[address + 1] = source[7:0];
            end
        end
        for (y = 0; y < 4; y = y + 1)
            for (x = 0; x < 8; x = x + 1)
                memory[SPRITE_BASE - ARENA_BASE + y * 64 + x] = 8'd1;

        repeat (8) @(posedge build_clk);
        @(negedge build_clk);
        build_reset = 1'b0;
        repeat (4) @(posedge pixel_clk);
        @(negedge pixel_clk);
        pixel_reset = 1'b0;

        test_render_fill();
        prepare_copper_dispatch_command();

        axi_write(32'h00000040, FRAMEBUFFER_BASE);
        axi_write(32'h00000044, FRAMEBUFFER_PITCH);
        axi_write(32'h00000048,
                  (OUTPUT_HEIGHT << 16) | OUTPUT_WIDTH);
        axi_write(32'h0000004c, 32'd0);
        axi_write(32'h00000050, 32'd0);
        axi_write(32'h00000054, 32'h00000003);
        axi_write(32'h00000018, 32'h00010203);
        // Keep disabled structural clients valid too: copper may legally
        // enable them later without bypassing address validation.
        axi_write(32'h00000080, 32'h00004000);
        axi_write(32'h00000084, 32'h00005000);
        axi_write(32'h00000090, 32'h00002022);
        axi_write(32'h00000094, 32'd1);
        axi_write(32'h000000c0, 32'h00006000);
        axi_write(32'h000000c4, 32'h00007000);
        axi_write(32'h000000d0, 32'h00002022);
        axi_write(32'h000000d4, 32'd1);
        write_sprite_palette(4'd1, 8'd1, SPRITE_ARGB);
        write_sprite_descriptor_word(3'd0, 32'h00010113);
        write_sprite_descriptor_word(3'd1, 32'h00010008);
        write_sprite_descriptor_word(3'd2, 32'h00ff0408);
        write_sprite_descriptor_word(3'd3, 32'h00040008);
        write_sprite_descriptor_word(3'd4, SPRITE_BASE);
        write_sprite_descriptor_word(3'd5, 32'h00000040);
        write_sprite_descriptor_word(3'd6, 32'd0);
        write_sprite_descriptor_word(3'd7, 32'd0);
        axi_write(32'h00000180, 32'd1);
        axi_write(32'h0000000c, 32'h00000001);
        axi_write(32'h00000010, 32'h00000001);

        wait_generation_and_lines(32'd1, 32'd8);
        underruns_before_update = pixel_underruns;
        prepare_copper_line_effect();
        // Submit while framebuffer and sprite scanout are active. The frame
        // boundary must quiesce and drain old-scene prefetch before promotion;
        // requiring commit_safe on the boundary cycle deadlocks this update.
        write_sprite_descriptor_word(3'd1, 32'h00010018);
        axi_write(32'h00000010, 32'h00000001);
        wait_generation_and_lines(32'd2, 32'd8);

        repeat (FRAME_TIMEOUT_CYCLES) @(posedge pixel_clk);
        $fatal(1,
            "integrated pipeline timed out gen=%0d built=%0d failed=%0d accepted=%0d rejected=%0d deferred=%0d validating=%0d dirty=%0d pending=%0d copper_run=%0d fault=%0d render_sub=%0d complete=%0d failed=%0d prod=%0d cons=%0d dispatch=%0d/%0d/%0d id=%0d",
            active_generation, lines_built, lines_failed,
            dut.copper_structural_state_i.candidates_accepted,
            dut.copper_structural_state_i.candidates_rejected,
            dut.copper_structural_state_i.candidates_deferred,
            dut.copper_structural_state_i.validating,
            dut.copper_structural_state_i.candidate_dirty,
            dut.copper_structural_state_i.pending_valid,
            dut.copper_running, dut.copper_faulted,
            dut.render_commands_submitted, dut.render_commands_completed,
            dut.render_commands_failed, dut.render_submission_producer,
            dut.render_submission_consumer, dut.copper_dispatch_valid,
            dut.copper_dispatch_allowed, dut.copper_dispatch_ready,
            dut.copper_dispatch_id);
    end
endmodule

`default_nettype wire
