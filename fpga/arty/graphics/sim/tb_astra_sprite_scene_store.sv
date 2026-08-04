`timescale 1ns/1ps
`default_nettype none

module tb_astra_sprite_scene_store;
    reg clk = 1'b0;
    always #2.5 clk = ~clk;

    reg reset = 1'b1;
    reg descriptor_write_enable = 1'b0;
    reg [5:0] descriptor_write_index = 6'd0;
    reg [2:0] descriptor_write_word = 3'd0;
    reg [31:0] descriptor_write_data = 32'd0;
    reg palette_write_enable = 1'b0;
    reg [3:0] palette_write_bank = 4'd0;
    reg [7:0] palette_write_index = 8'd0;
    reg [31:0] palette_write_argb = 32'd0;
    wire write_ready;
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
    reg baseline_restore_start = 1'b0;
    wire baseline_restore_busy;
    wire baseline_restore_done;
    reg copper_palette_write_enable = 1'b0;
    reg [3:0] copper_palette_write_bank = 4'd0;
    reg [7:0] copper_palette_write_index = 8'd0;
    reg [31:0] copper_palette_write_argb = 32'd0;
    wire copper_palette_write_ready;
    reg order_read_enable = 1'b0;
    reg [5:0] order_read_position = 6'd0;
    wire [5:0] order_read_index;
    reg descriptor_read_enable = 1'b0;
    reg [5:0] descriptor_read_index = 6'd0;
    wire [31:0] descriptor_word0;
    wire [31:0] descriptor_word1;
    wire [31:0] descriptor_word2;
    wire [31:0] descriptor_word3;
    wire [31:0] descriptor_word4;
    wire [31:0] descriptor_word5;
    wire [31:0] descriptor_word6;
    wire [31:0] descriptor_scale_step_x;
    wire [63:0] descriptor_collision_compatible;
    reg [3:0] palette0_read_bank = 4'd0;
    reg [7:0] palette0_read_index = 8'd0;
    wire [31:0] palette0_read_argb;
    reg [3:0] palette1_read_bank = 4'd0;
    reg [7:0] palette1_read_index = 8'd0;
    wire [31:0] palette1_read_argb;
    reg [3:0] palette2_read_bank = 4'd0;
    reg [7:0] palette2_read_index = 8'd0;
    wire [31:0] palette2_read_argb;
    reg [3:0] palette3_read_bank = 4'd0;
    reg [7:0] palette3_read_index = 8'd0;
    wire [31:0] palette3_read_argb;

    astra_sprite_scene_store dut (
        .clk(clk),
        .reset(reset),
        .descriptor_write_enable(descriptor_write_enable),
        .descriptor_write_index(descriptor_write_index),
        .descriptor_write_word(descriptor_write_word),
        .descriptor_write_data(descriptor_write_data),
        .palette_write_enable(palette_write_enable),
        .palette_write_bank(palette_write_bank),
        .palette_write_index(palette_write_index),
        .palette_write_argb(palette_write_argb),
        .write_ready(write_ready),
        .validate_start(validate_start),
        .validate_busy(validate_busy),
        .validate_done(validate_done),
        .validate_valid(validate_valid),
        .accept_pending(accept_pending),
        .pending_ready(pending_ready),
        .pending_valid(pending_valid),
        .activate_start(activate_start),
        .activate_busy(activate_busy),
        .activate_done(activate_done),
        .baseline_restore_start(baseline_restore_start),
        .baseline_restore_busy(baseline_restore_busy),
        .baseline_restore_done(baseline_restore_done),
        .copper_palette_write_enable(copper_palette_write_enable),
        .copper_palette_write_bank(copper_palette_write_bank),
        .copper_palette_write_index(copper_palette_write_index),
        .copper_palette_write_argb(copper_palette_write_argb),
        .copper_palette_write_ready(copper_palette_write_ready),
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

    task automatic write_descriptor_word(
        input [5:0] index,
        input [2:0] word_index,
        input [31:0] value
    );
        begin
            while (!write_ready)
                @(posedge clk);
            descriptor_write_index <= index;
            descriptor_write_word <= word_index;
            descriptor_write_data <= value;
            descriptor_write_enable <= 1'b1;
            @(posedge clk);
            descriptor_write_enable <= 1'b0;
        end
    endtask

    task automatic request_validation(input expected_valid);
        integer cycles;
        begin
            while (!write_ready)
                @(posedge clk);
            validate_start <= 1'b1;
            @(posedge clk);
            validate_start <= 1'b0;
            cycles = 0;
            while (!validate_done && cycles < 10000) begin
                @(posedge clk);
                cycles = cycles + 1;
            end
            if (!validate_done || validate_valid !== expected_valid) begin
                $display("FAIL validation expected=%0d got done=%0d valid=%0d cycles=%0d",
                         expected_valid, validate_done, validate_valid, cycles);
                $fatal(1);
            end
        end
    endtask

    integer wait_cycles;
    integer scale_batch;
    integer scale_slot;
    integer scale_pair;
    integer scale_source;
    integer scale_source_height;
    integer scale_destination;
    integer scale_destination_height;
    integer scale_position;
    integer scale_reference;
    integer scale_mapped;
    reg [31:0] scale_actual;
    reg [31:0] scale_expected;
    reg [63:0] scale_numerator;
    reg [63:0] scale_phase;
    initial begin
        repeat (4) @(posedge clk);
        reset <= 1'b0;
        repeat (2) @(posedge clk);

        write_descriptor_word(6'd0, 3'd0, 32'h00070323);
        write_descriptor_word(6'd0, 3'd1, 32'hfff00010);
        write_descriptor_word(6'd0, 3'd2, 32'h00ff8080);
        write_descriptor_word(6'd0, 3'd3, 32'h01000100);
        write_descriptor_word(6'd0, 3'd4, 32'h18010000);
        write_descriptor_word(6'd0, 3'd5, 32'h00000080);
        write_descriptor_word(6'd0, 3'd6, 32'h00010002);
        write_descriptor_word(6'd0, 3'd7, 32'd0);

        while (!write_ready)
            @(posedge clk);
        palette_write_bank <= 4'd3;
        palette_write_index <= 8'd5;
        palette_write_argb <= 32'h80abcdef;
        palette_write_enable <= 1'b1;
        @(posedge clk);
        palette_write_enable <= 1'b0;

        request_validation(1'b1);
        accept_pending <= 1'b1;
        @(posedge clk);
        accept_pending <= 1'b0;
        @(posedge clk);
        if (!pending_valid) begin
            $display("FAIL accepted generation did not become pending");
            $fatal(1);
        end

        wait_cycles = 0;
        while (!pending_ready && wait_cycles < 5000) begin
            @(posedge clk);
            wait_cycles = wait_cycles + 1;
        end
        if (!pending_ready) begin
            $display("FAIL pending clone timeout");
            $fatal(1);
        end

        activate_start <= 1'b1;
        @(posedge clk);
        activate_start <= 1'b0;
        @(posedge clk);
        if (!activate_busy || copper_palette_write_ready) begin
            $display("FAIL activation arbitration busy=%0d copper_ready=%0d",
                     activate_busy, copper_palette_write_ready);
            $fatal(1);
        end
        wait_cycles = 0;
        while (!activate_done && wait_cycles < 10000) begin
            @(posedge clk);
            wait_cycles = wait_cycles + 1;
        end
        if (!activate_done) begin
            $display("FAIL activation timeout");
            $fatal(1);
        end

        order_read_position <= 6'd63;
        order_read_enable <= 1'b1;
        descriptor_read_index <= 6'd0;
        descriptor_read_enable <= 1'b1;
        palette0_read_bank <= 4'd3;
        palette0_read_index <= 8'd5;
        palette1_read_bank <= 4'd3;
        palette1_read_index <= 8'd5;
        palette2_read_bank <= 4'd3;
        palette2_read_index <= 8'd5;
        palette3_read_bank <= 4'd3;
        palette3_read_index <= 8'd5;
        @(posedge clk);
        order_read_enable <= 1'b0;
        descriptor_read_enable <= 1'b0;
        @(posedge clk);

        if (order_read_index != 6'd0 || descriptor_word0 != 32'h00070323 ||
            descriptor_scale_step_x != 32'h00800000) begin
            $display("FAIL active descriptor/order step=%08x order=%0d word0=%08x",
                     descriptor_scale_step_x,
                     order_read_index, descriptor_word0);
            $fatal(1);
        end
        if (palette0_read_argb != 32'h80abcdef ||
            palette1_read_argb != 32'h80abcdef ||
            palette2_read_argb != 32'h80abcdef ||
            palette3_read_argb != 32'h80abcdef) begin
            $display("FAIL active palette replicas %08x %08x %08x %08x",
                     palette0_read_argb, palette1_read_argb,
                     palette2_read_argb, palette3_read_argb);
            $fatal(1);
        end

        if (!copper_palette_write_ready) begin
            $display("FAIL copper palette unexpectedly blocked");
            $fatal(1);
        end
        copper_palette_write_bank <= 4'd3;
        copper_palette_write_index <= 8'd5;
        copper_palette_write_argb <= 32'hff123456;
        copper_palette_write_enable <= 1'b1;
        @(posedge clk);
        copper_palette_write_enable <= 1'b0;
        repeat (2) @(posedge clk);
        if (palette0_read_argb != 32'hff123456 ||
            palette1_read_argb != 32'hff123456 ||
            palette2_read_argb != 32'hff123456 ||
            palette3_read_argb != 32'hff123456) begin
            $display("FAIL copper palette replicas %08x %08x %08x %08x",
                     palette0_read_argb, palette1_read_argb,
                     palette2_read_argb, palette3_read_argb);
            $fatal(1);
        end

        baseline_restore_start <= 1'b1;
        @(posedge clk);
        baseline_restore_start <= 1'b0;
        @(posedge clk);
        if (!baseline_restore_busy || copper_palette_write_ready) begin
            $display("FAIL baseline restore admission busy=%0d copper_ready=%0d",
                     baseline_restore_busy, copper_palette_write_ready);
            $fatal(1);
        end
        wait_cycles = 0;
        while (!baseline_restore_done && wait_cycles < 10000) begin
            @(posedge clk);
            wait_cycles = wait_cycles + 1;
        end
        if (!baseline_restore_done) begin
            $display("FAIL baseline palette restore timeout");
            $fatal(1);
        end
        repeat (2) @(posedge clk);
        if (baseline_restore_busy || !copper_palette_write_ready ||
            palette0_read_argb != 32'h80abcdef ||
            palette1_read_argb != 32'h80abcdef ||
            palette2_read_argb != 32'h80abcdef ||
            palette3_read_argb != 32'h80abcdef) begin
            $display("FAIL restored palette busy=%0d ready=%0d values=%08x %08x %08x %08x",
                     baseline_restore_busy, copper_palette_write_ready,
                     palette0_read_argb, palette1_read_argb,
                     palette2_read_argb, palette3_read_argb);
            $fatal(1);
        end
        $display("sprite copper palette restore pass cycles=%0d", wait_cycles);

        for (scale_slot = 0; scale_slot < 64;
             scale_slot = scale_slot + 1) begin
            write_descriptor_word(scale_slot[5:0], 3'd0, 32'h00000003);
            write_descriptor_word(scale_slot[5:0], 3'd1, 32'd0);
            write_descriptor_word(scale_slot[5:0], 3'd4, 32'h18000000);
            write_descriptor_word(scale_slot[5:0], 3'd5, 32'd128);
            write_descriptor_word(scale_slot[5:0], 3'd6, 32'd0);
            write_descriptor_word(scale_slot[5:0], 3'd7, 32'd0);
        end

        for (scale_batch = 0; scale_batch < 2048;
             scale_batch = scale_batch + 1) begin
            for (scale_slot = 0; scale_slot < 64;
                 scale_slot = scale_slot + 1) begin
                scale_pair = scale_batch * 64 + scale_slot;
                scale_source = scale_pair / 1024 + 1;
                scale_destination = scale_pair % 1024 + 1;
                scale_source_height =
                    (scale_destination - 1) % 128 + 1;
                scale_destination_height = scale_destination;
                write_descriptor_word(scale_slot[5:0], 3'd2,
                    {8'd0, 8'hff, scale_source_height[7:0],
                     scale_source[7:0]});
                write_descriptor_word(scale_slot[5:0], 3'd3,
                    {5'd0, scale_destination_height[10:0], 5'd0,
                     scale_destination[10:0]});
            end
            request_validation(1'b1);

            for (scale_slot = 0; scale_slot < 64;
                 scale_slot = scale_slot + 1) begin
                scale_pair = scale_batch * 64 + scale_slot;
                scale_source = scale_pair / 1024 + 1;
                scale_destination = scale_pair % 1024 + 1;
                scale_numerator = scale_source;
                scale_numerator = (scale_numerator << 24) +
                    scale_destination - 1;
                scale_expected = scale_numerator / scale_destination;
                scale_actual = dut.editable_bank_q ?
                    dut.scale_step1[scale_slot] :
                    dut.scale_step0[scale_slot];
                if (scale_actual !== scale_expected) begin
                    $display("FAIL scale step source=%0d destination=%0d actual=%08x expected=%08x",
                        scale_source, scale_destination, scale_actual,
                        scale_expected);
                    $fatal(1);
                end
                for (scale_position = 0;
                     scale_position < scale_destination;
                     scale_position = scale_position + 1) begin
                    scale_phase = scale_actual;
                    scale_phase = scale_phase * scale_position;
                    scale_mapped = scale_phase >> 24;
                    scale_reference =
                        (scale_position * scale_source) /
                        scale_destination;
                    if (scale_mapped != scale_reference) begin
                        $display("FAIL scale map source=%0d destination=%0d x=%0d actual=%0d expected=%0d step=%08x",
                            scale_source, scale_destination, scale_position,
                            scale_mapped, scale_reference, scale_actual);
                        $fatal(1);
                    end
                end
            end
        end
        $display("sprite dimension/scaling exactness pass pairs=131072 source_size_pairs=16384");

        write_descriptor_word(6'd0, 3'd2, 32'h00ff0100);
        request_validation(1'b0);
        write_descriptor_word(6'd0, 3'd2, 32'h00ff0181);
        request_validation(1'b0);
        write_descriptor_word(6'd0, 3'd2, 32'h00ff0001);
        request_validation(1'b0);
        write_descriptor_word(6'd0, 3'd2, 32'h00ff8101);
        request_validation(1'b0);
        write_descriptor_word(6'd0, 3'd2, 32'h00ff0101);
        request_validation(1'b1);
        $display("sprite dimension rejection pass");

        while (!write_ready)
            @(posedge clk);
        write_descriptor_word(6'd1, 3'd7, 32'h00000001);
        request_validation(1'b0);

        $display("ASTRA SPRITE SCENE STORE PASS");
        $finish;
    end
endmodule

`default_nettype wire
