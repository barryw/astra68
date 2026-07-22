// Descriptor, clipping, priority, collision, budget, and line-buffer tests.
`timescale 1ns/1ps

module tb_vega_sprite_builder;
    localparam [24:0] S0_BASE = 25'h0001000;
    localparam [24:0] S1_BASE = 25'h0002000;
    localparam [24:0] S2_BASE = 25'h0003000;
    localparam [24:0] S3_BASE = 25'h0004000;

    reg cpu_clk = 1'b0;
    reg mem_clk = 1'b0;
    reg pixel_clk = 1'b0;
    always #40 cpu_clk = ~cpu_clk;
    always #8.333 mem_clk = ~mem_clk;
    always #18.5 pixel_clk = ~pixel_clk;
    reg cpu_rst = 1'b1;
    reg mem_rst = 1'b1;

    reg cpu_table_write = 1'b0;
    reg [7:0] cpu_word_addr = 8'd0;
    reg [3:0] cpu_be = 4'd0;
    reg [31:0] cpu_wdata = 32'd0;
    wire [31:0] cpu_rdata;

    reg start = 1'b0;
    reg build_bank = 1'b0;
    reg [9:0] line_y = 10'd0;
    reg [9:0] line_width = 10'd40;
    reg enable = 1'b1;
    reg [15:0] pixel_budget = 16'd64;
    wire busy;
    wire done;
    wire config_error;
    wire overflow;
    wire [31:0] collision_bitmap;
    wire collision_event;

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
    wire [17:0] behind_pair;
    wire [17:0] front_pair;

    vega_sprite_builder dut (
        .cpu_clk(cpu_clk), .cpu_rst(cpu_rst),
        .cpu_table_write(cpu_table_write),
        .cpu_word_addr(cpu_word_addr), .cpu_be(cpu_be),
        .cpu_wdata(cpu_wdata), .cpu_rdata(cpu_rdata),
        .cpu_shadow_bank(2'd0), .cpu_write_bank(2'd0),
        .scene_copy_read(1'b0), .scene_copy_write(1'b0),
        .scene_copy_source_bank(2'd0), .scene_copy_dest_bank(2'd0),
        .scene_copy_index(8'd0),
        .mem_clk(mem_clk), .mem_rst(mem_rst), .start(start),
        .build_bank(build_bank), .line_y(line_y), .line_width(line_width),
        .enable(enable), .pixel_budget(pixel_budget),
        .busy(busy), .done(done), .config_error(config_error),
        .overflow(overflow), .collision_bitmap(collision_bitmap),
        .collision_event(collision_event),
        .mem_lock(mem_lock), .mem_valid(mem_valid), .mem_ready(mem_ready),
        .mem_write(), .mem_addr(mem_addr), .mem_be(), .mem_wdata(),
        .mem_rsp_valid(mem_rsp_valid), .mem_rdata(mem_rdata),
        .pixel_clk(pixel_clk), .display_bank(display_bank),
        .pixel_x(pixel_x), .behind_pair(behind_pair), .front_pair(front_pair)
    );

    function automatic [31:0] memory_value(input [24:0] address);
        begin
            case (address)
                S0_BASE: memory_value = 32'h11111111;
                S0_BASE + 25'd4: memory_value = 32'h11111111;
                S0_BASE + 25'd8: memory_value = 32'h11111111;
                S0_BASE + 25'd12: memory_value = 32'h11111111;
                S1_BASE: memory_value = 32'h22222222;
                S2_BASE: memory_value = 32'h33330000;
                S3_BASE: memory_value = 32'h12345678;
                default: memory_value = 32'd0;
            endcase
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
                $fatal(1, "sprite request accepted without lock");
            pending_addr <= mem_addr;
            pending <= 1'b1;
            request_count <= request_count + 1;
        end
    end

    task automatic write_word(
        input integer sprite,
        input integer word_offset,
        input [31:0] value
    );
        begin
            @(negedge cpu_clk);
            cpu_word_addr = sprite * 8 + word_offset;
            cpu_be = 4'b1100;
            cpu_wdata = value;
            cpu_table_write = 1'b1;
            @(negedge cpu_clk);
            cpu_be = 4'b0011;
            @(negedge cpu_clk);
            cpu_table_write = 1'b0;
            cpu_be = 4'd0;
            cpu_wdata = 32'd0;
        end
    endtask

    task automatic set_sprite(
        input integer sprite,
        input [31:0] ctrl,
        input integer x,
        input integer y,
        input integer width,
        input integer height,
        input [24:0] base,
        input integer pitch
    );
        begin
            write_word(sprite, 0, ctrl);
            write_word(sprite, 1, {y[15:0], x[15:0]});
            write_word(sprite, 2, {height[15:0], width[15:0]});
            write_word(sprite, 3, {7'd0, base});
            write_word(sprite, 4, pitch);
        end
    endtask

    task automatic start_line(input bank, output integer cycles);
        begin
            @(negedge mem_clk);
            build_bank = bank;
            start = 1'b1;
            @(negedge mem_clk);
            start = 1'b0;
            cycles = 0;
            while (!done) begin
                @(posedge mem_clk);
                cycles = cycles + 1;
                if (cycles > 4000)
                    $fatal(1, "sprite timeout state=%0d", dut.state);
            end
        end
    endtask

    function automatic [8:0] select_pixel(
        input [17:0] pair,
        input integer lane
    );
        select_pixel = lane[0] ? pair[8:0] : pair[17:9];
    endfunction

    task automatic check_pixel(
        input integer x,
        input [8:0] expected_behind,
        input [8:0] expected_front
    );
        reg [8:0] actual_behind;
        reg [8:0] actual_front;
        begin
            pixel_x = x[9:0];
            repeat (2) @(posedge pixel_clk);
            #1;
            actual_behind = select_pixel(behind_pair, x & 1);
            actual_front = select_pixel(front_pair, x & 1);
            if (actual_behind !== expected_behind)
                $fatal(1, "behind x=%0d got=%03x expected=%03x pair=%05x raw=%05x",
                       x, actual_behind, expected_behind, behind_pair,
                       dut.behind_line[{display_bank, x[9:3]}]);
            if (actual_front !== expected_front)
                $fatal(1, "front x=%0d got=%03x expected=%03x",
                       x, actual_front, expected_front);
        end
    endtask

    integer cycles;
    integer sprite_index;
    integer request_before;
    integer collision_events = 0;
    always @(posedge cpu_clk)
        if (collision_event)
            collision_events <= collision_events + 1;
    initial begin
        repeat (8) @(posedge mem_clk);
        cpu_rst = 1'b0;
        mem_rst = 1'b0;

        // Sprite 0: behind FB, bank 1, priority 1, collision enabled.
        set_sprite(0, 32'h00001133, 4, 2, 12, 2, S0_BASE, 6);
        // Sprite 1: front, bank 2, priority 2, collision enabled.
        set_sprite(1, 32'h00002223, 8, 2, 8, 1, S1_BASE, 4);
        // Sprite 2: same priority as sprite 1. Lower index 1 must win.
        set_sprite(2, 32'h00003203, 10, 2, 4, 1, S2_BASE, 2);
        // Sprite 3: highest priority, clipped at x=0 and flipped horizontally.
        set_sprite(3, 32'h00004307, -2, 2, 8, 1, S3_BASE, 4);

        cpu_word_addr = 8'd8;
        repeat (2) @(posedge cpu_clk);
        #1;
        if (cpu_rdata !== 32'h00002223)
            $fatal(1, "descriptor readback mismatch %08x", cpu_rdata);

        line_y = 10'd2;
        pixel_budget = 16'd64;
        start_line(1'b1, cycles);
        if (busy || config_error)
            $fatal(1, "sprite status busy=%b config_error=%b",
                   busy, config_error);
        if (cycles > 2200)
            $fatal(1, "sprite scanline cycle budget exceeded: %0d", cycles);

        display_bank = 1'b1;
        check_pixel(0, 9'd0, {1'b1, 8'h46});
        check_pixel(5, {1'b1, 8'h11}, {1'b1, 8'h41});
        check_pixel(8, {1'b1, 8'h11}, {1'b1, 8'h22});
        check_pixel(10, {1'b1, 8'h11}, {1'b1, 8'h22});
        check_pixel(15, {1'b1, 8'h11}, {1'b1, 8'h22});
        check_pixel(16, 9'd0, 9'd0);
        repeat (8) @(posedge cpu_clk);
        if ((collision_bitmap & 32'h00000003) != 32'h00000003)
            $fatal(1, "collision bitmap mismatch %08x", collision_bitmap);
        if (collision_events != 1)
            $fatal(1, "collision event count mismatch %0d", collision_events);

        // Highest priority clipped sprite consumes six pixels; every lower
        // priority sprite is dropped when only eight pixels are available.
        pixel_budget = 16'd8;
        start_line(1'b0, cycles);
        display_bank = 1'b0;
        check_pixel(0, 9'd0, {1'b1, 8'h46});
        check_pixel(8, 9'd0, 9'd0);
        repeat (8) @(posedge cpu_clk);
        if (!overflow)
            $fatal(1, "sprite budget overflow was not reported");

        // Overlapping sprites must survive every two- and eight-pixel storage
        // boundary. This is the colorkey reveal case used by the compositor.
        set_sprite(0, 32'h00001333, 16, 0, 16, 1, S0_BASE, 8);
        set_sprite(1, 32'h00002233, 24, 0, 16, 1, S0_BASE, 8);
        write_word(2, 0, 32'd0);
        write_word(3, 0, 32'd0);
        line_y = 10'd0;
        pixel_budget = 16'd64;
        start_line(1'b1, cycles);
        display_bank = 1'b1;
        check_pixel(24, {1'b1, 8'h11}, 9'd0);
        check_pixel(30, {1'b1, 8'h11}, 9'd0);
        check_pixel(31, {1'b1, 8'h11}, 9'd0);
        check_pixel(32, {1'b1, 8'h21}, 9'd0);

        // Invalid pitch is rejected without issuing out-of-contract traffic.
        write_word(0, 4, 32'd1);
        write_word(1, 0, 32'd0);
        write_word(2, 0, 32'd0);
        write_word(3, 0, 32'd0);
        pixel_budget = 16'd64;
        start_line(1'b1, cycles);
        if (!config_error)
            $fatal(1, "invalid sprite pitch did not report config error");

        // High address and pitch bits are invalid, not silently truncated.
        write_word(0, 4, 32'd6);
        write_word(0, 3, 32'h02001000);
        start_line(1'b0, cycles);
        if (!config_error)
            $fatal(1, "high sprite base did not report config error");
        write_word(0, 3, {7'd0, S0_BASE});
        write_word(0, 4, 32'h00010006);
        start_line(1'b1, cycles);
        if (!config_error)
            $fatal(1, "high sprite pitch did not report config error");

        // The final physical SDRAM byte is legal. Extending that same row by
        // one byte must be rejected before the builder issues a request.
        set_sprite(0, 32'h00001003, 0, 0, 1, 1,
                   25'h1ffffff, 1);
        request_before = request_count;
        start_line(1'b0, cycles);
        if (config_error || request_count - request_before != 1)
            $fatal(1, "last-byte sprite failed error=%b requests=%0d",
                   config_error, request_count - request_before);
        set_sprite(0, 32'h00001003, 0, 0, 3, 1,
                   25'h1ffffff, 2);
        request_before = request_count;
        start_line(1'b1, cycles);
        if (!config_error || request_count != request_before)
            $fatal(1, "out-of-range sprite was not rejected requests=%0d",
                   request_count - request_before);

        // The descriptor aperture above sprite 15 is reserved and reads zero.
        // Saturate the documented scanline budget: 16 sprites x 64 pixels.
        // The builder must still publish before the next 720-pixel line.
        set_sprite(16, 32'h0000f003, 0, 0, 64, 1, S0_BASE, 32);
        cpu_word_addr = 8'd128;
        repeat (2) @(posedge cpu_clk);
        #1;
        if (cpu_rdata !== 32'd0)
            $fatal(1, "reserved sprite descriptor read %08x", cpu_rdata);
        for (sprite_index = 0; sprite_index < 16;
             sprite_index = sprite_index + 1)
            set_sprite(sprite_index,
                       32'h00001003 | ((sprite_index & 15) << 8),
                       0, 0, 64, 1, S0_BASE, 32);
        line_y = 10'd0;
        pixel_budget = 16'd1024;
        start_line(1'b0, cycles);
        // Preserve arbitration headroom for simultaneous framebuffer fetches;
        // the whole-system deadline alone is not a sufficient guard against
        // serializing line-buffer composition.
        if (config_error || cycles >= 1400)
            $fatal(1, "saturated sprite line failed error=%b cycles=%0d",
                   config_error, cycles);
        display_bank = 1'b0;
        check_pixel(0, 9'd0, {1'b1, 8'h11});

        // A 1,023-pixel aligned row occupies exactly all 128 pattern words.
        // Shifting the same row by one byte requires 129 and must be rejected
        // before issuing any memory traffic.
        for (sprite_index = 1; sprite_index < 16;
             sprite_index = sprite_index + 1)
            write_word(sprite_index, 0, 32'd0);
        set_sprite(0, 32'h00001003, 0, 0, 1023, 1,
                   S0_BASE, 512);
        line_width = 10'd1023;
        pixel_budget = 16'd1023;
        request_before = request_count;
        start_line(1'b1, cycles);
        if (config_error || request_count - request_before != 128)
            $fatal(1, "128-word row failed error=%b requests=%0d",
                   config_error, request_count - request_before);

        set_sprite(0, 32'h00001003, 0, 0, 1023, 1,
                   S0_BASE + 25'd1, 512);
        request_before = request_count;
        start_line(1'b0, cycles);
        if (!config_error || request_count != request_before)
            $fatal(1, "129-word row was not rejected error=%b requests=%0d",
                   config_error, request_count - request_before);

        $display("VEGA SPRITE PASS requests=%0d cycles=%0d collisions=%0d",
                 request_count, cycles, collision_events);
        $finish;
    end
endmodule
