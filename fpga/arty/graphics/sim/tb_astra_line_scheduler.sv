`timescale 1ns/1ps
`default_nettype none

module tb_astra_line_scheduler;
    localparam integer OUTPUT_WIDTH = 16;
    localparam integer OUTPUT_HEIGHT = 8;
    localparam integer TOTAL_HEIGHT = 10;

    reg build_clk = 1'b0;
    reg pixel_clk = 1'b0;
    always #2.5 build_clk = ~build_clk;
    always #6.734 pixel_clk = ~pixel_clk;

    reg build_reset = 1'b1;
    reg pixel_reset = 1'b1;
    reg scene_changed = 1'b0;
    reg quiesce = 1'b0;
    reg scene_enable = 1'b1;
    reg framebuffer_enable = 1'b1;
    reg tile0_enable = 1'b1;
    reg tile1_enable = 1'b1;
    reg sprite_enable = 1'b1;
    wire line_prepare_valid;
    wire [9:0] line_prepare_y;
    reg line_prepare_ready = 1'b1;

    wire client_start;
    wire [1:0] client_build_slot;
    wire [9:0] client_line_y;
    wire [3:0] client_enable;
    reg [3:0] client_done = 4'd0;
    reg [3:0] client_line_complete = 4'd0;
    wire [31:0] lines_built;
    wire [31:0] lines_failed;
    wire [31:0] scheduler_overruns;
    wire scheduler_idle;

    reg [10:0] pixel_x = 11'd0;
    reg [9:0] pixel_y = 10'd0;
    wire [1:0] pixel_read_slot;
    wire pixel_line_available;
    wire [31:0] pixel_underruns;
    wire [3:0] pixel_slot_valid;
    wire [9:0] pixel_slot_tag0;
    wire [9:0] pixel_slot_tag1;
    wire [9:0] pixel_slot_tag2;
    wire [9:0] pixel_slot_tag3;

    astra_line_scheduler #(
        .OUTPUT_WIDTH(OUTPUT_WIDTH),
        .OUTPUT_HEIGHT(OUTPUT_HEIGHT),
        .TOTAL_HEIGHT(TOTAL_HEIGHT)
    ) dut (
        .build_clk(build_clk),
        .build_reset(build_reset),
        .scene_changed(scene_changed),
        .quiesce(quiesce),
        .scene_enable(scene_enable),
        .framebuffer_enable(framebuffer_enable),
        .tile0_enable(tile0_enable),
        .tile1_enable(tile1_enable),
        .sprite_enable(sprite_enable),
        .line_prepare_valid(line_prepare_valid),
        .line_prepare_y(line_prepare_y),
        .line_prepare_ready(line_prepare_ready),
        .client_start(client_start),
        .client_build_slot(client_build_slot),
        .client_line_y(client_line_y),
        .client_enable(client_enable),
        .client_done(client_done),
        .client_line_complete(client_line_complete),
        .lines_built(lines_built),
        .lines_failed(lines_failed),
        .scheduler_overruns(scheduler_overruns),
        .scheduler_idle(scheduler_idle),
        .pixel_clk(pixel_clk),
        .pixel_reset(pixel_reset),
        .pixel_x(pixel_x),
        .pixel_y(pixel_y),
        .pixel_read_slot(pixel_read_slot),
        .pixel_line_available(pixel_line_available),
        .pixel_underruns(pixel_underruns),
        .pixel_slot_valid(pixel_slot_valid),
        .pixel_slot_tag0(pixel_slot_tag0),
        .pixel_slot_tag1(pixel_slot_tag1),
        .pixel_slot_tag2(pixel_slot_tag2),
        .pixel_slot_tag3(pixel_slot_tag3)
    );

    integer countdown0 = 0;
    integer countdown1 = 0;
    integer countdown2 = 0;
    integer countdown3 = 0;
    reg [9:0] active_line = 10'd0;
    integer start_count [0:7];
    integer total_starts = 0;
    integer index;

    always @(posedge build_clk) begin
        client_done <= 4'd0;
        client_line_complete <= 4'd0;
        if (build_reset) begin
            countdown0 <= 0;
            countdown1 <= 0;
            countdown2 <= 0;
            countdown3 <= 0;
            active_line <= 10'd0;
        end else begin
            if (client_start) begin
                if (client_enable != 4'b1111)
                    $fatal(1, "scheduler launched wrong client mask %b",
                           client_enable);
                active_line <= client_line_y;
                start_count[client_line_y] <=
                    start_count[client_line_y] + 1;
                total_starts <= total_starts + 1;
                countdown0 <= 3;
                countdown1 <= 5;
                countdown2 <= 7;
                countdown3 <= 9;
            end
            if (countdown0 != 0) begin
                countdown0 <= countdown0 - 1;
                if (countdown0 == 1) begin
                    client_done[0] <= 1'b1;
                    client_line_complete[0] <= 1'b1;
                end
            end
            if (countdown1 != 0) begin
                countdown1 <= countdown1 - 1;
                if (countdown1 == 1) begin
                    client_done[1] <= 1'b1;
                    client_line_complete[1] <= active_line != 10'd5;
                end
            end
            if (countdown2 != 0) begin
                countdown2 <= countdown2 - 1;
                if (countdown2 == 1) begin
                    client_done[2] <= 1'b1;
                    client_line_complete[2] <= 1'b1;
                end
            end
            if (countdown3 != 0) begin
                countdown3 <= countdown3 - 1;
                if (countdown3 == 1) begin
                    client_done[3] <= 1'b1;
                    client_line_complete[3] <= 1'b1;
                end
            end
        end
    end

    task automatic pulse_scene_changed;
        begin
            @(negedge build_clk);
            scene_changed = 1'b1;
            @(negedge build_clk);
            scene_changed = 1'b0;
        end
    endtask

    task automatic wait_for_counts(
        input integer expected_built,
        input integer expected_failed
    );
        integer cycles;
        begin
            cycles = 0;
            while (lines_built < expected_built ||
                   lines_failed < expected_failed) begin
                @(posedge build_clk);
                cycles = cycles + 1;
                if (cycles > 1000)
                    $fatal(1, "scheduler completion timed out");
            end
        end
    endtask

    task automatic wait_for_pixel_slot(
        input integer slot,
        input integer tag,
        input integer valid
    );
        integer cycles;
        reg [9:0] observed_tag;
        begin
            cycles = 0;
            observed_tag = 10'h3ff;
            while (cycles < 100) begin
                @(posedge pixel_clk);
                #1;
                case (slot)
                    0: observed_tag = pixel_slot_tag0;
                    1: observed_tag = pixel_slot_tag1;
                    2: observed_tag = pixel_slot_tag2;
                    default: observed_tag = pixel_slot_tag3;
                endcase
                if (pixel_slot_valid[slot] === valid[0] &&
                    observed_tag == tag[9:0])
                    cycles = 1000;
                else
                    cycles = cycles + 1;
            end
            if (cycles != 1000)
                $fatal(1, "slot %0d did not reach tag=%0d valid=%0d",
                       slot, tag, valid);
        end
    endtask

    task automatic drive_line_end(input integer line);
        begin
            @(negedge pixel_clk);
            pixel_x = OUTPUT_WIDTH - 1;
            pixel_y = line[9:0];
            @(posedge pixel_clk);
            #1;
            @(negedge pixel_clk);
            pixel_x = 11'd0;
        end
    endtask

    integer built_before_hold;
    integer starts_before_quiesce;
    integer built_after_drain;
    integer quiesce_cycles;
    initial begin
        for (index = 0; index < 8; index = index + 1)
            start_count[index] = 0;

        repeat (5) @(posedge build_clk);
        repeat (3) @(posedge pixel_clk);
        pixel_reset = 1'b0;

        // Model the publication toggle arriving one pixel clock before its
        // separately synchronized payload. A stale tag must never publish.
        @(negedge pixel_clk);
        force dut.slot_toggle_sync = 4'b0001;
        force dut.slot_success_sync = 4'b0001;
        force dut.slot_tag0_sync = 10'd0;
        @(posedge pixel_clk);
        #1;
        if (dut.slot_capture_pending != 4'b0001 ||
            pixel_slot_valid[0] || pixel_slot_tag0 != 10'd0)
            $fatal(1, "slot payload captured with publication toggle");
        @(negedge pixel_clk);
        force dut.slot_tag0_sync = 10'd4;
        @(posedge pixel_clk);
        #1;
        if (dut.slot_capture_pending != 4'd0 ||
            !pixel_slot_valid[0] || pixel_slot_tag0 != 10'd4)
            $fatal(1, "settled slot payload was not captured");
        release dut.slot_toggle_sync;
        release dut.slot_success_sync;
        release dut.slot_tag0_sync;
        @(negedge pixel_clk);
        pixel_reset = 1'b1;
        repeat (3) @(posedge pixel_clk);
        @(negedge pixel_clk);
        pixel_reset = 1'b0;
        $display("bundled slot CDC skew pass");

        @(negedge build_clk);
        build_reset = 1'b0;

        line_prepare_ready = 1'b0;
        pulse_scene_changed();
        repeat (5) @(posedge build_clk);
        if (!line_prepare_valid || line_prepare_y != 0 || total_starts != 0)
            $fatal(1, "line preparation did not backpressure bootstrap");
        line_prepare_ready = 1'b1;
        wait_for_counts(4, 0);
        wait_for_pixel_slot(0, 0, 1);
        wait_for_pixel_slot(1, 1, 1);
        wait_for_pixel_slot(2, 2, 1);
        wait_for_pixel_slot(3, 3, 1);
        for (index = 0; index < 4; index = index + 1)
            if (start_count[index] != 1)
                $fatal(1, "bootstrap line %0d launched %0d times",
                       index, start_count[index]);
        $display("four-line scene bootstrap pass");

        // End of vertical blank selects the already complete line zero.
        drive_line_end(TOTAL_HEIGHT - 1);
        if (!pixel_line_available || pixel_read_slot != 2'd0)
            $fatal(1, "line zero was not selected after vblank");

        drive_line_end(0);
        wait_for_counts(5, 0);
        wait_for_pixel_slot(0, 4, 1);
        if (pixel_read_slot != 2'd1)
            $fatal(1, "line one was not selected");

        drive_line_end(1);
        wait_for_counts(5, 1);
        wait_for_pixel_slot(1, 5, 0);
        drive_line_end(2);
        wait_for_counts(6, 1);
        drive_line_end(3);
        wait_for_counts(7, 1);
        wait_for_pixel_slot(0, 4, 1);

        // Line five failed. At the end of line four the selector must repeat
        // complete line four and hold slot zero against the queued line-zero
        // rebuild until that repeated scanline has retired.
        drive_line_end(4);
        if (!pixel_line_available || pixel_read_slot != 2'd0 ||
            pixel_underruns != 32'd1)
            $fatal(1, "failed line did not select bounded repeat");
        built_before_hold = lines_built;
        repeat (40) @(posedge build_clk);
        if (start_count[0] != 1 || lines_built != built_before_hold)
            $fatal(1, "held repeat slot was overwritten");

        // Line six is complete, so retiring repeated line five releases slot
        // zero and allows the deferred line-zero request to run.
        drive_line_end(5);
        if (pixel_read_slot != 2'd2)
            $fatal(1, "line six did not release repeated slot");
        wait_for_counts(built_before_hold + 2, 1);
        wait_for_pixel_slot(0, 0, 1);
        if (scheduler_overruns != 32'd0)
            $fatal(1, "scheduler queue overran");
        $display("failure/repeat/hold/recovery pass");

        // A frame-boundary scene handoff stops new launches but lets the
        // already issued line finish. Retire events observed while quiesced
        // must not survive into the next scene epoch.
        starts_before_quiesce = total_starts;
        drive_line_end(6);
        quiesce_cycles = 0;
        while (total_starts == starts_before_quiesce &&
               quiesce_cycles < 100) begin
            @(posedge build_clk);
            quiesce_cycles = quiesce_cycles + 1;
        end
        if (total_starts == starts_before_quiesce)
            $fatal(1, "quiesce drain test never launched a line");
        @(negedge build_clk);
        quiesce = 1'b1;
        quiesce_cycles = 0;
        while (!scheduler_idle && quiesce_cycles < 100) begin
            @(posedge build_clk);
            quiesce_cycles = quiesce_cycles + 1;
        end
        if (!scheduler_idle)
            $fatal(1, "scheduler did not drain while quiesced");
        built_after_drain = lines_built;
        drive_line_end(7);
        repeat (40) @(posedge build_clk);
        if (!scheduler_idle || lines_built != built_after_drain ||
            total_starts != starts_before_quiesce + 1)
            $fatal(1, "scheduler launched work while quiesced");
        quiesce = 1'b0;
        $display("quiesce/drain pass");

        // A backdrop-only scene still publishes four complete slots and does
        // not deadlock the bootstrap state machine.
        framebuffer_enable = 1'b0;
        tile0_enable = 1'b0;
        tile1_enable = 1'b0;
        sprite_enable = 1'b0;
        pulse_scene_changed();
        wait_for_counts(built_before_hold + 6, 1);
        wait_for_pixel_slot(0, 0, 1);
        wait_for_pixel_slot(1, 1, 1);
        wait_for_pixel_slot(2, 2, 1);
        wait_for_pixel_slot(3, 3, 1);
        $display("zero-client backdrop bootstrap pass");

        $display("ASTRA LINE SCHEDULER PASS");
        $finish;
    end
endmodule

`default_nettype wire
