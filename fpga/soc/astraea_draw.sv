// Clipped geometry, glyph, and bounded flood-fill frontend for INDEX8 and
// RGB565 surfaces. All raster traffic passes through the exact pixel port.
`default_nettype none

module astraea_draw (
    input  wire        cpu_clk,
    input  wire        cpu_rst,
    input  wire        cpu_write_stb,
    input  wire [4:0]  cpu_reg,
    input  wire [3:0]  cpu_be,
    input  wire [31:0] cpu_wdata,
    output reg  [31:0] cpu_rdata,
    output wire        cpu_busy,
    output reg         cpu_done,
    output wire        cpu_irq,
    output wire        cache_flush,

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
    input  wire [31:0] mem_rdata
);
    localparam [7:0] OP_LINE         = 8'd0;
    localparam [7:0] OP_RECT         = 8'd1;
    localparam [7:0] OP_RECT_FILL    = 8'd2;
    localparam [7:0] OP_CIRCLE       = 8'd3;
    localparam [7:0] OP_CIRCLE_FILL  = 8'd4;
    localparam [7:0] OP_ELLIPSE      = 8'd5;
    localparam [7:0] OP_ELLIPSE_FILL = 8'd6;
    localparam [7:0] OP_PATTERN_FILL = 8'd7;
    localparam [7:0] OP_GLYPH_MASK1  = 8'd8;
    localparam [7:0] OP_GLYPH_A4     = 8'd9;
    localparam [7:0] OP_GLYPH_INDEX4 = 8'd10;
    localparam [7:0] OP_GLYPH_INDEX8 = 8'd11;
    localparam [7:0] OP_FLOOD_FILL   = 8'd12;

    localparam [1:0] GLYPH_MODE_MASK1  = 2'd0;
    localparam [1:0] GLYPH_MODE_A4     = 2'd1;
    localparam [1:0] GLYPH_MODE_INDEX4 = 2'd2;

    localparam [4:0] REG_DST          = 5'd0;
    localparam [4:0] REG_DST_PITCH    = 5'd1;
    localparam [4:0] REG_FORMAT       = 5'd2;
    localparam [4:0] REG_CLIP_MIN     = 5'd3;
    localparam [4:0] REG_CLIP_MAX     = 5'd4;
    localparam [4:0] REG_P0           = 5'd5;
    localparam [4:0] REG_P1           = 5'd6;
    localparam [4:0] REG_RADII        = 5'd7;
    localparam [4:0] REG_FG           = 5'd8;
    localparam [4:0] REG_BG           = 5'd9;
    localparam [4:0] REG_PATTERN_HI   = 5'd10;
    localparam [4:0] REG_PATTERN_LO   = 5'd11;
    localparam [4:0] REG_ORIGIN       = 5'd12;
    localparam [4:0] REG_SRC          = 5'd13;
    localparam [4:0] REG_SRC_PITCH    = 5'd14;
    localparam [4:0] REG_SRC_SIZE     = 5'd15;
    localparam [4:0] REG_PALETTE      = 5'd16;
    localparam [4:0] REG_WORK         = 5'd17;
    localparam [4:0] REG_WORK_ENTRIES = 5'd18;
    localparam [4:0] REG_OP           = 5'd19;
    localparam [4:0] REG_CTRL         = 5'd20;
    localparam [4:0] REG_STATUS       = 5'd21;
    localparam [4:0] REG_FENCE        = 5'd22;

    localparam [5:0] ST_IDLE = 6'd0;
    localparam [5:0] ST_EMIT = 6'd1;
    localparam [5:0] ST_GEOM_ADJUST = 6'd3;
    localparam [5:0] ST_SPAN_SETUP = 6'd2;
    localparam [5:0] ST_SPAN_POINT = 6'd6;
    localparam [5:0] ST_SPAN_ADVANCE = 6'd7;
    localparam [5:0] ST_LINE_SETUP = 6'd5;
    localparam [5:0] ST_LINE_POINT = 6'd4;
    localparam [5:0] ST_LINE_STEP = 6'd12;
    localparam [5:0] ST_RECT_EDGE = 6'd13;
    localparam [5:0] ST_RECT_NEXT = 6'd15;
    localparam [5:0] ST_RECT_FILL_ROW = 6'd14;
    localparam [5:0] ST_RECT_FILL_NEXT = 6'd10;
    localparam [5:0] ST_CIRCLE_POINT = 6'd11;
    localparam [5:0] ST_CIRCLE_SPAN = 6'd9;
    localparam [5:0] ST_CIRCLE_SLOT = 6'd8;
    localparam [5:0] ST_CIRCLE_STEP = 6'd24;
    localparam [5:0] ST_PATTERN_POINT = 6'd25;
    localparam [5:0] ST_PATTERN_NEXT = 6'd27;
    localparam [5:0] ST_FINISH = 6'd26;
    localparam [5:0] ST_RECT_FILL_SETUP = 6'd30;
    localparam [5:0] ST_PATTERN_SETUP = 6'd31;
    localparam [5:0] ST_ELL_MUL_RX = 6'd29;
    localparam [5:0] ST_ELL_MUL_RY = 6'd28;
    localparam [5:0] ST_ELL_MUL_CROSS = 6'd20;
    localparam [5:0] ST_ELL_POINT = 6'd21;
    localparam [5:0] ST_ELL_SPAN = 6'd23;
    localparam [5:0] ST_ELL_SLOT = 6'd22;
    localparam [5:0] ST_ELL_STEP = 6'd18;
    localparam [5:0] ST_PORT_WAIT = 6'd19;
    localparam [5:0] ST_PALETTE_ISSUE = 6'd17;
    localparam [5:0] ST_PALETTE_STORE = 6'd16;
    localparam [5:0] ST_GLYPH_DESC_ISSUE = 6'd48;
    localparam [5:0] ST_GLYPH_DESC_STORE = 6'd49;
    localparam [5:0] ST_GLYPH_SETUP = 6'd51;
    localparam [5:0] ST_GLYPH_SOURCE = 6'd50;
    localparam [5:0] ST_GLYPH_SOURCE_GET = 6'd54;
    localparam [5:0] ST_GLYPH_DECODE = 6'd55;
    localparam [5:0] ST_GLYPH_LOOKUP = 6'd53;
    localparam [5:0] ST_GLYPH_LOOKUP_WAIT = 6'd52;
    localparam [5:0] ST_GLYPH_BLEND_SETUP = 6'd60;
    localparam [5:0] ST_GEOM_DECIDE = 6'd61;
    localparam [5:0] ST_GLYPH_BLEND_STORE = 6'd63;
    localparam [5:0] ST_GLYPH_ADVANCE = 6'd62;
    localparam [5:0] ST_FLOOD_INIT = 6'd58;
    localparam [5:0] ST_FLOOD_READ = 6'd59;
    localparam [5:0] ST_FLOOD_PROCESS = 6'd57;
    localparam [5:0] ST_FLOOD_PUSH = 6'd56;
    localparam [5:0] ST_FLOOD_PUSH_DONE = 6'd40;
    localparam [5:0] ST_FLOOD_POP = 6'd41;
    localparam [5:0] ST_FLOOD_POP_DONE = 6'd43;
    localparam [5:0] ST_FLOOD_SEEK = 6'd42;
    localparam [5:0] ST_FLOOD_SCAN = 6'd46;
    localparam [5:0] ST_FLOOD_NEIGHBOR = 6'd47;
    localparam [5:0] ST_FLOOD_ADVANCE = 6'd45;
    localparam [5:0] ST_GLYPH_BLEND_CALC = 6'd44;
    localparam [5:0] ST_GLYPH_BLEND_PRODUCT = 6'd36;
    localparam [5:0] ST_GLYPH_BLEND_SUM = 6'd37;
    localparam [5:0] ST_GLYPH_BLEND_LOOKUP = 6'd39;
    localparam [5:0] ST_GLYPH_BLEND_RESULT = 6'd38;
    localparam [5:0] ST_PIXEL_ADDR = 6'd34;
    localparam [5:0] ST_PIXEL_ISSUE = 6'd35;
    localparam [5:0] ST_GLYPH_SOURCE_ADDR = 6'd33;
    localparam [5:0] ST_GLYPH_SOURCE_ISSUE = 6'd32;
    // State zero is idle whenever busy_mem is clear, so it can also serve as
    // the registered flood-pop decision state while a job is active.
    localparam [5:0] ST_FLOOD_POP_CHECK = ST_IDLE;

    localparam [2:0] GEOM_CIRCLE  = 3'd0;
    localparam [2:0] GEOM_ELLIPSE = 3'd1;
    localparam [2:0] GEOM_LINE    = 3'd2;
    localparam [2:0] GEOM_PATTERN = 3'd3;
    localparam [2:0] GEOM_SPAN    = 3'd4;
    localparam [2:0] GEOM_PATTERN_SETUP = 3'd5;
    localparam [2:0] GEOM_RECT_SETUP = 3'd6;

    localparam [3:0] EMIT_RET_PATTERN_NEXT   = 4'd0;
    localparam [3:0] EMIT_RET_SPAN_ADVANCE  = 4'd1;
    localparam [3:0] EMIT_RET_LINE_STEP     = 4'd2;
    localparam [3:0] EMIT_RET_FINISH        = 4'd3;
    localparam [3:0] EMIT_RET_RECT_NEXT     = 4'd4;
    localparam [3:0] EMIT_RET_CIRCLE_SLOT   = 4'd5;
    localparam [3:0] EMIT_RET_ELL_SLOT      = 4'd6;
    localparam [3:0] EMIT_RET_GLYPH_ADVANCE = 4'd7;
    localparam [3:0] EMIT_RET_FLOOD_NEIGHBOR= 4'd8;

    localparam [1:0] SPAN_RET_RECT_NEXT   = 2'd0;
    localparam [1:0] SPAN_RET_CIRCLE_SLOT = 2'd1;
    localparam [1:0] SPAN_RET_ELL_SLOT    = 2'd2;

    localparam [2:0] PORT_RET_EMIT             = 3'd0;
    localparam [2:0] PORT_RET_PALETTE_STORE    = 3'd1;
    localparam [2:0] PORT_RET_GLYPH_DESC_STORE = 3'd2;
    localparam [2:0] PORT_RET_GLYPH_SOURCE_GET = 3'd3;
    localparam [2:0] PORT_RET_GLYPH_BLEND_STORE= 3'd4;
    localparam [2:0] PORT_RET_FLOOD_PROCESS    = 3'd5;
    localparam [2:0] PORT_RET_FLOOD_PUSH_DONE  = 3'd6;
    localparam [2:0] PORT_RET_FLOOD_POP_DONE   = 3'd7;

    localparam [1:0] QUEUE_RET_POP      = 2'd0;
    localparam [1:0] QUEUE_RET_ADVANCE  = 2'd1;
    localparam [1:0] QUEUE_RET_NEIGHBOR = 2'd2;

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

    function automatic signed [16:0] coord_x(input [31:0] packed_coord);
        coord_x = {packed_coord[15], packed_coord[15:0]};
    endfunction

    function automatic signed [16:0] coord_y(input [31:0] packed_coord);
        coord_y = {packed_coord[31], packed_coord[31:16]};
    endfunction

    function automatic signed [17:0] abs_delta(
        input signed [16:0] a,
        input signed [16:0] b
    );
        reg signed [17:0] a_wide;
        reg signed [17:0] b_wide;
        begin
            a_wide = {a[16], a};
            b_wide = {b[16], b};
            abs_delta = a_wide >= b_wide ? a_wide - b_wide :
                                                  b_wide - a_wide;
        end
    endfunction

    reg        reg_irq_enable;

    // Per-register ownership makes the command file atomic without copying it:
    // the active command keeps its bank while the CPU writes the other bank.
    // Unchanged registers retain their current owner, so partial command updates
    // have the same semantics as the original register bank.
    (* ram_style = "block" *) reg [31:0] command_mem0 [0:31];
    (* ram_style = "block" *) reg [31:0] command_mem1 [0:31];
    reg [22:0] staging_owner_cpu;
    reg [22:0] command_owner_cpu;
    reg [22:0] staging_valid_cpu;
    reg [22:0] command_valid_cpu;
    // Prevalidate merged staging values, then hold the six results as bundled
    // command data until the synchronized command completes.
    reg [5:0] staging_range_valid_cpu;
    reg [5:0] command_range_valid_cpu;
    reg [31:0] command_cpu_q0;
    reg [31:0] command_cpu_q1;
    reg [31:0] command_mem_q0;
    reg [31:0] command_mem_q1;
    reg command_mem_owner_q;
    reg command_mem_valid_q;
    reg [4:0] config_read_index;
    reg [4:0] config_capture_index;
    reg config_capture_valid;
    reg [2:0] config_phase;
    reg config_valid_q;
    reg cfg_base_valid_q;
    reg cfg_clip_valid_q;
    reg cfg_radius_valid_q;
    reg cfg_glyph_source_valid_q;
    reg cfg_glyph_work_valid_q;
    reg cfg_glyph_format_valid_q;
    reg cfg_glyph_palette_valid_q;
    reg cfg_flood_valid_q;

    wire cpu_config_select = cpu_reg <= REG_OP || cpu_reg == REG_FENCE;
    wire [31:0] staging_owner_indexed = {9'd0, staging_owner_cpu};
    wire [31:0] command_owner_indexed = {9'd0, command_owner_cpu};
    wire [31:0] staging_valid_indexed = {9'd0, staging_valid_cpu};
    wire cpu_staging_owner = staging_owner_indexed[cpu_reg];
    wire cpu_command_owner = command_owner_indexed[cpu_reg];
    wire cpu_staging_valid = staging_valid_indexed[cpu_reg];
    wire cpu_write_bank = staging_owner_indexed[cpu_reg] ==
                          command_owner_indexed[cpu_reg] ?
                          ~cpu_command_owner : cpu_staging_owner;
    wire [31:0] command_cpu_value = cpu_staging_valid ?
        (cpu_staging_owner ? command_cpu_q1 : command_cpu_q0) : 32'd0;
    wire [31:0] command_cpu_write_data = merge_be(
        command_cpu_value, cpu_wdata, cpu_be);

    wire [31:0] command_valid_indexed = {9'd0, command_valid_cpu};
    wire [31:0] command_mem_value =
        command_mem_valid_q ?
        (command_mem_owner_q ? command_mem_q1 : command_mem_q0) : 32'd0;

    always @(posedge cpu_clk) begin
        command_cpu_q0 <= command_mem0[cpu_reg];
        command_cpu_q1 <= command_mem1[cpu_reg];
        if (!cpu_rst && cpu_write_stb && cpu_config_select) begin
            if (cpu_write_bank)
                command_mem1[cpu_reg] <= command_cpu_write_data;
            else
                command_mem0[cpu_reg] <= command_cpu_write_data;
        end
    end

    always @(posedge mem_clk) begin
        command_mem_q0 <= command_mem0[config_read_index];
        command_mem_q1 <= command_mem1[config_read_index];
        command_mem_owner_q <= command_owner_indexed[config_read_index];
        command_mem_valid_q <= command_valid_indexed[config_read_index];
    end

    reg [24:0] cfg_dst_cpu;
    reg [15:0] cfg_dst_pitch_cpu;
    reg [31:0] cfg_format_cpu;
    reg [31:0] cfg_clip_min_cpu;
    reg [31:0] cfg_clip_max_cpu;
    reg [31:0] cfg_p0_cpu;
    reg [31:0] cfg_p1_cpu;
    reg [31:0] cfg_radii_cpu;
    reg [15:0] cfg_fg_cpu;
    reg [15:0] cfg_bg_cpu;
    reg [63:0] cfg_pattern_cpu;
    reg [31:0] cfg_origin_cpu;
    reg [24:0] cfg_src_cpu;
    reg [15:0] cfg_src_pitch_cpu;
    reg [31:0] cfg_src_size_cpu;
    reg [24:0] cfg_palette_cpu;
    reg [24:0] cfg_work_cpu;
    reg [31:0] cfg_work_entries_cpu;
    reg [31:0] cfg_op_cpu;
    reg [31:0] cfg_fence_cpu;
    reg cfg_dst_addr_valid_cpu;
    reg cfg_src_addr_valid_cpu;
    reg cfg_palette_addr_valid_cpu;
    reg cfg_work_addr_valid_cpu;
    reg cfg_dst_pitch_valid_cpu;
    reg cfg_src_pitch_valid_cpu;
    reg start_toggle_cpu;
    reg start_pending_cpu;

    reg [1:0] busy_sync_cpu;
    reg [1:0] done_sync_cpu;
    reg done_seen_cpu;
    reg [7:0] error_meta_cpu;
    reg [7:0] error_sync_cpu;
    reg [7:0] error_cpu;
    reg done_sticky_cpu;
    reg [31:0] completed_fence_cpu;
    reg [31:0] completed_fence_meta_cpu;
    reg [31:0] completed_fence_sync_cpu;

    reg [1:0] start_sync_mem;
    reg start_seen_mem;
    reg busy_mem;
    reg done_toggle_mem;
    reg finish_pending_mem;
    reg [7:0] error_mem;
    wire [31:0] job_fence_mem = cfg_fence_cpu;
    reg [31:0] completed_fence_mem;

    wire busy_visible = start_pending_cpu || busy_sync_cpu[1];
    assign cpu_busy = busy_visible;
    assign cache_flush = busy_visible;
    assign cpu_irq = reg_irq_enable && done_sticky_cpu;

    always @* begin
        case (cpu_reg)
            REG_CTRL:         cpu_rdata = {30'd0, reg_irq_enable, 1'b0};
            REG_STATUS:       cpu_rdata = {16'd0, error_cpu, 6'd0,
                                           done_sticky_cpu, busy_visible};
            REG_FENCE:        cpu_rdata = busy_visible ? command_cpu_value :
                                                        completed_fence_cpu;
            default:          cpu_rdata = cpu_config_select ?
                                           command_cpu_value : 32'd0;
        endcase
    end

    always @(posedge cpu_clk) begin
        cpu_done <= 1'b0;
        if (cpu_rst) begin
            reg_irq_enable <= 1'b0;
            staging_owner_cpu <= 23'd0;
            command_owner_cpu <= 23'd0;
            staging_valid_cpu <= 23'd0;
            command_valid_cpu <= 23'd0;
            staging_range_valid_cpu <= 6'd0;
            command_range_valid_cpu <= 6'd0;
            start_toggle_cpu <= 1'b0;
            start_pending_cpu <= 1'b0;
            busy_sync_cpu <= 2'b00;
            done_sync_cpu <= 2'b00;
            done_seen_cpu <= 1'b0;
            error_meta_cpu <= 8'd0;
            error_sync_cpu <= 8'd0;
            error_cpu <= 8'd0;
            done_sticky_cpu <= 1'b0;
            completed_fence_cpu <= 32'd0;
            completed_fence_meta_cpu <= 32'd0;
            completed_fence_sync_cpu <= 32'd0;
            cpu_done <= 1'b0;
        end else begin
            busy_sync_cpu <= {busy_sync_cpu[0], busy_mem};
            done_sync_cpu <= {done_sync_cpu[0], done_toggle_mem};
            error_meta_cpu <= error_mem;
            error_sync_cpu <= error_meta_cpu;
            completed_fence_meta_cpu <= completed_fence_mem;
            completed_fence_sync_cpu <= completed_fence_meta_cpu;
            if (busy_sync_cpu[1])
                start_pending_cpu <= 1'b0;
            if (done_sync_cpu[1] != done_seen_cpu) begin
                done_seen_cpu <= done_sync_cpu[1];
                start_pending_cpu <= 1'b0;
                done_sticky_cpu <= 1'b1;
                error_cpu <= error_sync_cpu;
                completed_fence_cpu <= completed_fence_sync_cpu;
                cpu_done <= 1'b1;
            end

            if (cpu_write_stb) begin
                if (cpu_config_select) begin
                    staging_owner_cpu[cpu_reg] <= cpu_write_bank;
                    staging_valid_cpu[cpu_reg] <= 1'b1;
                    case (cpu_reg)
                        REG_DST: staging_range_valid_cpu[0] <=
                            command_cpu_write_data[31:25] == 7'd0;
                        REG_DST_PITCH: staging_range_valid_cpu[1] <=
                            command_cpu_write_data[31:16] == 16'd0;
                        REG_SRC: staging_range_valid_cpu[2] <=
                            command_cpu_write_data[31:25] == 7'd0;
                        REG_SRC_PITCH: staging_range_valid_cpu[3] <=
                            command_cpu_write_data[31:16] == 16'd0;
                        REG_PALETTE: staging_range_valid_cpu[4] <=
                            command_cpu_write_data[31:25] == 7'd0;
                        REG_WORK: staging_range_valid_cpu[5] <=
                            command_cpu_write_data[31:25] == 7'd0;
                        default: begin end
                    endcase
                end
                case (cpu_reg)
                    REG_CTRL: begin
                        if (cpu_be[0]) begin
                            reg_irq_enable <= cpu_wdata[1];
                            if (cpu_wdata[0] && !busy_visible) begin
                                command_owner_cpu <= staging_owner_cpu;
                                command_valid_cpu <= staging_valid_cpu;
                                command_range_valid_cpu <=
                                    staging_range_valid_cpu;
                                start_toggle_cpu <= ~start_toggle_cpu;
                                start_pending_cpu <= 1'b1;
                                done_sticky_cpu <= 1'b0;
                                error_cpu <= 8'd0;
                            end
                        end
                    end
                    default: begin end
                endcase
            end
        end
    end

    reg [5:0] state;
    reg [3:0] emit_return;
    reg [1:0] span_return;
    reg       line_finish_rect;
    reg [2:0] port_return;
    // The CPU-domain command snapshot is immutable from accepted START through
    // completion. Use it directly as bundled data after the synchronized start
    // toggle instead of spending a second register bank on identical fields.
    wire job_rgb565 = cfg_format_cpu[0];
    wire job_pattern_opaque = cfg_op_cpu[8];
    wire [24:0] job_dst = cfg_dst_cpu;
    wire [15:0] job_pitch = cfg_dst_pitch_cpu;
    wire signed [16:0] clip_min_x = coord_x(cfg_clip_min_cpu);
    wire signed [16:0] clip_min_y = coord_y(cfg_clip_min_cpu);
    wire signed [16:0] clip_max_x = coord_x(cfg_clip_max_cpu);
    wire signed [16:0] clip_max_y = coord_y(cfg_clip_max_cpu);
    wire [15:0] job_fg = cfg_fg_cpu;
    wire [15:0] job_bg = cfg_bg_cpu;
    wire [63:0] job_pattern = cfg_pattern_cpu;
    // Glyph opcodes are contiguous 8..11. Glyph-only states can use the low
    // mode bits directly instead of rebuilding a full-byte opcode decoder.
    wire [1:0] job_glyph_mode = cfg_op_cpu[1:0];
    wire [7:0] job_transparent_index = cfg_op_cpu[23:16];
    wire [24:0] job_src = cfg_src_cpu;
    wire [15:0] job_src_pitch = cfg_src_pitch_cpu;
    wire [24:0] job_palette = cfg_palette_cpu;
    wire [24:0] job_work = cfg_work_cpu;
    wire [15:0] job_work_entries = cfg_work_entries_cpu[15:0];
    wire signed [16:0] pattern_origin_x = coord_x(cfg_origin_cpu);
    wire signed [16:0] pattern_origin_y = coord_y(cfg_origin_cpu);

    reg signed [16:0] emit_x;
    reg signed [16:0] emit_y;
    reg [15:0] emit_color;
    reg signed [16:0] span_raw_x0;
    reg signed [16:0] span_raw_x1;
    reg signed [16:0] span_y;
    reg [15:0] span_color;
    reg signed [16:0] span_x;
    reg signed [16:0] span_end_x;
    reg span_valid_q;
    reg signed [16:0] span_clipped_x0_q;
    reg signed [16:0] span_clipped_x1_q;
    reg geometry_setup_valid_q;

    reg signed [16:0] line_setup_x0;
    reg signed [16:0] line_setup_y0;
    reg signed [16:0] line_setup_x1;
    reg signed [16:0] line_setup_y1;
    reg signed [16:0] line_x;
    reg signed [16:0] line_y;
    reg signed [16:0] line_x1;
    reg signed [16:0] line_y1;
    reg signed [17:0] line_dx;
    reg signed [17:0] line_dy;
    reg signed [17:0] line_err;
    reg signed [1:0] line_sx;
    reg signed [1:0] line_sy;

    wire signed [18:0] line_e2 =
        $signed({line_err[17], line_err}) <<< 1;
    reg line_step_x;
    reg line_step_y;

    reg signed [16:0] rect_x0;
    reg signed [16:0] rect_y0;
    reg signed [16:0] rect_x1;
    reg signed [16:0] rect_y1;
    reg [1:0] rect_edge;
    reg signed [16:0] rect_fill_y;
    reg signed [16:0] rect_fill_end_y;

    reg signed [16:0] circle_cx;
    reg signed [16:0] circle_cy;
    reg signed [16:0] circle_x;
    reg signed [16:0] circle_y;
    reg signed [19:0] circle_err;
    reg [2:0] circle_slot;
    reg circle_filled;
    wire signed [16:0] circle_step_y = circle_y + 17'sd1;
    wire signed [19:0] circle_step_y_term =
        $signed({{3{circle_step_y[16]}}, circle_step_y}) <<< 1;
    wire signed [19:0] circle_step_err =
        circle_err + 20'sd1 + circle_step_y_term;
    wire signed [19:0] circle_x_wide =
        $signed({{3{circle_x[16]}}, circle_x});
    wire circle_adjust_needed =
        (((circle_err - circle_x_wide) <<< 1) + 20'sd1) > 0;
    wire signed [19:0] circle_adjusted_err =
        circle_err + 20'sd1 - ((circle_x_wide - 20'sd1) <<< 1);

    reg [15:0] ellipse_rx;
    reg [15:0] ellipse_ry;
    reg [31:0] ellipse_rx2;
    reg [31:0] ellipse_ry2;
    reg signed [16:0] ellipse_cx;
    reg signed [16:0] ellipse_cy;
    reg signed [16:0] ellipse_x;
    reg signed [16:0] ellipse_y;
    reg signed [47:0] ellipse_dx;
    reg signed [47:0] ellipse_dy;
    reg signed [47:0] ellipse_err;
    reg [1:0] ellipse_slot;
    reg ellipse_filled;
    reg ellipse_tipping;
    reg ellipse_step_y;
    reg [1:0] ellipse_alu_phase;
    reg [2:0] geometry_step_kind;
    reg [31:0] ellipse_cross_lo;
    reg [31:0] ellipse_cross_hi;
    reg [47:0] ellipse_cross_product;

    localparam [1:0] ELL_ALU_IDLE   = 2'd0;
    localparam [1:0] ELL_ALU_INIT   = 2'd1;
    localparam [1:0] ELL_ALU_STEP_X = 2'd2;
    localparam [1:0] ELL_ALU_STEP_Y = 2'd3;

    wire signed [48:0] ellipse_e2 =
        $signed({ellipse_err[47], ellipse_err}) <<< 1;
    wire signed [47:0] ellipse_dx_step =
        $signed({16'd0, ellipse_ry2}) <<< 1;
    wire signed [47:0] ellipse_dy_step =
        $signed({16'd0, ellipse_rx2}) <<< 1;
    wire [35:0] shared_mul_product;

    reg signed [47:0] ellipse_alu_a;
    reg signed [47:0] ellipse_alu_b;
    reg ellipse_alu_sub;
    wire signed [47:0] ellipse_alu_result = ellipse_alu_sub ?
        ellipse_alu_a - ellipse_alu_b : ellipse_alu_a + ellipse_alu_b;

    always @* begin
        ellipse_alu_a = 48'sd0;
        ellipse_alu_b = 48'sd0;
        ellipse_alu_sub = 1'b0;
        case (state)
            ST_ELL_STEP: begin
                ellipse_alu_a = ellipse_dx;
                ellipse_alu_b = ellipse_dx_step;
            end
            ST_GEOM_ADJUST: begin
                ellipse_alu_a = ellipse_dy;
                ellipse_alu_b = ellipse_dy_step;
            end
            ST_ELL_MUL_RX: begin
                case (ellipse_alu_phase)
                    ELL_ALU_INIT: begin
                        ellipse_alu_a = ellipse_dx;
                        ellipse_alu_b = $signed({16'd0, ellipse_rx2});
                    end
                    ELL_ALU_STEP_X: begin
                        ellipse_alu_a = ellipse_err;
                        ellipse_alu_b = ellipse_dx;
                    end
                    ELL_ALU_STEP_Y: begin
                        ellipse_alu_a = ellipse_err;
                        ellipse_alu_b = ellipse_dy;
                    end
                    default: begin end
                endcase
            end
            default: begin end
        endcase
    end

    reg signed [16:0] geometry_alu0_a;
    reg signed [16:0] geometry_alu0_b;
    reg geometry_alu0_sub;
    reg signed [16:0] geometry_alu1_a;
    reg signed [16:0] geometry_alu1_b;
    reg geometry_alu1_sub;
    reg signed [16:0] geometry_alu2_a;
    reg signed [16:0] geometry_alu2_b;
    reg geometry_alu2_sub;
    wire signed [16:0] geometry_alu0_result = geometry_alu0_sub ?
        geometry_alu0_a - geometry_alu0_b : geometry_alu0_a + geometry_alu0_b;
    wire signed [16:0] geometry_alu1_result = geometry_alu1_sub ?
        geometry_alu1_a - geometry_alu1_b : geometry_alu1_a + geometry_alu1_b;
    wire signed [16:0] geometry_alu2_result = geometry_alu2_sub ?
        geometry_alu2_a - geometry_alu2_b : geometry_alu2_a + geometry_alu2_b;

    // Geometry point/span states consume these registered operands. Their
    // predecessor states already choose the next slot, so preparing operands
    // there removes the FSM/slot decode from the add/sub result cycle without
    // adding a drawing cycle.
    task automatic prepare_circle_geometry;
        input [2:0] slot;
        input filled;
        input signed [16:0] cx;
        input signed [16:0] cy;
        input signed [16:0] x;
        input signed [16:0] y;
        begin
            geometry_alu0_a <= cx;
            geometry_alu1_a <= filled ? cx : cy;
            geometry_alu2_a <= filled ? cy : 17'sd0;
            geometry_alu0_b <= 17'sd0;
            geometry_alu1_b <= 17'sd0;
            geometry_alu2_b <= 17'sd0;
            geometry_alu0_sub <= 1'b0;
            geometry_alu1_sub <= 1'b0;
            geometry_alu2_sub <= 1'b0;
            if (filled) begin
                geometry_alu0_sub <= 1'b1;
                geometry_alu2_sub <= slot[0];
                if (slot[1]) begin
                    geometry_alu0_b <= y;
                    geometry_alu1_b <= y;
                    geometry_alu2_b <= x;
                end else begin
                    geometry_alu0_b <= x;
                    geometry_alu1_b <= x;
                    geometry_alu2_b <= y;
                end
            end else begin
                case (slot)
                    3'd0: begin geometry_alu0_b <= x;
                                 geometry_alu1_b <= y; end
                    3'd1: begin geometry_alu0_b <= y;
                                 geometry_alu1_b <= x; end
                    3'd2: begin geometry_alu0_b <= y;
                                 geometry_alu0_sub <= 1'b1;
                                 geometry_alu1_b <= x; end
                    3'd3: begin geometry_alu0_b <= x;
                                 geometry_alu0_sub <= 1'b1;
                                 geometry_alu1_b <= y; end
                    3'd4: begin geometry_alu0_b <= x;
                                 geometry_alu0_sub <= 1'b1;
                                 geometry_alu1_b <= y;
                                 geometry_alu1_sub <= 1'b1; end
                    3'd5: begin geometry_alu0_b <= y;
                                 geometry_alu0_sub <= 1'b1;
                                 geometry_alu1_b <= x;
                                 geometry_alu1_sub <= 1'b1; end
                    3'd6: begin geometry_alu0_b <= y;
                                 geometry_alu1_b <= x;
                                 geometry_alu1_sub <= 1'b1; end
                    default: begin geometry_alu0_b <= x;
                                   geometry_alu1_b <= y;
                                   geometry_alu1_sub <= 1'b1; end
                endcase
            end
        end
    endtask

    task automatic prepare_ellipse_geometry;
        input [1:0] slot;
        input filled;
        input signed [16:0] cx;
        input signed [16:0] cy;
        input signed [16:0] x;
        input signed [16:0] y;
        begin
            geometry_alu0_a <= cx;
            geometry_alu0_b <= x;
            geometry_alu1_a <= filled ? cx : cy;
            geometry_alu1_b <= filled ? x : y;
            geometry_alu2_a <= filled ? cy : 17'sd0;
            geometry_alu2_b <= filled ? y : 17'sd0;
            geometry_alu0_sub <= filled ? 1'b0 : !slot[0];
            geometry_alu1_sub <= filled ? 1'b1 : slot[1];
            geometry_alu2_sub <= filled ? slot[0] : 1'b0;
        end
    endtask

    reg signed [16:0] pattern_x;
    reg signed [16:0] pattern_y;
    reg signed [16:0] pattern_x1;
    reg signed [16:0] pattern_y1;
    reg [5:0] pattern_bit_index;
    reg pattern_bit;
    reg pattern_bit_q;
    always @* begin
        pattern_bit_index = {
            pattern_y[2:0] - pattern_origin_y[2:0],
            pattern_x[2:0] - pattern_origin_x[2:0]
        };
        pattern_bit = job_pattern[6'd63 - pattern_bit_index];
    end

    (* ram_style = "block" *) reg [15:0] glyph_palette_mem [0:255];
    reg [15:0] glyph_palette_q;
    wire [31:0] pixel_rdata;
    reg [31:0] pixel_result_mem;
    reg [7:0] palette_index;
    reg [7:0] palette_last_index;
    reg [15:0] glyph_desc_index;
    reg [1:0] glyph_desc_word;
    reg glyph_desc_valid_q;
    reg glyph_desc_validate_pending;
    reg [24:0] glyph_source_base;
    reg [15:0] glyph_source_x;
    reg [15:0] glyph_source_y;
    reg signed [16:0] glyph_dest_x;
    reg signed [16:0] glyph_dest_y;
    reg [15:0] glyph_width;
    reg [15:0] glyph_height;
    reg [15:0] glyph_col;
    reg [15:0] glyph_row;
    reg glyph_col_last_q;
    reg glyph_row_last_q;
    reg glyph_desc_more_q;
    reg glyph_source_cache_valid;
    reg [24:0] glyph_source_cache_addr;
    reg [7:0] glyph_source_cache_byte;
    reg [7:0] glyph_index;
    reg [3:0] glyph_coverage;
    reg [15:0] blend_destination;
    reg [1:0] blend_channel;
    reg [4:0] blend_red;
    reg [5:0] blend_green;
    reg [5:0] blend_foreground_component;
    reg [5:0] blend_destination_component;
    always @* begin
        case (blend_channel)
            2'd0: begin
                blend_foreground_component = {1'b0, job_fg[15:11]};
                blend_destination_component =
                    {1'b0, blend_destination[15:11]};
            end
            2'd1: begin
                blend_foreground_component = job_fg[10:5];
                blend_destination_component = blend_destination[10:5];
            end
            default: begin
                blend_foreground_component = {1'b0, job_fg[4:0]};
                blend_destination_component =
                    {1'b0, blend_destination[4:0]};
            end
        endcase
    end
    wire signed [6:0] blend_difference =
        $signed({1'b0, blend_foreground_component}) -
        $signed({1'b0, blend_destination_component});
    wire [9:0] blend_base =
        {blend_destination_component, 4'd0} -
        {4'd0, blend_destination_component} + 10'd7;
    reg signed [6:0] blend_difference_q;
    reg [3:0] blend_coverage_q;
    reg signed [11:0] blend_product_q;
    reg [9:0] blend_base_q;
    wire signed [12:0] blend_sum =
        $signed({3'd0, blend_base_q}) +
        $signed({blend_product_q[11], blend_product_q});

    // Exact floor(n / 15) for the rounded RGB565 blend numerator. The
    // arithmetic is split across registered states and the quotient lives in
    // one synchronous ECP5 block RAM instead of a 75 MHz combinational divider.
    (* ram_style = "block" *) reg [5:0] blend_div15_lut [0:1023];
    reg [9:0] blend_div15_addr;
    reg [5:0] blend_div15_q;
    integer blend_div15_init;
    initial begin
        for (blend_div15_init = 0; blend_div15_init < 1024;
             blend_div15_init = blend_div15_init + 1)
            blend_div15_lut[blend_div15_init] =
                blend_div15_init < 960 ? blend_div15_init / 15 : 6'd63;
    end
    always @(posedge mem_clk)
        blend_div15_q <= blend_div15_lut[blend_div15_addr];

    always @(posedge mem_clk) begin
        if (state == ST_PALETTE_STORE)
            glyph_palette_mem[palette_index] <= pixel_result_mem[15:0];
        if (mem_rst)
            glyph_palette_q <= 16'd0;
        else if (state == ST_GLYPH_LOOKUP)
            glyph_palette_q <= glyph_palette_mem[glyph_index];
    end

    wire [16:0] glyph_source_pixel_x = {1'b0, glyph_source_x} +
                                                {1'b0, glyph_col};
    wire [16:0] glyph_source_pixel_y = {1'b0, glyph_source_y} +
                                                {1'b0, glyph_row};
    reg [16:0] glyph_source_byte_x;
    always @* begin
        case (job_glyph_mode)
            GLYPH_MODE_MASK1: glyph_source_byte_x =
                                glyph_source_pixel_x >> 3;
            GLYPH_MODE_A4, GLYPH_MODE_INDEX4: glyph_source_byte_x =
                                glyph_source_pixel_x >> 1;
            default: glyph_source_byte_x = glyph_source_pixel_x;
        endcase
    end
    reg [16:0] glyph_source_byte_x_q;
    reg [31:0] glyph_source_row_offset;
    reg [32:0] glyph_source_addr_q;
    reg glyph_source_addr_valid_q;
    reg glyph_source_cache_hit_q;
    wire [32:0] glyph_source_addr_next =
        {1'b0, glyph_source_row_offset} +
        {8'd0, glyph_source_base} +
        {16'd0, glyph_source_byte_x_q};
    wire [24:0] glyph_source_addr = glyph_source_addr_q[24:0];
    wire signed [16:0] glyph_pixel_x = glyph_dest_x +
                                      $signed({1'b0, glyph_col});
    wire signed [16:0] glyph_pixel_y = glyph_dest_y +
                                      $signed({1'b0, glyph_row});
    wire glyph_pixel_inside_clip = glyph_pixel_x >= clip_min_x &&
                                   glyph_pixel_x < clip_max_x &&
                                   glyph_pixel_y >= clip_min_y &&
                                   glyph_pixel_y < clip_max_y;
    reg signed [16:0] glyph_pixel_x_q;
    reg signed [16:0] glyph_pixel_y_q;
    reg glyph_pixel_inside_clip_q;
    reg [17:0] shared_mul_a;
    reg [17:0] shared_mul_b;
    reg shared_mul_pending;
    assign shared_mul_product = shared_mul_a * shared_mul_b;

    wire [25:0] glyph_desc_addr_wide = {1'b0, job_work} +
        {6'd0, glyph_desc_index, 4'd0} +
        {22'd0, glyph_desc_word, 2'd0};
    wire [25:0] glyph_palette_addr_wide = {1'b0, job_palette} +
                                          {17'd0, palette_index, 1'b0};
    wire [25:0] glyph_desc_source_sum = {1'b0, job_src} +
                                         {1'b0, pixel_result_mem[24:0]};

    reg signed [16:0] flood_x;
    reg signed [16:0] flood_y;
    reg signed [16:0] flood_scan_x;
    reg signed [16:0] flood_push_x;
    reg signed [16:0] flood_push_y;
    reg [15:0] flood_target;
    reg [15:0] flood_queue_count;
    reg [1:0] queue_return;
    reg [2:0] flood_read_kind;
    reg flood_neighbor_below;
    reg flood_span_above;
    reg flood_span_below;
    reg flood_pop_inside_q;
    localparam [2:0] FLOOD_READ_SEED = 3'd0;
    localparam [2:0] FLOOD_READ_POP = 3'd1;
    localparam [2:0] FLOOD_READ_SEEK = 3'd2;
    localparam [2:0] FLOOD_READ_SCAN = 3'd3;
    localparam [2:0] FLOOD_READ_NEIGHBOR = 3'd4;
    localparam [1:0] DISPATCH_FLOOD_POP_RESULT = 2'd0;
    localparam [1:0] DISPATCH_FLOOD_PUSH_ADDR  = 2'd1;
    localparam [1:0] DISPATCH_FLOOD_POP_ADDR   = 2'd2;
    localparam [1:0] DISPATCH_GLYPH_SOURCE     = 2'd3;
    wire [15:0] port_pixel_value = job_rgb565 ? pixel_result_mem[15:0] :
                                                {8'd0, pixel_result_mem[7:0]};
    wire [15:0] job_fill_value = job_rgb565 ? job_fg : {8'd0, job_fg[7:0]};
    wire [25:0] flood_push_addr_next = {1'b0, job_work} +
                                       {8'd0, flood_queue_count, 2'b00};
    wire [25:0] flood_pop_addr_next = {1'b0, job_work} +
        {8'd0, flood_queue_count - 16'd1, 2'b00};
    reg [25:0] flood_queue_addr_q;
    reg [1:0] dispatch_kind;

    wire emit_inside_clip = emit_x >= clip_min_x && emit_x < clip_max_x &&
                            emit_y >= clip_min_y && emit_y < clip_max_y;
    wire signed [16:0] span_clipped_x0 = span_raw_x0 < clip_min_x ?
                                         clip_min_x : span_raw_x0;
    wire signed [16:0] span_clipped_x1 = span_raw_x1 >= clip_max_x ?
                                         clip_max_x - 17'sd1 : span_raw_x1;
    reg [31:0] emit_row_offset;
    reg [31:0] emit_addr_q;
    reg pixel_access_write;
    reg [1:0] pixel_access_size;
    reg [31:0] pixel_access_wdata;
    wire [31:0] emit_addr_next = emit_row_offset +
        {7'd0, job_dst} +
        ({16'd0, emit_x[15:0]} << job_rgb565);
    wire emit_addr_valid = emit_addr_q[31:25] == 7'd0;

    reg pixel_start;
    reg pixel_write;
    reg [1:0] pixel_size;
    reg [24:0] pixel_addr;
    reg [31:0] pixel_wdata;
    wire pixel_busy;
    wire pixel_done;
    astraea_pixel_port pixel_port_i (
        .clk(mem_clk), .rst(mem_rst), .start(pixel_start),
        .write(pixel_write), .size(pixel_size), .byte_addr(pixel_addr),
        .wdata(pixel_wdata), .rdata(pixel_rdata),
        .busy(pixel_busy), .done(pixel_done),
        .mem_lock(mem_lock), .mem_valid(mem_valid), .mem_ready(mem_ready),
        .mem_write(mem_write), .mem_addr(mem_addr), .mem_be(mem_be),
        .mem_wdata(mem_wdata), .mem_rsp_valid(mem_rsp_valid),
        .mem_rdata(mem_rdata)
    );

    wire cfg_is_glyph = cfg_op_cpu[7:0] >= OP_GLYPH_MASK1 &&
                        cfg_op_cpu[7:0] <= OP_GLYPH_INDEX8;
    wire [25:0] cfg_palette_end = {1'b0, cfg_palette_cpu} +
        ((cfg_op_cpu[7:0] == OP_GLYPH_INDEX8 ? 26'd256 : 26'd16) << 1);
    wire [25:0] cfg_work_end = {1'b0, cfg_work_cpu} +
        ({10'd0, cfg_work_entries_cpu[15:0]} <<
         (cfg_is_glyph ? 4 : 2));
    wire cfg_single_glyph_valid = cfg_work_entries_cpu[15:0] != 16'd0 ||
        (cfg_src_size_cpu[15:0] != 16'd0 &&
         cfg_src_size_cpu[31:16] != 16'd0 &&
         !cfg_src_size_cpu[15] && !cfg_src_size_cpu[31] &&
         !cfg_p1_cpu[15] && !cfg_p1_cpu[31] &&
         ({1'b0, cfg_p1_cpu[15:0]} + {1'b0, cfg_src_size_cpu[15:0]} <=
          17'h10000) &&
         ({1'b0, cfg_p1_cpu[31:16]} + {1'b0, cfg_src_size_cpu[31:16]} <=
          17'h10000));
    wire cfg_op_flags_valid =
        ((cfg_op_cpu[7:0] == OP_PATTERN_FILL ||
          cfg_op_cpu[7:0] == OP_GLYPH_MASK1) ?
             cfg_op_cpu[23:9] == 15'd0 :
         (cfg_op_cpu[7:0] == OP_GLYPH_INDEX4 ||
          cfg_op_cpu[7:0] == OP_GLYPH_INDEX8) ?
             cfg_op_cpu[15:9] == 7'd0 : cfg_op_cpu[23:8] == 16'd0);
    wire cfg_glyph_source_valid = !cfg_is_glyph ||
        (cfg_src_addr_valid_cpu && cfg_src_pitch_valid_cpu &&
         cfg_src_pitch_cpu != 16'd0 &&
         cfg_work_entries_cpu[31:16] == 16'd0 &&
         cfg_single_glyph_valid);
    wire cfg_glyph_work_valid = !cfg_is_glyph ||
        cfg_work_entries_cpu[15:0] == 16'd0 ||
        (cfg_work_addr_valid_cpu && cfg_work_end <= 26'h2000000);
    wire cfg_glyph_format_valid = !cfg_is_glyph ||
        cfg_op_cpu[7:0] != OP_GLYPH_A4 || cfg_format_cpu[0];
    wire cfg_glyph_palette_valid = !cfg_is_glyph ||
        (cfg_op_cpu[7:0] != OP_GLYPH_INDEX4 &&
         cfg_op_cpu[7:0] != OP_GLYPH_INDEX8) ||
        (cfg_palette_addr_valid_cpu && cfg_palette_end <= 26'h2000000);
    wire cfg_flood_valid = cfg_op_cpu[7:0] != OP_FLOOD_FILL ||
        (cfg_work_addr_valid_cpu && cfg_work_entries_cpu[31:16] == 16'd0 &&
         cfg_work_entries_cpu[15:0] != 16'd0 &&
         cfg_work_end <= 26'h2000000);
    wire cfg_base_valid = cfg_op_cpu[7:0] <= OP_FLOOD_FILL &&
                          cfg_op_cpu[31:24] == 8'd0 &&
                          cfg_op_cpu[15:9] == 7'd0 &&
                          cfg_op_flags_valid &&
                          cfg_format_cpu <= 32'd1 &&
                          cfg_dst_addr_valid_cpu &&
                          cfg_dst_pitch_valid_cpu &&
                          cfg_dst_pitch_cpu != 16'd0;
    wire cfg_clip_valid = coord_x(cfg_clip_min_cpu) >= 0 &&
                          coord_y(cfg_clip_min_cpu) >= 0 &&
                          coord_x(cfg_clip_max_cpu) >
                              coord_x(cfg_clip_min_cpu) &&
                          coord_y(cfg_clip_max_cpu) >
                              coord_y(cfg_clip_min_cpu);
    wire cfg_radius_valid =
        !((cfg_op_cpu[7:0] == OP_CIRCLE ||
           cfg_op_cpu[7:0] == OP_CIRCLE_FILL) && cfg_radii_cpu[15]) &&
        !((cfg_op_cpu[7:0] == OP_ELLIPSE ||
           cfg_op_cpu[7:0] == OP_ELLIPSE_FILL) &&
          (cfg_radii_cpu[31] || cfg_radii_cpu[15]));
    wire config_valid_next = cfg_base_valid_q &&
        cfg_clip_valid_q && cfg_radius_valid_q &&
        cfg_glyph_source_valid_q && cfg_glyph_work_valid_q &&
        cfg_glyph_format_valid_q && cfg_glyph_palette_valid_q &&
        cfg_flood_valid_q;

    task automatic dispatch_command;
        begin
            case (cfg_op_cpu[7:0])
                OP_LINE: begin
                    line_setup_x0 <= coord_x(cfg_p0_cpu);
                    line_setup_y0 <= coord_y(cfg_p0_cpu);
                    line_setup_x1 <= coord_x(cfg_p1_cpu);
                    line_setup_y1 <= coord_y(cfg_p1_cpu);
                    line_finish_rect <= 1'b0;
                    state <= ST_LINE_SETUP;
                end
                OP_RECT, OP_RECT_FILL, OP_PATTERN_FILL: begin
                    rect_x0 <= coord_x(cfg_p0_cpu) < coord_x(cfg_p1_cpu) ?
                               coord_x(cfg_p0_cpu) : coord_x(cfg_p1_cpu);
                    rect_x1 <= coord_x(cfg_p0_cpu) > coord_x(cfg_p1_cpu) ?
                               coord_x(cfg_p0_cpu) : coord_x(cfg_p1_cpu);
                    rect_y0 <= coord_y(cfg_p0_cpu) < coord_y(cfg_p1_cpu) ?
                               coord_y(cfg_p0_cpu) : coord_y(cfg_p1_cpu);
                    rect_y1 <= coord_y(cfg_p0_cpu) > coord_y(cfg_p1_cpu) ?
                               coord_y(cfg_p0_cpu) : coord_y(cfg_p1_cpu);
                    rect_edge <= 2'd0;
                    rect_fill_y <= coord_y(cfg_p0_cpu) < coord_y(cfg_p1_cpu) ?
                                   coord_y(cfg_p0_cpu) : coord_y(cfg_p1_cpu);
                    pattern_x <= coord_x(cfg_p0_cpu) < coord_x(cfg_p1_cpu) ?
                                 coord_x(cfg_p0_cpu) : coord_x(cfg_p1_cpu);
                    pattern_y <= coord_y(cfg_p0_cpu) < coord_y(cfg_p1_cpu) ?
                                 coord_y(cfg_p0_cpu) : coord_y(cfg_p1_cpu);
                    pattern_x1 <= coord_x(cfg_p0_cpu) > coord_x(cfg_p1_cpu) ?
                                  coord_x(cfg_p0_cpu) : coord_x(cfg_p1_cpu);
                    pattern_y1 <= coord_y(cfg_p0_cpu) > coord_y(cfg_p1_cpu) ?
                                  coord_y(cfg_p0_cpu) : coord_y(cfg_p1_cpu);
                    state <= cfg_op_cpu[7:0] == OP_RECT ? ST_RECT_EDGE :
                             cfg_op_cpu[7:0] == OP_RECT_FILL ?
                             ST_RECT_FILL_SETUP : ST_PATTERN_SETUP;
                end
                OP_CIRCLE, OP_CIRCLE_FILL: begin
                    circle_cx <= coord_x(cfg_p0_cpu);
                    circle_cy <= coord_y(cfg_p0_cpu);
                    circle_x <= {1'b0, cfg_radii_cpu[15:0]};
                    circle_y <= 17'sd0;
                    circle_err <= 20'sd0;
                    circle_slot <= 3'd0;
                    circle_filled <= cfg_op_cpu[7:0] == OP_CIRCLE_FILL;
                    prepare_circle_geometry(
                        3'd0,
                        cfg_op_cpu[7:0] == OP_CIRCLE_FILL,
                        coord_x(cfg_p0_cpu),
                        coord_y(cfg_p0_cpu),
                        $signed({1'b0, cfg_radii_cpu[15:0]}),
                        17'sd0
                    );
                    state <= cfg_op_cpu[7:0] == OP_CIRCLE_FILL ?
                             ST_CIRCLE_SPAN : ST_CIRCLE_POINT;
                end
                OP_ELLIPSE, OP_ELLIPSE_FILL: begin
                    ellipse_cx <= coord_x(cfg_p0_cpu);
                    ellipse_cy <= coord_y(cfg_p0_cpu);
                    ellipse_rx <= cfg_radii_cpu[15:0];
                    ellipse_ry <= cfg_radii_cpu[31:16];
                    ellipse_filled <= cfg_op_cpu[7:0] == OP_ELLIPSE_FILL;
                    ellipse_tipping <= 1'b0;
                    ellipse_alu_phase <= ELL_ALU_IDLE;
                    ellipse_slot <= 2'd0;
                    if (cfg_radii_cpu[15:0] == 16'd0) begin
                        line_setup_x0 <= coord_x(cfg_p0_cpu);
                        line_setup_x1 <= coord_x(cfg_p0_cpu);
                        line_setup_y0 <= coord_y(cfg_p0_cpu) -
                            $signed({1'b0, cfg_radii_cpu[31:16]});
                        line_setup_y1 <= coord_y(cfg_p0_cpu) +
                            $signed({1'b0, cfg_radii_cpu[31:16]});
                        line_finish_rect <= 1'b0;
                        state <= ST_LINE_SETUP;
                    end else if (cfg_radii_cpu[31:16] == 16'd0) begin
                        line_setup_x0 <= coord_x(cfg_p0_cpu) -
                            $signed({1'b0, cfg_radii_cpu[15:0]});
                        line_setup_x1 <= coord_x(cfg_p0_cpu) +
                            $signed({1'b0, cfg_radii_cpu[15:0]});
                        line_setup_y0 <= coord_y(cfg_p0_cpu);
                        line_setup_y1 <= coord_y(cfg_p0_cpu);
                        line_finish_rect <= 1'b0;
                        state <= ST_LINE_SETUP;
                    end else begin
                        state <= ST_ELL_MUL_RX;
                    end
                end
                OP_GLYPH_MASK1, OP_GLYPH_A4,
                OP_GLYPH_INDEX4, OP_GLYPH_INDEX8: begin
                    glyph_desc_index <= 16'd0;
                    glyph_desc_word <= 2'd0;
                    glyph_source_base <= cfg_src_cpu;
                    glyph_source_x <= cfg_p1_cpu[15:0];
                    glyph_source_y <= cfg_p1_cpu[31:16];
                    glyph_dest_x <= coord_x(cfg_p0_cpu);
                    glyph_dest_y <= coord_y(cfg_p0_cpu);
                    glyph_width <= cfg_src_size_cpu[15:0];
                    glyph_height <= cfg_src_size_cpu[31:16];
                    glyph_source_cache_valid <= 1'b0;
                    if (cfg_op_cpu[7:0] == OP_GLYPH_INDEX4 ||
                        cfg_op_cpu[7:0] == OP_GLYPH_INDEX8) begin
                        palette_index <= 8'd0;
                        palette_last_index <=
                            cfg_op_cpu[7:0] == OP_GLYPH_INDEX8 ?
                            8'hff : 8'h0f;
                        state <= ST_PALETTE_ISSUE;
                    end else begin
                        state <= cfg_work_entries_cpu[15:0] != 16'd0 ?
                                 ST_GLYPH_DESC_ISSUE : ST_GLYPH_SETUP;
                    end
                end
                OP_FLOOD_FILL: begin
                    flood_x <= coord_x(cfg_p0_cpu);
                    flood_y <= coord_y(cfg_p0_cpu);
                    flood_queue_count <= 16'd0;
                    flood_span_above <= 1'b0;
                    flood_span_below <= 1'b0;
                    state <= ST_FLOOD_INIT;
                end
                default: state <= ST_FINISH;
            endcase
        end
    endtask

    always @(posedge mem_clk) begin
        pixel_start <= 1'b0;
        if (mem_rst) begin
            start_sync_mem <= 2'b00;
            start_seen_mem <= 1'b0;
            busy_mem <= 1'b0;
            done_toggle_mem <= 1'b0;
            finish_pending_mem <= 1'b0;
            error_mem <= 8'd0;
            completed_fence_mem <= 32'd0;
            config_read_index <= 5'd0;
            config_capture_index <= 5'd0;
            config_capture_valid <= 1'b0;
            config_phase <= 3'd0;
            config_valid_q <= 1'b0;
            cfg_base_valid_q <= 1'b0;
            cfg_clip_valid_q <= 1'b0;
            cfg_radius_valid_q <= 1'b0;
            cfg_glyph_source_valid_q <= 1'b0;
            cfg_glyph_work_valid_q <= 1'b0;
            cfg_glyph_format_valid_q <= 1'b0;
            cfg_glyph_palette_valid_q <= 1'b0;
            cfg_flood_valid_q <= 1'b0;
            cfg_dst_cpu <= 25'd0;
            cfg_dst_pitch_cpu <= 16'd0;
            cfg_format_cpu <= 32'd0;
            cfg_clip_min_cpu <= 32'd0;
            cfg_clip_max_cpu <= 32'd0;
            cfg_p0_cpu <= 32'd0;
            cfg_p1_cpu <= 32'd0;
            cfg_radii_cpu <= 32'd0;
            cfg_fg_cpu <= 16'd0;
            cfg_bg_cpu <= 16'd0;
            cfg_pattern_cpu <= 64'd0;
            cfg_origin_cpu <= 32'd0;
            cfg_src_cpu <= 25'd0;
            cfg_src_pitch_cpu <= 16'd0;
            cfg_src_size_cpu <= 32'd0;
            cfg_palette_cpu <= 25'd0;
            cfg_work_cpu <= 25'd0;
            cfg_work_entries_cpu <= 32'd0;
            cfg_op_cpu <= 32'd0;
            cfg_fence_cpu <= 32'd0;
            cfg_dst_addr_valid_cpu <= 1'b1;
            cfg_src_addr_valid_cpu <= 1'b1;
            cfg_palette_addr_valid_cpu <= 1'b1;
            cfg_work_addr_valid_cpu <= 1'b1;
            cfg_dst_pitch_valid_cpu <= 1'b1;
            cfg_src_pitch_valid_cpu <= 1'b1;
            state <= ST_IDLE;
            emit_return <= EMIT_RET_FINISH;
            span_return <= SPAN_RET_RECT_NEXT;
            line_finish_rect <= 1'b0;
            port_return <= PORT_RET_EMIT;
            emit_x <= 17'sd0;
            emit_y <= 17'sd0;
            emit_color <= 16'd0;
            span_raw_x0 <= 17'sd0;
            span_raw_x1 <= 17'sd0;
            span_y <= 17'sd0;
            span_color <= 16'd0;
            span_x <= 17'sd0;
            span_end_x <= 17'sd0;
            span_valid_q <= 1'b0;
            span_clipped_x0_q <= 17'sd0;
            span_clipped_x1_q <= 17'sd0;
            geometry_setup_valid_q <= 1'b0;
            line_setup_x0 <= 17'sd0;
            line_setup_y0 <= 17'sd0;
            line_setup_x1 <= 17'sd0;
            line_setup_y1 <= 17'sd0;
            line_x <= 17'sd0;
            line_y <= 17'sd0;
            line_x1 <= 17'sd0;
            line_y1 <= 17'sd0;
            line_dx <= 18'sd0;
            line_dy <= 18'sd0;
            line_err <= 18'sd0;
            line_sx <= 2'sd0;
            line_sy <= 2'sd0;
            line_step_x <= 1'b0;
            line_step_y <= 1'b0;
            rect_x0 <= 17'sd0;
            rect_y0 <= 17'sd0;
            rect_x1 <= 17'sd0;
            rect_y1 <= 17'sd0;
            rect_edge <= 2'd0;
            rect_fill_y <= 17'sd0;
            rect_fill_end_y <= 17'sd0;
            circle_cx <= 17'sd0;
            circle_cy <= 17'sd0;
            circle_x <= 17'sd0;
            circle_y <= 17'sd0;
            circle_err <= 20'sd0;
            circle_slot <= 3'd0;
            circle_filled <= 1'b0;
            ellipse_rx <= 16'd0;
            ellipse_ry <= 16'd0;
            ellipse_rx2 <= 32'd0;
            ellipse_ry2 <= 32'd0;
            ellipse_cx <= 17'sd0;
            ellipse_cy <= 17'sd0;
            ellipse_x <= 17'sd0;
            ellipse_y <= 17'sd0;
            ellipse_dx <= 48'sd0;
            ellipse_dy <= 48'sd0;
            ellipse_err <= 48'sd0;
            ellipse_slot <= 2'd0;
            ellipse_filled <= 1'b0;
            ellipse_tipping <= 1'b0;
            ellipse_step_y <= 1'b0;
            ellipse_alu_phase <= ELL_ALU_IDLE;
            geometry_step_kind <= GEOM_CIRCLE;
            geometry_alu0_a <= 17'sd0;
            geometry_alu0_b <= 17'sd0;
            geometry_alu0_sub <= 1'b0;
            geometry_alu1_a <= 17'sd0;
            geometry_alu1_b <= 17'sd0;
            geometry_alu1_sub <= 1'b0;
            geometry_alu2_a <= 17'sd0;
            geometry_alu2_b <= 17'sd0;
            geometry_alu2_sub <= 1'b0;
            ellipse_cross_lo <= 32'd0;
            ellipse_cross_hi <= 32'd0;
            ellipse_cross_product <= 48'd0;
            shared_mul_a <= 18'd0;
            shared_mul_b <= 18'd0;
            shared_mul_pending <= 1'b0;
            pattern_x <= 17'sd0;
            pattern_y <= 17'sd0;
            pattern_x1 <= 17'sd0;
            pattern_y1 <= 17'sd0;
            pattern_bit_q <= 1'b0;
            palette_index <= 8'd0;
            palette_last_index <= 8'd0;
            glyph_desc_index <= 16'd0;
            glyph_desc_word <= 2'd0;
            glyph_desc_valid_q <= 1'b0;
            glyph_desc_validate_pending <= 1'b0;
            glyph_source_base <= 25'd0;
            glyph_source_x <= 16'd0;
            glyph_source_y <= 16'd0;
            glyph_dest_x <= 17'sd0;
            glyph_dest_y <= 17'sd0;
            glyph_width <= 16'd0;
            glyph_height <= 16'd0;
            glyph_col <= 16'd0;
            glyph_row <= 16'd0;
            glyph_col_last_q <= 1'b0;
            glyph_row_last_q <= 1'b0;
            glyph_desc_more_q <= 1'b0;
            glyph_source_byte_x_q <= 17'd0;
            glyph_source_row_offset <= 32'd0;
            glyph_source_addr_q <= 33'd0;
            glyph_source_addr_valid_q <= 1'b0;
            glyph_source_cache_hit_q <= 1'b0;
            glyph_pixel_x_q <= 17'sd0;
            glyph_pixel_y_q <= 17'sd0;
            glyph_pixel_inside_clip_q <= 1'b0;
            glyph_source_cache_valid <= 1'b0;
            glyph_source_cache_addr <= 25'd0;
            glyph_source_cache_byte <= 8'd0;
            glyph_index <= 8'd0;
            glyph_coverage <= 4'd0;
            blend_destination <= 16'd0;
            blend_channel <= 2'd0;
            blend_red <= 5'd0;
            blend_green <= 6'd0;
            blend_difference_q <= 7'sd0;
            blend_coverage_q <= 4'd0;
            blend_product_q <= 12'sd0;
            blend_base_q <= 10'd0;
            blend_div15_addr <= 10'd0;
            flood_x <= 17'sd0;
            flood_y <= 17'sd0;
            flood_scan_x <= 17'sd0;
            flood_push_x <= 17'sd0;
            flood_push_y <= 17'sd0;
            flood_target <= 16'd0;
            flood_queue_count <= 16'd0;
            queue_return <= QUEUE_RET_POP;
            flood_read_kind <= FLOOD_READ_SEED;
            flood_neighbor_below <= 1'b0;
            flood_span_above <= 1'b0;
            flood_span_below <= 1'b0;
            flood_pop_inside_q <= 1'b0;
            flood_queue_addr_q <= 26'd0;
            dispatch_kind <= DISPATCH_FLOOD_POP_RESULT;
            emit_row_offset <= 32'd0;
            emit_addr_q <= 32'd0;
            pixel_access_write <= 1'b0;
            pixel_access_size <= 2'd0;
            pixel_access_wdata <= 32'd0;
            pixel_start <= 1'b0;
            pixel_write <= 1'b0;
            pixel_size <= 2'd0;
            pixel_addr <= 25'd0;
            pixel_wdata <= 32'd0;
            pixel_result_mem <= 32'd0;
        end else begin
            start_sync_mem <= {start_sync_mem[0], start_toggle_cpu};

            if (finish_pending_mem) begin
                finish_pending_mem <= 1'b0;
                busy_mem <= 1'b0;
                completed_fence_mem <= job_fence_mem;
                done_toggle_mem <= ~done_toggle_mem;
            end else if (!busy_mem &&
                start_sync_mem[1] != start_seen_mem) begin
                start_seen_mem <= start_sync_mem[1];
                error_mem <= 8'd0;
                busy_mem <= 1'b1;
                config_read_index <= 5'd0;
                config_capture_index <= 5'd0;
                config_capture_valid <= 1'b0;
                config_phase <= 3'd1;
                shared_mul_pending <= 1'b0;
                state <= ST_IDLE;
            end else if (busy_mem) begin
                if (config_phase == 3'd1) begin
                    if (!config_capture_valid) begin
                        config_capture_valid <= 1'b1;
                        config_capture_index <= config_read_index;
                        config_read_index <= 5'd1;
                    end else begin
                        case (config_capture_index)
                            REG_DST: begin
                                cfg_dst_cpu <= command_mem_value[24:0];
                                cfg_dst_addr_valid_cpu <=
                                    command_range_valid_cpu[0];
                            end
                            REG_DST_PITCH: begin
                                cfg_dst_pitch_cpu <= command_mem_value[15:0];
                                cfg_dst_pitch_valid_cpu <=
                                    command_range_valid_cpu[1];
                            end
                            REG_FORMAT: cfg_format_cpu <= command_mem_value;
                            REG_CLIP_MIN: cfg_clip_min_cpu <= command_mem_value;
                            REG_CLIP_MAX: cfg_clip_max_cpu <= command_mem_value;
                            REG_P0: cfg_p0_cpu <= command_mem_value;
                            REG_P1: cfg_p1_cpu <= command_mem_value;
                            REG_RADII: cfg_radii_cpu <= command_mem_value;
                            REG_FG: cfg_fg_cpu <= command_mem_value[15:0];
                            REG_BG: cfg_bg_cpu <= command_mem_value[15:0];
                            REG_PATTERN_HI:
                                cfg_pattern_cpu[63:32] <= command_mem_value;
                            REG_PATTERN_LO:
                                cfg_pattern_cpu[31:0] <= command_mem_value;
                            REG_ORIGIN: cfg_origin_cpu <= command_mem_value;
                            REG_SRC: begin
                                cfg_src_cpu <= command_mem_value[24:0];
                                cfg_src_addr_valid_cpu <=
                                    command_range_valid_cpu[2];
                            end
                            REG_SRC_PITCH: begin
                                cfg_src_pitch_cpu <= command_mem_value[15:0];
                                cfg_src_pitch_valid_cpu <=
                                    command_range_valid_cpu[3];
                            end
                            REG_SRC_SIZE: cfg_src_size_cpu <= command_mem_value;
                            REG_PALETTE: begin
                                cfg_palette_cpu <= command_mem_value[24:0];
                                cfg_palette_addr_valid_cpu <=
                                    command_range_valid_cpu[4];
                            end
                            REG_WORK: begin
                                cfg_work_cpu <= command_mem_value[24:0];
                                cfg_work_addr_valid_cpu <=
                                    command_range_valid_cpu[5];
                            end
                            REG_WORK_ENTRIES:
                                cfg_work_entries_cpu <= command_mem_value;
                            REG_OP: cfg_op_cpu <= command_mem_value;
                            REG_FENCE: cfg_fence_cpu <= command_mem_value;
                            default: begin end
                        endcase

                        if (config_capture_index == REG_FENCE) begin
                            config_capture_valid <= 1'b0;
                            config_phase <= 3'd2;
                        end else begin
                            config_capture_index <= config_read_index;
                            if (config_read_index == REG_OP)
                                config_read_index <= REG_FENCE;
                            else if (config_read_index < REG_OP)
                                config_read_index <= config_read_index + 5'd1;
                        end
                    end
                end else if (config_phase == 3'd2) begin
                    cfg_base_valid_q <= cfg_base_valid;
                    cfg_clip_valid_q <= cfg_clip_valid;
                    cfg_radius_valid_q <= cfg_radius_valid;
                    cfg_glyph_source_valid_q <= cfg_glyph_source_valid;
                    cfg_glyph_work_valid_q <= cfg_glyph_work_valid;
                    cfg_glyph_format_valid_q <= cfg_glyph_format_valid;
                    cfg_glyph_palette_valid_q <= cfg_glyph_palette_valid;
                    cfg_flood_valid_q <= cfg_flood_valid;
                    config_phase <= 3'd3;
                end else if (config_phase == 3'd3) begin
                    config_valid_q <= config_valid_next;
                    config_phase <= 3'd4;
                end else if (config_phase == 3'd4) begin
                    config_phase <= 3'd0;
                    if (!config_valid_q) begin
                        error_mem <= 8'd1;
                        completed_fence_mem <= cfg_fence_cpu;
                        busy_mem <= 1'b0;
                        done_toggle_mem <= ~done_toggle_mem;
                        shared_mul_pending <= 1'b0;
                        state <= ST_IDLE;
                    end else begin
                        dispatch_command();
                    end
                end else begin
                case (state)
                    ST_FLOOD_POP_CHECK: begin
                        case (dispatch_kind)
                            DISPATCH_FLOOD_PUSH_ADDR: begin
                                if (flood_queue_addr_q[25]) begin
                                    error_mem <= 8'd4;
                                    state <= ST_FINISH;
                                end else begin
                                    pixel_write <= 1'b1;
                                    pixel_size <= 2'd2;
                                    pixel_addr <= flood_queue_addr_q[24:0];
                                    pixel_wdata <= {flood_push_y[15:0],
                                                    flood_push_x[15:0]};
                                    pixel_start <= 1'b1;
                                    port_return <=
                                        PORT_RET_FLOOD_PUSH_DONE;
                                    state <= ST_PORT_WAIT;
                                end
                            end
                            DISPATCH_FLOOD_POP_ADDR: begin
                                if (flood_queue_addr_q[25]) begin
                                    error_mem <= 8'd4;
                                    state <= ST_FINISH;
                                end else begin
                                    pixel_write <= 1'b0;
                                    pixel_size <= 2'd2;
                                    pixel_addr <= flood_queue_addr_q[24:0];
                                    pixel_wdata <= 32'd0;
                                    pixel_start <= 1'b1;
                                    port_return <= PORT_RET_FLOOD_POP_DONE;
                                    state <= ST_PORT_WAIT;
                                end
                            end
                            DISPATCH_GLYPH_SOURCE: begin
                                if (!glyph_source_addr_valid_q) begin
                                    error_mem <= 8'd4;
                                    state <= ST_FINISH;
                                end else if (glyph_source_cache_hit_q) begin
                                    state <= ST_GLYPH_DECODE;
                                end else begin
                                    pixel_write <= 1'b0;
                                    pixel_size <= 2'd0;
                                    pixel_addr <= glyph_source_addr;
                                    pixel_wdata <= 32'd0;
                                    pixel_start <= 1'b1;
                                    port_return <=
                                        PORT_RET_GLYPH_SOURCE_GET;
                                    state <= ST_PORT_WAIT;
                                end
                            end
                            default: begin
                                if (!flood_pop_inside_q) begin
                                    state <= ST_FLOOD_POP;
                                end else begin
                                    emit_x <= flood_x;
                                    emit_y <= flood_y;
                                    flood_read_kind <= FLOOD_READ_POP;
                                    state <= ST_FLOOD_READ;
                                end
                            end
                        endcase
                    end
                    ST_EMIT: begin
                        if (emit_inside_clip) begin
                            if (!shared_mul_pending) begin
                                shared_mul_a <= {1'b0, emit_y};
                                shared_mul_b <= {2'd0, job_pitch};
                                shared_mul_pending <= 1'b1;
                            end else begin
                                emit_row_offset <= shared_mul_product[31:0];
                                shared_mul_pending <= 1'b0;
                                pixel_access_write <= 1'b1;
                                pixel_access_size <= job_rgb565 ? 2'd1 : 2'd0;
                                pixel_access_wdata <= {16'd0, emit_color};
                                port_return <= PORT_RET_EMIT;
                                state <= ST_PIXEL_ADDR;
                            end
                        end else begin
                            shared_mul_pending <= 1'b0;
                            case (emit_return)
                                EMIT_RET_PATTERN_NEXT:
                                    state <= ST_PATTERN_NEXT;
                                EMIT_RET_SPAN_ADVANCE:
                                    state <= ST_SPAN_ADVANCE;
                                EMIT_RET_LINE_STEP:
                                    state <= ST_LINE_STEP;
                                EMIT_RET_FINISH:
                                    state <= ST_FINISH;
                                EMIT_RET_RECT_NEXT:
                                    state <= ST_RECT_NEXT;
                                EMIT_RET_CIRCLE_SLOT:
                                    state <= ST_CIRCLE_SLOT;
                                EMIT_RET_ELL_SLOT:
                                    state <= ST_ELL_SLOT;
                                EMIT_RET_GLYPH_ADVANCE:
                                    state <= ST_GLYPH_ADVANCE;
                                EMIT_RET_FLOOD_NEIGHBOR:
                                    state <= ST_FLOOD_NEIGHBOR;
                                default: state <= ST_FINISH;
                            endcase
                        end
                    end
                    ST_GEOM_ADJUST: begin
                        case (geometry_step_kind)
                            GEOM_ELLIPSE: begin
                                if (ellipse_step_y) begin
                                    ellipse_y <= ellipse_y + 17'sd1;
                                    ellipse_dy <= ellipse_alu_result;
                                    ellipse_alu_phase <= ELL_ALU_STEP_Y;
                                    state <= ST_ELL_MUL_RX;
                                end else begin
                                    state <= ST_GEOM_DECIDE;
                                end
                            end
                            GEOM_LINE: begin
                                if (line_step_y) begin
                                    line_y <= line_y +
                                        {{15{line_sy[1]}}, line_sy};
                                    line_err <= line_err + line_dx;
                                end
                                state <= ST_GEOM_DECIDE;
                            end
                            default: begin
                                if (circle_adjust_needed) begin
                                    circle_x <= circle_x - 17'sd1;
                                    circle_err <= circle_adjusted_err;
                                end
                                state <= ST_GEOM_DECIDE;
                            end
                        endcase
                    end
                    ST_GEOM_DECIDE: begin
                        case (geometry_step_kind)
                            GEOM_ELLIPSE: begin
                                ellipse_slot <= 2'd0;
                                if (ellipse_x > 17'sd0) begin
                                    if (ellipse_y <
                                        $signed({1'b0, ellipse_ry})) begin
                                        ellipse_x <= 17'sd0;
                                        ellipse_y <= ellipse_y + 17'sd1;
                                        ellipse_tipping <= 1'b1;
                                        prepare_ellipse_geometry(
                                            2'd0,
                                            ellipse_filled,
                                            ellipse_cx,
                                            ellipse_cy,
                                            17'sd0,
                                            ellipse_y + 17'sd1
                                        );
                                        state <= ellipse_filled ?
                                            ST_ELL_SPAN : ST_ELL_POINT;
                                    end else begin
                                        state <= ST_FINISH;
                                    end
                                end else begin
                                    prepare_ellipse_geometry(
                                        2'd0,
                                        ellipse_filled,
                                        ellipse_cx,
                                        ellipse_cy,
                                        ellipse_x,
                                        ellipse_y
                                    );
                                    state <= ellipse_filled ? ST_ELL_SPAN :
                                                                ST_ELL_POINT;
                                end
                            end
                            GEOM_LINE: state <= ST_LINE_POINT;
                            GEOM_PATTERN: begin
                                if (pattern_bit_q || job_pattern_opaque) begin
                                    emit_x <= pattern_x;
                                    emit_y <= pattern_y;
                                    emit_color <= pattern_bit_q ? job_fg :
                                                                 job_bg;
                                    emit_return <= EMIT_RET_PATTERN_NEXT;
                                    state <= ST_EMIT;
                                end else begin
                                    state <= ST_PATTERN_NEXT;
                                end
                            end
                            GEOM_SPAN: begin
                                if (!span_valid_q) begin
                                    case (span_return)
                                        SPAN_RET_RECT_NEXT:
                                            state <= ST_RECT_FILL_NEXT;
                                        SPAN_RET_CIRCLE_SLOT:
                                            state <= ST_CIRCLE_SLOT;
                                        SPAN_RET_ELL_SLOT:
                                            state <= ST_ELL_SLOT;
                                        default: state <= ST_FINISH;
                                    endcase
                                end else begin
                                    span_x <= span_clipped_x0_q;
                                    span_end_x <= span_clipped_x1_q;
                                    state <= ST_SPAN_POINT;
                                end
                            end
                            GEOM_PATTERN_SETUP: begin
                                state <= geometry_setup_valid_q ?
                                         ST_PATTERN_POINT : ST_FINISH;
                            end
                            GEOM_RECT_SETUP: begin
                                state <= geometry_setup_valid_q ?
                                         ST_RECT_FILL_ROW : ST_FINISH;
                            end
                            default: begin
                                circle_slot <= 3'd0;
                                if (circle_x < circle_y)
                                    state <= ST_FINISH;
                                else begin
                                    prepare_circle_geometry(
                                        3'd0,
                                        circle_filled,
                                        circle_cx,
                                        circle_cy,
                                        circle_x,
                                        circle_y
                                    );
                                    state <= circle_filled ?
                                        ST_CIRCLE_SPAN : ST_CIRCLE_POINT;
                                end
                            end
                        endcase
                    end
                    ST_PIXEL_ADDR: begin
                        emit_addr_q <= emit_addr_next;
                        state <= ST_PIXEL_ISSUE;
                    end
                    ST_PIXEL_ISSUE: begin
                        if (!emit_addr_valid) begin
                            error_mem <= 8'd4;
                            state <= ST_FINISH;
                        end else begin
                            pixel_write <= pixel_access_write;
                            pixel_size <= pixel_access_size;
                            pixel_addr <= emit_addr_q[24:0];
                            pixel_wdata <= pixel_access_wdata;
                            pixel_start <= 1'b1;
                            state <= ST_PORT_WAIT;
                        end
                    end

                    ST_SPAN_SETUP: begin
                        span_valid_q <=
                            !(span_y < clip_min_y || span_y >= clip_max_y ||
                              span_raw_x1 < clip_min_x ||
                              span_raw_x0 >= clip_max_x ||
                              span_raw_x1 < span_raw_x0);
                        span_clipped_x0_q <= span_clipped_x0;
                        span_clipped_x1_q <= span_clipped_x1;
                        geometry_step_kind <= GEOM_SPAN;
                        state <= ST_GEOM_DECIDE;
                    end
                    ST_SPAN_POINT: begin
                        emit_x <= span_x;
                        emit_y <= span_y;
                        emit_color <= span_color;
                        emit_return <= EMIT_RET_SPAN_ADVANCE;
                        state <= ST_EMIT;
                    end
                    ST_SPAN_ADVANCE: begin
                        if (span_x == span_end_x) begin
                            case (span_return)
                                SPAN_RET_RECT_NEXT:
                                    state <= ST_RECT_FILL_NEXT;
                                SPAN_RET_CIRCLE_SLOT:
                                    state <= ST_CIRCLE_SLOT;
                                SPAN_RET_ELL_SLOT:
                                    state <= ST_ELL_SLOT;
                                default: state <= ST_FINISH;
                            endcase
                        end else begin
                            span_x <= span_x + 17'sd1;
                            state <= ST_SPAN_POINT;
                        end
                    end

                    ST_LINE_SETUP: begin
                        line_x <= line_setup_x0;
                        line_y <= line_setup_y0;
                        line_x1 <= line_setup_x1;
                        line_y1 <= line_setup_y1;
                        line_dx <= abs_delta(line_setup_x1, line_setup_x0);
                        line_dy <= abs_delta(line_setup_y1, line_setup_y0);
                        line_sx <= line_setup_x0 < line_setup_x1 ?
                                   2'sd1 : -2'sd1;
                        line_sy <= line_setup_y0 < line_setup_y1 ?
                                   2'sd1 : -2'sd1;
                        line_err <= abs_delta(line_setup_x1, line_setup_x0) -
                                    abs_delta(line_setup_y1, line_setup_y0);
                        state <= ST_LINE_POINT;
                    end
                    ST_LINE_POINT: begin
                        emit_x <= line_x;
                        emit_y <= line_y;
                        emit_color <= job_fg;
                        if (line_x == line_x1 && line_y == line_y1)
                            emit_return <= line_finish_rect ?
                                EMIT_RET_RECT_NEXT : EMIT_RET_FINISH;
                        else
                            emit_return <= EMIT_RET_LINE_STEP;
                        line_step_x <=
                            line_e2 > -$signed({line_dy[17], line_dy});
                        line_step_y <=
                            line_e2 < $signed({line_dx[17], line_dx});
                        state <= ST_EMIT;
                    end
                    ST_LINE_STEP: begin
                        if (line_step_x) begin
                            line_x <= line_x +
                                {{15{line_sx[1]}}, line_sx};
                            line_err <= line_err - line_dy;
                        end
                        geometry_step_kind <= GEOM_LINE;
                        state <= ST_GEOM_ADJUST;
                    end

                    ST_RECT_EDGE: begin
                        case (rect_edge)
                            2'd0: begin
                                line_setup_x0 <= rect_x0;
                                line_setup_y0 <= rect_y0;
                                line_setup_x1 <= rect_x1;
                                line_setup_y1 <= rect_y0;
                            end
                            2'd1: begin
                                line_setup_x0 <= rect_x1;
                                line_setup_y0 <= rect_y0;
                                line_setup_x1 <= rect_x1;
                                line_setup_y1 <= rect_y1;
                            end
                            2'd2: begin
                                line_setup_x0 <= rect_x1;
                                line_setup_y0 <= rect_y1;
                                line_setup_x1 <= rect_x0;
                                line_setup_y1 <= rect_y1;
                            end
                            default: begin
                                line_setup_x0 <= rect_x0;
                                line_setup_y0 <= rect_y1;
                                line_setup_x1 <= rect_x0;
                                line_setup_y1 <= rect_y0;
                            end
                        endcase
                        line_finish_rect <= 1'b1;
                        state <= ST_LINE_SETUP;
                    end
                    ST_RECT_NEXT: begin
                        if (rect_edge == 2'd3)
                            state <= ST_FINISH;
                        else begin
                            rect_edge <= rect_edge + 2'd1;
                            state <= ST_RECT_EDGE;
                        end
                    end

                    ST_RECT_FILL_SETUP: begin
                        geometry_setup_valid_q <=
                            !(rect_x1 < clip_min_x ||
                              rect_x0 >= clip_max_x ||
                              rect_y1 < clip_min_y ||
                              rect_y0 >= clip_max_y);
                        rect_fill_y <= rect_y0 < clip_min_y ?
                                       clip_min_y : rect_y0;
                        rect_fill_end_y <= rect_y1 >= clip_max_y ?
                                           clip_max_y - 17'sd1 : rect_y1;
                        geometry_step_kind <= GEOM_RECT_SETUP;
                        state <= ST_GEOM_DECIDE;
                    end
                    ST_RECT_FILL_ROW: begin
                        span_raw_x0 <= rect_x0;
                        span_raw_x1 <= rect_x1;
                        span_y <= rect_fill_y;
                        span_color <= job_fg;
                        span_return <= SPAN_RET_RECT_NEXT;
                        state <= ST_SPAN_SETUP;
                    end
                    ST_RECT_FILL_NEXT: begin
                        if (rect_fill_y == rect_fill_end_y)
                            state <= ST_FINISH;
                        else begin
                            rect_fill_y <= rect_fill_y + 17'sd1;
                            state <= ST_RECT_FILL_ROW;
                        end
                    end

                    ST_CIRCLE_POINT: begin
                        emit_x <= geometry_alu0_result;
                        emit_y <= geometry_alu1_result;
                        emit_color <= job_fg;
                        emit_return <= EMIT_RET_CIRCLE_SLOT;
                        state <= ST_EMIT;
                    end
                    ST_CIRCLE_SPAN: begin
                        span_raw_x0 <= geometry_alu0_result;
                        span_raw_x1 <= geometry_alu1_result;
                        span_y <= geometry_alu2_result;
                        span_color <= job_fg;
                        span_return <= SPAN_RET_CIRCLE_SLOT;
                        state <= ST_SPAN_SETUP;
                    end
                    ST_CIRCLE_SLOT: begin
                        if (circle_slot == (circle_filled ? 3'd3 : 3'd7)) begin
                            state <= ST_CIRCLE_STEP;
                        end else begin
                            circle_slot <= circle_slot + 3'd1;
                            prepare_circle_geometry(
                                circle_slot + 3'd1,
                                circle_filled,
                                circle_cx,
                                circle_cy,
                                circle_x,
                                circle_y
                            );
                            state <= circle_filled ? ST_CIRCLE_SPAN :
                                                     ST_CIRCLE_POINT;
                        end
                    end
                    ST_CIRCLE_STEP: begin
                        circle_y <= circle_step_y;
                        circle_err <= circle_step_err;
                        geometry_step_kind <= GEOM_CIRCLE;
                        state <= ST_GEOM_ADJUST;
                    end

                    ST_ELL_MUL_RX: begin
                        case (ellipse_alu_phase)
                            ELL_ALU_INIT: begin
                                ellipse_err <= ellipse_alu_result;
                                ellipse_alu_phase <= ELL_ALU_IDLE;
                                shared_mul_pending <= 1'b0;
                                ellipse_slot <= 2'd0;
                                prepare_ellipse_geometry(
                                    2'd0,
                                    ellipse_filled,
                                    ellipse_cx,
                                    ellipse_cy,
                                    ellipse_x,
                                    ellipse_y
                                );
                                state <= ellipse_filled ? ST_ELL_SPAN :
                                                            ST_ELL_POINT;
                            end
                            ELL_ALU_STEP_X: begin
                                ellipse_err <= ellipse_alu_result;
                                ellipse_alu_phase <= ELL_ALU_IDLE;
                                shared_mul_pending <= 1'b0;
                                geometry_step_kind <= GEOM_ELLIPSE;
                                state <= ST_GEOM_ADJUST;
                            end
                            ELL_ALU_STEP_Y: begin
                                ellipse_err <= ellipse_alu_result;
                                ellipse_alu_phase <= ELL_ALU_IDLE;
                                shared_mul_pending <= 1'b0;
                                state <= ST_GEOM_DECIDE;
                            end
                            default: begin
                                if (!shared_mul_pending) begin
                                    shared_mul_a <= {2'd0, ellipse_rx};
                                    shared_mul_b <= {2'd0, ellipse_rx};
                                    shared_mul_pending <= 1'b1;
                                end else begin
                                    ellipse_rx2 <= shared_mul_product[31:0];
                                    shared_mul_pending <= 1'b0;
                                    state <= ST_ELL_MUL_RY;
                                end
                            end
                        endcase
                    end
                    ST_ELL_MUL_RY: begin
                        if (!shared_mul_pending) begin
                            shared_mul_a <= {2'd0, ellipse_ry};
                            shared_mul_b <= {2'd0, ellipse_ry};
                            shared_mul_pending <= 1'b1;
                        end else begin
                            ellipse_ry2 <= shared_mul_product[31:0];
                            shared_mul_pending <= 1'b0;
                            state <= ST_ELL_MUL_CROSS;
                        end
                    end
                    ST_ELL_MUL_CROSS: begin
                        case (ellipse_alu_phase)
                            ELL_ALU_IDLE: begin
                                if (!shared_mul_pending) begin
                                    shared_mul_a <= {2'd0,
                                                     ellipse_ry2[15:0]};
                                    shared_mul_b <= {2'd0, ellipse_rx};
                                    shared_mul_pending <= 1'b1;
                                end else begin
                                    ellipse_cross_lo <=
                                        shared_mul_product[31:0];
                                    shared_mul_pending <= 1'b0;
                                    ellipse_alu_phase <= ELL_ALU_INIT;
                                end
                            end
                            ELL_ALU_INIT: begin
                                if (!shared_mul_pending) begin
                                    shared_mul_a <= {2'd0,
                                                     ellipse_ry2[31:16]};
                                    shared_mul_b <= {2'd0, ellipse_rx};
                                    shared_mul_pending <= 1'b1;
                                end else begin
                                    ellipse_cross_hi <=
                                        shared_mul_product[31:0];
                                    shared_mul_pending <= 1'b0;
                                    ellipse_alu_phase <= ELL_ALU_STEP_X;
                                end
                            end
                            ELL_ALU_STEP_X: begin
                                ellipse_cross_product <=
                                    {ellipse_cross_hi, 16'd0} +
                                    {16'd0, ellipse_cross_lo};
                                ellipse_alu_phase <= ELL_ALU_STEP_Y;
                            end
                            default: begin
                                ellipse_x <= -$signed({1'b0, ellipse_rx});
                                ellipse_y <= 17'sd0;
                                ellipse_dx <=
                                    $signed({16'd0, ellipse_ry2}) -
                                    $signed(ellipse_cross_product << 1);
                                ellipse_dy <=
                                    $signed({16'd0, ellipse_rx2});
                                ellipse_alu_phase <= ELL_ALU_INIT;
                                state <= ST_ELL_MUL_RX;
                            end
                        endcase
                    end
                    ST_ELL_POINT: begin
                        emit_x <= geometry_alu0_result;
                        emit_y <= geometry_alu1_result;
                        emit_color <= job_fg;
                        emit_return <= EMIT_RET_ELL_SLOT;
                        state <= ST_EMIT;
                    end
                    ST_ELL_SPAN: begin
                        span_raw_x0 <= geometry_alu0_result;
                        span_raw_x1 <= geometry_alu1_result;
                        span_y <= geometry_alu2_result;
                        span_color <= job_fg;
                        span_return <= SPAN_RET_ELL_SLOT;
                        state <= ST_SPAN_SETUP;
                    end
                    ST_ELL_SLOT: begin
                        if (ellipse_slot == (ellipse_filled ? 2'd1 : 2'd3)) begin
                            ellipse_slot <= 2'd0;
                            if (ellipse_tipping) begin
                                if (ellipse_y >= $signed({1'b0, ellipse_ry}))
                                    state <= ST_FINISH;
                                else begin
                                    ellipse_y <= ellipse_y + 17'sd1;
                                    prepare_ellipse_geometry(
                                        2'd0,
                                        ellipse_filled,
                                        ellipse_cx,
                                        ellipse_cy,
                                        ellipse_x,
                                        ellipse_y + 17'sd1
                                    );
                                    state <= ellipse_filled ? ST_ELL_SPAN :
                                                                ST_ELL_POINT;
                                end
                            end else begin
                                state <= ST_ELL_STEP;
                            end
                        end else begin
                            ellipse_slot <= ellipse_slot + 2'd1;
                            prepare_ellipse_geometry(
                                ellipse_slot + 2'd1,
                                ellipse_filled,
                                ellipse_cx,
                                ellipse_cy,
                                ellipse_x,
                                ellipse_y
                            );
                            state <= ellipse_filled ? ST_ELL_SPAN :
                                                        ST_ELL_POINT;
                        end
                    end
                    ST_ELL_STEP: begin
                        ellipse_step_y <=
                            ellipse_e2 <= $signed({ellipse_dy[47],
                                                   ellipse_dy});
                        if (ellipse_e2 >= $signed({ellipse_dx[47],
                                                  ellipse_dx})) begin
                            ellipse_x <= ellipse_x + 17'sd1;
                            ellipse_dx <= ellipse_alu_result;
                            ellipse_alu_phase <= ELL_ALU_STEP_X;
                            state <= ST_ELL_MUL_RX;
                        end else begin
                            geometry_step_kind <= GEOM_ELLIPSE;
                            state <= ST_GEOM_ADJUST;
                        end
                    end

                    ST_PATTERN_SETUP: begin
                        geometry_setup_valid_q <=
                            !(rect_x1 < clip_min_x ||
                              rect_x0 >= clip_max_x ||
                              rect_y1 < clip_min_y ||
                              rect_y0 >= clip_max_y);
                        pattern_x <= rect_x0 < clip_min_x ?
                                     clip_min_x : rect_x0;
                        pattern_y <= rect_y0 < clip_min_y ?
                                     clip_min_y : rect_y0;
                        pattern_x1 <= rect_x1 >= clip_max_x ?
                                      clip_max_x - 17'sd1 : rect_x1;
                        pattern_y1 <= rect_y1 >= clip_max_y ?
                                      clip_max_y - 17'sd1 : rect_y1;
                        geometry_step_kind <= GEOM_PATTERN_SETUP;
                        state <= ST_GEOM_DECIDE;
                    end
                    ST_PATTERN_POINT: begin
                        pattern_bit_q <= pattern_bit;
                        geometry_step_kind <= GEOM_PATTERN;
                        state <= ST_GEOM_DECIDE;
                    end
                    ST_PATTERN_NEXT: begin
                        if (pattern_x == pattern_x1) begin
                            pattern_x <= rect_x0;
                            if (pattern_y == pattern_y1)
                                state <= ST_FINISH;
                            else begin
                                pattern_y <= pattern_y + 17'sd1;
                                state <= ST_PATTERN_POINT;
                            end
                        end else begin
                            pattern_x <= pattern_x + 17'sd1;
                            state <= ST_PATTERN_POINT;
                        end
                    end

                    ST_PORT_WAIT: begin
                        if (pixel_done) begin
                            pixel_result_mem <= pixel_rdata;
                            case (port_return)
                                PORT_RET_EMIT: begin
                                    case (emit_return)
                                        EMIT_RET_PATTERN_NEXT:
                                            state <= ST_PATTERN_NEXT;
                                        EMIT_RET_SPAN_ADVANCE:
                                            state <= ST_SPAN_ADVANCE;
                                        EMIT_RET_LINE_STEP:
                                            state <= ST_LINE_STEP;
                                        EMIT_RET_FINISH:
                                            state <= ST_FINISH;
                                        EMIT_RET_RECT_NEXT:
                                            state <= ST_RECT_NEXT;
                                        EMIT_RET_CIRCLE_SLOT:
                                            state <= ST_CIRCLE_SLOT;
                                        EMIT_RET_ELL_SLOT:
                                            state <= ST_ELL_SLOT;
                                        EMIT_RET_GLYPH_ADVANCE:
                                            state <= ST_GLYPH_ADVANCE;
                                        EMIT_RET_FLOOD_NEIGHBOR:
                                            state <= ST_FLOOD_NEIGHBOR;
                                        default: state <= ST_FINISH;
                                    endcase
                                end
                                PORT_RET_PALETTE_STORE:
                                    state <= ST_PALETTE_STORE;
                                PORT_RET_GLYPH_DESC_STORE:
                                    state <= ST_GLYPH_DESC_STORE;
                                PORT_RET_GLYPH_SOURCE_GET:
                                    state <= ST_GLYPH_SOURCE_GET;
                                PORT_RET_GLYPH_BLEND_STORE:
                                    state <= ST_GLYPH_BLEND_STORE;
                                PORT_RET_FLOOD_PROCESS:
                                    state <= ST_FLOOD_PROCESS;
                                PORT_RET_FLOOD_PUSH_DONE:
                                    state <= ST_FLOOD_PUSH_DONE;
                                PORT_RET_FLOOD_POP_DONE:
                                    state <= ST_FLOOD_POP_DONE;
                                default: state <= ST_FINISH;
                            endcase
                        end
                    end

                    ST_PALETTE_ISSUE: begin
                        if (glyph_palette_addr_wide[25]) begin
                            error_mem <= 8'd4;
                            state <= ST_FINISH;
                        end else begin
                            pixel_write <= 1'b0;
                            pixel_size <= 2'd1;
                            pixel_addr <= glyph_palette_addr_wide[24:0];
                            pixel_wdata <= 32'd0;
                            pixel_start <= 1'b1;
                            port_return <= PORT_RET_PALETTE_STORE;
                            state <= ST_PORT_WAIT;
                        end
                    end
                    ST_PALETTE_STORE: begin
                        if (palette_index == palette_last_index) begin
                            state <= job_work_entries != 16'd0 ?
                                     ST_GLYPH_DESC_ISSUE : ST_GLYPH_SETUP;
                        end else begin
                            palette_index <= palette_index + 8'd1;
                            state <= ST_PALETTE_ISSUE;
                        end
                    end

                    ST_GLYPH_DESC_ISSUE: begin
                        if (glyph_desc_addr_wide[25]) begin
                            error_mem <= 8'd4;
                            state <= ST_FINISH;
                        end else begin
                            pixel_write <= 1'b0;
                            pixel_size <= 2'd2;
                            pixel_addr <= glyph_desc_addr_wide[24:0];
                            pixel_wdata <= 32'd0;
                            pixel_start <= 1'b1;
                            port_return <= PORT_RET_GLYPH_DESC_STORE;
                            state <= ST_PORT_WAIT;
                        end
                    end
                    ST_GLYPH_DESC_STORE: begin
                        if (glyph_desc_validate_pending) begin
                            glyph_desc_validate_pending <= 1'b0;
                            if (!glyph_desc_valid_q) begin
                                error_mem <= 8'd1;
                                state <= ST_FINISH;
                            end else begin
                                state <= ST_GLYPH_SETUP;
                            end
                        end else begin
                            case (glyph_desc_word)
                                2'd0: begin
                                    if (pixel_result_mem[31:25] != 7'd0 ||
                                        glyph_desc_source_sum[25]) begin
                                        error_mem <= 8'd1;
                                        state <= ST_FINISH;
                                    end else begin
                                        glyph_source_base <=
                                            glyph_desc_source_sum[24:0];
                                        glyph_desc_word <= 2'd1;
                                        state <= ST_GLYPH_DESC_ISSUE;
                                    end
                                end
                                2'd1: begin
                                    if (pixel_result_mem[31] ||
                                        pixel_result_mem[15]) begin
                                        error_mem <= 8'd1;
                                        state <= ST_FINISH;
                                    end else begin
                                        glyph_source_x <= pixel_result_mem[15:0];
                                        glyph_source_y <= pixel_result_mem[31:16];
                                        glyph_desc_word <= 2'd2;
                                        state <= ST_GLYPH_DESC_ISSUE;
                                    end
                                end
                                2'd2: begin
                                    glyph_dest_x <= coord_x(pixel_result_mem);
                                    glyph_dest_y <= coord_y(pixel_result_mem);
                                    glyph_desc_word <= 2'd3;
                                    state <= ST_GLYPH_DESC_ISSUE;
                                end
                                default: begin
                                    glyph_width <= pixel_result_mem[15:0];
                                    glyph_height <= pixel_result_mem[31:16];
                                    glyph_desc_valid_q <=
                                        pixel_result_mem[15:0] != 16'd0 &&
                                        pixel_result_mem[31:16] != 16'd0 &&
                                        !pixel_result_mem[15] &&
                                        !pixel_result_mem[31] &&
                                        ({1'b0, glyph_source_x} +
                                         {1'b0, pixel_result_mem[15:0]} <=
                                         17'h10000) &&
                                        ({1'b0, glyph_source_y} +
                                         {1'b0, pixel_result_mem[31:16]} <=
                                         17'h10000);
                                    glyph_desc_word <= 2'd0;
                                    glyph_desc_validate_pending <= 1'b1;
                                end
                            endcase
                        end
                    end
                    ST_GLYPH_SETUP: begin
                        glyph_col <= 16'd0;
                        glyph_row <= 16'd0;
                        glyph_source_cache_valid <= 1'b0;
                        state <= ST_GLYPH_SOURCE;
                    end
                    ST_GLYPH_SOURCE: begin
                        if (!shared_mul_pending) begin
                            shared_mul_a <= {1'b0,
                                             glyph_source_pixel_y};
                            shared_mul_b <= {2'd0, job_src_pitch};
                            shared_mul_pending <= 1'b1;
                            glyph_pixel_x_q <= glyph_pixel_x;
                            glyph_pixel_y_q <= glyph_pixel_y;
                            glyph_pixel_inside_clip_q <=
                                glyph_pixel_inside_clip;
                            glyph_source_byte_x_q <= glyph_source_byte_x;
                            glyph_col_last_q <=
                                glyph_col + 16'd1 == glyph_width;
                            glyph_row_last_q <=
                                glyph_row + 16'd1 == glyph_height;
                            glyph_desc_more_q <= job_work_entries != 16'd0 &&
                                glyph_desc_index + 16'd1 < job_work_entries;
                        end else begin
                            glyph_source_row_offset <=
                                shared_mul_product[31:0];
                            shared_mul_pending <= 1'b0;
                            state <= ST_GLYPH_SOURCE_ADDR;
                        end
                    end
                    ST_GLYPH_SOURCE_ADDR: begin
                        glyph_source_addr_q <= glyph_source_addr_next;
                        glyph_source_addr_valid_q <=
                            glyph_source_addr_next[32:25] == 8'd0;
                        state <= ST_GLYPH_SOURCE_ISSUE;
                    end
                    ST_GLYPH_SOURCE_ISSUE: begin
                        glyph_source_cache_hit_q <=
                            glyph_source_cache_valid &&
                            glyph_source_cache_addr == glyph_source_addr;
                        dispatch_kind <= DISPATCH_GLYPH_SOURCE;
                        state <= ST_FLOOD_POP_CHECK;
                    end
                    ST_GLYPH_SOURCE_GET: begin
                        glyph_source_cache_valid <= 1'b1;
                        glyph_source_cache_addr <= glyph_source_addr;
                        glyph_source_cache_byte <= pixel_result_mem[7:0];
                        state <= ST_GLYPH_DECODE;
                    end
                    ST_GLYPH_DECODE: begin
                        case (job_glyph_mode)
                            GLYPH_MODE_MASK1: begin
                                if (glyph_source_cache_byte[
                                    3'd7 - glyph_source_pixel_x[2:0]]) begin
                                    emit_x <= glyph_pixel_x_q;
                                    emit_y <= glyph_pixel_y_q;
                                    emit_color <= job_fg;
                                    emit_return <= EMIT_RET_GLYPH_ADVANCE;
                                    state <= ST_EMIT;
                                end else if (job_pattern_opaque) begin
                                    emit_x <= glyph_pixel_x_q;
                                    emit_y <= glyph_pixel_y_q;
                                    emit_color <= job_bg;
                                    emit_return <= EMIT_RET_GLYPH_ADVANCE;
                                    state <= ST_EMIT;
                                end else begin
                                    state <= ST_GLYPH_ADVANCE;
                                end
                            end
                            GLYPH_MODE_A4: begin
                                glyph_coverage <= glyph_source_pixel_x[0] ?
                                    glyph_source_cache_byte[3:0] :
                                    glyph_source_cache_byte[7:4];
                                if ((glyph_source_pixel_x[0] ?
                                     glyph_source_cache_byte[3:0] :
                                     glyph_source_cache_byte[7:4]) == 4'd0 ||
                                    !glyph_pixel_inside_clip_q) begin
                                    state <= ST_GLYPH_ADVANCE;
                                end else if ((glyph_source_pixel_x[0] ?
                                              glyph_source_cache_byte[3:0] :
                                              glyph_source_cache_byte[7:4]) ==
                                             4'd15) begin
                                    emit_x <= glyph_pixel_x_q;
                                    emit_y <= glyph_pixel_y_q;
                                    emit_color <= job_fg;
                                    emit_return <= EMIT_RET_GLYPH_ADVANCE;
                                    state <= ST_EMIT;
                                end else begin
                                    state <= ST_GLYPH_BLEND_SETUP;
                                end
                            end
                            GLYPH_MODE_INDEX4: begin
                                glyph_index <= glyph_source_pixel_x[0] ?
                                    {4'd0, glyph_source_cache_byte[3:0]} :
                                    {4'd0, glyph_source_cache_byte[7:4]};
                                if ((glyph_source_pixel_x[0] ?
                                     glyph_source_cache_byte[3:0] :
                                     glyph_source_cache_byte[7:4]) ==
                                    job_transparent_index[3:0])
                                    state <= ST_GLYPH_ADVANCE;
                                else
                                    state <= ST_GLYPH_LOOKUP;
                            end
                            default: begin
                                glyph_index <= glyph_source_cache_byte;
                                if (glyph_source_cache_byte ==
                                    job_transparent_index)
                                    state <= ST_GLYPH_ADVANCE;
                                else
                                    state <= ST_GLYPH_LOOKUP;
                            end
                        endcase
                    end
                    ST_GLYPH_LOOKUP: begin
                        state <= ST_GLYPH_LOOKUP_WAIT;
                    end
                    ST_GLYPH_LOOKUP_WAIT: begin
                        if (glyph_pixel_inside_clip_q) begin
                            emit_x <= glyph_pixel_x_q;
                            emit_y <= glyph_pixel_y_q;
                            emit_color <= glyph_palette_q;
                            emit_return <= EMIT_RET_GLYPH_ADVANCE;
                            state <= ST_EMIT;
                        end else begin
                            state <= ST_GLYPH_ADVANCE;
                        end
                    end
                    ST_GLYPH_BLEND_SETUP: begin
                        if (!shared_mul_pending) begin
                            shared_mul_a <= {1'b0, glyph_pixel_y_q};
                            shared_mul_b <= {2'd0, job_pitch};
                            shared_mul_pending <= 1'b1;
                        end else begin
                            emit_x <= glyph_pixel_x_q;
                            emit_y <= glyph_pixel_y_q;
                            emit_row_offset <= shared_mul_product[31:0];
                            shared_mul_pending <= 1'b0;
                            pixel_access_write <= 1'b0;
                            pixel_access_size <= 2'd1;
                            pixel_access_wdata <= 32'd0;
                            port_return <= PORT_RET_GLYPH_BLEND_STORE;
                            state <= ST_PIXEL_ADDR;
                        end
                    end
                    ST_GLYPH_BLEND_STORE: begin
                        blend_destination <= pixel_result_mem[15:0];
                        blend_channel <= 2'd0;
                        state <= ST_GLYPH_BLEND_CALC;
                    end
                    ST_GLYPH_BLEND_CALC: begin
                        blend_difference_q <= blend_difference;
                        blend_coverage_q <= glyph_coverage;
                        blend_base_q <= blend_base;
                        state <= ST_GLYPH_BLEND_PRODUCT;
                    end
                    ST_GLYPH_BLEND_PRODUCT: begin
                        blend_product_q <= blend_difference_q *
                            $signed({1'b0, blend_coverage_q});
                        state <= ST_GLYPH_BLEND_SUM;
                    end
                    ST_GLYPH_BLEND_SUM: begin
                        blend_div15_addr <= blend_sum[9:0];
                        state <= ST_GLYPH_BLEND_LOOKUP;
                    end
                    ST_GLYPH_BLEND_LOOKUP: begin
                        state <= ST_GLYPH_BLEND_RESULT;
                    end
                    ST_GLYPH_BLEND_RESULT: begin
                        case (blend_channel)
                            2'd0: begin
                                blend_red <= blend_div15_q[4:0];
                                blend_channel <= 2'd1;
                                state <= ST_GLYPH_BLEND_CALC;
                            end
                            2'd1: begin
                                blend_green <= blend_div15_q;
                                blend_channel <= 2'd2;
                                state <= ST_GLYPH_BLEND_CALC;
                            end
                            default: begin
                                emit_color <= {blend_red, blend_green,
                                               blend_div15_q[4:0]};
                                emit_return <= EMIT_RET_GLYPH_ADVANCE;
                                state <= ST_EMIT;
                            end
                        endcase
                    end
                    ST_GLYPH_ADVANCE: begin
                        if (glyph_col_last_q) begin
                            glyph_col <= 16'd0;
                            glyph_source_cache_valid <= 1'b0;
                            if (glyph_row_last_q) begin
                                glyph_row <= 16'd0;
                                if (glyph_desc_more_q) begin
                                    glyph_desc_index <=
                                        glyph_desc_index + 16'd1;
                                    glyph_desc_word <= 2'd0;
                                    state <= ST_GLYPH_DESC_ISSUE;
                                end else begin
                                    state <= ST_FINISH;
                                end
                            end else begin
                                glyph_row <= glyph_row + 16'd1;
                                state <= ST_GLYPH_SOURCE;
                            end
                        end else begin
                            glyph_col <= glyph_col + 16'd1;
                            state <= ST_GLYPH_SOURCE;
                        end
                    end

                    ST_FLOOD_INIT: begin
                        if (flood_x < clip_min_x || flood_x >= clip_max_x ||
                            flood_y < clip_min_y || flood_y >= clip_max_y) begin
                            state <= ST_FINISH;
                        end else begin
                            emit_x <= flood_x;
                            emit_y <= flood_y;
                            flood_read_kind <= FLOOD_READ_SEED;
                            state <= ST_FLOOD_READ;
                        end
                    end
                    ST_FLOOD_READ: begin
                        if (!shared_mul_pending) begin
                            shared_mul_a <= {1'b0, emit_y};
                            shared_mul_b <= {2'd0, job_pitch};
                            shared_mul_pending <= 1'b1;
                        end else begin
                            emit_row_offset <= shared_mul_product[31:0];
                            shared_mul_pending <= 1'b0;
                            pixel_access_write <= 1'b0;
                            pixel_access_size <= job_rgb565 ? 2'd1 : 2'd0;
                            pixel_access_wdata <= 32'd0;
                            port_return <= PORT_RET_FLOOD_PROCESS;
                            state <= ST_PIXEL_ADDR;
                        end
                    end
                    ST_FLOOD_PROCESS: begin
                        case (flood_read_kind)
                            FLOOD_READ_SEED: begin
                                flood_target <= port_pixel_value;
                                if (port_pixel_value == job_fill_value) begin
                                    state <= ST_FINISH;
                                end else begin
                                    flood_push_x <= flood_x;
                                    flood_push_y <= flood_y;
                                    queue_return <= QUEUE_RET_POP;
                                    state <= ST_FLOOD_PUSH;
                                end
                            end
                            FLOOD_READ_POP: begin
                                state <= port_pixel_value == flood_target ?
                                         ST_FLOOD_SEEK : ST_FLOOD_POP;
                            end
                            FLOOD_READ_SEEK: begin
                                if (port_pixel_value == flood_target) begin
                                    flood_x <= flood_x - 17'sd1;
                                    state <= ST_FLOOD_SEEK;
                                end else begin
                                    flood_scan_x <= flood_x;
                                    flood_span_above <= 1'b0;
                                    flood_span_below <= 1'b0;
                                    state <= ST_FLOOD_SCAN;
                                end
                            end
                            FLOOD_READ_SCAN: begin
                                if (port_pixel_value == flood_target) begin
                                    emit_color <= job_fg;
                                    emit_return <= EMIT_RET_FLOOD_NEIGHBOR;
                                    flood_neighbor_below <= 1'b0;
                                    state <= ST_EMIT;
                                end else begin
                                    state <= ST_FLOOD_POP;
                                end
                            end
                            default: begin
                                if (port_pixel_value == flood_target) begin
                                    if ((!flood_neighbor_below &&
                                         !flood_span_above) ||
                                        (flood_neighbor_below &&
                                         !flood_span_below)) begin
                                        if (flood_neighbor_below)
                                            flood_span_below <= 1'b1;
                                        else begin
                                            flood_span_above <= 1'b1;
                                            flood_neighbor_below <= 1'b1;
                                        end
                                        flood_push_x <= emit_x;
                                        flood_push_y <= emit_y;
                                        queue_return <=
                                            flood_neighbor_below ?
                                            QUEUE_RET_ADVANCE :
                                            QUEUE_RET_NEIGHBOR;
                                        state <= ST_FLOOD_PUSH;
                                    end else begin
                                        state <= flood_neighbor_below ?
                                                 ST_FLOOD_ADVANCE :
                                                 ST_FLOOD_NEIGHBOR;
                                        if (!flood_neighbor_below)
                                            flood_neighbor_below <= 1'b1;
                                    end
                                end else begin
                                    if (flood_neighbor_below) begin
                                        flood_span_below <= 1'b0;
                                        state <= ST_FLOOD_ADVANCE;
                                    end else begin
                                        flood_span_above <= 1'b0;
                                        flood_neighbor_below <= 1'b1;
                                        state <= ST_FLOOD_NEIGHBOR;
                                    end
                                end
                            end
                        endcase
                    end
                    ST_FLOOD_PUSH: begin
                        if (flood_queue_count >= job_work_entries) begin
                            error_mem <= 8'd3;
                            state <= ST_FINISH;
                        end else begin
                            flood_queue_addr_q <= flood_push_addr_next;
                            dispatch_kind <= DISPATCH_FLOOD_PUSH_ADDR;
                            state <= ST_FLOOD_POP_CHECK;
                        end
                    end
                    ST_FLOOD_PUSH_DONE: begin
                        flood_queue_count <= flood_queue_count + 16'd1;
                        case (queue_return)
                            QUEUE_RET_POP: state <= ST_FLOOD_POP;
                            QUEUE_RET_ADVANCE: state <= ST_FLOOD_ADVANCE;
                            QUEUE_RET_NEIGHBOR: state <= ST_FLOOD_NEIGHBOR;
                            default: state <= ST_FINISH;
                        endcase
                    end
                    ST_FLOOD_POP: begin
                        if (flood_queue_count == 16'd0) begin
                            state <= ST_FINISH;
                        end else begin
                            flood_queue_addr_q <= flood_pop_addr_next;
                            dispatch_kind <= DISPATCH_FLOOD_POP_ADDR;
                            state <= ST_FLOOD_POP_CHECK;
                        end
                    end
                    ST_FLOOD_POP_DONE: begin
                        flood_queue_count <= flood_queue_count - 16'd1;
                        flood_x <= coord_x(pixel_result_mem);
                        flood_y <= coord_y(pixel_result_mem);
                        flood_pop_inside_q <=
                            coord_x(pixel_result_mem) >= clip_min_x &&
                            coord_x(pixel_result_mem) < clip_max_x &&
                            coord_y(pixel_result_mem) >= clip_min_y &&
                            coord_y(pixel_result_mem) < clip_max_y;
                        dispatch_kind <= DISPATCH_FLOOD_POP_RESULT;
                        state <= ST_FLOOD_POP_CHECK;
                    end
                    ST_FLOOD_SEEK: begin
                        if (flood_x <= clip_min_x) begin
                            flood_scan_x <= flood_x;
                            flood_span_above <= 1'b0;
                            flood_span_below <= 1'b0;
                            state <= ST_FLOOD_SCAN;
                        end else begin
                            emit_x <= flood_x - 17'sd1;
                            emit_y <= flood_y;
                            flood_read_kind <= FLOOD_READ_SEEK;
                            state <= ST_FLOOD_READ;
                        end
                    end
                    ST_FLOOD_SCAN: begin
                        emit_x <= flood_scan_x;
                        emit_y <= flood_y;
                        flood_read_kind <= FLOOD_READ_SCAN;
                        state <= ST_FLOOD_READ;
                    end
                    ST_FLOOD_NEIGHBOR: begin
                        if (!flood_neighbor_below) begin
                            if (flood_y <= clip_min_y) begin
                                flood_span_above <= 1'b0;
                                flood_neighbor_below <= 1'b1;
                            end else begin
                                emit_x <= flood_scan_x;
                                emit_y <= flood_y - 17'sd1;
                                flood_read_kind <= FLOOD_READ_NEIGHBOR;
                                state <= ST_FLOOD_READ;
                            end
                        end else if (flood_y + 17'sd1 >= clip_max_y) begin
                            flood_span_below <= 1'b0;
                            state <= ST_FLOOD_ADVANCE;
                        end else begin
                            emit_x <= flood_scan_x;
                            emit_y <= flood_y + 17'sd1;
                            flood_read_kind <= FLOOD_READ_NEIGHBOR;
                            state <= ST_FLOOD_READ;
                        end
                    end
                    ST_FLOOD_ADVANCE: begin
                        if (flood_scan_x + 17'sd1 >= clip_max_x) begin
                            state <= ST_FLOOD_POP;
                        end else begin
                            flood_scan_x <= flood_scan_x + 17'sd1;
                            state <= ST_FLOOD_SCAN;
                        end
                    end

                    ST_FINISH: begin
                        finish_pending_mem <= 1'b1;
                        shared_mul_pending <= 1'b0;
                        state <= ST_IDLE;
                    end
                    default: begin
                        error_mem <= 8'd2;
                        finish_pending_mem <= 1'b1;
                        shared_mul_pending <= 1'b0;
                        state <= ST_IDLE;
                    end
                endcase
                end
            end
        end
    end

    wire unused_pixel_busy = pixel_busy;
endmodule

`default_nettype wire
