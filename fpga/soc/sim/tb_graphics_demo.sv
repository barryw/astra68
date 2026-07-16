// Full TG030 + pin-level SDRAM execution gate for the visual diagnostic ROM.
`timescale 1ns/1ps

module tb_graphics_demo;
    localparam integer LINE_DEADLINE_MEM_CYCLES = 2383;

    reg clk25 = 1'b0;
    reg rstn = 1'b0;
    always #20 clk25 = ~clk25;

    wire [7:0] leds;
    wire [3:0] gpdi;
    wire sdram_clk;
    wire sdram_cke;
    wire sdram_csn;
    wire sdram_wen;
    wire sdram_rasn;
    wire sdram_casn;
    wire [1:0] sdram_ba;
    wire [1:0] sdram_dqm;
    wire [12:0] sdram_a;
    wire [15:0] sdram_d;

    astra_soc #(
        .RST_MAX(16'd16),
        .ROM_WORDS(1024),
        .SDRAM_ENABLE(1'b1),
        .SDRAM_READY_DELAY(1000),
        .HDMI_ENABLE(1'b1),
        .CPU_CLK_DIV_BIT(0),
        .SD_BOOT_ENABLE(1'b0),
        .ASTRA_HOST_ENABLE(1'b0)
    ) dut (
        .clk25_mhz(clk25), .reset_n(rstn),
        .buttons(6'd0), .switches(4'd0),
        .ftdi_rxd(), .ftdi_txd(1'b1), .leds(leds), .gpdi_dp(gpdi),
        .sdram_clk(sdram_clk), .sdram_cke(sdram_cke),
        .sdram_csn(sdram_csn), .sdram_wen(sdram_wen),
        .sdram_rasn(sdram_rasn), .sdram_casn(sdram_casn),
        .sdram_ba(sdram_ba), .sdram_dqm(sdram_dqm),
        .sdram_a(sdram_a), .sdram_d(sdram_d)
    );

    wire [15:0] model_dq;
    wire model_dq_oe;
    assign sdram_d = model_dq_oe ? model_dq : 16'hzzzz;
    astra_sdram_model memory (
        .sdram_clk(sdram_clk), .cke(sdram_cke), .cs(sdram_csn),
        .ras(sdram_rasn), .cas(sdram_casn), .we(sdram_wen),
        .addr(sdram_a), .ba(sdram_ba), .dqm(sdram_dqm),
        .dq_in(sdram_d), .dq_out(model_dq), .dq_oe(model_dq_oe)
    );

    initial begin
        repeat (20) @(posedge clk25);
        rstn = 1'b1;
    end

    integer copper_moves = 0;
    integer color_changes = 0;
    reg [23:0] last_rgb = 24'd0;
    always @(posedge dut.clk) begin
        if (dut.astraea_cop_move_stb)
            copper_moves <= copper_moves + 1;
    end
    always @(posedge dut.video_pixel_clk) begin
        if (dut.vega_graphics_active && dut.vega_rgb != last_rgb) begin
            last_rgb <= dut.vega_rgb;
            color_changes <= color_changes + 1;
        end
    end

    integer mem_cycles = 0;
    integer line_start_cycle = 0;
    integer last_line_cycles = 0;
    integer max_line_cycles = 0;
    integer tile_start_cycle = 0;
    integer tile_done_cycles = 0;
    integer sprite_start_cycle = 0;
    integer sprite_done_cycles = 0;
    integer fetch_done_cycles = 0;
    integer max_line_y = 0;
    reg [31:0] observed_sprite_active = 32'd0;
    reg [31:0] observed_sprite_accepted = 32'd0;
    reg [31:0] observed_collision = 32'd0;
    reg [3:0] previous_fetch_state = 4'd0;
    reg previous_ready_toggle = 1'b0;
    always @(posedge dut.g_sdram_enabled.sd_domain_clk) begin
        mem_cycles <= mem_cycles + 1;
        previous_fetch_state <= dut.vega_i.fetch_state_mem;
        if (previous_fetch_state == 4'd0 &&
            dut.vega_i.fetch_state_mem != 4'd0)
            line_start_cycle <= mem_cycles;
        if (dut.vega_i.ready_toggle_mem != previous_ready_toggle) begin
            previous_ready_toggle <= dut.vega_i.ready_toggle_mem;
            last_line_cycles <= mem_cycles - line_start_cycle;
            if (mem_cycles - line_start_cycle > max_line_cycles) begin
                max_line_cycles <= mem_cycles - line_start_cycle;
                max_line_y <= dut.vega_i.ready_y_mem;
                $display("GRAPHICS LINE MAX y=%0d total=%0d fetch=%0d tile=%0d sprite=%0d",
                         dut.vega_i.ready_y_mem,
                         mem_cycles - line_start_cycle,
                         fetch_done_cycles, tile_done_cycles,
                         sprite_done_cycles);
            end
        end
        if (previous_fetch_state != 4'd5 &&
            dut.vega_i.fetch_state_mem == 4'd5)
            fetch_done_cycles <= mem_cycles - line_start_cycle;
        if (dut.vega_i.tile_start_mem)
            tile_start_cycle <= mem_cycles;
        if (dut.vega_i.tile_done_mem)
            tile_done_cycles <= mem_cycles - tile_start_cycle;
        if (dut.vega_i.sprite_start_mem)
            sprite_start_cycle <= mem_cycles;
        if (dut.vega_i.sprite_done_mem)
            sprite_done_cycles <= mem_cycles - sprite_start_cycle;
        if (dut.vega_i.sprite_builder_i.eval_final_valid &&
            dut.vega_i.sprite_builder_i.eval_final_accept)
            observed_sprite_active <= observed_sprite_active |
                (32'd1 << dut.vega_i.sprite_builder_i.eval_final_index);
        if (dut.vega_i.sprite_builder_i.state == 6'd17 &&
            (dut.vega_i.sprite_builder_i.budget_remaining[15:10] != 6'd0 ||
             dut.vega_i.sprite_builder_i.active_meta_width <=
                dut.vega_i.sprite_builder_i.budget_remaining[9:0]))
            observed_sprite_accepted <= observed_sprite_accepted |
                (32'd1 << dut.vega_i.sprite_builder_i.render_sprite_index);
        observed_collision <= observed_collision |
                              dut.vega_i.sprite_builder_i.collision_bitmap_mem;
    end

    integer timeout = 0;
    always @(posedge dut.clk) begin
        if (!rstn) begin
            timeout <= 0;
        end else begin
            timeout <= timeout + 1;
            if (leds[7]) begin
                $display("GRAPHICS FAIL stage=%0d LEDs=%02x pc=%08x",
                         leds[6:0], leds, dut.cpu_adr);
                $display("status underrun=%b config=%b tile_err=%b sprite_err=%b",
                         dut.vega_i.underrun_sticky_cpu,
                         dut.vega_i.config_error_cpu,
                         dut.vega_i.tile_config_error_mem,
                         dut.vega_i.sprite_config_error_mem);
                $display("states fetch=%0d tile=%0d sprite=%0d current_line=%0d",
                         dut.vega_i.fetch_state_mem,
                         dut.vega_i.tile_builder_i.state,
                         dut.vega_i.sprite_builder_i.state,
                         mem_cycles - line_start_cycle);
                $display("line_cycles last=%0d max=%0d tile=%0d sprite=%0d",
                         last_line_cycles, max_line_cycles,
                         tile_done_cycles, sprite_done_cycles);
                $display("max_line_y=%0d fetch=%0d",
                         max_line_y, fetch_done_cycles);
                $display("collision cpu=%08x published=%08x accumulated=%08x",
                         dut.vega_i.sprite_collision_cpu,
                         dut.vega_i.sprite_builder_i.collision_published_mem,
                         dut.vega_i.sprite_builder_i.collision_bitmap_mem);
                $display("sprite observed active=%08x accepted=%08x collision=%08x",
                         observed_sprite_active, observed_sprite_accepted,
                         observed_collision);
                $display("sprite d0=%08x/%08x/%08x/%08x/%08x d1=%08x/%08x/%08x/%08x/%08x",
                         dut.vega_i.sprite_builder_i.descriptor_mem[0],
                         dut.vega_i.sprite_builder_i.descriptor_mem[1],
                         dut.vega_i.sprite_builder_i.descriptor_mem[2],
                         dut.vega_i.sprite_builder_i.descriptor_mem[3],
                         dut.vega_i.sprite_builder_i.descriptor_mem[4],
                         dut.vega_i.sprite_builder_i.descriptor_mem[8],
                         dut.vega_i.sprite_builder_i.descriptor_mem[9],
                         dut.vega_i.sprite_builder_i.descriptor_mem[10],
                         dut.vega_i.sprite_builder_i.descriptor_mem[11],
                         dut.vega_i.sprite_builder_i.descriptor_mem[12]);
                $display("sprite pattern words=%04x %04x %04x %04x",
                         memory.memory[24'h049008],
                         memory.memory[24'h049009],
                         memory.memory[24'h04900a],
                         memory.memory[24'h04900b]);
                $fatal(1, "graphics ROM failed");
            end
            if (leds == 8'h5a) begin
                if (dut.vega_i.config_error_cpu ||
                    dut.vega_i.underrun_sticky_cpu)
                    $fatal(1, "Vega completed with error status=%08x",
                           dut.vega_rdata);
                if ((dut.vega_i.sprite_collision_cpu & 32'h3) != 32'h3)
                    $fatal(1, "sprite collision path was not exercised");
                if (copper_moves < 8)
                    $fatal(1, "copper raster list was not exercised moves=%0d",
                           copper_moves);
                if (color_changes < 32)
                    $fatal(1, "compositor produced too little variation=%0d",
                           color_changes);
                if (max_line_cycles >= LINE_DEADLINE_MEM_CYCLES)
                    $fatal(1,
                           "scanline deadline missed cycles=%0d deadline=%0d",
                           max_line_cycles, LINE_DEADLINE_MEM_CYCLES);
                if (observed_sprite_active == 32'hffffffff) begin
                    if (dut.vega_i.fb_format_mem == 3'd0) begin
                        if (dut.vega_i.sprite_budget_effective_mem != 16'd512)
                            $fatal(1, "RGB565 sprite budget was not clamped");
                        if (observed_sprite_accepted != 32'h0000ffff ||
                            !dut.vega_i.sprite_overflow_cpu)
                            $fatal(1,
                                   "RGB565 admission mismatch accepted=%08x overflow=%b",
                                   observed_sprite_accepted,
                                   dut.vega_i.sprite_overflow_cpu);
                    end else begin
                        if (dut.vega_i.sprite_budget_effective_mem != 16'd1024)
                            $fatal(1, "INDEX8 sprite budget was unexpectedly limited");
                        if (observed_sprite_accepted != 32'hffffffff ||
                            dut.vega_i.sprite_overflow_cpu)
                            $fatal(1,
                                   "INDEX8 admission mismatch accepted=%08x overflow=%b",
                                   observed_sprite_accepted,
                                   dut.vega_i.sprite_overflow_cpu);
                    end
                end
                $display("GRAPHICS DEMO PASS copper_moves=%0d colors=%0d fence=%0d max_line_cycles=%0d",
                         copper_moves, color_changes,
                         dut.g_sdram_enabled.astraea_i.draw_i.completed_fence_mem,
                         max_line_cycles);
                $finish;
            end
            if (timeout > 32'd3000000)
                $fatal(1, "graphics ROM timeout pc=%08x leds=%02x state=%0d",
                       dut.cpu_adr, leds,
                       dut.g_sdram_enabled.astraea_i.draw_i.state);
        end
    end
endmodule
