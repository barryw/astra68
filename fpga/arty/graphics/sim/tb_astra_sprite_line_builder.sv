`timescale 1ns/1ps
`default_nettype none

module tb_astra_sprite_line_builder #(
    parameter integer PERF_MODE = 0
);
    localparam [31:0] ARENA_BASE = 32'h18000000;
    localparam integer OUTPUT_WIDTH =
        PERF_MODE == 1 || PERF_MODE == 5 ? 1280 :
        PERF_MODE == 6 || PERF_MODE == 7 ? 128 : 64;
    localparam integer OUTPUT_HEIGHT = PERF_MODE == 7 ? 128 : 32;
    localparam integer BUILD_DEADLINE = PERF_MODE == 4 ? 300 : 4300;
    localparam integer PERF_CYCLE_BUDGET = 2000;
    localparam integer COLLISION_CYCLE_BUDGET = 2000;
    localparam [31:0] DIMENSION_BASE = ARENA_BASE + 32'h00004000;

    reg build_clk = 1'b0;
    always #2.5 build_clk = ~build_clk;
    reg pixel_clk = 1'b0;
    always #6.734 pixel_clk = ~pixel_clk;
    reg reset = 1'b1;
    reg descriptor_write_enable = 1'b0;
    reg [5:0] descriptor_write_index = 6'd0;
    reg [2:0] descriptor_write_word = 3'd0;
    reg [31:0] descriptor_write_data = 32'd0;
    reg palette_write_enable = 1'b0;
    reg [3:0] palette_write_bank = 4'd0;
    reg [7:0] palette_write_index = 8'd0;
    reg [31:0] palette_write_argb = 32'd0;
    wire scene_write_ready;
    reg validate_start = 1'b0;
    wire validate_busy;
    wire validate_done;
    wire validate_valid;
    reg accept_pending = 1'b0;
    wire pending_ready;
    wire pending_valid;
    reg activate_start = 1'b0;
    wire activate_busy;
    wire activate_done;

    wire order_read_enable;
    wire [5:0] order_read_position;
    wire [5:0] order_read_index;
    wire descriptor_read_enable;
    wire [5:0] descriptor_read_index;
    wire [31:0] descriptor_word0;
    wire [31:0] descriptor_word1;
    wire [31:0] descriptor_word2;
    wire [31:0] descriptor_word3;
    wire [31:0] descriptor_word4;
    wire [31:0] descriptor_word5;
    wire [31:0] descriptor_word6;
