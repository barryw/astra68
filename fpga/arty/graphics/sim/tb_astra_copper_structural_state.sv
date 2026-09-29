`timescale 1ns/1ps
`default_nettype none

module tb_astra_copper_structural_state;
    reg clk = 1'b0;
    always #2.5 clk = ~clk;

    reg reset = 1'b1;
    reg frame_boundary = 1'b0;
    reg baseline_changed = 1'b0;
    reg copper_running = 1'b0;
    reg copper_fault = 1'b0;
    reg [15:0] validate_target = 16'd0;
    reg [31:0] validate_data = 32'd0;
    wire validate_allowed;
    reg move_valid = 1'b0;
    reg [15:0] move_target = 16'd0;
    reg [31:0] move_data = 32'd0;
    wire move_allowed;
    wire move_ready;

    wire scene_enable;
    wire framebuffer_enable;
    wire [1:0] framebuffer_format;
    wire [31:0] framebuffer_base;
    wire [31:0] framebuffer_pitch;
    wire [12:0] framebuffer_width;
    wire [12:0] framebuffer_height;
    wire [31:0] candidates_accepted;
    wire [31:0] candidates_rejected;
    wire [31:0] candidates_deferred;

    astra_copper_structural_state #(
        .ARENA_BASE(32'h18000000),
        .ARENA_LIMIT(32'h20000000),
        .OUTPUT_WIDTH(16),
        .OUTPUT_HEIGHT(8)
    ) dut (
        .clk(clk), .reset(reset), .frame_boundary(frame_boundary),
        .baseline_changed(baseline_changed),
        .copper_running(copper_running), .copper_fault(copper_fault),
        .baseline_scene_enable(1'b1),
        .baseline_framebuffer_enable(1'b1),
        .baseline_framebuffer_format(2'd1),
        .baseline_framebuffer_base(32'h18000000),
        .baseline_framebuffer_pitch(32'd64),
        .baseline_framebuffer_width(13'd16),
        .baseline_framebuffer_height(13'd8),
        .baseline_framebuffer_viewport_x(32'sd0),
        .baseline_framebuffer_viewport_y(32'sd0),
        .baseline_framebuffer_wrap_x(1'b0),
        .baseline_framebuffer_wrap_y(1'b0),
        .validate_target(validate_target), .validate_data(validate_data),
        .validate_allowed(validate_allowed),
        .move_valid(move_valid), .move_target(move_target),
        .move_data(move_data), .move_allowed(move_allowed),
        .move_ready(move_ready),
        .scene_enable(scene_enable),
        .framebuffer_enable(framebuffer_enable),
        .framebuffer_format(framebuffer_format),
        .framebuffer_base(framebuffer_base),
        .framebuffer_pitch(framebuffer_pitch),
        .framebuffer_width(framebuffer_width),
        .framebuffer_height(framebuffer_height),
        .candidates_accepted(candidates_accepted),
        .candidates_rejected(candidates_rejected),
        .candidates_deferred(candidates_deferred)
    );

    task automatic pulse_frame;
        begin
            @(negedge clk); frame_boundary = 1'b1;
            @(negedge clk); frame_boundary = 1'b0;
        end
    endtask

    task automatic write_move(input [15:0] target, input [31:0] data);
        begin
            @(negedge clk);
            move_target = target;
            move_data = data;
            move_valid = 1'b1;
            while (!move_ready) @(negedge clk);
            @(negedge clk);
            move_valid = 1'b0;
        end
    endtask

    task automatic finish_list;
        begin
            @(negedge clk); copper_running = 1'b1;
            repeat (2) @(negedge clk);
            copper_running = 1'b0;
            repeat (20) @(negedge clk);
        end
    endtask

    initial begin
        repeat (4) @(negedge clk);
        reset = 1'b0;
        pulse_frame();

        move_target = 16'hffff;
        #1;
        if (move_allowed || !move_ready)
            $fatal(1, "capacity ready depends on request permission");

        validate_target = 16'h0048;
        validate_data = {3'd0, 13'd16, 3'd0, 13'd32};
        #1;
        if (!validate_allowed) $fatal(1, "legal size rejected");
        validate_data = 32'd0;
        #1;
        if (validate_allowed) $fatal(1, "zero size accepted");

        copper_running = 1'b1;
        write_move(16'h0044, 32'd128);
        write_move(16'h0048, {3'd0, 13'd16, 3'd0, 13'd32});
        @(negedge clk); copper_running = 1'b0;
        repeat (20) @(negedge clk);
        if (framebuffer_pitch != 32'd64 || framebuffer_width != 13'd16)
            $fatal(1, "structural state changed before vblank");
        pulse_frame();
        #1;
        if (framebuffer_pitch != 32'd128 || framebuffer_width != 13'd32 ||
            candidates_accepted != 32'd1)
            $fatal(1, "valid structural candidate not promoted");

        copper_running = 1'b1;
        write_move(16'h0040, 32'h10000000);
        @(negedge clk); copper_running = 1'b0;
        repeat (20) @(negedge clk);
        pulse_frame();
        #1;
        if (framebuffer_base != 32'h18000000 ||
            candidates_rejected != 32'd1)
            $fatal(1, "invalid structural candidate escaped validation");

        copper_running = 1'b1;
        write_move(16'h0044, 32'd128);
        copper_fault = 1'b1;
        @(negedge clk); copper_running = 1'b0;
        repeat (2) @(negedge clk);
        copper_fault = 1'b0;
        pulse_frame();
        #1;
        if (framebuffer_pitch != 32'd64 || candidates_rejected != 32'd2)
            $fatal(1, "faulted list structural state was not discarded");

        validate_target = 16'h0080;
        validate_data = 32'h18010000;
        move_target = validate_target;
        #1;
        if (validate_allowed || move_allowed)
            $fatal(1, "retired tile0 structural target accepted");
        validate_target = 16'h00d4;
        move_target = validate_target;
        #1;
        if (validate_allowed || move_allowed)
            $fatal(1, "retired tile1 structural target accepted");

        copper_running = 1'b1;
        write_move(16'h0044, 32'd192);
        @(negedge clk); copper_running = 1'b0;
        repeat (20) @(negedge clk);
        pulse_frame();
        #1;
        if (framebuffer_pitch != 32'd192 || candidates_accepted != 32'd2)
            $fatal(1, "framebuffer structural update was blocked");

        $display("ASTRA COPPER STRUCTURAL STATE PASS");
        $finish;
    end
endmodule

`default_nettype wire
