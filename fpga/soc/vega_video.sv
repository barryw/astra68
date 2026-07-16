// Vega display core. The bootstrap text console remains a separate BRAM-only
// rescue path; setting DISPLAY_EN selects this SDRAM-backed graphics pipeline.
`default_nettype none

module vega_video (
    input  wire        cpu_clk,
    input  wire        cpu_rst,
    input  wire        cpu_write_stb,
    input  wire        cpu_read_stb,
    input  wire [15:0] cpu_addr,
    input  wire [3:0]  cpu_be,
    input  wire [31:0] cpu_wdata,
    output reg  [31:0] cpu_rdata,
    output wire        cpu_irq,
    input  wire        display_ready,

    // Copper writes use the same register semantics and CPU clock. CPU writes
    // win the unlikely same-cycle collision.
    input  wire        cop_write_stb,
    input  wire [15:0] cop_addr,
    input  wire [31:0] cop_wdata,

    input  wire        mem_clk,
    input  wire        mem_rst,
    output wire        mem_lock,
    output wire        mem_valid,
    input  wire        mem_ready,
    output wire        mem_write,
    output wire [24:0] mem_addr,
    output wire [3:0]  mem_be,
    output wire [31:0] mem_wdata,
    input  wire        mem_rsp_valid,
    input  wire [31:0] mem_rdata,

    input  wire        pixel_clk,
    input  wire        pixel_rst,
    input  wire [9:0]  pixel_x,
    input  wire [9:0]  pixel_y,
    input  wire [23:0] post_rgb,
    output wire [23:0] rgb,
    output wire        graphics_active,
    output wire [9:0]  beam_x,
    output wire [9:0]  beam_y
);
    localparam [31:0] VEGA_ID = 32'h56454741;
    localparam [31:0] VEGA_VERSION = 32'h00030000;

    localparam [31:0] CAP_POST_TEXT = 32'h00000001;
    localparam [31:0] CAP_FRAMEBUFFER = 32'h00000002;
    localparam [31:0] CAP_PALETTE = 32'h00000004;
    localparam [31:0] CAP_TILEMAP = 32'h00000008;
    localparam [31:0] CAP_SPRITE = 32'h00000010;
    localparam [31:0] CAP_INDEX8 = 32'h00000020;

    localparam [3:0] FETCH_IDLE = 4'd0;
    localparam [3:0] FETCH_PREP = 4'd1;
    localparam [3:0] FETCH_ISSUE = 4'd2;
    localparam [3:0] FETCH_WAIT = 4'd3;
    localparam [3:0] FETCH_YIELD = 4'd4;
    localparam [3:0] FETCH_BUILD_WAIT = 4'd5;

    localparam [1:0] MEM_OWNER_NONE = 2'd0;
    localparam [1:0] MEM_OWNER_FB = 2'd1;
    localparam [1:0] MEM_OWNER_TILE = 2'd2;
    localparam [1:0] MEM_OWNER_SPRITE = 2'd3;

    // Admission limits measured with the full 720-pixel scanline workload.
    // RGB565 consumes twice the framebuffer bandwidth of INDEX8, so its
    // hardware ceiling leaves enough time for both tile layers to complete.
    localparam [15:0] SPRITE_BUDGET_MAX = 16'd1024;
    localparam [15:0] SPRITE_BUDGET_RGB565 = 16'd512;

    wire regs_cpu_rst;
    wire sprite_cpu_rst;
    wire fetch_mem_rst;
    wire tile_mem_rst;
    wire sprite_mem_rst;

    // Keep the top-level reset as a small asynchronous assertion tree. Each
    // large physical subsystem releases through its own local synchronizer.
    vega_reset_release regs_cpu_reset_i (
        .clk(cpu_clk), .assert_reset(cpu_rst), .reset(regs_cpu_rst)
    );
    vega_reset_release sprite_cpu_reset_i (
        .clk(cpu_clk), .assert_reset(cpu_rst), .reset(sprite_cpu_rst)
    );
    vega_reset_release fetch_mem_reset_i (
        .clk(mem_clk), .assert_reset(mem_rst), .reset(fetch_mem_rst)
    );
    vega_reset_release tile_mem_reset_i (
        .clk(mem_clk), .assert_reset(mem_rst), .reset(tile_mem_rst)
    );
    vega_reset_release sprite_mem_reset_i (
        .clk(mem_clk), .assert_reset(mem_rst), .reset(sprite_mem_rst)
    );

    function automatic [31:0] merge_be(
        input [31:0] old_value,
        input [31:0] new_value,
        input [3:0] enables
    );
        reg [31:0] value;
        begin
            value = old_value;
            if (enables[3]) value[31:24] = new_value[31:24];
            if (enables[2]) value[23:16] = new_value[23:16];
            if (enables[1]) value[15:8] = new_value[15:8];
            if (enables[0]) value[7:0] = new_value[7:0];
            merge_be = value;
        end
    endfunction

    function automatic [23:0] rgb565_to_rgb888(input [15:0] value);
        begin
            rgb565_to_rgb888 = {
                value[15:11], value[15:13],
                value[10:5], value[10:9],
                value[4:0], value[4:2]
            };
        end
    endfunction

    reg [4:0]  reg_ctrl;
    reg [2:0]  reg_irq_en;
    reg [2:0]  reg_irq_stat;
    reg [2:0]  reg_mode;
    reg [15:0] reg_raster_cmp;
    reg [23:0] reg_backdrop;
    reg [31:0] reg_fb_base_staged;
    reg [31:0] reg_fb_base_active;
    reg [31:0] reg_fb_pitch;
    reg [31:0] reg_fb_format;
    reg [15:0] reg_fb_colorkey;
    reg        flip_pending_cpu;
    reg [19:0] reg_tile_ctrl [0:1];
    reg [31:0] reg_tile_map [0:1];
    reg [31:0] reg_tile_set [0:1];
    reg [31:0] reg_tile_size [0:1];
    reg [31:0] reg_tile_scroll [0:1];
    reg        reg_spr_ctrl;
    reg [15:0] reg_spr_budget;
    reg [15:0] active_width_cpu;
    reg [15:0] active_height_cpu;

    wire use_cpu_write = cpu_write_stb;
    wire use_cop_write = cop_write_stb && !cpu_write_stb;
    wire write_stb = use_cpu_write || use_cop_write;
    wire [15:0] write_addr = use_cpu_write ? cpu_addr : cop_addr;
    wire [31:0] write_data = use_cpu_write ? cpu_wdata : cop_wdata;
    wire [3:0] write_be = use_cpu_write ? cpu_be : 4'b1111;
    wire [31:0] merged_ctrl = merge_be(
        {27'd0, reg_ctrl}, write_data, write_be);
    wire [31:0] merged_irq_en = merge_be(
        {29'd0, reg_irq_en}, write_data, write_be);
    wire [31:0] merged_irq_stat = merge_be(
        32'd0, write_data, write_be);
    wire [31:0] merged_mode = merge_be(
        {29'd0, reg_mode}, write_data, write_be);
    wire [31:0] merged_raster_cmp = merge_be(
        {16'd0, reg_raster_cmp}, write_data, write_be);
    wire [31:0] merged_backdrop = merge_be(
        {8'd0, reg_backdrop}, write_data, write_be);
    wire [31:0] merged_fb_colorkey = merge_be(
        {16'd0, reg_fb_colorkey}, write_data, write_be);
    wire [31:0] merged_tile_ctrl = merge_be(
        {12'd0, reg_tile_ctrl[write_addr[5]]}, write_data, write_be);

    // Palette storage is already part of the ABI. The compositor consumes it
    // when indexed tile and sprite layers are enabled in the next slice.
    (* ram_style = "block" *) reg [31:0] palette_mem [0:255];
    (* ram_style = "block" *) reg [31:0] palette_tile1_mem [0:255];
    (* ram_style = "block" *) reg [31:0] palette_sprite_mem [0:255];
    (* ram_style = "block" *) reg [31:0] palette_framebuffer_mem [0:255];
    reg [31:0] palette_cpu_q;
    integer palette_init;
    initial begin
        for (palette_init = 0; palette_init < 256;
             palette_init = palette_init + 1)
            palette_mem[palette_init] = 32'd0;
        for (palette_init = 0; palette_init < 256;
             palette_init = palette_init + 1)
            palette_tile1_mem[palette_init] = 32'd0;
        for (palette_init = 0; palette_init < 256;
             palette_init = palette_init + 1)
            palette_sprite_mem[palette_init] = 32'd0;
        for (palette_init = 0; palette_init < 256;
             palette_init = palette_init + 1)
            palette_framebuffer_mem[palette_init] = 32'd0;
    end

    wire palette_select = cpu_addr[15:10] == 6'b000001;
    wire [7:0] palette_cpu_index = cpu_addr[9:2];
    wire palette_write = write_stb && write_addr[15:10] == 6'b000001;
    wire [7:0] palette_write_index = write_addr[9:2];
    wire sprite_table_select = cpu_addr >= 16'h1000 &&
                               cpu_addr < 16'h1400;
    wire sprite_table_write = write_stb && write_addr >= 16'h1000 &&
                              write_addr < 16'h1400;

    reg [1:0] vblank_toggle_sync_cpu;
    reg       vblank_toggle_seen_cpu;
    reg [1:0] raster_toggle_sync_cpu;
    reg       raster_toggle_seen_cpu;
    reg [1:0] underrun_toggle_sync_cpu;
    reg       underrun_toggle_seen_cpu;
    reg       underrun_sticky_cpu;
    reg [9:0] beam_x_meta_cpu;
    reg [9:0] beam_x_cpu;
    reg [9:0] beam_y_meta_cpu;
    reg [9:0] beam_y_cpu;
    reg [1:0] vblank_level_sync_cpu;
    reg [1:0] hblank_level_sync_cpu;
    reg [1:0] fetch_busy_sync_cpu;
    reg [1:0] tile_error_sync_cpu;
    reg [1:0] sprite_error_sync_cpu;
    reg       fetch_busy_mem;
    reg       ready_bank_mem;
    reg [9:0] ready_y_mem;
    reg       ready_toggle_mem;
    wire      tile_config_error_mem;
    wire      sprite_config_error_mem;

    wire sprite_overflow_cpu;
    wire [31:0] sprite_collision_cpu;
    wire sprite_collision_event_cpu;
    wire [31:0] sprite_table_rdata;

    reg vblank_toggle_pixel;
    reg raster_toggle_pixel;
    reg underrun_toggle_pixel;
    wire vblank_level_pixel = pixel_y >= 10'd480;
    wire hblank_level_pixel = pixel_x >= 10'd720;
    wire [16:0] fb_min_pitch_cpu = reg_fb_format[2:0] == 3'd1 ?
                                    {1'b0, active_width_cpu} :
                                    {active_width_cpu, 1'b0};
    wire [32:0] fb_end_cpu = {1'b0, reg_fb_base_active} +
        (active_height_cpu - 16'd1) * reg_fb_pitch[15:0] +
        fb_min_pitch_cpu;
    wire fb_config_error_cpu = reg_ctrl[0] && reg_ctrl[1] &&
        (reg_fb_format[31:3] != 29'd0 ||
         reg_fb_format[2:0] > 3'd1 ||
         reg_fb_base_active[31:25] != 7'd0 ||
         reg_fb_base_active[1:0] != 2'b00 ||
         reg_fb_pitch[31:16] != 16'd0 ||
         reg_fb_pitch[1:0] != 2'b00 ||
         reg_fb_pitch[15:0] < fb_min_pitch_cpu[15:0] ||
         fb_end_cpu > 33'h02000000);
    wire tile_register_error_cpu =
        (reg_tile_ctrl[0][0] &&
         (reg_tile_map[0][31:25] != 7'd0 ||
          reg_tile_set[0][31:25] != 7'd0 ||
          reg_tile_size[0][31:8] != 24'd0)) ||
        (reg_tile_ctrl[1][0] &&
         (reg_tile_map[1][31:25] != 7'd0 ||
          reg_tile_set[1][31:25] != 7'd0 ||
          reg_tile_size[1][31:8] != 24'd0));
    wire config_error_cpu = fb_config_error_cpu || tile_register_error_cpu ||
                            tile_error_sync_cpu[1] ||
                            sprite_error_sync_cpu[1];

    assign beam_x = pixel_x;
    assign beam_y = pixel_y;
    assign cpu_irq = |(reg_irq_en[2:0] & reg_irq_stat[2:0]);

    always @(posedge cpu_clk) begin
        // Palette entries are atomic 32-bit MMIO registers. Avoiding a
        // read-modify-write here keeps each replica a true dual-port BRAM:
        // CPU/copper write on one port, pixel lookup on the other.
        if (palette_write && write_be == 4'b1111) begin
            palette_mem[palette_write_index] <= write_data & 32'h00ffffff;
            palette_tile1_mem[palette_write_index] <=
                write_data & 32'h00ffffff;
            palette_sprite_mem[palette_write_index] <=
                write_data & 32'h00ffffff;
            palette_framebuffer_mem[palette_write_index] <=
                write_data & 32'h00ffffff;
        end else begin
            palette_cpu_q <= palette_mem[palette_cpu_index];
        end

        if (regs_cpu_rst) begin
            reg_ctrl <= 5'd0;
            reg_irq_en <= 3'd0;
            reg_irq_stat <= 3'd0;
            reg_mode <= 3'd0;
            reg_raster_cmp <= 16'd0;
            reg_backdrop <= 24'h101820;
            reg_fb_base_staged <= 32'd0;
            reg_fb_base_active <= 32'd0;
            reg_fb_pitch <= 32'd1440;
            reg_fb_format <= 32'd0;
            reg_fb_colorkey <= 16'd0;
            flip_pending_cpu <= 1'b0;
            reg_tile_ctrl[0] <= 20'd0;
            reg_tile_ctrl[1] <= 20'd0;
            reg_tile_map[0] <= 32'd0;
            reg_tile_map[1] <= 32'd0;
            reg_tile_set[0] <= 32'd0;
            reg_tile_set[1] <= 32'd0;
            reg_tile_size[0] <= 32'd0;
            reg_tile_size[1] <= 32'd0;
            reg_tile_scroll[0] <= 32'd0;
            reg_tile_scroll[1] <= 32'd0;
            reg_spr_ctrl <= 1'b0;
            reg_spr_budget <= 16'd1024;
            vblank_toggle_sync_cpu <= 2'b00;
            vblank_toggle_seen_cpu <= 1'b0;
            raster_toggle_sync_cpu <= 2'b00;
            raster_toggle_seen_cpu <= 1'b0;
            underrun_toggle_sync_cpu <= 2'b00;
            underrun_toggle_seen_cpu <= 1'b0;
            underrun_sticky_cpu <= 1'b0;
            beam_x_meta_cpu <= 10'd0;
            beam_x_cpu <= 10'd0;
            beam_y_meta_cpu <= 10'd0;
            beam_y_cpu <= 10'd0;
            vblank_level_sync_cpu <= 2'b00;
            hblank_level_sync_cpu <= 2'b00;
            fetch_busy_sync_cpu <= 2'b00;
            tile_error_sync_cpu <= 2'b00;
            sprite_error_sync_cpu <= 2'b00;
        end else begin
            vblank_toggle_sync_cpu <= {vblank_toggle_sync_cpu[0],
                                        vblank_toggle_pixel};
            raster_toggle_sync_cpu <= {raster_toggle_sync_cpu[0],
                                        raster_toggle_pixel};
            underrun_toggle_sync_cpu <= {underrun_toggle_sync_cpu[0],
                                          underrun_toggle_pixel};
            beam_x_meta_cpu <= pixel_x;
            beam_x_cpu <= beam_x_meta_cpu;
            beam_y_meta_cpu <= pixel_y;
            beam_y_cpu <= beam_y_meta_cpu;
            vblank_level_sync_cpu <= {vblank_level_sync_cpu[0],
                                      vblank_level_pixel};
            hblank_level_sync_cpu <= {hblank_level_sync_cpu[0],
                                      hblank_level_pixel};
            fetch_busy_sync_cpu <= {fetch_busy_sync_cpu[0], fetch_busy_mem};
            tile_error_sync_cpu <= {tile_error_sync_cpu[0],
                                    tile_config_error_mem};
            sprite_error_sync_cpu <= {sprite_error_sync_cpu[0],
                                      sprite_config_error_mem};

            if (vblank_toggle_sync_cpu[1] != vblank_toggle_seen_cpu) begin
                vblank_toggle_seen_cpu <= vblank_toggle_sync_cpu[1];
                reg_irq_stat[0] <= 1'b1;
                if (flip_pending_cpu) begin
                    reg_fb_base_active <= reg_fb_base_staged;
                    flip_pending_cpu <= 1'b0;
                end
            end
            if (raster_toggle_sync_cpu[1] != raster_toggle_seen_cpu) begin
                raster_toggle_seen_cpu <= raster_toggle_sync_cpu[1];
                reg_irq_stat[1] <= 1'b1;
            end
            if (underrun_toggle_sync_cpu[1] != underrun_toggle_seen_cpu) begin
                underrun_toggle_seen_cpu <= underrun_toggle_sync_cpu[1];
                underrun_sticky_cpu <= 1'b1;
            end
            if (sprite_collision_event_cpu)
                reg_irq_stat[2] <= 1'b1;

            if (write_stb && write_addr < 16'h0400) begin
                case (write_addr[9:2])
                    8'h02: reg_ctrl <= merged_ctrl[4:0];
                    8'h04: reg_irq_en <= merged_irq_en[2:0];
                    8'h05: reg_irq_stat <= reg_irq_stat &
                                             ~merged_irq_stat[2:0];
                    8'h06: reg_mode <= merged_mode[2:0];
                    8'h09: reg_raster_cmp <= merged_raster_cmp[15:0];
                    8'h0c: reg_backdrop <= merged_backdrop[23:0];
                    8'h10: begin
                        reg_fb_base_staged <= merge_be(reg_fb_base_staged,
                                                       write_data, write_be);
                        flip_pending_cpu <= 1'b1;
                    end
                    8'h11: reg_fb_pitch <= merge_be(reg_fb_pitch, write_data,
                                                     write_be);
                    8'h12: reg_fb_format <= merge_be(reg_fb_format, write_data,
                                                      write_be);
                    8'h13: reg_fb_colorkey <=
                        merged_fb_colorkey[15:0];
                    default: begin end
                endcase
            end

            if (write_stb && write_addr >= 16'h0080 &&
                write_addr < 16'h00c0) begin
                case (write_addr[4:2])
                    3'd0: reg_tile_ctrl[write_addr[5]] <=
                        merged_tile_ctrl[19:0] & 20'hf003f;
                    3'd1: reg_tile_map[write_addr[5]] <=
                        merge_be(reg_tile_map[write_addr[5]],
                                 write_data, write_be);
                    3'd2: reg_tile_set[write_addr[5]] <=
                        merge_be(reg_tile_set[write_addr[5]],
                                 write_data, write_be);
                    3'd3: reg_tile_size[write_addr[5]] <=
                        merge_be(reg_tile_size[write_addr[5]],
                                 write_data, write_be);
                    3'd4: reg_tile_scroll[write_addr[5]] <=
                        merge_be(reg_tile_scroll[write_addr[5]], write_data,
                                 write_be);
                    default: begin end
                endcase
            end

            if (write_stb && write_addr >= 16'h0800 &&
                write_addr < 16'h080c) begin
                case (write_addr[3:2])
                    2'd0: if (write_be[0])
                        reg_spr_ctrl <= write_data[0];
                    2'd1: reg_spr_budget <=
                        merge_be({16'd0, reg_spr_budget}, write_data,
                                 write_be);
                    default: begin end
                endcase
            end

            // Writing one to the sticky underrun status bit clears it.
            if (write_stb && write_addr[9:2] == 8'h03 && write_be[0] &&
                write_data[5])
                underrun_sticky_cpu <= 1'b0;
        end
    end

    always @* begin
        case (reg_mode[2:0])
            3'd1: begin active_width_cpu = 16'd640;
                        active_height_cpu = 16'd480; end
            3'd2: begin active_width_cpu = 16'd320;
                        active_height_cpu = 16'd240; end
            3'd3: begin active_width_cpu = 16'd320;
                        active_height_cpu = 16'd200; end
            3'd4: begin active_width_cpu = 16'd400;
                        active_height_cpu = 16'd300; end
            3'd5: begin active_width_cpu = 16'd640;
                        active_height_cpu = 16'd400; end
            default: begin active_width_cpu = 16'd720;
                           active_height_cpu = 16'd480; end
        endcase
    end

    always @* begin
        cpu_rdata = 32'd0;
        if (palette_select) begin
            cpu_rdata = palette_cpu_q;
        end else if (cpu_addr >= 16'h0080 && cpu_addr < 16'h00c0) begin
            case (cpu_addr[4:2])
                3'd0: cpu_rdata = reg_tile_ctrl[cpu_addr[5]];
                3'd1: cpu_rdata = {7'd0, reg_tile_map[cpu_addr[5]]};
                3'd2: cpu_rdata = {7'd0, reg_tile_set[cpu_addr[5]]};
                3'd3: cpu_rdata = {24'd0, reg_tile_size[cpu_addr[5]]};
                3'd4: cpu_rdata = reg_tile_scroll[cpu_addr[5]];
                default: cpu_rdata = 32'd0;
            endcase
        end else if (cpu_addr >= 16'h0800 && cpu_addr < 16'h080c) begin
            case (cpu_addr[3:2])
                2'd0: cpu_rdata = reg_spr_ctrl;
                2'd1: cpu_rdata = {16'd0, reg_spr_budget};
                2'd2: cpu_rdata = sprite_collision_cpu;
                default: cpu_rdata = 32'd0;
            endcase
        end else if (sprite_table_select) begin
            cpu_rdata = sprite_table_rdata;
        end else begin
            case (cpu_addr[9:2])
                8'h00: cpu_rdata = VEGA_ID;
                8'h01: cpu_rdata = VEGA_VERSION;
                8'h02: cpu_rdata = reg_ctrl;
                8'h03: cpu_rdata = {24'd0, fetch_busy_sync_cpu[1],
                                     config_error_cpu,
                                     underrun_sticky_cpu, display_ready,
                                     sprite_overflow_cpu, flip_pending_cpu,
                                     hblank_level_sync_cpu[1],
                                     vblank_level_sync_cpu[1]};
                8'h04: cpu_rdata = reg_irq_en;
                8'h05: cpu_rdata = reg_irq_stat;
                8'h06: cpu_rdata = reg_mode;
                8'h07: cpu_rdata = CAP_POST_TEXT | CAP_FRAMEBUFFER |
                                     CAP_PALETTE | CAP_TILEMAP | CAP_SPRITE |
                                     CAP_INDEX8;
                8'h08: cpu_rdata = {6'd0, beam_y_cpu, 6'd0, beam_x_cpu};
                8'h09: cpu_rdata = reg_raster_cmp;
                8'h0a: cpu_rdata = {active_height_cpu, active_width_cpu};
                8'h0c: cpu_rdata = reg_backdrop;
                8'h10: cpu_rdata = reg_fb_base_staged;
                8'h11: cpu_rdata = reg_fb_pitch;
                8'h12: cpu_rdata = reg_fb_format;
                8'h13: cpu_rdata = reg_fb_colorkey;
                default: cpu_rdata = 32'd0;
            endcase
        end
    end

    wire unused_cpu_read_stb = cpu_read_stb;

    // ---------------------------------------------------------------------
    // Pixel-domain mode mapping and scanline request/retirement.
    // ---------------------------------------------------------------------
    reg [4:0] ctrl_meta_pixel;
    reg [4:0] ctrl_pixel;
    reg [2:0] mode_meta_pixel;
    reg [2:0] mode_pixel;
    reg [2:0] fb_format_meta_pixel;
    reg [2:0] fb_format_pixel;
    reg [23:0] backdrop_meta_pixel;
    reg [23:0] backdrop_pixel;
    reg [15:0] colorkey_meta_pixel;
    reg [15:0] colorkey_pixel;
    reg [9:0] raster_meta_pixel;
    reg [9:0] raster_pixel;
    reg [1:0] tile_above_meta_pixel;
    reg [1:0] tile_above_pixel;
    reg [1:0] tile_enable_meta_pixel;
    reg [1:0] tile_enable_pixel;
    reg       sprite_global_meta_pixel;
    reg       sprite_global_pixel;

    reg        logical_active_now;
    reg [9:0]  logical_x_now;
    reg [9:0]  logical_y_now;
    reg        next_logical_active;
    reg [9:0]  next_logical_y;
    reg [9:0]  next_physical_y;
    reg        prefetch_logical_active;
    reg [9:0]  prefetch_logical_y;
    reg [9:0]  prefetch_physical_y;

    always @* begin
        logical_active_now = 1'b0;
        logical_x_now = 10'd0;
        logical_y_now = 10'd0;
        case (mode_pixel)
            3'd1: begin
                if (pixel_x >= 10'd40 && pixel_x < 10'd680 &&
                    pixel_y < 10'd480) begin
                    logical_active_now = 1'b1;
                    logical_x_now = pixel_x - 10'd40;
                    logical_y_now = pixel_y;
                end
            end
            3'd2: begin
                if (pixel_x >= 10'd40 && pixel_x < 10'd680 &&
                    pixel_y < 10'd480) begin
                    logical_active_now = 1'b1;
                    logical_x_now = (pixel_x - 10'd40) >> 1;
                    logical_y_now = pixel_y >> 1;
                end
            end
            3'd3: begin
                if (pixel_x >= 10'd40 && pixel_x < 10'd680 &&
                    pixel_y >= 10'd40 && pixel_y < 10'd440) begin
                    logical_active_now = 1'b1;
                    logical_x_now = (pixel_x - 10'd40) >> 1;
                    logical_y_now = (pixel_y - 10'd40) >> 1;
                end
            end
            3'd4: begin
                if (pixel_x >= 10'd160 && pixel_x < 10'd560 &&
                    pixel_y >= 10'd90 && pixel_y < 10'd390) begin
                    logical_active_now = 1'b1;
                    logical_x_now = pixel_x - 10'd160;
                    logical_y_now = pixel_y - 10'd90;
                end
            end
            3'd5: begin
                if (pixel_x >= 10'd40 && pixel_x < 10'd680 &&
                    pixel_y >= 10'd40 && pixel_y < 10'd440) begin
                    logical_active_now = 1'b1;
                    logical_x_now = pixel_x - 10'd40;
                    logical_y_now = pixel_y - 10'd40;
                end
            end
            default: begin
                if (pixel_x < 10'd720 && pixel_y < 10'd480) begin
                    logical_active_now = 1'b1;
                    logical_x_now = pixel_x;
                    logical_y_now = pixel_y;
                end
            end
        endcase

        next_physical_y = pixel_y < 10'd524 ? pixel_y + 10'd1 : 10'd0;
        if (pixel_y < 10'd523)
            prefetch_physical_y = pixel_y + 10'd2;
        else if (pixel_y == 10'd523)
            prefetch_physical_y = 10'd0;
        else
            prefetch_physical_y = 10'd1;
        next_logical_active = 1'b0;
        prefetch_logical_active = 1'b0;
        next_logical_y = 10'd0;
        prefetch_logical_y = 10'd0;
        case (mode_pixel)
            3'd1: begin
                next_logical_active = next_physical_y < 10'd480;
                next_logical_y = next_physical_y;
                prefetch_logical_active = prefetch_physical_y < 10'd480;
                prefetch_logical_y = prefetch_physical_y;
            end
            3'd2: begin
                next_logical_active = next_physical_y < 10'd480;
                next_logical_y = next_physical_y >> 1;
                prefetch_logical_active = prefetch_physical_y < 10'd480;
                prefetch_logical_y = prefetch_physical_y >> 1;
            end
            3'd3: begin
                if (next_physical_y < 10'd40 || next_physical_y >= 10'd440)
                    next_logical_y = 10'd0;
                else begin
                    next_logical_active = 1'b1;
                    next_logical_y = (next_physical_y - 10'd40) >> 1;
                end
                if (prefetch_physical_y >= 10'd40 &&
                    prefetch_physical_y < 10'd440) begin
                    prefetch_logical_active = 1'b1;
                    prefetch_logical_y =
                        (prefetch_physical_y - 10'd40) >> 1;
                end
            end
            3'd4: begin
                if (next_physical_y < 10'd90 || next_physical_y >= 10'd390)
                    next_logical_y = 10'd0;
                else begin
                    next_logical_active = 1'b1;
                    next_logical_y = next_physical_y - 10'd90;
                end
                if (prefetch_physical_y >= 10'd90 &&
                    prefetch_physical_y < 10'd390) begin
                    prefetch_logical_active = 1'b1;
                    prefetch_logical_y = prefetch_physical_y - 10'd90;
                end
            end
            3'd5: begin
                if (next_physical_y < 10'd40 || next_physical_y >= 10'd440)
                    next_logical_y = 10'd0;
                else begin
                    next_logical_active = 1'b1;
                    next_logical_y = next_physical_y - 10'd40;
                end
                if (prefetch_physical_y >= 10'd40 &&
                    prefetch_physical_y < 10'd440) begin
                    prefetch_logical_active = 1'b1;
                    prefetch_logical_y = prefetch_physical_y - 10'd40;
                end
            end
            default: begin
                next_logical_active = next_physical_y < 10'd480;
                next_logical_y = next_physical_y;
                prefetch_logical_active = prefetch_physical_y < 10'd480;
                prefetch_logical_y = prefetch_physical_y;
            end
        endcase
    end

    reg [9:0] request_y_pixel;
    reg [9:0] last_request_y_pixel;
    reg       request_toggle_pixel;
    reg [1:0] ready_toggle_sync_pixel;
    reg       ready_toggle_seen_pixel;
    reg [9:0] ready_y_meta_pixel;
    reg [9:0] ready_y_pixel;
    reg       ready_bank_meta_pixel;
    reg       ready_bank_pixel;
    reg       display_bank_pixel;
    reg [9:0] display_y_pixel;
    reg       display_line_valid_pixel;

    always @(posedge pixel_clk) begin
        if (pixel_rst) begin
            ctrl_meta_pixel <= 5'd0;
            ctrl_pixel <= 5'd0;
            mode_meta_pixel <= 3'd0;
            mode_pixel <= 3'd0;
            fb_format_meta_pixel <= 3'd0;
            fb_format_pixel <= 3'd0;
            backdrop_meta_pixel <= 24'd0;
            backdrop_pixel <= 24'd0;
            colorkey_meta_pixel <= 16'd0;
            colorkey_pixel <= 16'd0;
            raster_meta_pixel <= 10'd0;
            raster_pixel <= 10'd0;
            tile_above_meta_pixel <= 2'b00;
            tile_above_pixel <= 2'b00;
            tile_enable_meta_pixel <= 2'b00;
            tile_enable_pixel <= 2'b00;
            sprite_global_meta_pixel <= 1'b0;
            sprite_global_pixel <= 1'b0;
            request_y_pixel <= 10'd0;
            last_request_y_pixel <= 10'h3ff;
            request_toggle_pixel <= 1'b0;
            ready_toggle_sync_pixel <= 2'b00;
            ready_toggle_seen_pixel <= 1'b0;
            ready_y_meta_pixel <= 10'd0;
            ready_y_pixel <= 10'd0;
            ready_bank_meta_pixel <= 1'b0;
            ready_bank_pixel <= 1'b0;
            display_bank_pixel <= 1'b0;
            display_y_pixel <= 10'h3ff;
            display_line_valid_pixel <= 1'b0;
            vblank_toggle_pixel <= 1'b0;
            raster_toggle_pixel <= 1'b0;
            underrun_toggle_pixel <= 1'b0;
        end else begin
            ctrl_meta_pixel <= reg_ctrl[4:0];
            ctrl_pixel <= ctrl_meta_pixel;
            mode_meta_pixel <= reg_mode[2:0];
            mode_pixel <= mode_meta_pixel;
            fb_format_meta_pixel <= reg_fb_format[2:0];
            fb_format_pixel <= fb_format_meta_pixel;
            backdrop_meta_pixel <= reg_backdrop[23:0];
            backdrop_pixel <= backdrop_meta_pixel;
            colorkey_meta_pixel <= reg_fb_colorkey[15:0];
            colorkey_pixel <= colorkey_meta_pixel;
            raster_meta_pixel <= reg_raster_cmp[9:0];
            raster_pixel <= raster_meta_pixel;
            tile_above_meta_pixel <= {reg_tile_ctrl[1][4],
                                      reg_tile_ctrl[0][4]};
            tile_above_pixel <= tile_above_meta_pixel;
            tile_enable_meta_pixel <= {reg_tile_ctrl[1][0],
                                       reg_tile_ctrl[0][0]};
            tile_enable_pixel <= tile_enable_meta_pixel;
            sprite_global_meta_pixel <= reg_spr_ctrl;
            sprite_global_pixel <= sprite_global_meta_pixel;
            ready_toggle_sync_pixel <= {ready_toggle_sync_pixel[0],
                                        ready_toggle_mem};
            ready_y_meta_pixel <= ready_y_mem;
            ready_y_pixel <= ready_y_meta_pixel;
            ready_bank_meta_pixel <= ready_bank_mem;
            ready_bank_pixel <= ready_bank_meta_pixel;

            if (ready_toggle_sync_pixel[1] != ready_toggle_seen_pixel) begin
                ready_toggle_seen_pixel <= ready_toggle_sync_pixel[1];
            end

            if (pixel_x == 10'd0 && pixel_y == 10'd480)
                vblank_toggle_pixel <= ~vblank_toggle_pixel;
            if (pixel_x == 10'd0 && pixel_y == raster_pixel)
                raster_toggle_pixel <= ~raster_toggle_pixel;

            if (pixel_x == 10'd0 && pixel_y == 10'd500) begin
                // FB_BASE latches after vblank starts. Re-fetch line zero well
                // inside blanking so a page flip cannot retain an old first
                // scanline.
                request_y_pixel <= 10'd0;
                last_request_y_pixel <= 10'd0;
                request_toggle_pixel <= ~request_toggle_pixel;
            end else if (pixel_x == 10'd720 &&
                         prefetch_logical_active &&
                         prefetch_logical_y != last_request_y_pixel) begin
                // The displayed bank is no longer read after x=719. Begin
                // building the line after next here, which gives the builders
                // one complete 858-pixel scanline instead of only the visible
                // 720-pixel interval.
                request_y_pixel <= prefetch_logical_y;
                last_request_y_pixel <= prefetch_logical_y;
                request_toggle_pixel <= ~request_toggle_pixel;
            end

            // Retire a completed line during horizontal blank. The new bank is
            // then stable before its first BRAM read at x=0.
            if (pixel_x == 10'd720 && next_logical_active) begin
                if (next_logical_y != display_y_pixel ||
                    !display_line_valid_pixel) begin
                    if (ready_y_pixel == next_logical_y &&
                        ready_toggle_sync_pixel[1] ==
                        ready_toggle_seen_pixel) begin
                        display_bank_pixel <= ready_bank_pixel;
                        display_y_pixel <= ready_y_pixel;
                        display_line_valid_pixel <= 1'b1;
                    end else begin
                        display_y_pixel <= next_logical_y;
                        display_line_valid_pixel <= 1'b0;
                        if (ctrl_pixel[0] &&
                            (ctrl_pixel[1] || |tile_enable_pixel ||
                             (ctrl_pixel[2] && sprite_global_pixel)))
                            underrun_toggle_pixel <= ~underrun_toggle_pixel;
                    end
                end
            end
        end
    end

    // ---------------------------------------------------------------------
    // SDRAM-domain framebuffer fetcher. Requests and ordered responses use
    // independent indices so the controller FIFO remains full. It drains and
    // yields after each 32-word burst.
    // ---------------------------------------------------------------------
    reg [2:0] ctrl_meta_mem;
    reg [2:0] ctrl_mem;
    reg       spr_global_meta_mem;
    reg       spr_global_mem;
    reg [15:0] spr_budget_meta_mem;
    reg [15:0] spr_budget_mem;
    reg [2:0] mode_meta_mem;
    reg [2:0] mode_mem;
    reg [24:0] fb_base_meta_mem;
    reg [24:0] fb_base_mem;
    reg [15:0] fb_pitch_meta_mem;
    reg [15:0] fb_pitch_mem;
    reg [2:0] fb_format_meta_mem;
    reg [2:0] fb_format_mem;
    wire [15:0] sprite_budget_limit_mem =
        ctrl_mem[0] && ctrl_mem[1] && fb_format_mem == 3'd0 ?
        SPRITE_BUDGET_RGB565 : SPRITE_BUDGET_MAX;
    wire [15:0] sprite_budget_effective_mem =
        spr_budget_mem < sprite_budget_limit_mem ?
        spr_budget_mem : sprite_budget_limit_mem;
    reg [1:0] request_toggle_sync_mem;
    reg       request_toggle_seen_mem;
    reg [9:0] request_y_meta_mem;
    reg [9:0] request_y_mem;
    reg [3:0] fetch_state_mem;
    reg [9:0] fetch_y_mem;
    reg [9:0] fetch_width_mem;
    reg [9:0] fetch_word_count_mem;
    reg [9:0] fetch_word_index_mem;
    reg [9:0] fetch_rsp_index_mem;
    reg [5:0] fetch_outstanding_mem;
    reg [24:0] fetch_row_addr_mem;
    reg [4:0] burst_count_mem;
    reg       build_bank_mem;
    reg       tile_done_seen_mem;
    reg       sprite_done_seen_mem;

    (* ram_style = "block" *) reg [31:0] framebuffer_line [0:1023];
    reg [31:0] framebuffer_word_pixel;

    reg tile_start_mem;
    wire tile_busy_mem;
    wire tile_done_mem;
    wire tile_mem_lock;
    wire tile_mem_valid;
    wire tile_mem_ready;
    wire tile_mem_write;
    wire [24:0] tile_mem_addr;
    wire [3:0] tile_mem_be;
    wire [31:0] tile_mem_wdata;
    wire tile_mem_rsp_valid;
    wire [31:0] tile_mem_rdata;
    wire [17:0] tile0_pair_pixel;
    wire [17:0] tile1_pair_pixel;

    reg sprite_start_mem;
    wire sprite_busy_mem;
    wire sprite_done_mem;
    wire sprite_mem_lock;
    wire sprite_mem_valid;
    wire sprite_mem_ready;
    wire sprite_mem_write;
    wire [24:0] sprite_mem_addr;
    wire [3:0] sprite_mem_be;
    wire [31:0] sprite_mem_wdata;
    wire sprite_mem_rsp_valid;
    wire [31:0] sprite_mem_rdata;
    wire [17:0] sprite_behind_pair_pixel;
    wire [17:0] sprite_front_pair_pixel;

    vega_tile_builder tile_builder_i (
        .mem_clk(mem_clk), .mem_rst(tile_mem_rst), .start(tile_start_mem),
        .build_bank(build_bank_mem), .line_y(fetch_y_mem),
        .line_width(fetch_width_mem),
        .tile0_ctrl({12'd0, reg_tile_ctrl[0]}),
        .tile0_map(reg_tile_map[0][24:0]),
        .tile0_set(reg_tile_set[0][24:0]),
        .tile0_size(reg_tile_size[0][7:0]),
        .tile0_scroll(reg_tile_scroll[0]),
        .tile1_ctrl({12'd0, reg_tile_ctrl[1]}),
        .tile1_map(reg_tile_map[1][24:0]),
        .tile1_set(reg_tile_set[1][24:0]),
        .tile1_size(reg_tile_size[1][7:0]),
        .tile1_scroll(reg_tile_scroll[1]),
        .busy(tile_busy_mem), .done(tile_done_mem),
        .config_error(tile_config_error_mem),
        .mem_lock(tile_mem_lock), .mem_valid(tile_mem_valid),
        .mem_ready(tile_mem_ready), .mem_write(tile_mem_write),
        .mem_addr(tile_mem_addr), .mem_be(tile_mem_be),
        .mem_wdata(tile_mem_wdata), .mem_rsp_valid(tile_mem_rsp_valid),
        .mem_rdata(tile_mem_rdata),
        .pixel_clk(pixel_clk), .display_bank(display_bank_pixel),
        .pixel_x(logical_x_now), .tile0_pair(tile0_pair_pixel),
        .tile1_pair(tile1_pair_pixel)
    );

    vega_sprite_builder sprite_builder_i (
        .cpu_clk(cpu_clk), .cpu_rst(sprite_cpu_rst),
        .cpu_table_write(sprite_table_write),
        .cpu_word_addr(write_stb ? write_addr[9:2] : cpu_addr[9:2]),
        .cpu_be(write_be), .cpu_wdata(write_data),
        .cpu_rdata(sprite_table_rdata),
        .mem_clk(mem_clk), .mem_rst(sprite_mem_rst), .start(sprite_start_mem),
        .build_bank(build_bank_mem), .line_y(fetch_y_mem),
        .line_width(fetch_width_mem),
        .enable(ctrl_mem[2] && spr_global_mem),
        .pixel_budget(sprite_budget_effective_mem),
        .busy(sprite_busy_mem), .done(sprite_done_mem),
        .config_error(sprite_config_error_mem),
        .overflow(sprite_overflow_cpu),
        .collision_bitmap(sprite_collision_cpu),
        .collision_event(sprite_collision_event_cpu),
        .mem_lock(sprite_mem_lock), .mem_valid(sprite_mem_valid),
        .mem_ready(sprite_mem_ready), .mem_write(sprite_mem_write),
        .mem_addr(sprite_mem_addr), .mem_be(sprite_mem_be),
        .mem_wdata(sprite_mem_wdata),
        .mem_rsp_valid(sprite_mem_rsp_valid),
        .mem_rdata(sprite_mem_rdata),
        .pixel_clk(pixel_clk), .display_bank(display_bank_pixel),
        .pixel_x(logical_x_now),
        .behind_pair(sprite_behind_pair_pixel),
        .front_pair(sprite_front_pair_pixel)
    );

    wire display_enabled_mem = ctrl_mem[0];
    wire fetch_enabled_mem = ctrl_mem[0] && ctrl_mem[1];
    wire [10:0] fetch_min_pitch_mem = fb_format_mem == 3'd1 ?
                                      {1'b0, fetch_width_mem} :
                                      {fetch_width_mem, 1'b0};
    wire fetch_config_valid_mem = fb_base_mem[1:0] == 2'b00 &&
                                  fb_pitch_mem[1:0] == 2'b00 &&
                                  fb_format_mem <= 3'd1 &&
                                  fb_pitch_mem >= {5'd0, fetch_min_pitch_mem};
    wire fb_mem_lock = fetch_state_mem == FETCH_ISSUE ||
                       fetch_state_mem == FETCH_WAIT;
    wire any_client_mem_lock = fb_mem_lock || tile_mem_lock ||
                               sprite_mem_lock;
    wire fb_mem_valid = fetch_state_mem == FETCH_ISSUE &&
                        fetch_word_index_mem < fetch_word_count_mem;
    wire [24:0] fb_mem_addr = fetch_row_addr_mem +
                              {13'd0, fetch_word_index_mem, 2'b00};
    wire fb_mem_ready;
    wire fb_mem_rsp_valid;
    wire [31:0] fb_mem_rdata = mem_rdata;
    wire fetch_accept_mem = fb_mem_valid && fb_mem_ready;

    reg [1:0] mem_owner_mem;
    reg [1:0] mem_last_owner_mem;
    reg [1:0] mem_selected_owner;
    reg       mem_current_owner_locked;
    reg [1:0] request_count_mem;
    reg       request_write_mem;
    reg [24:0] request_addr_mem;
    reg [3:0] request_be_mem;
    reg [31:0] request_wdata_mem;
    reg       request_tail_write_mem;
    reg [24:0] request_tail_addr_mem;
    reg [3:0] request_tail_be_mem;
    reg [31:0] request_tail_wdata_mem;
    reg       outbound_lock_mem;

    // Builders perform substantial BRAM-only work between SDRAM requests.
    // Hold an owner through its response, then rotate among pending clients so
    // those local phases overlap framebuffer fetch instead of serializing the
    // complete scanline jobs.
    always @* begin
        case (mem_owner_mem)
            MEM_OWNER_FB: mem_current_owner_locked = fb_mem_lock;
            MEM_OWNER_TILE: mem_current_owner_locked = tile_mem_lock;
            MEM_OWNER_SPRITE: mem_current_owner_locked = sprite_mem_lock;
            default: mem_current_owner_locked = 1'b0;
        endcase

        if (mem_current_owner_locked) begin
            mem_selected_owner = mem_owner_mem;
        end else begin
            mem_selected_owner = MEM_OWNER_NONE;
            case (mem_last_owner_mem)
                MEM_OWNER_FB: begin
                    if (tile_mem_lock)
                        mem_selected_owner = MEM_OWNER_TILE;
                    else if (sprite_mem_lock)
                        mem_selected_owner = MEM_OWNER_SPRITE;
                    else if (fb_mem_lock)
                        mem_selected_owner = MEM_OWNER_FB;
                end
                MEM_OWNER_TILE: begin
                    if (sprite_mem_lock)
                        mem_selected_owner = MEM_OWNER_SPRITE;
                    else if (fb_mem_lock)
                        mem_selected_owner = MEM_OWNER_FB;
                    else if (tile_mem_lock)
                        mem_selected_owner = MEM_OWNER_TILE;
                end
                default: begin
                    if (fb_mem_lock)
                        mem_selected_owner = MEM_OWNER_FB;
                    else if (tile_mem_lock)
                        mem_selected_owner = MEM_OWNER_TILE;
                    else if (sprite_mem_lock)
                        mem_selected_owner = MEM_OWNER_SPRITE;
                end
            endcase
        end
    end

    wire selected_mem_valid =
        mem_selected_owner == MEM_OWNER_FB ? fb_mem_valid :
        mem_selected_owner == MEM_OWNER_TILE ? tile_mem_valid :
        mem_selected_owner == MEM_OWNER_SPRITE ? sprite_mem_valid : 1'b0;
    wire selected_mem_write =
        mem_selected_owner == MEM_OWNER_TILE ? tile_mem_write :
        mem_selected_owner == MEM_OWNER_SPRITE ? sprite_mem_write : 1'b0;
    wire [24:0] selected_mem_addr =
        mem_selected_owner == MEM_OWNER_FB ? fb_mem_addr :
        mem_selected_owner == MEM_OWNER_TILE ? tile_mem_addr :
        mem_selected_owner == MEM_OWNER_SPRITE ? sprite_mem_addr : 25'd0;
    wire [3:0] selected_mem_be =
        mem_selected_owner == MEM_OWNER_TILE ? tile_mem_be :
        mem_selected_owner == MEM_OWNER_SPRITE ? sprite_mem_be : 4'b1111;
    wire [31:0] selected_mem_wdata =
        mem_selected_owner == MEM_OWNER_TILE ? tile_mem_wdata :
        mem_selected_owner == MEM_OWNER_SPRITE ? sprite_mem_wdata : 32'd0;

    // Two elastic entries terminate Vega's round-robin request path before the
    // system SDRAM arbiter. Enqueue readiness depends only on registered
    // occupancy, while simultaneous dequeue/enqueue sustains one word per
    // memory clock once a stream is active.
    wire request_fifo_ready = !request_count_mem[1];
    wire request_enqueue = selected_mem_valid && request_fifo_ready;
    wire request_dequeue = |request_count_mem && mem_ready;
    reg [1:0] request_count_next;

    // Occupancy has exactly three legal states. Spell out its transitions so
    // the external SDRAM request path does not pass through a generic adder.
    always @* begin
        request_count_next = request_count_mem;
        case ({request_enqueue, request_dequeue})
            2'b10: request_count_next = request_count_mem[0] ? 2'd2 : 2'd1;
            2'b01: request_count_next = request_count_mem[1] ? 2'd1 : 2'd0;
            default: request_count_next = request_count_mem;
        endcase
    end

    assign mem_lock = outbound_lock_mem;
    assign mem_valid = |request_count_mem;
    assign mem_write = request_write_mem;
    assign mem_addr = request_addr_mem;
    assign mem_be = request_be_mem;
    assign mem_wdata = request_wdata_mem;
    assign fb_mem_ready = mem_selected_owner == MEM_OWNER_FB &&
                          request_fifo_ready;
    // Responses belong to the registered owner held across the complete
    // request burst. Do not rebuild that decision from live client state on
    // the response path; that creates a long state-to-BRAM write-enable cone.
    assign fb_mem_rsp_valid = mem_owner_mem == MEM_OWNER_FB &&
                              mem_rsp_valid;
    assign tile_mem_ready = mem_selected_owner == MEM_OWNER_TILE &&
                            request_fifo_ready;
    assign tile_mem_rsp_valid = mem_owner_mem == MEM_OWNER_TILE &&
                                mem_rsp_valid;
    assign tile_mem_rdata = mem_rdata;
    assign sprite_mem_ready = mem_selected_owner == MEM_OWNER_SPRITE &&
                              request_fifo_ready;
    assign sprite_mem_rsp_valid = mem_owner_mem == MEM_OWNER_SPRITE &&
                                  mem_rsp_valid;
    assign sprite_mem_rdata = mem_rdata;

    always @(posedge mem_clk) begin
        if (fetch_mem_rst) begin
            ctrl_meta_mem <= 3'd0;
            ctrl_mem <= 3'd0;
            spr_global_meta_mem <= 1'b0;
            spr_global_mem <= 1'b0;
            spr_budget_meta_mem <= 16'd1024;
            spr_budget_mem <= 16'd1024;
            mode_meta_mem <= 3'd0;
            mode_mem <= 3'd0;
            fb_base_meta_mem <= 25'd0;
            fb_base_mem <= 25'd0;
            fb_pitch_meta_mem <= 16'd0;
            fb_pitch_mem <= 16'd0;
            fb_format_meta_mem <= 3'd0;
            fb_format_mem <= 3'd0;
            request_toggle_sync_mem <= 2'b00;
            request_toggle_seen_mem <= 1'b0;
            request_y_meta_mem <= 10'd0;
            request_y_mem <= 10'd0;
            fetch_state_mem <= FETCH_IDLE;
            fetch_busy_mem <= 1'b0;
            fetch_y_mem <= 10'd0;
            fetch_width_mem <= 10'd720;
            fetch_word_count_mem <= 10'd360;
            fetch_word_index_mem <= 10'd0;
            fetch_rsp_index_mem <= 10'd0;
            fetch_outstanding_mem <= 6'd0;
            fetch_row_addr_mem <= 25'd0;
            burst_count_mem <= 5'd0;
            build_bank_mem <= 1'b1;
            ready_bank_mem <= 1'b0;
            ready_y_mem <= 10'd0;
            ready_toggle_mem <= 1'b0;
            tile_done_seen_mem <= 1'b0;
            sprite_done_seen_mem <= 1'b0;
            tile_start_mem <= 1'b0;
            sprite_start_mem <= 1'b0;
            mem_owner_mem <= MEM_OWNER_NONE;
            mem_last_owner_mem <= MEM_OWNER_SPRITE;
            request_count_mem <= 2'd0;
            request_write_mem <= 1'b0;
            request_addr_mem <= 25'd0;
            request_be_mem <= 4'd0;
            request_wdata_mem <= 32'd0;
            request_tail_write_mem <= 1'b0;
            request_tail_addr_mem <= 25'd0;
            request_tail_be_mem <= 4'd0;
            request_tail_wdata_mem <= 32'd0;
            outbound_lock_mem <= 1'b0;
        end else begin
            tile_start_mem <= 1'b0;
            sprite_start_mem <= 1'b0;
            request_count_mem <= request_count_next;
            case ({request_enqueue, request_dequeue})
                2'b10: begin
                    if (request_count_mem == 2'd0) begin
                        request_write_mem <= selected_mem_write;
                        request_addr_mem <= selected_mem_addr;
                        request_be_mem <= selected_mem_be;
                        request_wdata_mem <= selected_mem_wdata;
                    end else begin
                        request_tail_write_mem <= selected_mem_write;
                        request_tail_addr_mem <= selected_mem_addr;
                        request_tail_be_mem <= selected_mem_be;
                        request_tail_wdata_mem <= selected_mem_wdata;
                    end
                end
                2'b01: begin
                    if (request_count_mem == 2'd2) begin
                        request_write_mem <= request_tail_write_mem;
                        request_addr_mem <= request_tail_addr_mem;
                        request_be_mem <= request_tail_be_mem;
                        request_wdata_mem <= request_tail_wdata_mem;
                    end
                end
                2'b11: begin
                    request_write_mem <= selected_mem_write;
                    request_addr_mem <= selected_mem_addr;
                    request_be_mem <= selected_mem_be;
                    request_wdata_mem <= selected_mem_wdata;
                end
                default: begin end
            endcase
            // Every enqueueing client holds its local lock, so enqueue is
            // already covered by any_client_mem_lock. Express the remaining
            // queued-work cases directly from registered occupancy: count two
            // remains nonempty after one dequeue, and count one remains
            // nonempty while the system port is stalled. This keeps client
            // arbitration and request data off the registered lock path.
            outbound_lock_mem <= request_count_mem[1] ||
                                 (request_count_mem[0] && !mem_ready) ||
                                 any_client_mem_lock;
            if (tile_done_mem)
                tile_done_seen_mem <= 1'b1;
            if (sprite_done_mem)
                sprite_done_seen_mem <= 1'b1;
            case ({fetch_accept_mem, fb_mem_rsp_valid})
                2'b10: fetch_outstanding_mem <=
                    fetch_outstanding_mem + 6'd1;
                2'b01: fetch_outstanding_mem <=
                    fetch_outstanding_mem - 6'd1;
                default: fetch_outstanding_mem <= fetch_outstanding_mem;
            endcase
            if (fb_mem_rsp_valid) begin
                framebuffer_line[{build_bank_mem,
                                  fetch_rsp_index_mem[8:0]}] <= fb_mem_rdata;
                fetch_rsp_index_mem <= fetch_rsp_index_mem + 10'd1;
            end
            mem_owner_mem <= mem_selected_owner;
            if (mem_selected_owner != MEM_OWNER_NONE)
                mem_last_owner_mem <= mem_selected_owner;
            ctrl_meta_mem <= reg_ctrl[2:0];
            ctrl_mem <= ctrl_meta_mem;
            spr_global_meta_mem <= reg_spr_ctrl;
            spr_global_mem <= spr_global_meta_mem;
            spr_budget_meta_mem <= reg_spr_budget;
            spr_budget_mem <= spr_budget_meta_mem;
            mode_meta_mem <= reg_mode[2:0];
            mode_mem <= mode_meta_mem;
            fb_base_meta_mem <= reg_fb_base_active[24:0];
            fb_base_mem <= fb_base_meta_mem;
            fb_pitch_meta_mem <= reg_fb_pitch[15:0];
            fb_pitch_mem <= fb_pitch_meta_mem;
            fb_format_meta_mem <= reg_fb_format[2:0];
            fb_format_mem <= fb_format_meta_mem;
            request_toggle_sync_mem <= {request_toggle_sync_mem[0],
                                        request_toggle_pixel};
            request_y_meta_mem <= request_y_pixel;
            request_y_mem <= request_y_meta_mem;

            case (fetch_state_mem)
                FETCH_IDLE: begin
                    fetch_busy_mem <= 1'b0;
                    if (request_toggle_sync_mem[1] !=
                        request_toggle_seen_mem) begin
                        request_toggle_seen_mem <= request_toggle_sync_mem[1];
                        fetch_y_mem <= request_y_mem;
                        case (mode_mem)
                            3'd1, 3'd5: fetch_width_mem <= 10'd640;
                            3'd2, 3'd3: fetch_width_mem <= 10'd320;
                            3'd4: fetch_width_mem <= 10'd400;
                            default: fetch_width_mem <= 10'd720;
                        endcase
                        build_bank_mem <= ~ready_bank_mem;
                        fetch_busy_mem <= 1'b1;
                        fetch_state_mem <= FETCH_PREP;
                    end
                end

                FETCH_PREP: begin
                    fetch_word_count_mem <= fb_format_mem == 3'd1 ?
                        (fetch_width_mem + 10'd3) >> 2 :
                        (fetch_width_mem + 10'd1) >> 1;
                    fetch_word_index_mem <= 10'd0;
                    fetch_rsp_index_mem <= 10'd0;
                    fetch_outstanding_mem <= 6'd0;
                    fetch_row_addr_mem <= fb_base_mem +
                        fetch_y_mem * fb_pitch_mem;
                    burst_count_mem <= 5'd0;
                    tile_done_seen_mem <= 1'b0;
                    sprite_done_seen_mem <= 1'b0;
                    if (!display_enabled_mem) begin
                        ready_bank_mem <= build_bank_mem;
                        ready_y_mem <= fetch_y_mem;
                        ready_toggle_mem <= ~ready_toggle_mem;
                        fetch_state_mem <= FETCH_IDLE;
                    end else begin
                        tile_start_mem <= 1'b1;
                        sprite_start_mem <= 1'b1;
                        if (!fetch_enabled_mem || !fetch_config_valid_mem)
                            fetch_state_mem <= FETCH_BUILD_WAIT;
                        else
                            fetch_state_mem <= FETCH_ISSUE;
                    end
                end

                FETCH_ISSUE: begin
                    if (fetch_accept_mem) begin
                        fetch_word_index_mem <= fetch_word_index_mem + 10'd1;
                        burst_count_mem <= burst_count_mem + 5'd1;
                        if (fetch_word_index_mem + 10'd1 >=
                            fetch_word_count_mem ||
                            burst_count_mem == 5'd31)
                            fetch_state_mem <= FETCH_WAIT;
                    end
                end

                FETCH_WAIT: begin
                    if (fetch_outstanding_mem == 6'd0 ||
                        (fb_mem_rsp_valid &&
                         fetch_outstanding_mem == 6'd1)) begin
                        if (fetch_word_index_mem >= fetch_word_count_mem)
                            fetch_state_mem <= FETCH_BUILD_WAIT;
                        else
                            fetch_state_mem <= FETCH_YIELD;
                    end
                end

                FETCH_YIELD: begin
                    burst_count_mem <= 5'd0;
                    fetch_state_mem <= FETCH_ISSUE;
                end

                FETCH_BUILD_WAIT: begin
                    if ((tile_done_seen_mem || tile_done_mem) &&
                        (sprite_done_seen_mem || sprite_done_mem)) begin
                        ready_bank_mem <= build_bank_mem;
                        ready_y_mem <= fetch_y_mem;
                        ready_toggle_mem <= ~ready_toggle_mem;
                        fetch_state_mem <= FETCH_IDLE;
                    end
                end

                default: fetch_state_mem <= FETCH_IDLE;
            endcase
        end
    end

    // ---------------------------------------------------------------------
    // Pixel compositor. Tile/sprite pairs and the RGB565 framebuffer are read
    // in stage one, palette BRAMs in stage two, and the ordered layer result is
    // registered in stage three (two pixel clocks after its input coordinate).
    // ---------------------------------------------------------------------
    function automatic [8:0] select_pair_pixel(
        input [17:0] pair,
        input odd
    );
        select_pair_pixel = odd ? pair[8:0] : pair[17:9];
    endfunction

    reg        logical_active_d1;
    reg [1:0]  logical_x_byte_d1;
    reg        logical_x_odd_d1;
    reg        line_valid_d1;
    reg        fb_enable_d1;
    reg        sprite_enable_d1;
    reg        colorkey_enable_d1;
    reg [15:0] colorkey_d1;
    reg [23:0] backdrop_d1;
    reg [1:0]  tile_above_d1;

    reg [7:0] framebuffer_index_d1;
    always @* begin
        case (logical_x_byte_d1)
            2'd0: framebuffer_index_d1 = framebuffer_word_pixel[31:24];
            2'd1: framebuffer_index_d1 = framebuffer_word_pixel[23:16];
            2'd2: framebuffer_index_d1 = framebuffer_word_pixel[15:8];
            default: framebuffer_index_d1 = framebuffer_word_pixel[7:0];
        endcase
    end

    wire framebuffer_indexed_d1 = fb_format_pixel == 3'd1;
    wire [15:0] framebuffer_direct_d1 = logical_x_odd_d1 ?
                                           framebuffer_word_pixel[15:0] :
                                           framebuffer_word_pixel[31:16];
    wire [15:0] framebuffer_pixel_d1 = framebuffer_indexed_d1 ?
        {8'd0, framebuffer_index_d1} : framebuffer_direct_d1;
    wire [8:0] tile0_pixel_d1 = select_pair_pixel(
        tile0_pair_pixel, logical_x_odd_d1);
    wire [8:0] tile1_pixel_d1 = select_pair_pixel(
        tile1_pair_pixel, logical_x_odd_d1);
    wire [8:0] sprite_behind_pixel_d1 = select_pair_pixel(
        sprite_behind_pair_pixel, logical_x_odd_d1);
    wire [8:0] sprite_front_pixel_d1 = select_pair_pixel(
        sprite_front_pair_pixel, logical_x_odd_d1);
    wire framebuffer_opaque_d1 = !(colorkey_enable_d1 &&
        (framebuffer_indexed_d1 ?
            framebuffer_index_d1 == colorkey_d1[7:0] :
            framebuffer_direct_d1 == colorkey_d1));
    wire sprite_front_selected_d1 = sprite_enable_d1 &&
                                     sprite_front_pixel_d1[8];
    wire sprite_behind_selected_d1 = sprite_enable_d1 &&
        !sprite_front_pixel_d1[8] && sprite_behind_pixel_d1[8] &&
        !(fb_enable_d1 && framebuffer_opaque_d1);
    wire [7:0] sprite_palette_index_d1 = sprite_front_selected_d1 ?
        sprite_front_pixel_d1[7:0] : sprite_behind_pixel_d1[7:0];

    reg [31:0] palette_tile0_q;
    reg [31:0] palette_tile1_q;
    reg [31:0] palette_sprite_q;
    reg [31:0] palette_framebuffer_q;
    reg        logical_active_d2;
    reg        line_valid_d2;
    reg        fb_enable_d2;
    reg        framebuffer_opaque_d2;
    reg        framebuffer_indexed_d2;
    reg [15:0] framebuffer_pixel_d2;
    reg [23:0] backdrop_d2;
    reg        tile0_opaque_d2;
    reg        tile1_opaque_d2;
    reg        sprite_opaque_d2;
    reg        sprite_front_d2;
    reg [1:0]  tile_above_d2;
    reg [23:0] graphics_rgb_pixel;
    reg [23:0] composed_rgb;

    // Keep palette lookup registers in a dedicated process so Yosys can fold
    // each one into the BRAM's synchronous pixel port.
    always @(posedge pixel_clk) begin
        palette_tile0_q <= palette_mem[tile0_pixel_d1[7:0]];
        palette_tile1_q <= palette_tile1_mem[tile1_pixel_d1[7:0]];
        palette_sprite_q <= palette_sprite_mem[sprite_palette_index_d1];
        palette_framebuffer_q <=
            palette_framebuffer_mem[framebuffer_index_d1];
    end

    always @* begin
        composed_rgb = backdrop_d2;
        if (logical_active_d2 && line_valid_d2) begin
            // TILE1 is farther back than TILE0 within each priority group.
            if (tile1_opaque_d2 && !tile_above_d2[1])
                composed_rgb = palette_tile1_q[23:0];
            if (tile0_opaque_d2 && !tile_above_d2[0])
                composed_rgb = palette_tile0_q[23:0];
            if (sprite_opaque_d2 && !sprite_front_d2)
                composed_rgb = palette_sprite_q[23:0];
            if (fb_enable_d2 && framebuffer_opaque_d2)
                composed_rgb = framebuffer_indexed_d2 ?
                               palette_framebuffer_q[23:0] :
                               rgb565_to_rgb888(framebuffer_pixel_d2);
            if (sprite_opaque_d2 && sprite_front_d2)
                composed_rgb = palette_sprite_q[23:0];
            if (tile1_opaque_d2 && tile_above_d2[1])
                composed_rgb = palette_tile1_q[23:0];
            if (tile0_opaque_d2 && tile_above_d2[0])
                composed_rgb = palette_tile0_q[23:0];
        end
    end

    always @(posedge pixel_clk) begin
        framebuffer_word_pixel <= framebuffer_line[fb_format_pixel == 3'd1 ?
            {display_bank_pixel, 1'b0, logical_x_now[9:2]} :
            {display_bank_pixel, logical_x_now[9:1]}];
        if (pixel_rst) begin
            logical_active_d1 <= 1'b0;
            logical_x_byte_d1 <= 2'd0;
            logical_x_odd_d1 <= 1'b0;
            line_valid_d1 <= 1'b0;
            fb_enable_d1 <= 1'b0;
            sprite_enable_d1 <= 1'b0;
            colorkey_enable_d1 <= 1'b0;
            colorkey_d1 <= 16'd0;
            backdrop_d1 <= 24'd0;
            tile_above_d1 <= 2'b00;
            logical_active_d2 <= 1'b0;
            line_valid_d2 <= 1'b0;
            fb_enable_d2 <= 1'b0;
            framebuffer_opaque_d2 <= 1'b0;
            framebuffer_indexed_d2 <= 1'b0;
            framebuffer_pixel_d2 <= 16'd0;
            backdrop_d2 <= 24'd0;
            tile0_opaque_d2 <= 1'b0;
            tile1_opaque_d2 <= 1'b0;
            sprite_opaque_d2 <= 1'b0;
            sprite_front_d2 <= 1'b0;
            tile_above_d2 <= 2'b00;
            graphics_rgb_pixel <= 24'd0;
        end else begin
            logical_active_d1 <= logical_active_now;
            logical_x_byte_d1 <= logical_x_now[1:0];
            logical_x_odd_d1 <= logical_x_now[0];
            line_valid_d1 <= display_line_valid_pixel &&
                             display_y_pixel == logical_y_now;
            fb_enable_d1 <= ctrl_pixel[1];
            sprite_enable_d1 <= ctrl_pixel[2];
            colorkey_enable_d1 <= ctrl_pixel[3];
            colorkey_d1 <= colorkey_pixel;
            backdrop_d1 <= ctrl_pixel[4] ? backdrop_pixel : 24'd0;
            tile_above_d1 <= tile_above_pixel;

            logical_active_d2 <= logical_active_d1;
            line_valid_d2 <= line_valid_d1;
            fb_enable_d2 <= fb_enable_d1;
            framebuffer_opaque_d2 <= framebuffer_opaque_d1;
            framebuffer_indexed_d2 <= framebuffer_indexed_d1;
            framebuffer_pixel_d2 <= framebuffer_pixel_d1;
            backdrop_d2 <= backdrop_d1;
            tile0_opaque_d2 <= tile0_pixel_d1[8];
            tile1_opaque_d2 <= tile1_pixel_d1[8];
            sprite_opaque_d2 <= sprite_front_selected_d1 ||
                                sprite_behind_selected_d1;
            sprite_front_d2 <= sprite_front_selected_d1;
            tile_above_d2 <= tile_above_d1;
            graphics_rgb_pixel <= composed_rgb;
        end
    end

    assign graphics_active = ctrl_pixel[0];
    assign rgb = graphics_active ? graphics_rgb_pixel : post_rgb;
endmodule

(* keep_hierarchy = "yes" *)
module vega_reset_release (
    input  wire clk,
    input  wire assert_reset,
    output wire reset
);
    (* keep = "true" *) reg [1:0] release_pipe = 2'b11;

    always @(posedge clk or posedge assert_reset) begin
        if (assert_reset)
            release_pipe <= 2'b11;
        else
            release_pipe <= {release_pipe[0], 1'b0};
    end

    assign reset = release_pipe[1];
endmodule

`default_nettype wire