wire [31:0] descriptor_scale_step_x;
    wire [63:0] descriptor_collision_compatible;
    wire [3:0] palette0_read_bank;
    wire [7:0] palette0_read_index;
    wire [31:0] palette0_read_argb;
    wire [3:0] palette1_read_bank;
    wire [7:0] palette1_read_index;
    wire [31:0] palette1_read_argb;
    wire [3:0] palette2_read_bank;
    wire [7:0] palette2_read_index;
    wire [31:0] palette2_read_argb;
    wire [3:0] palette3_read_bank;
    wire [7:0] palette3_read_index;
    wire [31:0] palette3_read_argb;

    astra_sprite_scene_store #(
        .ARENA_BASE(ARENA_BASE),
        .ARENA_LIMIT(ARENA_BASE + 32'h00100000)
    ) scene_i (
        .clk(build_clk), .reset(reset),
        .descriptor_write_enable(descriptor_write_enable),
        .descriptor_write_index(descriptor_write_index),
        .descriptor_write_word(descriptor_write_word),
        .descriptor_write_data(descriptor_write_data),
        .palette_write_enable(palette_write_enable),
        .palette_write_bank(palette_write_bank),
        .palette_write_index(palette_write_index),
        .palette_write_argb(palette_write_argb),
        .write_ready(scene_write_ready),
        .validate_start(validate_start), .validate_busy(validate_busy),
        .validate_done(validate_done), .validate_valid(validate_valid),
        .accept_pending(accept_pending), .pending_ready(pending_ready),
        .pending_valid(pending_valid), .activate_start(activate_start),
        .activate_busy(activate_busy), .activate_done(activate_done),
        .baseline_restore_start(1'b0), .baseline_restore_busy(),
        .baseline_restore_done(), .copper_palette_write_enable(1'b0),
        .copper_palette_write_bank(4'd0),
        .copper_palette_write_index(8'd0),
        .copper_palette_write_argb(32'd0),
        .copper_palette_write_ready(),
        .order_read_enable(order_read_enable),
        .order_read_position(order_read_position),
        .order_read_index(order_read_index),
        .descriptor_read_enable(descriptor_read_enable),
        .descriptor_read_index(descriptor_read_index),
        .descriptor_word0(descriptor_word0),
        .descriptor_word1(descriptor_word1),
        .descriptor_word2(descriptor_word2),
        .descriptor_word3(descriptor_word3),
        .descriptor_word4(descriptor_word4),
        .descriptor_word5(descriptor_word5),
        .descriptor_word6(descriptor_word6),
.descriptor_scale_step_x(descriptor_scale_step_x),
        .descriptor_collision_compatible(descriptor_collision_compatible),
        .palette0_read_bank(palette0_read_bank),
        .palette0_read_index(palette0_read_index),
        .palette0_read_argb(palette0_read_argb),
        .palette1_read_bank(palette1_read_bank),
        .palette1_read_index(palette1_read_index),
        .palette1_read_argb(palette1_read_argb),
        .palette2_read_bank(palette2_read_bank),
        .palette2_read_index(palette2_read_index),
        .palette2_read_argb(palette2_read_argb),
        .palette3_read_bank(palette3_read_bank),
        .palette3_read_index(palette3_read_index),
        .palette3_read_argb(palette3_read_argb)
    );

    reg start = 1'b0;
    reg [1:0] build_slot = 2'd0;
    reg [9:0] line_y = 10'd0;
    wire busy;
    wire done;
    wire line_complete;
    wire [1:0] completed_slot;
    wire [3:0] slot_valid;
    wire fetch_error;
    wire deadline_error;
    wire [31:0] build_cycles;
    wire [31:0] max_build_cycles;
    wire [31:0] axi_error_count;
    wire [31:0] deadline_error_count;
    wire [31:0] read_bytes;
    wire [63:0] overflow_bitmap;
    wire [9:0] overflow_line;
    wire [31:0] overflow_count;
    wire [31:0] pixels_admitted;
    wire [31:0] pixels_dropped;
    reg [5:0] collision_read_row = 6'd0;
    wire [63:0] collision_read_data;
    wire [31:0] collision_frame;
    wire collision_event;
    reg [1:0] pixel_read_slot = 2'd0;
    reg [10:0] pixel_read_x = 11'd0;
    wire [31:0] pixel_front_argb;
    wire [31:0] pixel_behind_argb;

    reg clear_was_active = 1'b0;
    reg clear_completed = 1'b0;
    reg [8:0] completed_clear_quad = 9'd0;

    // The clear sequencer owns clear_quad for the whole build. Copying the
    // finished line must not reuse it and reconnect completion policy to the
    // working-memory clear address.
    always @(posedge build_clk) begin
        if (reset || start) begin
            clear_was_active <= 1'b0;
            clear_completed <= 1'b0;
        end else begin
            if (dut.clear_active_q)
                clear_was_active <= 1'b1;
            if (clear_was_active && !dut.clear_active_q) begin
                completed_clear_quad <= dut.clear_quad_q;
                clear_completed <= 1'b1;
            end
            if (clear_completed && dut.copy_active_q &&
                dut.clear_quad_q != completed_clear_quad) begin
                $display("FAIL copy reused clear_quad clear=%0d copy=%0d",
                         completed_clear_quad, dut.clear_quad_q);
                $fatal(1);
            end
        end
    end

    wire [5:0] arid;
    wire [31:0] araddr;
    wire [7:0] arlen;
    wire [2:0] arsize;
    wire [1:0] arburst;
    wire [3:0] arcache;
    wire [2:0] arprot;
    wire [3:0] arqos;
    wire arvalid;
    wire arready;
    wire [5:0] rid = 6'd0;
    reg [63:0] rdata;
    wire [1:0] rresp = PERF_MODE == 3 ? 2'b10 : 2'b00;
    wire rlast;
    wire rvalid;
    wire rready;

    astra_sprite_line_builder #(
        .OUTPUT_WIDTH(OUTPUT_WIDTH),
        .OUTPUT_HEIGHT(OUTPUT_HEIGHT),
        .PIXEL_BUDGET(PERF_MODE == 2 ? 8 : 2048),
        .MAX_BUILD_CYCLES(BUILD_DEADLINE)
    ) dut (
        .build_clk(build_clk), .build_reset(reset),
        .start(start), .build_slot(build_slot), .line_y(line_y),
        .order_read_enable(order_read_enable),
        .order_read_position(order_read_position),
        .order_read_index(order_read_index),
        .descriptor_read_enable(descriptor_read_enable),
        .descriptor_read_index(descriptor_read_index),
        .descriptor_word0(descriptor_word0),
        .descriptor_word1(descriptor_word1),
        .descriptor_word2(descriptor_word2),
        .descriptor_word3(descriptor_word3),
        .descriptor_word4(descriptor_word4),
        .descriptor_word5(descriptor_word5),
        .descriptor_word6(descriptor_word6),
.descriptor_scale_step_x(descriptor_scale_step_x),
        .descriptor_collision_compatible(descriptor_collision_compatible),
        .palette0_read_bank(palette0_read_bank),
        .palette0_read_index(palette0_read_index),
        .palette0_read_argb(palette0_read_argb),
        .palette1_read_bank(palette1_read_bank),
        .palette1_read_index(palette1_read_index),
        .palette1_read_argb(palette1_read_argb),
        .palette2_read_bank(palette2_read_bank),
        .palette2_read_index(palette2_read_index),
        .palette2_read_argb(palette2_read_argb),
        .palette3_read_bank(palette3_read_bank),
        .palette3_read_index(palette3_read_index),
        .palette3_read_argb(palette3_read_argb),
        .busy(busy), .done(done), .line_complete(line_complete),
        .completed_slot(completed_slot), .slot_valid(slot_valid),
        .fetch_error(fetch_error), .deadline_error(deadline_error),
        .build_cycles(build_cycles),
        .max_build_cycles(max_build_cycles),
        .axi_error_count(axi_error_count),
        .deadline_error_count(deadline_error_count),
        .read_bytes(read_bytes),
        .overflow_bitmap(overflow_bitmap), .overflow_line(overflow_line),
        .overflow_count(overflow_count),
        .pixels_admitted(pixels_admitted),
        .pixels_dropped(pixels_dropped),
        .collision_read_row(collision_read_row),
        .collision_read_data(collision_read_data),
        .collision_frame(collision_frame),
        .collision_event(collision_event),
        .m_axi_arid(arid), .m_axi_araddr(araddr), .m_axi_arlen(arlen),
        .m_axi_arsize(arsize), .m_axi_arburst(arburst),
        .m_axi_arcache(arcache), .m_axi_arprot(arprot),
        .m_axi_arqos(arqos), .m_axi_arvalid(arvalid),
        .m_axi_arready(arready), .m_axi_rid(rid), .m_axi_rdata(rdata),
        .m_axi_rresp(rresp), .m_axi_rlast(rlast),
        .m_axi_rvalid(rvalid), .m_axi_rready(rready),
        .pixel_clk(pixel_clk), .pixel_reset(reset),
        .pixel_read_slot(pixel_read_slot), .pixel_read_x(pixel_read_x),
        .pixel_front_argb(pixel_front_argb),
        .pixel_behind_argb(pixel_behind_argb)
    );

    reg [7:0] memory [0:65535];
    localparam integer RESPONSE_INITIAL_LATENCY =
        PERF_MODE == 1 || PERF_MODE == 5 ? 96 :
        PERF_MODE == 6 ? 16 : 4;
    reg [31:0] request_address [0:15];
    reg [4:0] request_beats [0:15];
    reg [3:0] request_write_ptr = 4'd0;
    reg [3:0] request_read_ptr = 4'd0;
    reg [4:0] request_count = 5'd0;
    reg response_active = 1'b0;
    reg [31:0] response_address = 32'd0;
    reg [4:0] response_beats = 5'd0;
    reg [7:0] response_delay = 8'd0;
    reg [5:0] outstanding_requests = 6'd0;
    reg [5:0] max_outstanding_requests = 6'd0;
    integer byte_lane;
    always @* begin
        rdata = 64'd0;
        for (byte_lane = 0; byte_lane < 8; byte_lane = byte_lane + 1)
            rdata[byte_lane * 8 +: 8] = memory[
                response_address - ARENA_BASE + byte_lane
            ];
    end
    assign arready = request_count != 5'd16;
    assign rvalid = response_active && PERF_MODE != 4;
    assign rlast = response_beats == 5'd1;
    wire request_accept = arvalid && arready;
    wire response_accept = rvalid && rready;
    wire response_complete = response_accept && rlast;
    wire response_start = !response_active && request_count != 5'd0 &&
                          response_delay == 8'd0;
    reg dimension_probe_active = 1'b0;
    reg [31:0] dimension_allocation_end = 32'd0;

    always @(posedge build_clk) begin
        if (reset) begin
            request_write_ptr <= 4'd0;
            request_read_ptr <= 4'd0;
            request_count <= 5'd0;
            response_active <= 1'b0;
            response_address <= 32'd0;
            response_beats <= 5'd0;
            response_delay <= 8'd0;
            outstanding_requests <= 6'd0;
            max_outstanding_requests <= 6'd0;
        end else begin
            if (request_accept) begin
                request_address[request_write_ptr] <= araddr;
                request_beats[request_write_ptr] <= arlen + 5'd1;
                request_write_ptr <= request_write_ptr + 4'd1;
                if (!response_active && request_count == 5'd0)
                    response_delay <= RESPONSE_INITIAL_LATENCY;
            end

            if (response_delay != 8'd0)
                response_delay <= response_delay - 8'd1;

            if (response_start) begin
                response_active <= 1'b1;
                response_address <= request_address[request_read_ptr];
                response_beats <= request_beats[request_read_ptr];
                request_read_ptr <= request_read_ptr + 4'd1;
            end else if (response_accept) begin
                if (response_beats == 5'd1) begin
                    response_active <= 1'b0;
                    response_beats <= 5'd0;
                end else begin
                    response_address <= response_address + 32'd8;
                    response_beats <= response_beats - 5'd1;
                end
            end

            case ({request_accept, response_start})
                2'b10: request_count <= request_count + 5'd1;
                2'b01: request_count <= request_count - 5'd1;
                default: begin end
            endcase

            case ({request_accept, response_complete})
                2'b10: begin
                    outstanding_requests <= outstanding_requests + 6'd1;
                    if (outstanding_requests + 6'd1 >
                        max_outstanding_requests)
                        max_outstanding_requests <=
                            outstanding_requests + 6'd1;
                end
                2'b01: outstanding_requests <=
                    outstanding_requests - 6'd1;
                default: begin end
            endcase
        end
    end

    always @(posedge build_clk) begin
        if (dimension_probe_active && request_accept &&
            (araddr < DIMENSION_BASE ||
             araddr + (({24'd0, arlen} + 32'd1) << 3) >
                 dimension_allocation_end)) begin
            $display("FAIL dimension fetch outside allocation address=%08x beats=%0d end=%08x",
                     araddr, arlen + 8'd1, dimension_allocation_end);
            $fatal(1);
        end
    end

    task automatic write_descriptor_word(
        input [5:0] index,
        input [2:0] word_index,
        input [31:0] value
    );
        begin
            while (!scene_write_ready)
                @(posedge build_clk);
            descriptor_write_index <= index;
            descriptor_write_word <= word_index;
            descriptor_write_data <= value;
            descriptor_write_enable <= 1'b1;
            @(posedge build_clk);
            descriptor_write_enable <= 1'b0;
        end
    endtask

    task automatic write_palette(
        input [3:0] bank,
        input [7:0] index,
        input [31:0] value
    );
        begin
            while (!scene_write_ready)
                @(posedge build_clk);
            palette_write_bank <= bank;
            palette_write_index <= index;
            palette_write_argb <= value;
            palette_write_enable <= 1'b1;
            @(posedge build_clk);
            palette_write_enable <= 1'b0;
        end
    endtask

    task automatic write_standard_sprite(
        input [5:0] index,
        input [7:0] sprite_priority,
        input [31:0] base_address
    );
        begin
            write_descriptor_word(index, 3'd0,
                32'h00010013 | ({24'd0, sprite_priority} << 8));
            write_descriptor_word(index, 3'd1, 32'd0);
            write_descriptor_word(index, 3'd2, 32'h00ff0108);
            write_descriptor_word(index, 3'd3, 32'h00010008);
            write_descriptor_word(index, 3'd4, base_address);
            write_descriptor_word(index, 3'd5, 32'h00000040);
            write_descriptor_word(index, 3'd6, 32'd0);
            write_descriptor_word(index, 3'd7, 32'd0);
        end
    endtask

    task automatic promote_scene;
        integer timeout;
        begin
            validate_start <= 1'b1;
            @(posedge build_clk);
            validate_start <= 1'b0;
            timeout = 0;
            while (!validate_done && timeout < 10000) begin
                @(posedge build_clk);
                timeout = timeout + 1;
            end
            if (!validate_valid)
                $fatal(1, "sprite scene validation failed");
            accept_pending <= 1'b1;
            @(posedge build_clk);
            accept_pending <= 1'b0;
            timeout = 0;
            while (!pending_ready && timeout < 5000) begin
                @(posedge build_clk);
                timeout = timeout + 1;
            end
            if (!pending_ready)
                $fatal(1, "sprite pending clone timeout");
            activate_start <= 1'b1;
            @(posedge build_clk);
            activate_start <= 1'b0;
            timeout = 0;
            while (!activate_done && timeout < 10000) begin
                @(posedge build_clk);
                timeout = timeout + 1;
            end
            if (!activate_done)
                $fatal(1, "sprite activation timeout");
            @(posedge build_clk);
        end
    endtask

    task automatic check_pixel(
        input [10:0] x,
        input [31:0] expected_front,
        input [31:0] expected_behind
    );
        begin
            pixel_read_x <= x;
            repeat (2) @(posedge pixel_clk);
            #1;
            if (pixel_front_argb !== expected_front ||
                pixel_behind_argb !== expected_behind) begin
                $display("FAIL pixel x=%0d front=%08x/%08x behind=%08x/%08x",
                         x, pixel_front_argb, expected_front,
                         pixel_behind_argb, expected_behind);
                $fatal(1);
            end
        end
    endtask

    task automatic build_line(
        input [1:0] slot,
        input [9:0] y
    );
        integer line_timeout;
        begin
            build_slot <= slot;
            line_y <= y;
            start <= 1'b1;
            @(posedge build_clk);
            start <= 1'b0;
            line_timeout = 0;
            while (!done && line_timeout < 5000) begin
                @(posedge build_clk);
                line_timeout = line_timeout + 1;
            end
            if (!done || !line_complete || fetch_error || deadline_error) begin
                $display("FAIL line done=%0d complete=%0d fetch=%0d deadline=%0d cycles=%0d",
                         done, line_complete, fetch_error, deadline_error,
                         build_cycles);
                $fatal(1);
            end
            @(posedge build_clk);
        end
    endtask

    function automatic [7:0] dimension_pixel(
        input integer x,
        input integer y
    );
        begin
            dimension_pixel = ((x * 3 + y * 5) % 4) + 1;
        end
    endfunction

    function automatic [31:0] dimension_argb(input [7:0] index);
        begin
            case (index)
                8'd1: dimension_argb = 32'hffff0000;
                8'd2: dimension_argb = 32'hff00ff00;
                8'd3: dimension_argb = 32'hff0000ff;
                8'd4: dimension_argb = 32'hffffffff;
                default: dimension_argb = 32'd0;
            endcase
        end
    endfunction

    integer memory_index;
    reg [31:0] admitted_before_offscreen;
    reg [31:0] dropped_before_offscreen;
    integer dimension_case;
    integer dimension_width;
    integer dimension_height;
    integer dimension_pitch;
    integer dimension_x;
    integer dimension_y;
    integer dimension_source_x;
    integer dimension_source_y;
    reg [31:0] dimension_admitted_before;
    reg [31:0] dimension_expected_first;
    reg [31:0] dimension_expected_last;
    reg [31:0] dimension_flags;
    integer timeout;
    initial begin
        for (memory_index = 0; memory_index < 65536;
             memory_index = memory_index + 1)
            memory[memory_index] = 8'd0;
        memory[32'h00001000] = 8'd1;
        memory[32'h00001001] = 8'd2;
        memory[32'h00001002] = 8'd3;
        memory[32'h00001003] = 8'd4;
        memory[32'h00001040] = 8'd3;
        memory[32'h00001041] = 8'd3;
        memory[32'h00001042] = 8'd3;
        memory[32'h00001043] = 8'd3;
        memory[32'h000010c0] = 8'd1;
        memory[32'h000010c1] = 8'd2;
        memory[32'h000010c2] = 8'd3;
        memory[32'h000010c3] = 8'd4;
        for (memory_index = 32'h00002000;
             memory_index < 32'h00002080;
             memory_index = memory_index + 1)
            memory[memory_index] = 8'd1;

        repeat (5) @(posedge build_clk);
        reset <= 1'b0;
        repeat (3) @(posedge build_clk);

        dut.buffer_phase_x[0] = 32'h12345678;
        @(posedge build_clk);
        #1;
        if (dut.render_phase_x_q != 32'h12345678) begin
            $display("FAIL idle renderer did not preload slot payload");
            $fatal(1);
        end

        if (PERF_MODE == 1 || PERF_MODE == 5) begin
            write_palette(4'd1, 8'd1, 32'hff20e080);
            for (memory_index = 0; memory_index < 64;
                 memory_index = memory_index + 1) begin
                write_descriptor_word(memory_index[5:0], 3'd0,
                    (PERF_MODE == 5 ? 32'h00010033 : 32'h00010013) |
                    (memory_index << 8));
                write_descriptor_word(memory_index[5:0], 3'd1, 32'd0);
                write_descriptor_word(memory_index[5:0], 3'd2,
                    32'h00ff0180);
                write_descriptor_word(memory_index[5:0], 3'd3,
                    32'h00010080);
                write_descriptor_word(memory_index[5:0], 3'd4,
                    ARENA_BASE + 32'h00002000);
                write_descriptor_word(memory_index[5:0], 3'd5,
                    32'h00000080);
                write_descriptor_word(memory_index[5:0], 3'd6,
                    PERF_MODE == 5 ? 32'hffffffff : 32'd0);
                write_descriptor_word(memory_index[5:0], 3'd7, 32'd0);
            end
            promote_scene();
            build_line(2'd0, 10'd0);
            if (pixels_admitted != 32'd2048 ||
                pixels_dropped != 32'd6144 ||
                overflow_bitmap != 64'h0000ffffffffffff ||
                read_bytes != 32'd2048 ||
                build_cycles > (PERF_MODE == 5 ?
                    COLLISION_CYCLE_BUDGET : PERF_CYCLE_BUDGET) ||
                max_build_cycles != build_cycles ||
                max_outstanding_requests < 6'd2 ||
                axi_error_count != 32'd0 ||
                deadline_error_count != 32'd0) begin
                $display("FAIL 64x128 cycles=%0d admitted=%0d dropped=%0d overflow=%016x bytes=%0d max_outstanding=%0d",
                         build_cycles, pixels_admitted, pixels_dropped,
                         overflow_bitmap, read_bytes,
                         max_outstanding_requests);
                $fatal(1);
            end
            pixel_read_slot <= 2'd0;
            check_pixel(11'd0, 32'hff20e080, 32'd0);
            check_pixel(11'd127, 32'hff20e080, 32'd0);
            check_pixel(11'd128, 32'd0, 32'd0);
            if (PERF_MODE == 5) begin
                build_line(2'd1, 10'd0);
                for (memory_index = 0; memory_index < 64;
                    memory_index = memory_index + 1) begin
                    collision_read_row <= memory_index[5:0];
                    repeat (3) @(posedge build_clk);
                    if (collision_read_data !==
                        (memory_index < 48 ? 64'd0 :
                         (64'hffff000000000000 &
                          ~(64'd1 << memory_index[5:0])))) begin
                        $display("FAIL 16-way collision row=%0d data=%016x",
                            memory_index, collision_read_data);
                        $fatal(1);
                    end
                end
                $display("ASTRA SPRITE 16-WAY COLLISION PASS cycles=%0d outstanding=%0d",
                         build_cycles, max_outstanding_requests);
                $finish;
            end
            $display("ASTRA SPRITE 16X128 PERFORMANCE PASS cycles=%0d outstanding=%0d",
                     build_cycles, max_outstanding_requests);
            $finish;
        end

        if (PERF_MODE == 8) begin
            write_palette(4'd1, 8'd1, 32'hffff0000);
            for (memory_index = 0; memory_index < 17;
                 memory_index = memory_index + 1)
                write_standard_sprite(memory_index[5:0],
                    memory_index[7:0], ARENA_BASE + 32'h00001000);
            promote_scene();
            build_line(2'd0, 10'd0);
            if (pixels_admitted != 32'd128 || pixels_dropped != 32'd8 ||
                overflow_bitmap != 64'h0000000000000001 ||
                overflow_line != 10'd0 || overflow_count != 32'd1 ||
                read_bytes != 32'd128) begin
                $display("FAIL sprite-count cap admitted=%0d dropped=%0d bitmap=%016x line=%0d count=%0d bytes=%0d",
                         pixels_admitted, pixels_dropped, overflow_bitmap,
                         overflow_line, overflow_count, read_bytes);
                $fatal(1);
            end
            $display("ASTRA SPRITE 16-PER-LINE LIMIT PASS cycles=%0d",
                     build_cycles);
            $finish;
        end

        if (PERF_MODE == 2) begin
            for (memory_index = 32'h00001000;
                 memory_index < 32'h00001008;
                 memory_index = memory_index + 1)
                memory[memory_index] = 8'd1;
            for (memory_index = 32'h00001040;
                 memory_index < 32'h00001048;
                 memory_index = memory_index + 1)
                memory[memory_index] = 8'd3;
            write_palette(4'd1, 8'd1, 32'hffff0000);
            write_palette(4'd1, 8'd3, 32'hff0000ff);
            write_standard_sprite(6'd0, 8'd1,
                ARENA_BASE + 32'h00001000);
            write_standard_sprite(6'd1, 8'd2,
                ARENA_BASE + 32'h00001040);
            promote_scene();
            build_line(2'd0, 10'd0);
            if (pixels_admitted != 32'd8 || pixels_dropped != 32'd8 ||
                overflow_bitmap != 64'h0000000000000001 ||
                overflow_line != 10'd0 || overflow_count != 32'd1) begin
                $display("FAIL overflow admitted=%0d dropped=%0d bitmap=%016x line=%0d count=%0d",
                         pixels_admitted, pixels_dropped, overflow_bitmap,
                         overflow_line, overflow_count);
                $fatal(1);
            end
            pixel_read_slot <= 2'd0;
            check_pixel(11'd0, 32'hff0000ff, 32'd0);
            check_pixel(11'd7, 32'hff0000ff, 32'd0);
            check_pixel(11'd8, 32'd0, 32'd0);
            $display("ASTRA SPRITE OVERFLOW PASS cycles=%0d", build_cycles);
            $finish;
        end

        if (PERF_MODE == 3 || PERF_MODE == 4) begin
            for (memory_index = 32'h00001000;
                 memory_index < 32'h00001008;
                 memory_index = memory_index + 1)
                memory[memory_index] = 8'd1;
            write_palette(4'd1, 8'd1, 32'hffff0000);
            write_standard_sprite(6'd0, 8'd1,
                ARENA_BASE + 32'h00001000);
            promote_scene();

            build_slot <= 2'd0;
            line_y <= 10'd0;
            start <= 1'b1;
            @(posedge build_clk);
            start <= 1'b0;
            timeout = 0;
            while (!done && timeout < 1000) begin
                @(posedge build_clk);
                timeout = timeout + 1;
            end
            if (!done || line_complete || !fetch_error || slot_valid[0]) begin
                $display("FAIL AXI failure mode=%0d done=%0d complete=%0d fetch=%0d deadline=%0d valid=%x cycles=%0d",
                         PERF_MODE, done, line_complete, fetch_error,
                         deadline_error, slot_valid, build_cycles);
                $fatal(1);
            end
            if (PERF_MODE == 3 && deadline_error) begin
                $display("FAIL AXI SLVERR incorrectly reported deadline");
                $fatal(1);
            end
            if (PERF_MODE == 3 &&
                (axi_error_count == 32'd0 ||
                 deadline_error_count != 32'd0)) begin
                $display("FAIL AXI SLVERR counters axi=%0d deadline=%0d",
                         axi_error_count, deadline_error_count);
                $fatal(1);
            end
            if (PERF_MODE == 4 &&
                (!deadline_error || build_cycles != BUILD_DEADLINE ||
                 axi_error_count != 32'd0 ||
                 deadline_error_count != 32'd1 ||
                 max_build_cycles != BUILD_DEADLINE)) begin
                $display("FAIL AXI deadline flag/cycles deadline=%0d cycles=%0d expected=%0d",
                         deadline_error, build_cycles, BUILD_DEADLINE);
                $fatal(1);
            end
            if (PERF_MODE == 3)
                $display("ASTRA SPRITE AXI SLVERR PASS cycles=%0d",
                         build_cycles);
            else
                $display("ASTRA SPRITE AXI DEADLINE PASS cycles=%0d",
                         build_cycles);
            $finish;
        end

        if (PERF_MODE == 6) begin
            for (memory_index = 32'h00001fc0;
                 memory_index < 32'h00002000;
                 memory_index = memory_index + 1)
                memory[memory_index] = 8'd1;
            for (memory_index = 32'h00002000;
                 memory_index < 32'h00002040;
                 memory_index = memory_index + 1)
                memory[memory_index] = 8'd2;
            write_palette(4'd1, 8'd1, 32'hffff0000);
            write_palette(4'd1, 8'd2, 32'hff00ff00);
            write_descriptor_word(6'd0, 3'd0, 32'h00010013);
            write_descriptor_word(6'd0, 3'd1, 32'd0);
            write_descriptor_word(6'd0, 3'd2, 32'h00ff0180);
            write_descriptor_word(6'd0, 3'd3, 32'h00010080);
            write_descriptor_word(6'd0, 3'd4,
                ARENA_BASE + 32'h00001fc0);
            write_descriptor_word(6'd0, 3'd5, 32'h00000080);
            write_descriptor_word(6'd0, 3'd6, 32'd0);
            write_descriptor_word(6'd0, 3'd7, 32'd0);
            promote_scene();
            build_line(2'd0, 10'd0);
            pixel_read_slot <= 2'd0;
            check_pixel(11'd0, 32'hffff0000, 32'd0);
            check_pixel(11'd63, 32'hffff0000, 32'd0);
            check_pixel(11'd64, 32'hff00ff00, 32'd0);
            check_pixel(11'd127, 32'hff00ff00, 32'd0);
            if (read_bytes != 32'd128 ||
                max_outstanding_requests < 6'd2 ||
                axi_error_count != 32'd0 ||
                deadline_error_count != 32'd0) begin
                $display("FAIL 4K split bytes=%0d outstanding=%0d axi=%0d deadline=%0d",
                    read_bytes, max_outstanding_requests, axi_error_count,
                    deadline_error_count);
                $fatal(1);
            end
            $display("ASTRA SPRITE 4K SPLIT PASS cycles=%0d outstanding=%0d",
                     build_cycles, max_outstanding_requests);
            $finish;
        end

        if (PERF_MODE == 7) begin
            write_palette(4'd1, 8'd1, 32'hffff0000);
            write_palette(4'd1, 8'd2, 32'hff00ff00);
            write_palette(4'd1, 8'd3, 32'hff0000ff);
            write_palette(4'd1, 8'd4, 32'hffffffff);

            for (dimension_case = 1; dimension_case <= 128;
                 dimension_case = dimension_case + 1) begin
                dimension_width = dimension_case;
                dimension_height = 129 - dimension_case;
                dimension_pitch = dimension_width <= 64 ? 64 : 128;
                for (dimension_y = 0;
                     dimension_y < dimension_height;
                     dimension_y = dimension_y + 1) begin
                    for (dimension_x = 0;
                         dimension_x < dimension_width;
                         dimension_x = dimension_x + 1) begin
                        memory[32'h00004000 +
                               dimension_y * dimension_pitch +
                               dimension_x] =
                            dimension_pixel(dimension_x, dimension_y);
                    end
                end

                dimension_flags = 32'h00010013;
                if ((dimension_case & 1) != 0)
                    dimension_flags = dimension_flags | 32'h00000004;
                if ((dimension_case & 2) != 0)
                    dimension_flags = dimension_flags | 32'h00000008;
                write_descriptor_word(6'd0, 3'd0, dimension_flags);
                write_descriptor_word(6'd0, 3'd1, 32'd0);
                write_descriptor_word(6'd0, 3'd2,
                    {8'd0, 8'hff, dimension_height[7:0],
                     dimension_width[7:0]});
                write_descriptor_word(6'd0, 3'd3,
                    {5'd0, dimension_height[10:0], 5'd0,
                     dimension_width[10:0]});
                write_descriptor_word(6'd0, 3'd4, DIMENSION_BASE);
                write_descriptor_word(6'd0, 3'd5,
                    dimension_pitch[31:0]);
                write_descriptor_word(6'd0, 3'd6, 32'd0);
                write_descriptor_word(6'd0, 3'd7, 32'd0);
                promote_scene();

                dimension_allocation_end = DIMENSION_BASE +
                    dimension_pitch * dimension_height;
                dimension_probe_active = 1'b1;
                dimension_admitted_before = pixels_admitted;
                build_line(2'd0, 10'd0);
                dimension_probe_active = 1'b0;
                if (pixels_admitted - dimension_admitted_before !=
                        dimension_width ||
                    read_bytes != ((dimension_width + 7) & ~7)) begin
                    $display("FAIL dimension first row width=%0d height=%0d admitted=%0d read=%0d",
                             dimension_width, dimension_height,
                             pixels_admitted - dimension_admitted_before,
                             read_bytes);
                    $fatal(1);
                end
                dimension_source_y = (dimension_flags & 32'h8) != 0 ?
                    dimension_height - 1 : 0;
                dimension_source_x = (dimension_flags & 32'h4) != 0 ?
                    dimension_width - 1 : 0;
                dimension_expected_first = dimension_argb(
                    dimension_pixel(dimension_source_x,
                                    dimension_source_y));
                dimension_source_x = (dimension_flags & 32'h4) != 0 ?
                    0 : dimension_width - 1;
                dimension_expected_last = dimension_argb(
                    dimension_pixel(dimension_source_x,
                                    dimension_source_y));
                pixel_read_slot <= 2'd0;
                check_pixel(11'd0, dimension_expected_first, 32'd0);
                check_pixel(dimension_width - 1,
                            dimension_expected_last, 32'd0);
                if (dimension_width < 128)
                    check_pixel(dimension_width, 32'd0, 32'd0);

                dimension_probe_active = 1'b1;
                dimension_admitted_before = pixels_admitted;
                build_line(2'd1, dimension_height - 1);
                dimension_probe_active = 1'b0;
                if (pixels_admitted - dimension_admitted_before !=
                        dimension_width ||
                    read_bytes != ((dimension_width + 7) & ~7)) begin
                    $display("FAIL dimension last row width=%0d height=%0d admitted=%0d read=%0d",
                             dimension_width, dimension_height,
                             pixels_admitted - dimension_admitted_before,
                             read_bytes);
                    $fatal(1);
                end
                dimension_source_y = (dimension_flags & 32'h8) != 0 ?
                    0 : dimension_height - 1;
                dimension_source_x = (dimension_flags & 32'h4) != 0 ?
                    dimension_width - 1 : 0;
                dimension_expected_first = dimension_argb(
                    dimension_pixel(dimension_source_x,
                                    dimension_source_y));
                dimension_source_x = (dimension_flags & 32'h4) != 0 ?
                    0 : dimension_width - 1;
                dimension_expected_last = dimension_argb(
                    dimension_pixel(dimension_source_x,
                                    dimension_source_y));
                pixel_read_slot <= 2'd1;
                check_pixel(11'd0, dimension_expected_first, 32'd0);
                check_pixel(dimension_width - 1,
                            dimension_expected_last, 32'd0);
                if (dimension_width < 128)
                    check_pixel(dimension_width, 32'd0, 32'd0);
            end
            $display("ASTRA SPRITE VARIABLE DIMENSIONS PASS widths=1..128 heights=1..128 pitches=64,128");
            $finish;
        end

        write_palette(4'd1, 8'd1, 32'hffff0000);
        write_palette(4'd1, 8'd2, 32'hff00ff00);
        write_palette(4'd1, 8'd3, 32'hff0000ff);
        write_palette(4'd1, 8'd4, 32'hffffffff);
        write_descriptor_word(6'd0, 3'd0, 32'h00010113);
        write_descriptor_word(6'd0, 3'd1, 32'h00010002);
        write_descriptor_word(6'd0, 3'd2, 32'h00ff0204);
        write_descriptor_word(6'd0, 3'd3, 32'h00040008);
        write_descriptor_word(6'd0, 3'd4, ARENA_BASE + 32'h00001000);
        write_descriptor_word(6'd0, 3'd5, 32'h00000040);
        write_descriptor_word(6'd0, 3'd6, 32'd0);
        write_descriptor_word(6'd0, 3'd7, 32'd0);
        promote_scene();

        build_line(2'd0, 10'd2);

        pixel_read_slot <= 2'd0;
        check_pixel(11'd0, 32'd0, 32'd0);
        check_pixel(11'd2, 32'hffff0000, 32'd0);
        check_pixel(11'd3, 32'hffff0000, 32'd0);
        check_pixel(11'd4, 32'hff00ff00, 32'd0);
        check_pixel(11'd5, 32'hff00ff00, 32'd0);
        check_pixel(11'd6, 32'hff0000ff, 32'd0);
        check_pixel(11'd7, 32'hff0000ff, 32'd0);
        check_pixel(11'd8, 32'hffffffff, 32'd0);
        check_pixel(11'd9, 32'hffffffff, 32'd0);
        check_pixel(11'd10, 32'd0, 32'd0);

        if (pixels_admitted != 32'd8 || pixels_dropped != 32'd0 ||
            overflow_bitmap != 64'd0 || read_bytes != 32'd8) begin
            $display("FAIL counters admitted=%0d dropped=%0d overflow=%016x bytes=%0d",
                     pixels_admitted, pixels_dropped, overflow_bitmap,
                     read_bytes);
            $fatal(1);
        end

        // Second generation: two translucent, colliding front sprites plus
        // one clipped, X/Y-reflected sprite behind the framebuffer.
        memory[32'h00001001] = 8'd1;
        memory[32'h00001002] = 8'd1;
        memory[32'h00001003] = 8'd1;
        write_palette(4'd2, 8'd1, 32'h80ff0000);
        write_palette(4'd2, 8'd3, 32'h800000ff);
        write_descriptor_word(6'd0, 3'd0, 32'h00020133);
        write_descriptor_word(6'd0, 3'd1, 32'h00000000);
        write_descriptor_word(6'd0, 3'd2, 32'h00ff0104);
        write_descriptor_word(6'd0, 3'd3, 32'h00010004);
        write_descriptor_word(6'd0, 3'd4, ARENA_BASE + 32'h00001000);
        write_descriptor_word(6'd0, 3'd5, 32'h00000040);
        write_descriptor_word(6'd0, 3'd6, 32'h00010002);
        write_descriptor_word(6'd1, 3'd0, 32'h00020233);
        write_descriptor_word(6'd1, 3'd1, 32'h00000000);
        write_descriptor_word(6'd1, 3'd2, 32'h00ff0104);
        write_descriptor_word(6'd1, 3'd3, 32'h00010004);
        write_descriptor_word(6'd1, 3'd4, ARENA_BASE + 32'h00001040);
        write_descriptor_word(6'd1, 3'd5, 32'h00000040);
        write_descriptor_word(6'd1, 3'd6, 32'h00020001);
        write_descriptor_word(6'd1, 3'd7, 32'd0);
        write_descriptor_word(6'd2, 3'd0, 32'h0001030f);
        write_descriptor_word(6'd2, 3'd1, 32'h0000fffe);
        write_descriptor_word(6'd2, 3'd2, 32'h00ff0204);
        write_descriptor_word(6'd2, 3'd3, 32'h00020004);
        write_descriptor_word(6'd2, 3'd4, ARENA_BASE + 32'h00001080);
        write_descriptor_word(6'd2, 3'd5, 32'h00000040);
        write_descriptor_word(6'd2, 3'd6, 32'd0);
        write_descriptor_word(6'd2, 3'd7, 32'd0);
        promote_scene();

        build_line(2'd1, 10'd0);
        pixel_read_slot <= 2'd1;
        check_pixel(11'd0, 32'hc0400080, 32'hff00ff00);
        check_pixel(11'd1, 32'hc0400080, 32'hffff0000);
        check_pixel(11'd2, 32'hc0400080, 32'd0);
        check_pixel(11'd3, 32'hc0400080, 32'd0);
        check_pixel(11'd4, 32'd0, 32'd0);

        // Starting line zero for the next frame atomically publishes the
        // completed previous-frame collision matrix.
        build_line(2'd2, 10'd0);
        collision_read_row <= 6'd0;
        repeat (2) @(posedge build_clk);
        if (!collision_read_data[1]) begin
            $display("FAIL collision row0=%016x", collision_read_data);
            $fatal(1);
        end
        collision_read_row <= 6'd1;
        repeat (2) @(posedge build_clk);
        if (!collision_read_data[0]) begin
            $display("FAIL collision row1=%016x", collision_read_data);
            $fatal(1);
        end

        // Fully off-screen descriptors remain valid and consume no source
        // bandwidth. Hiding by position is supported independently of the
        // descriptor enable and visibility bits.
        write_descriptor_word(6'd0, 3'd1, 32'h0000fffc);
        write_descriptor_word(6'd1, 3'd1, 32'h00000500);
        write_descriptor_word(6'd2, 3'd1, 32'hfffe0000);
        promote_scene();
        admitted_before_offscreen = pixels_admitted;
        dropped_before_offscreen = pixels_dropped;
        build_line(2'd0, 10'd0);
        pixel_read_slot <= 2'd0;
        check_pixel(11'd0, 32'd0, 32'd0);
        if (pixels_admitted != admitted_before_offscreen ||
            pixels_dropped != dropped_before_offscreen ||
            overflow_bitmap != 64'd0 || read_bytes != 32'd0) begin
            $display("FAIL off-screen counters admitted=%0d/%0d dropped=%0d/%0d overflow=%016x bytes=%0d",
                     pixels_admitted, admitted_before_offscreen,
                     pixels_dropped, dropped_before_offscreen, overflow_bitmap,
                     read_bytes);
            $fatal(1);
        end
        $display("fully off-screen positioning pass");

        $display("ASTRA SPRITE LINE BUILDER PASS cycles=%0d", build_cycles);
        $finish;
    end

    wire unused_axi = &{1'b0, arid, arsize, arburst, arcache, arprot, arqos,
                        completed_slot, slot_valid, overflow_line,
                        overflow_count, collision_read_data, collision_frame,
                        collision_event, validate_busy, activate_busy,
                        pending_valid};
endmodule

`default_nettype wire
