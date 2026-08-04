`timescale 1ns/1ps
`default_nettype none

module tb_astra_palette_store;
    reg control_clk = 1'b0;
    reg pixel_clk = 1'b0;
    always #3 control_clk = ~control_clk;
    always #7 pixel_clk = ~pixel_clk;

    reg control_reset = 1'b1;
    reg pixel_reset = 1'b1;
    reg baseline_restore_start = 1'b0;
    wire baseline_restore_busy;
    wire baseline_restore_done;
    wire host_write_ready;
    reg framebuffer_write_enable = 1'b0;
    reg [7:0] framebuffer_write_index = 8'd0;
    reg [31:0] framebuffer_write_argb = 32'd0;
    reg tile_write_enable = 1'b0;
    reg [3:0] tile_write_bank = 4'd0;
    reg [7:0] tile_write_index = 8'd0;
    reg [31:0] tile_write_argb = 32'd0;
    reg copper_write_enable = 1'b0;
    reg copper_write_tile = 1'b0;
    reg [3:0] copper_write_bank = 4'd0;
    reg [7:0] copper_write_index = 8'd0;
    reg [31:0] copper_write_argb = 32'd0;
    wire copper_write_ready;
    reg [7:0] framebuffer_read_index = 8'd0;
    wire [31:0] framebuffer_read_argb;
    reg [3:0] tile0_read_bank = 4'd0;
    reg [7:0] tile0_read_index = 8'd0;
    wire [31:0] tile0_read_argb;
    reg [3:0] tile1_read_bank = 4'd0;
    reg [7:0] tile1_read_index = 8'd0;
    wire [31:0] tile1_read_argb;

    astra_palette_store dut (.*);

    task automatic host_framebuffer_write(
        input [7:0] index,
        input [31:0] argb
    );
        begin
            @(negedge control_clk);
            while (!host_write_ready) @(negedge control_clk);
            framebuffer_write_index = index;
            framebuffer_write_argb = argb;
            framebuffer_write_enable = 1'b1;
            @(negedge control_clk);
            framebuffer_write_enable = 1'b0;
        end
    endtask

    task automatic host_tile_write(
        input [3:0] bank,
        input [7:0] index,
        input [31:0] argb
    );
        begin
            @(negedge control_clk);
            while (!host_write_ready) @(negedge control_clk);
            tile_write_bank = bank;
            tile_write_index = index;
            tile_write_argb = argb;
            tile_write_enable = 1'b1;
            @(negedge control_clk);
            tile_write_enable = 1'b0;
        end
    endtask

    task automatic wait_pixel_updates;
        begin
            repeat (12) @(posedge pixel_clk);
            #1;
        end
    endtask

    integer timeout;
    initial begin
        repeat (5) @(posedge control_clk);
        control_reset = 1'b0;
        pixel_reset = 1'b0;

        framebuffer_read_index = 8'h2a;
        tile0_read_bank = 4'h7;
        tile0_read_index = 8'h55;
        tile1_read_bank = 4'h7;
        tile1_read_index = 8'h55;
        host_framebuffer_write(8'h2a, 32'hff112233);
        host_tile_write(4'h7, 8'h55, 32'hff445566);
        wait_pixel_updates();
        if (framebuffer_read_argb != 32'hff112233 ||
            tile0_read_argb != 32'hff445566 ||
            tile1_read_argb != 32'hff445566)
            $fatal(1, "host baseline did not reach active palettes");

        // The write-through path must expose the new value on the exact
        // lookup cycle, not one source pixel later.
        @(negedge pixel_clk);
        if (!copper_write_ready)
            $fatal(1, "copper unexpectedly backpressured while idle");
        copper_write_enable = 1'b1;
        copper_write_tile = 1'b0;
        copper_write_index = 8'h2a;
        copper_write_argb = 32'hffaa5500;
        @(posedge pixel_clk);
        #1;
        if (framebuffer_read_argb != 32'hffaa5500)
            $fatal(1, "framebuffer copper write-through missed exact pixel");
        @(negedge pixel_clk);
        copper_write_enable = 1'b0;

        @(negedge pixel_clk);
        copper_write_enable = 1'b1;
        copper_write_tile = 1'b1;
        copper_write_bank = 4'h7;
        copper_write_index = 8'h55;
        copper_write_argb = 32'hff00aa55;
        @(posedge pixel_clk);
        #1;
        if (tile0_read_argb != 32'hff00aa55 ||
            tile1_read_argb != 32'hff00aa55)
            $fatal(1, "tile copper write-through missed exact pixel");
        @(negedge pixel_clk);
        copper_write_enable = 1'b0;

        @(negedge control_clk);
        baseline_restore_start = 1'b1;
        @(negedge control_clk);
        baseline_restore_start = 1'b0;
        if (!baseline_restore_busy || host_write_ready)
            $fatal(1, "restore did not apply host backpressure");

        timeout = 0;
        while (!baseline_restore_done && timeout < 30000) begin
            @(posedge control_clk);
            timeout = timeout + 1;
        end
        if (!baseline_restore_done || baseline_restore_busy)
            $fatal(1, "baseline restore did not acknowledge completion");
        repeat (3) @(posedge pixel_clk);
        #1;
        if (framebuffer_read_argb != 32'hff112233 ||
            tile0_read_argb != 32'hff445566 ||
            tile1_read_argb != 32'hff445566)
            $fatal(1, "vblank baseline restore did not undo copper state");

        $display("ASTRA PALETTE STORE PASS restore_clocks=%0d", timeout);
        $finish;
    end
endmodule

`default_nettype wire
