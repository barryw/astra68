// Full TG030 + pin-level SDRAM execution gate for the visual diagnostic ROM.
`timescale 1ns/1ps

module tb_graphics_demo #(
    parameter bit PRODUCTION_MAP = 1'b0,
    parameter bit DIAGNOSTIC_ONLY = 1'b0,
    parameter bit SDRAM_EXEC = 1'b0
);
    localparam integer LINE_DEADLINE_MEM_CYCLES = 1906;
    localparam integer DIAGNOSTIC_ROM_WORDS = 1024;
    localparam integer MAX_DMA_CACHE_FENCE_CYCLES = 256;
    localparam [24:0] STAGE2_SDRAM_OFFSET = 25'h1e00000;

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
        .USB_ENABLE(1'b0),
        .CPU_CLK_DIV_BIT(0),
        .SD_BOOT_ENABLE(PRODUCTION_MAP),
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

    reg [31:0] diagnostic_rom [0:DIAGNOSTIC_ROM_WORDS-1];
    integer diagnostic_word;
    reg [24:0] diagnostic_offset;
    reg [23:0] diagnostic_key;

    function automatic [23:0] model_key(input [24:0] byte_offset);
        model_key = {byte_offset[9], byte_offset[11:10],
                     byte_offset[24:12], byte_offset[8:2], 1'b0};
    endfunction

    initial begin
        if (SDRAM_EXEC) begin
            for (diagnostic_word = 0;
                 diagnostic_word < DIAGNOSTIC_ROM_WORDS;
                 diagnostic_word = diagnostic_word + 1)
                diagnostic_rom[diagnostic_word] = 32'd0;
            $readmemh("sdram_exec_rom.hex", diagnostic_rom);
            for (diagnostic_word = 0;
                 diagnostic_word < DIAGNOSTIC_ROM_WORDS;
                 diagnostic_word = diagnostic_word + 1) begin
                diagnostic_offset = STAGE2_SDRAM_OFFSET +
                                    diagnostic_word * 4;
                diagnostic_key = model_key(diagnostic_offset);
                memory.memory[diagnostic_key] = {
                    diagnostic_rom[diagnostic_word][23:16],
                    diagnostic_rom[diagnostic_word][31:24]
                };
                memory.memory[diagnostic_key + 1'b1] = {
                    diagnostic_rom[diagnostic_word][7:0],
                    diagnostic_rom[diagnostic_word][15:8]
                };
            end
        end
    end

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
                $display("GRAPHICS LINE MAX y=%0d total=%0d fetch=%0d sprite=%0d",
                         dut.vega_i.ready_y_mem,
                         mem_cycles - line_start_cycle,
                         fetch_done_cycles, sprite_done_cycles);
            end
        end
        if (previous_fetch_state != 4'd5 &&
            dut.vega_i.fetch_state_mem == 4'd5)
            fetch_done_cycles <= mem_cycles - line_start_cycle;
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
    integer dma_cache_fence_cycles = 0;
    integer max_dma_cache_fence_cycles = 0;
    reg cpu_sdram_during_dma_seen = 1'b0;
    reg unfenced_dma_seen = 1'b0;
    reg astraea_caps_read_seen = 1'b0;
    reg astraea_caps_response_pending = 1'b0;
    always @(posedge dut.clk) begin
        if (!rstn) begin
            timeout <= 0;
            dma_cache_fence_cycles <= 0;
            max_dma_cache_fence_cycles <= 0;
            cpu_sdram_during_dma_seen <= 1'b0;
            unfenced_dma_seen <= 1'b0;
            astraea_caps_read_seen <= 1'b0;
            astraea_caps_response_pending <= 1'b0;
        end else begin
            timeout <= timeout + 1;
            astraea_caps_response_pending <= 1'b0;
            if (dut.bs == 3'd1 && dut.cpu_rw_n && dut.sel_astraea &&
                dut.cpu_adr[15:0] == 16'h0018) begin
                astraea_caps_read_seen <= 1'b1;
                astraea_caps_response_pending <= 1'b1;
                if (dut.g_sdram_enabled.astraea_i.cpu_addr !== 16'h0018 ||
                    dut.astraea_rdata !== 32'h000000ff)
                    $fatal(1,
                           "Astraea SoC capability read mismatch bus=%08x device_addr=%04x device_data=%08x captured=%08x",
                           dut.cpu_adr,
                           dut.g_sdram_enabled.astraea_i.cpu_addr,
                           dut.astraea_rdata, dut.cpu_din);
            end
            if (astraea_caps_response_pending) begin
                if (dut.cpu_din !== 32'h000000ff ||
                    dut.cpu_din_visible !== 32'h000000ff)
                    $fatal(1,
                           "Astraea capability response was not retained for TG68K");
            end
            if (SDRAM_EXEC) begin
                if (dut.astraea_cache_flush) begin
                    dma_cache_fence_cycles <= dma_cache_fence_cycles + 1;
                    if (dma_cache_fence_cycles + 1 >
                        max_dma_cache_fence_cycles)
                        max_dma_cache_fence_cycles <=
                            dma_cache_fence_cycles + 1;
                end else begin
                    dma_cache_fence_cycles <= 0;
                end
                if (dut.astraea_dma_active && !dut.astraea_cache_flush)
                    unfenced_dma_seen <= 1'b1;
                if (dut.astraea_dma_active && !dut.astraea_cache_flush &&
                    dut.sdram_cpu_start)
                    cpu_sdram_during_dma_seen <= 1'b1;
                if (dma_cache_fence_cycles >=
                    MAX_DMA_CACHE_FENCE_CYCLES)
                    $fatal(1,
                           "graphics DMA cache fence exceeded bound cycles=%0d",
                           dma_cache_fence_cycles + 1);
            end
            if (leds[7]) begin
                $display("GRAPHICS FAIL stage=%0d LEDs=%02x pc=%08x",
                         leds[6:0], leds, dut.cpu_adr);
                $display("status underrun=%b config=%b sprite_err=%b",
                         dut.vega_i.underrun_sticky_cpu,
                         dut.vega_i.config_error_cpu,
                         dut.vega_i.sprite_config_error_mem);
                $display("states fetch=%0d sprite=%0d current_line=%0d",
                         dut.vega_i.fetch_state_mem,
                         dut.vega_i.sprite_builder_i.state,
                         mem_cycles - line_start_cycle);
                $display("line_cycles last=%0d max=%0d sprite=%0d",
                         last_line_cycles, max_line_cycles,
                         sprite_done_cycles);
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
                         dut.vega_i.sprite_builder_i.descriptor_mem0[0],
                         dut.vega_i.sprite_builder_i.descriptor_mem0[1],
                         dut.vega_i.sprite_builder_i.descriptor_mem0[2],
                         dut.vega_i.sprite_builder_i.descriptor_mem0[3],
                         dut.vega_i.sprite_builder_i.descriptor_mem0[4],
                         dut.vega_i.sprite_builder_i.descriptor_mem0[8],
                         dut.vega_i.sprite_builder_i.descriptor_mem0[9],
                         dut.vega_i.sprite_builder_i.descriptor_mem0[10],
                         dut.vega_i.sprite_builder_i.descriptor_mem0[11],
                         dut.vega_i.sprite_builder_i.descriptor_mem0[12]);
                $display("sprite pattern words=%04x %04x %04x %04x",
                         memory.memory[24'h049008],
                         memory.memory[24'h049009],
                         memory.memory[24'h04900a],
                         memory.memory[24'h04900b]);
                $fatal(1, "graphics ROM failed");
            end
            if (leds == 8'h5a) begin
                if (DIAGNOSTIC_ONLY) begin
                    if (!astraea_caps_read_seen)
                        $fatal(1, "Astraea capability read was not exercised");
                    if (SDRAM_EXEC &&
                        (!unfenced_dma_seen ||
                         !cpu_sdram_during_dma_seen))
                        $fatal(1,
                               "SDRAM CPU did not progress during graphics DMA unfenced=%b cpu_request=%b",
                               unfenced_dma_seen,
                               cpu_sdram_during_dma_seen);
                    $display("BLITTER CONFIG DIAGNOSTIC PASS fence_max=%0d cpu_during_dma=%b",
                             max_dma_cache_fence_cycles,
                             cpu_sdram_during_dma_seen);
                    $finish;
                end else begin
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
                    if (observed_sprite_active == 32'h0000ffff) begin
                        if (dut.vega_i.fb_format_mem == 3'd0) begin
                            if (dut.vega_i.sprite_budget_effective_mem != 16'd512)
                                $fatal(1, "RGB565 sprite budget was not clamped");
                            if (observed_sprite_accepted != 32'h0000ffff ||
                                dut.vega_i.sprite_overflow_cpu)
                                $fatal(1,
                                       "RGB565 admission mismatch accepted=%08x overflow=%b",
                                       observed_sprite_accepted,
                                       dut.vega_i.sprite_overflow_cpu);
                        end else begin
                            if (dut.vega_i.sprite_budget_effective_mem != 16'd1024)
                                $fatal(1, "INDEX8 sprite budget was unexpectedly limited");
                            if (observed_sprite_accepted != 32'h0000ffff ||
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
            end
            if (timeout > 32'd3000000) begin
                $display("VEGA PRESENT validate=%b step=%0d pending=%b copy=%0d/%b locked=%b invalid=%b done=%b generation=%08x/%08x",
                         dut.vega_i.present_validate_busy_cpu,
                         dut.vega_i.present_validate_step_cpu,
                         dut.vega_i.present_pending_cpu,
                         dut.vega_i.scene_copy_state,
                         dut.vega_i.scene_copy_present,
                         dut.vega_i.scene_locked,
                         dut.vega_i.present_invalid_sticky_cpu,
                         dut.vega_i.present_done_sticky_cpu,
                         dut.vega_i.completed_generation_cpu,
                         dut.vega_i.pending_generation_cpu);
                $display("VEGA RETIRE vblank=%b/%b/%b frame=%0d writers=%b fences draw=%08x/%08x blit=%08x/%08x",
                         dut.vega_i.vblank_toggle_pixel,
                         dut.vega_i.vblank_toggle_sync_cpu[1],
                         dut.vega_i.vblank_toggle_seen_cpu,
                         dut.vega_i.frame_counter_cpu,
                         dut.present_writers_idle,
                         dut.vega_i.draw_completed_fence,
                         dut.vega_i.pending_draw_fence_cpu,
                         dut.vega_i.blitter_completed_fence,
                         dut.vega_i.pending_blitter_fence_cpu);
                if (DIAGNOSTIC_ONLY) begin
                    $display("BLIT CPU pending=%b busy_sync=%b done=%b start=%b done_sync=%b seen=%b",
                             dut.g_sdram_enabled.astraea_i.blitter_i.start_pending_cpu,
                             dut.g_sdram_enabled.astraea_i.blitter_i.busy_sync_cpu,
                             dut.g_sdram_enabled.astraea_i.blitter_i.done_sticky_cpu,
                             dut.g_sdram_enabled.astraea_i.blitter_i.start_toggle_cpu,
                             dut.g_sdram_enabled.astraea_i.blitter_i.done_sync_cpu,
                             dut.g_sdram_enabled.astraea_i.blitter_i.done_seen_cpu);
                    $display("BLIT MEM busy=%b state=%0d start_sync=%b seen=%b done=%b error=%0d",
                             dut.g_sdram_enabled.astraea_i.blitter_i.busy_mem,
                             dut.g_sdram_enabled.astraea_i.blitter_i.state_mem,
                             dut.g_sdram_enabled.astraea_i.blitter_i.start_sync_mem,
                             dut.g_sdram_enabled.astraea_i.blitter_i.start_seen_mem,
                             dut.g_sdram_enabled.astraea_i.blitter_i.done_toggle_mem,
                             dut.g_sdram_enabled.astraea_i.blitter_i.error_mem);
                    $display("BLIT CFG cpu dim=%08x op=%08x valid=%b/%b fence=%08x",
                             dut.g_sdram_enabled.astraea_i.blitter_i.cfg_dim_cpu,
                             dut.g_sdram_enabled.astraea_i.blitter_i.cfg_op_cpu,
                             dut.g_sdram_enabled.astraea_i.blitter_i.cfg_fields_valid_cpu,
                             dut.g_sdram_enabled.astraea_i.blitter_i.cfg_op_valid_cpu,
                             dut.g_sdram_enabled.astraea_i.blitter_i.cfg_fence_cpu);
                    $display("BLIT CFG mem dim=%08x op=%08x valid=%b/%b pitch=%04x",
                             dut.g_sdram_enabled.astraea_i.blitter_i.cfg_dim_mem,
                             dut.g_sdram_enabled.astraea_i.blitter_i.cfg_op_mem,
                             dut.g_sdram_enabled.astraea_i.blitter_i.cfg_fields_valid_mem,
                             dut.g_sdram_enabled.astraea_i.blitter_i.cfg_op_valid_mem,
                             dut.g_sdram_enabled.astraea_i.blitter_i.cfg_dst_pitch_mem);
                end
                $fatal(1, "graphics ROM timeout pc=%08x leds=%02x state=%0d",
                       dut.cpu_adr, leds,
                       dut.g_sdram_enabled.astraea_i.draw_i.state);
            end
        end
    end

    always @(posedge dut.clk) begin
        if (SDRAM_EXEC && dut.bus_fault_capture_strobe)
            $fatal(1,
                   "SDRAM execution fault during graphics DMA status=%08x address=%08x target=%08x cache_fence=%b dma_owner=%0d blit_state=%0d",
                   dut.bus_fault_capture_status,
                   dut.bus_fault_capture_address,
                   dut.bus_fault_capture_target,
                   dut.astraea_cache_flush,
                   dut.g_sdram_enabled.dma_owner,
                   dut.g_sdram_enabled.astraea_i.blitter_i.state_mem);
    end
endmodule
