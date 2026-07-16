// Thirty-two 4bpp sprites rendered into ping-pong scanline buffers. Descriptor
// evaluation and pattern fetch happen in the SDRAM clock domain; scanout only
// touches deterministic BRAM.
`default_nettype none

module vega_sprite_builder (
    input  wire        cpu_clk,
    input  wire        cpu_rst,
    input  wire        cpu_table_write,
    input  wire [7:0]  cpu_word_addr,
    input  wire [3:0]  cpu_be,
    input  wire [31:0] cpu_wdata,
    output reg  [31:0] cpu_rdata,

    input  wire        mem_clk,
    input  wire        mem_rst,
    input  wire        start,
    input  wire        build_bank,
    input  wire [9:0]  line_y,
    input  wire [9:0]  line_width,
    input  wire        enable,
    input  wire [15:0] pixel_budget,

    output reg         busy,
    output reg         done,
    output reg         config_error,
    output reg         overflow,
    output reg  [31:0] collision_bitmap,
    output reg         collision_event,

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
    input  wire        display_bank,
    input  wire [9:0]  pixel_x,
    output wire [17:0] behind_pair,
    output wire [17:0] front_pair
);
    localparam [5:0] ST_IDLE       = 6'd0;
    localparam [5:0] ST_CLEAR      = 6'd1;
    localparam [5:0] ST_DESC_SETUP = 6'd2;
    localparam [5:0] ST_DESC_ISSUE = 6'd3;
    localparam [5:0] ST_DESC_DRAIN = 6'd4;
    localparam [5:0] ST_DESC_CAP   = 6'd5;
    localparam [5:0] ST_SELECT     = 6'd6;
    localparam [5:0] ST_FIND       = 6'd7;
    localparam [5:0] ST_RENDER_PREP = 6'd8;
    localparam [5:0] ST_ROW_SETUP  = 6'd9;
    localparam [5:0] ST_ROW_ISSUE  = 6'd10;
    localparam [5:0] ST_ROW_DRAIN  = 6'd11;
    localparam [5:0] ST_ROW_YIELD  = 6'd12;
    localparam [5:0] ST_LINE_READ  = 6'd13;
    localparam [5:0] ST_LINE_WRITE = 6'd14;
    localparam [5:0] ST_FINISH     = 6'd15;
    localparam [5:0] ST_SELECT_SETUP = 6'd16;
    localparam [5:0] ST_SELECT_META = 6'd17;
    localparam [5:0] ST_FIND_META   = 6'd18;
    localparam [5:0] ST_PATTERN_ALIGN = 6'd19;
    localparam [5:0] ST_SELECT_DECIDE = 6'd20;
    localparam [5:0] ST_FIND_CALC = 6'd21;
    localparam [5:0] ST_PATTERN_READ = 6'd22;

    // The register table is exactly the ABI's 32 entries x 8 longs. Port A is
    // CPU/copper MMIO; port B evaluates one coherent descriptor per scanline.
    (* ram_style = "block" *) reg [31:0] descriptor_mem [0:255];
    reg [31:0] descriptor_mem_q;
    reg [7:0] descriptor_read_addr;
    integer descriptor_init;
    initial begin
        for (descriptor_init = 0; descriptor_init < 256;
             descriptor_init = descriptor_init + 1)
            descriptor_mem[descriptor_init] = 32'd0;
    end

    always @(posedge cpu_clk) begin
        cpu_rdata <= descriptor_mem[cpu_word_addr];
        if (cpu_table_write) begin
            if (cpu_be[3])
                descriptor_mem[cpu_word_addr][31:24] <= cpu_wdata[31:24];
            if (cpu_be[2])
                descriptor_mem[cpu_word_addr][23:16] <= cpu_wdata[23:16];
            if (cpu_be[1])
                descriptor_mem[cpu_word_addr][15:8] <= cpu_wdata[15:8];
            if (cpu_be[0])
                descriptor_mem[cpu_word_addr][7:0] <= cpu_wdata[7:0];
        end
    end

    always @(posedge mem_clk)
        descriptor_mem_q <= descriptor_mem[descriptor_read_addr];

    // Compose eight pixels in parallel so a saturated sprite line can meet
    // the scanline deadline. The inactive build bank is cleared while the
    // independent descriptor pipeline runs.
    (* ram_style = "block" *) reg [71:0] behind_line [0:255];
    (* ram_style = "block" *) reg [71:0] front_line [0:255];
    (* ram_style = "block" *) reg [47:0] collision_line [0:255];

    wire [7:0] pixel_line_addr = {display_bank, pixel_x[9:3]};
    reg [71:0] behind_word_q;
    reg [71:0] front_word_q;
    reg [1:0] pixel_pair_q;
    reg [17:0] selected_behind_pair;
    reg [17:0] selected_front_pair;
    always @* begin
        case (pixel_pair_q)
            2'd0: begin
                selected_behind_pair = behind_word_q[71:54];
                selected_front_pair = front_word_q[71:54];
            end
            2'd1: begin
                selected_behind_pair = behind_word_q[53:36];
                selected_front_pair = front_word_q[53:36];
            end
            2'd2: begin
                selected_behind_pair = behind_word_q[35:18];
                selected_front_pair = front_word_q[35:18];
            end
            default: begin
                selected_behind_pair = behind_word_q[17:0];
                selected_front_pair = front_word_q[17:0];
            end
        endcase
    end
    assign behind_pair = selected_behind_pair;
    assign front_pair = selected_front_pair;
    always @(posedge pixel_clk) begin
        behind_word_q <= behind_line[pixel_line_addr];
        front_word_q <= front_line[pixel_line_addr];
        pixel_pair_q <= pixel_x[2:1];
    end

    // Active metadata is consumed by index after descriptor evaluation. Keep
    // it in three block RAMs instead of building seven 32-way register muxes.
    // {ctrl, screen_x, visible_width, source_low, row_base}
    (* ram_style = "block" *) reg [80:0] active_meta_mem [0:31];
    reg [80:0] active_meta_q;
    reg [4:0] active_meta_read_addr;

    // One bitmap per priority implements the exact admission order: priority
    // 15 down to 0, then descriptor index 0 up to 31. This replaces the
    // repeated 32-entry comparison tournament with a small priority encoder.
    (* ram_style = "distributed" *) reg [31:0] priority_masks [0:15];
    (* ram_style = "distributed" *) reg [31:0] priority_eval_masks [0:15];
    (* ram_style = "distributed" *) reg [31:0] priority_first_masks [0:15];
    reg [3:0] priority_read_addr;
    wire [31:0] priority_read_data = priority_masks[priority_read_addr];
    wire [31:0] priority_read_onehot =
        priority_first_masks[priority_read_addr];
    reg priority_write_en;
    reg [3:0] priority_write_addr;
    reg [31:0] priority_write_data;

    reg [19:0] desc_ctrl;
    reg desc_ctrl_invalid;
    reg [31:0] desc_pos;
    reg [31:0] desc_size;
    reg [31:0] desc_base;
    reg [4:0] desc_index;
    reg [2:0] desc_word;
    reg       desc_pipe1_valid;
    reg       desc_pipe2_valid;
    reg [4:0] desc_pipe1_index;
    reg [4:0] desc_pipe2_index;
    reg [2:0] desc_pipe1_word;
    reg [2:0] desc_pipe2_word;
    reg       desc_issue_done;
    reg [5:0] state;
    reg job_bank;
    reg [9:0] job_y;
    reg [9:0] job_width;
    reg job_enable;
    reg [15:0] job_budget;
    reg [3:0] clear_chunk;
    reg line_clear_active;
    reg [6:0] line_clear_index;
    reg [7:0] line_clear_count;
    wire [10:0] line_clear_count_calc =
        ({1'b0, line_width} + 11'd7) >> 3;

    // Descriptor words arrive five clocks apart. Pipeline geometry, row
    // multiply, base addition, and range checking so evaluation does not add
    // three dead clocks to every descriptor.
    reg eval_raw_valid;
    reg [4:0] eval_raw_index;
    reg [19:0] eval_raw_ctrl;
    reg eval_raw_ctrl_invalid;
    reg [31:0] eval_raw_pos;
    reg [31:0] eval_raw_size;
    reg [31:0] eval_raw_base;
    reg [31:0] eval_raw_pitch_word;

    reg signed [17:0] eval_calc_x_signed;
    reg signed [17:0] eval_calc_y_signed;
    reg signed [17:0] eval_calc_right_signed;
    reg signed [17:0] eval_calc_bottom_signed;
    reg signed [17:0] eval_calc_left_signed;
    reg signed [17:0] eval_calc_right_clipped_signed;
    reg eval_calc_active;
    reg eval_calc_invalid;
    reg [9:0] eval_calc_left;
    reg [9:0] eval_calc_visible_width;
    reg [15:0] eval_calc_width;
    reg [15:0] eval_calc_height;
    reg [15:0] eval_calc_pitch;
    reg [15:0] eval_calc_source_x;
    reg [15:0] eval_calc_row_offset;
    reg [15:0] eval_calc_source_row;
    reg [16:0] eval_calc_row_bytes;

    always @* begin
        eval_calc_x_signed = {{2{eval_raw_pos[15]}},
                              eval_raw_pos[15:0]};
        eval_calc_y_signed = {{2{eval_raw_pos[31]}},
                              eval_raw_pos[31:16]};
        eval_calc_width = eval_raw_size[15:0];
        eval_calc_height = eval_raw_size[31:16];
        eval_calc_pitch = eval_raw_pitch_word[15:0];
        eval_calc_right_signed = eval_calc_x_signed +
                                 $signed({2'b00, eval_calc_width});
        eval_calc_bottom_signed = eval_calc_y_signed +
                                  $signed({2'b00, eval_calc_height});
        eval_calc_left_signed = eval_calc_x_signed < 0 ?
                                18'sd0 : eval_calc_x_signed;
        eval_calc_right_clipped_signed =
            eval_calc_right_signed > $signed({8'd0, job_width}) ?
            $signed({8'd0, job_width}) : eval_calc_right_signed;
        eval_calc_left = eval_calc_left_signed[9:0];
        eval_calc_visible_width =
            eval_calc_right_clipped_signed > eval_calc_left_signed ?
            eval_calc_right_clipped_signed[9:0] -
                eval_calc_left_signed[9:0] :
            10'd0;
        eval_calc_source_x = eval_calc_left_signed - eval_calc_x_signed;
        eval_calc_row_offset = $signed({8'd0, job_y}) -
                               eval_calc_y_signed;
        eval_calc_source_row = eval_raw_ctrl[3] ?
            eval_calc_height - 16'd1 - eval_calc_row_offset :
            eval_calc_row_offset;
        eval_calc_row_bytes = ({1'b0, eval_calc_width} + 17'd1) >> 1;
        eval_calc_active = eval_raw_ctrl[0] && eval_raw_ctrl[1] &&
                           eval_calc_width != 16'd0 &&
                           eval_calc_height != 16'd0 &&
                           $signed({8'd0, job_y}) >= eval_calc_y_signed &&
                           $signed({8'd0, job_y}) <
                               eval_calc_bottom_signed &&
                           eval_calc_right_signed > 0 &&
                           eval_calc_x_signed <
                               $signed({8'd0, job_width}) &&
                           eval_calc_visible_width != 10'd0;
        eval_calc_invalid = eval_raw_ctrl_invalid ||
                            eval_raw_pitch_word[31:16] != 16'd0 ||
                            {1'b0, eval_calc_pitch} < eval_calc_row_bytes;
    end

    reg eval_geometry_valid;
    reg [4:0] eval_geometry_index;
    reg [19:0] eval_geometry_ctrl;
    reg eval_geometry_active;
    reg eval_geometry_invalid;
    reg [9:0] eval_geometry_left;
    reg [9:0] eval_geometry_visible_width;
    reg [15:0] eval_geometry_source_x;
    reg [15:0] eval_geometry_full_width;
    reg [15:0] eval_geometry_source_row;
    reg [15:0] eval_geometry_pitch;
    reg [31:0] eval_geometry_base;
    reg [16:0] eval_geometry_row_bytes;

    reg eval_product_valid;
    reg [4:0] eval_product_index;
    reg [19:0] eval_product_ctrl;
    reg eval_product_active;
    reg eval_product_invalid;
    reg [9:0] eval_product_left;
    reg [9:0] eval_product_visible_width;
    reg [15:0] eval_product_source_x;
    reg [15:0] eval_product_full_width;
    reg [31:0] eval_product_base;
    reg [16:0] eval_product_row_bytes;
    reg [31:0] eval_product_offset;

    reg eval_base_valid;
    reg [4:0] eval_base_index;
    reg [19:0] eval_base_ctrl;
    reg eval_base_active;
    reg eval_base_invalid;
    reg [9:0] eval_base_left;
    reg [9:0] eval_base_visible_width;
    reg [15:0] eval_base_source_x;
    reg [15:0] eval_base_full_width;
    reg [16:0] eval_base_row_bytes;
    reg [31:0] eval_base_row_base_wide;
    wire [31:0] priority_eval_read_data =
        priority_eval_masks[eval_base_ctrl[11:8]];

    reg eval_final_valid;
    reg [4:0] eval_final_index;
    reg [19:0] eval_final_ctrl;
    reg eval_final_active;
    reg eval_final_invalid;
    reg [9:0] eval_final_left;
    reg [9:0] eval_final_visible_width;
    reg [15:0] eval_final_source_x;
    reg [15:0] eval_final_full_width;
    reg [15:0] eval_final_source_low;
    reg [31:0] eval_final_row_base_wide;
    reg eval_final_address_invalid;
    reg [31:0] eval_final_priority_mask;

    // Active sprites always have a nonzero row span. Validate the inclusive
    // final byte so a legal row ending at 0x01ffffff is a single carry test.
    wire [32:0] eval_base_last_byte_wide =
        {1'b0, eval_base_row_base_wide} +
        {{16{1'b0}}, eval_base_row_bytes} - 33'd1;
    wire eval_final_reject = eval_final_active &&
        (eval_final_invalid || eval_final_address_invalid);
    wire eval_final_accept = eval_final_active && !eval_final_invalid &&
        !eval_final_address_invalid;

    reg [3:0] select_priority;
    reg [31:0] select_priority_mask;
    reg [31:0] selected_onehot_q;
    reg [9:0] select_meta_width;
    reg [15:0] budget_remaining;
    reg overflow_frame_mem;
    reg overflow_line_mem;

    reg render_end_after_sprite;
    reg [4:0] render_order [0:31];
    reg [5:0] render_count;
    reg [5:0] render_position;
    reg [4:0] render_sprite_index;
    reg [19:0] render_ctrl;
    reg [9:0] render_screen_x;
    reg [9:0] render_remaining;
    reg [15:0] render_source_low;
    reg [24:0] render_row_base;
    reg [6:0] render_word_cursor;
    reg [2:0] render_nibble_cursor;

    reg [3:0] render_chunk_count;
    reg [24:0] row_word_base;
    reg [7:0] row_word_count;
    reg [7:0] row_issue_index;
    reg [7:0] row_rsp_index;
    reg [6:0] row_outstanding;
    reg [4:0] row_burst_count;
    reg [6:0] row_read_a;
    reg [6:0] row_read_b;
    reg [31:0] row_data_a_q;
    reg [31:0] row_data_b_q;
    (* ram_style = "block" *) reg [31:0] row_pattern_a [0:127];
    (* ram_style = "block" *) reg [31:0] row_pattern_b [0:127];
    always @(posedge mem_clk) begin
        row_data_a_q <= row_pattern_a[row_read_a];
        row_data_b_q <= row_pattern_b[row_read_b];
    end

    // Admission has two cycles between selections while metadata is consumed.
    // The descriptor pass records each priority's first one-hot result, so a
    // new priority does not put a 32-bit carry chain after the mask RAM.
    // Subsequent selections use the already-registered working mask.
    wire [31:0] selection_mask_onehot = select_priority_mask &
        (~select_priority_mask + 32'd1);
    wire [4:0] selected_index = {
        |(selected_onehot_q & 32'hffff0000),
        |(selected_onehot_q & 32'hff00ff00),
        |(selected_onehot_q & 32'hf0f0f0f0),
        |(selected_onehot_q & 32'hcccccccc),
        |(selected_onehot_q & 32'haaaaaaaa)
    };
    wire [19:0] active_meta_ctrl = active_meta_q[80:61];
    wire [9:0] active_meta_x = active_meta_q[60:51];
    wire [9:0] active_meta_width = active_meta_q[50:41];
    wire [15:0] active_meta_source_low = active_meta_q[40:25];
    wire [24:0] active_meta_row_base = active_meta_q[24:0];

    always @* begin
        active_meta_read_addr = 5'd0;
        if (state == ST_SELECT && select_priority_mask != 32'd0)
            active_meta_read_addr = selected_index;
        else if (render_position != 6'd0)
            active_meta_read_addr =
                render_order[render_position - 6'd1];
    end

    always @* begin
        priority_read_addr = select_priority;
        if (state == ST_SELECT_SETUP)
            priority_read_addr = 4'd15;
        else if ((state == ST_SELECT || state == ST_SELECT_META ||
                  state == ST_SELECT_DECIDE) &&
                 select_priority != 4'd0)
            priority_read_addr = select_priority - 4'd1;

        priority_write_en = 1'b0;
        priority_write_addr = 4'd0;
        priority_write_data = 32'd0;
        if (state == ST_CLEAR) begin
            priority_write_en = 1'b1;
            priority_write_addr = clear_chunk[3:0];
        end else if (eval_final_valid && eval_final_accept) begin
            priority_write_en = 1'b1;
            priority_write_addr = eval_final_ctrl[11:8];
            priority_write_data = eval_final_priority_mask |
                (32'd1 << eval_final_index);
        end
    end

    always @(posedge mem_clk) begin
        if (!mem_rst && priority_write_en) begin
            priority_masks[priority_write_addr] <= priority_write_data;
            priority_eval_masks[priority_write_addr] <= priority_write_data;
        end
        if (!mem_rst && state == ST_CLEAR)
            priority_first_masks[clear_chunk[3:0]] <= 32'd0;
        else if (!mem_rst && eval_final_valid && eval_final_accept &&
                 eval_final_priority_mask == 32'd0)
            priority_first_masks[eval_final_ctrl[11:8]] <=
                32'd1 << eval_final_index;
    end

    always @(posedge mem_clk) begin
        if (!mem_rst && eval_final_valid && eval_final_accept)
            active_meta_mem[eval_final_index] <= {
                eval_final_ctrl,
                eval_final_left,
                eval_final_visible_width,
                eval_final_source_low,
                eval_final_row_base_wide[24:0]
            };
        active_meta_q <= active_meta_mem[active_meta_read_addr];
    end

    reg [3:0] prep_count;

    wire [24:0] row_byte_low_calc = render_row_base +
                                    {10'd0, render_source_low[15:1]};
    wire [24:0] row_word_base_calc = {row_byte_low_calc[24:2], 2'b00};
    wire [16:0] row_source_nibble_low = {1'b0, render_source_low} +
        {14'd0, render_row_base[1:0], 1'b0};
    wire [10:0] row_nibble_span =
        {8'd0, row_source_nibble_low[2:0]} +
        {1'b0, render_remaining} + 11'd7;
    wire [7:0] row_word_count_calc = row_nibble_span[10:3];
    wire [10:0] row_last_nibble =
        {8'd0, row_source_nibble_low[2:0]} +
        {1'b0, render_remaining} - 11'd1;

    always @* begin
        prep_count = 4'd8 - {1'b0, render_screen_x[2:0]};
        if (render_remaining < prep_count)
            prep_count = render_remaining[3:0];
    end

    wire [3:0] prep_reverse_delta = prep_count - 4'd1;
    wire prep_reverse_crosses_word =
        prep_reverse_delta > {1'b0, render_nibble_cursor};
    wire [3:0] prep_forward_end =
        {1'b0, render_nibble_cursor} + prep_count;
    wire [6:0] prep_row_word_index = render_ctrl[2] ?
        render_word_cursor - {6'd0, prep_reverse_crosses_word} :
        render_word_cursor;
    wire [2:0] prep_nibble_offset = render_ctrl[2] ?
        render_nibble_cursor - prep_reverse_delta[2:0] :
        render_nibble_cursor;
    wire prep_need_word1 = render_ctrl[2] ?
        prep_reverse_crosses_word : prep_forward_end > 4'd8;

    wire [3:0] cursor_forward_next =
        {1'b0, render_nibble_cursor} + render_chunk_count;
    wire cursor_reverse_crosses_word =
        render_chunk_count > {1'b0, render_nibble_cursor};
    wire [2:0] cursor_reverse_next =
        render_nibble_cursor - render_chunk_count[2:0];

    reg [71:0] behind_build_q;
    reg [71:0] front_build_q;
    reg [47:0] collision_build_q;
    reg [47:0] collision_compose_q;
    reg [2:0] prep_nibble_offset_q;
    reg prep_need_word1_q;
    reg [2:0] prep_screen_lane_q;
    reg [31:0] ascending_pattern_q;
    reg [31:0] compose_nibbles_q;
    reg [7:0] compose_lane_mask_q;

    wire [31:0] aligned_pattern_word1 = prep_need_word1_q ?
        row_data_b_q : 32'd0;
    wire [63:0] shifted_pattern_window =
        {row_data_a_q, aligned_pattern_word1} <<
        {prep_nibble_offset_q, 2'b00};
    wire [31:0] ascending_pattern = shifted_pattern_window[63:32];
    reg [31:0] reversed_pattern;
    integer reverse_lane;
    always @* begin
        reversed_pattern = 32'd0;
        for (reverse_lane = 0; reverse_lane < 8;
             reverse_lane = reverse_lane + 1)
            reversed_pattern[(7 - reverse_lane) * 4 +: 4] =
                ascending_pattern_q[reverse_lane * 4 +: 4];
    end
    wire [3:0] reverse_skip = 4'd8 - render_chunk_count;
    wire [31:0] screen_order_pattern = render_ctrl[2] ?
        reversed_pattern << {reverse_skip, 2'b00} : ascending_pattern_q;
    wire [31:0] aligned_lane_nibbles =
        screen_order_pattern >> {prep_screen_lane_q, 2'b00};
    wire [8:0] chunk_lane_bits =
        (9'd1 << render_chunk_count) - 9'd1;
    wire [7:0] aligned_lane_mask =
        chunk_lane_bits[7:0] << prep_screen_lane_q;

    reg [71:0] composed_behind;
    reg [71:0] composed_front;
    reg [47:0] composed_collision;
    integer compose_lane;

    wire [3:0] compose_lane_nibble [0:7];
    wire [5:0] compose_lane_owner [0:7];
    wire [7:0] compose_lane_opaque;
    wire [7:0] compose_lane_collides;
    wire [31:0] compose_owner_bit [0:7];
    genvar collision_lane;
    generate
        for (collision_lane = 0; collision_lane < 8;
             collision_lane = collision_lane + 1) begin : g_collision_lane
            assign compose_lane_nibble[collision_lane] =
                compose_nibbles_q[(7 - collision_lane) * 4 +: 4];
            assign compose_lane_owner[collision_lane] =
                collision_compose_q[(7 - collision_lane) * 6 +: 6];
            assign compose_lane_opaque[collision_lane] =
                compose_lane_mask_q[collision_lane] &&
                compose_lane_nibble[collision_lane] != render_ctrl[19:16];
            assign compose_lane_collides[collision_lane] =
                compose_lane_opaque[collision_lane] && render_ctrl[5] &&
                compose_lane_owner[collision_lane][5] &&
                compose_lane_owner[collision_lane][4:0] !=
                    render_sprite_index;
            assign compose_owner_bit[collision_lane] =
                compose_lane_collides[collision_lane] ?
                (32'd1 << compose_lane_owner[collision_lane][4:0]) :
                32'd0;
        end
    endgenerate

    wire [31:0] compose_owner_pair0 =
        compose_owner_bit[0] | compose_owner_bit[1];
    wire [31:0] compose_owner_pair1 =
        compose_owner_bit[2] | compose_owner_bit[3];
    wire [31:0] compose_owner_pair2 =
        compose_owner_bit[4] | compose_owner_bit[5];
    wire [31:0] compose_owner_pair3 =
        compose_owner_bit[6] | compose_owner_bit[7];
    wire [31:0] compose_owner_quad0 =
        compose_owner_pair0 | compose_owner_pair1;
    wire [31:0] compose_owner_quad1 =
        compose_owner_pair2 | compose_owner_pair3;
    wire [31:0] compose_owner_collision_bits =
        compose_owner_quad0 | compose_owner_quad1;
    wire compose_any_collision = |compose_lane_collides;
    wire [31:0] compose_current_collision_bit = compose_any_collision ?
        (32'd1 << render_sprite_index) : 32'd0;
    wire [31:0] composed_collision_bits =
        compose_owner_collision_bits | compose_current_collision_bit;

    always @* begin
        composed_behind = behind_build_q;
        composed_front = front_build_q;
        composed_collision = collision_compose_q;
        for (compose_lane = 0; compose_lane < 8;
             compose_lane = compose_lane + 1) begin
            if (compose_lane_opaque[compose_lane]) begin
                if (render_ctrl[4])
                    composed_behind[(7 - compose_lane) * 9 +: 9] =
                        {1'b1, render_ctrl[15:12],
                         compose_lane_nibble[compose_lane]};
                else
                    composed_front[(7 - compose_lane) * 9 +: 9] =
                        {1'b1, render_ctrl[15:12],
                         compose_lane_nibble[compose_lane]};

                if (render_ctrl[5])
                    composed_collision[
                        (7 - compose_lane) * 6 +: 6] =
                        {1'b1, render_sprite_index};
            end
        end
    end

    wire [7:0] compose_line_addr = {job_bank, render_screen_x[9:3]};
    wire [7:0] clear_line_addr = {job_bank, line_clear_index};
    wire [7:0] build_line_addr = line_clear_active ?
                                clear_line_addr : compose_line_addr;
    wire line_write_enable = line_clear_active || state == ST_LINE_WRITE;
    wire [71:0] line_write_behind = line_clear_active ?
                                      72'd0 : composed_behind;
    wire [71:0] line_write_front = line_clear_active ?
                                     72'd0 : composed_front;
    wire [47:0] line_write_collision = line_clear_active ?
                                         48'd0 : composed_collision;
    always @(posedge mem_clk) begin
        if (mem_rst) begin
            behind_build_q <= 72'd0;
            front_build_q <= 72'd0;
            collision_build_q <= 48'd0;
        end else begin
            behind_build_q <= behind_line[build_line_addr];
            front_build_q <= front_line[build_line_addr];
            collision_build_q <= collision_line[build_line_addr];
            if (line_write_enable) begin
                behind_line[build_line_addr] <= line_write_behind;
                front_line[build_line_addr] <= line_write_front;
                collision_line[build_line_addr] <= line_write_collision;
            end
        end
    end

    reg [31:0] collision_bitmap_mem;
    reg [31:0] collision_published_mem;
    reg collision_line_event_mem;
    reg collision_toggle_mem;
    reg overflow_published_mem;
    reg [31:0] collision_meta_cpu;
    reg overflow_meta_cpu;
    reg [1:0] collision_toggle_sync_cpu;
    reg collision_toggle_seen_cpu;

    always @(posedge cpu_clk) begin
        collision_event <= 1'b0;
        if (cpu_rst) begin
            collision_meta_cpu <= 32'd0;
            collision_bitmap <= 32'd0;
            overflow_meta_cpu <= 1'b0;
            overflow <= 1'b0;
            collision_toggle_sync_cpu <= 2'b00;
            collision_toggle_seen_cpu <= 1'b0;
            collision_event <= 1'b0;
        end else begin
            collision_meta_cpu <= collision_published_mem;
            collision_bitmap <= collision_meta_cpu;
            overflow_meta_cpu <= overflow_published_mem;
            overflow <= overflow_meta_cpu;
            collision_toggle_sync_cpu <= {collision_toggle_sync_cpu[0],
                                          collision_toggle_mem};
            if (collision_toggle_sync_cpu[1] !=
                collision_toggle_seen_cpu) begin
                collision_toggle_seen_cpu <= collision_toggle_sync_cpu[1];
                collision_event <= 1'b1;
            end
        end
    end

    // ST_ROW_SETUP rejects zero-length rows and ST_ROW_ISSUE transitions on
    // the same edge that accepts the final word, so the index comparison is
    // redundant. Keeping the shared-arbiter request on the registered state
    // removes a row-width-to-grant combinational path.
    wire row_issue_state = state == ST_ROW_ISSUE;
    wire row_drain_state = state == ST_ROW_DRAIN;
    assign mem_lock = row_issue_state || row_drain_state;
    assign mem_valid = row_issue_state;
    assign mem_write = 1'b0;
    assign mem_addr = row_word_base + {15'd0, row_issue_index, 2'b00};
    assign mem_be = 4'b1111;
    assign mem_wdata = 32'd0;
    wire row_accept = mem_valid && mem_ready;

    always @(posedge mem_clk) begin
        done <= 1'b0;
        if (mem_rst) begin
            state <= ST_IDLE;
            busy <= 1'b0;
            done <= 1'b0;
            config_error <= 1'b0;
            job_bank <= 1'b0;
            job_y <= 10'd0;
            job_width <= 10'd0;
            job_enable <= 1'b0;
            job_budget <= 16'd0;
            clear_chunk <= 4'd0;
            line_clear_active <= 1'b0;
            line_clear_index <= 7'd0;
            line_clear_count <= 8'd0;
            desc_ctrl <= 20'd0;
            desc_ctrl_invalid <= 1'b0;
            desc_pos <= 32'd0;
            desc_size <= 32'd0;
            desc_base <= 32'd0;
            desc_index <= 5'd0;
            desc_word <= 3'd0;
            desc_pipe1_valid <= 1'b0;
            desc_pipe2_valid <= 1'b0;
            desc_pipe1_index <= 5'd0;
            desc_pipe2_index <= 5'd0;
            desc_pipe1_word <= 3'd0;
            desc_pipe2_word <= 3'd0;
            desc_issue_done <= 1'b0;
            descriptor_read_addr <= 8'd0;
            eval_raw_valid <= 1'b0;
            eval_raw_index <= 5'd0;
            eval_raw_ctrl <= 20'd0;
            eval_raw_ctrl_invalid <= 1'b0;
            eval_raw_pos <= 32'd0;
            eval_raw_size <= 32'd0;
            eval_raw_base <= 32'd0;
            eval_raw_pitch_word <= 32'd0;
            eval_geometry_valid <= 1'b0;
            eval_geometry_index <= 5'd0;
            eval_geometry_ctrl <= 20'd0;
            eval_geometry_active <= 1'b0;
            eval_geometry_invalid <= 1'b0;
            eval_geometry_left <= 10'd0;
            eval_geometry_visible_width <= 10'd0;
            eval_geometry_source_x <= 16'd0;
            eval_geometry_full_width <= 16'd0;
            eval_geometry_source_row <= 16'd0;
            eval_geometry_pitch <= 16'd0;
            eval_geometry_base <= 32'd0;
            eval_geometry_row_bytes <= 17'd0;
            eval_product_valid <= 1'b0;
            eval_product_index <= 5'd0;
            eval_product_ctrl <= 20'd0;
            eval_product_active <= 1'b0;
            eval_product_invalid <= 1'b0;
            eval_product_left <= 10'd0;
            eval_product_visible_width <= 10'd0;
            eval_product_source_x <= 16'd0;
            eval_product_full_width <= 16'd0;
            eval_product_base <= 32'd0;
            eval_product_row_bytes <= 17'd0;
            eval_product_offset <= 32'd0;
            eval_base_valid <= 1'b0;
            eval_base_index <= 5'd0;
            eval_base_ctrl <= 20'd0;
            eval_base_active <= 1'b0;
            eval_base_invalid <= 1'b0;
            eval_base_left <= 10'd0;
            eval_base_visible_width <= 10'd0;
            eval_base_source_x <= 16'd0;
            eval_base_full_width <= 16'd0;
            eval_base_row_bytes <= 17'd0;
            eval_base_row_base_wide <= 32'd0;
            eval_final_valid <= 1'b0;
            eval_final_index <= 5'd0;
            eval_final_ctrl <= 20'd0;
            eval_final_active <= 1'b0;
            eval_final_invalid <= 1'b0;
            eval_final_left <= 10'd0;
            eval_final_visible_width <= 10'd0;
            eval_final_source_x <= 16'd0;
            eval_final_full_width <= 16'd0;
            eval_final_source_low <= 16'd0;
            eval_final_row_base_wide <= 32'd0;
            eval_final_address_invalid <= 1'b0;
            eval_final_priority_mask <= 32'd0;
            select_priority <= 4'd0;
            select_priority_mask <= 32'd0;
            selected_onehot_q <= 32'd0;
            select_meta_width <= 10'd0;
            budget_remaining <= 16'd0;
            overflow_frame_mem <= 1'b0;
            overflow_line_mem <= 1'b0;
            render_end_after_sprite <= 1'b0;
            render_count <= 6'd0;
            render_position <= 6'd0;
            render_sprite_index <= 5'd0;
            render_ctrl <= 20'd0;
            render_screen_x <= 10'd0;
            render_remaining <= 10'd0;
            render_source_low <= 16'd0;
            render_row_base <= 25'd0;
            render_word_cursor <= 7'd0;
            render_nibble_cursor <= 3'd0;
            render_chunk_count <= 4'd0;
            row_word_base <= 25'd0;
            row_word_count <= 8'd0;
            row_issue_index <= 8'd0;
            row_rsp_index <= 8'd0;
            row_outstanding <= 7'd0;
            row_burst_count <= 5'd0;
            row_read_a <= 7'd0;
            row_read_b <= 7'd0;
            prep_nibble_offset_q <= 3'd0;
            prep_need_word1_q <= 1'b0;
            prep_screen_lane_q <= 3'd0;
            ascending_pattern_q <= 32'd0;
            compose_nibbles_q <= 32'd0;
            compose_lane_mask_q <= 8'd0;
            collision_compose_q <= 48'd0;
            collision_bitmap_mem <= 32'd0;
            collision_published_mem <= 32'd0;
            collision_line_event_mem <= 1'b0;
            collision_toggle_mem <= 1'b0;
            overflow_published_mem <= 1'b0;
        end else begin
            eval_raw_valid <= 1'b0;
            eval_geometry_valid <= eval_raw_valid;
            eval_product_valid <= eval_geometry_valid;
            eval_base_valid <= eval_product_valid;
            eval_final_valid <= eval_base_valid;

            if (eval_raw_valid) begin
                eval_geometry_index <= eval_raw_index;
                eval_geometry_ctrl <= eval_raw_ctrl;
                eval_geometry_active <= eval_calc_active;
                eval_geometry_invalid <= eval_calc_invalid;
                eval_geometry_left <= eval_calc_left;
                eval_geometry_visible_width <= eval_calc_visible_width;
                eval_geometry_source_x <= eval_calc_source_x;
                eval_geometry_full_width <= eval_calc_width;
                eval_geometry_source_row <= eval_calc_source_row;
                eval_geometry_pitch <= eval_calc_pitch;
                eval_geometry_base <= eval_raw_base;
                eval_geometry_row_bytes <= eval_calc_row_bytes;
            end
            if (eval_geometry_valid) begin
                eval_product_index <= eval_geometry_index;
                eval_product_ctrl <= eval_geometry_ctrl;
                eval_product_active <= eval_geometry_active;
                eval_product_invalid <= eval_geometry_invalid;
                eval_product_left <= eval_geometry_left;
                eval_product_visible_width <=
                    eval_geometry_visible_width;
                eval_product_source_x <= eval_geometry_source_x;
                eval_product_full_width <= eval_geometry_full_width;
                eval_product_base <= eval_geometry_base;
                eval_product_row_bytes <= eval_geometry_row_bytes;
                eval_product_offset <= eval_geometry_source_row *
                                       eval_geometry_pitch;
            end
            if (eval_product_valid) begin
                eval_base_index <= eval_product_index;
                eval_base_ctrl <= eval_product_ctrl;
                eval_base_active <= eval_product_active;
                eval_base_invalid <= eval_product_invalid;
                eval_base_left <= eval_product_left;
                eval_base_visible_width <= eval_product_visible_width;
                eval_base_source_x <= eval_product_source_x;
                eval_base_full_width <= eval_product_full_width;
                eval_base_row_bytes <= eval_product_row_bytes;
                eval_base_row_base_wide <= eval_product_base +
                                           eval_product_offset;
            end
            if (eval_base_valid) begin
                eval_final_index <= eval_base_index;
                eval_final_ctrl <= eval_base_ctrl;
                eval_final_active <= eval_base_active;
                eval_final_invalid <= eval_base_invalid;
                eval_final_left <= eval_base_left;
                eval_final_visible_width <= eval_base_visible_width;
                eval_final_source_x <= eval_base_source_x;
                eval_final_full_width <= eval_base_full_width;
                eval_final_source_low <= eval_base_ctrl[2] ?
                    eval_base_full_width - eval_base_source_x -
                        {6'd0, eval_base_visible_width} :
                    eval_base_source_x;
                eval_final_row_base_wide <= eval_base_row_base_wide;
                eval_final_address_invalid <=
                    |eval_base_last_byte_wide[32:25];
                // Descriptor results are five clocks apart. Read the mirrored
                // evaluation mask one stage early so selection's one-hot
                // encoder has no path back into descriptor admission.
                eval_final_priority_mask <= priority_eval_read_data;
            end
            if (eval_final_valid && eval_final_reject)
                config_error <= 1'b1;

            if (state == ST_SELECT_SETUP ||
                (state == ST_SELECT && select_priority_mask == 32'd0 &&
                 select_priority != 4'd0))
                selected_onehot_q <= priority_read_onehot;
            else if (state == ST_SELECT_META)
                selected_onehot_q <= selection_mask_onehot;

            if (line_clear_active) begin
                if ({1'b0, line_clear_index} + 8'd1 >= line_clear_count)
                    line_clear_active <= 1'b0;
                else
                    line_clear_index <= line_clear_index + 7'd1;
            end

            case ({row_accept, mem_rsp_valid})
                2'b10: row_outstanding <= row_outstanding + 7'd1;
                2'b01: row_outstanding <= row_outstanding - 7'd1;
                default: row_outstanding <= row_outstanding;
            endcase
            if (mem_rsp_valid) begin
                row_pattern_a[row_rsp_index[6:0]] <= mem_rdata;
                row_pattern_b[row_rsp_index[6:0]] <= mem_rdata;
                row_rsp_index <= row_rsp_index + 8'd1;
            end
            case (state)
                ST_IDLE: begin
                    busy <= 1'b0;
                    if (start) begin
                        busy <= 1'b1;
                        config_error <= 1'b0;
                        job_bank <= build_bank;
                        job_y <= line_y;
                        job_width <= line_width;
                        job_enable <= enable;
                        job_budget <= pixel_budget;
                        clear_chunk <= 4'd0;
                        line_clear_active <= line_width != 10'd0;
                        line_clear_index <= 7'd0;
                        line_clear_count <= line_clear_count_calc[7:0];
                        select_priority_mask <= 32'd0;
                        render_count <= 6'd0;
                        overflow_line_mem <= 1'b0;
                        collision_line_event_mem <= 1'b0;
                        if (line_y == 10'd0) begin
                            collision_bitmap_mem <= 32'd0;
                            overflow_frame_mem <= 1'b0;
                        end
                        state <= ST_CLEAR;
                    end
                end

                ST_CLEAR: begin
                    if (clear_chunk == 4'd15) begin
                        if (job_enable)
                            state <= ST_DESC_SETUP;
                        else if (!line_clear_active)
                            state <= ST_FINISH;
                    end else begin
                        clear_chunk <= clear_chunk + 4'd1;
                    end
                end

                ST_DESC_SETUP: begin
                    desc_index <= 5'd0;
                    desc_word <= 3'd0;
                    desc_pipe1_valid <= 1'b0;
                    desc_pipe2_valid <= 1'b0;
                    desc_issue_done <= 1'b0;
                    state <= ST_DESC_ISSUE;
                end

                ST_DESC_ISSUE: begin
                    desc_pipe2_valid <= desc_pipe1_valid;
                    desc_pipe2_index <= desc_pipe1_index;
                    desc_pipe2_word <= desc_pipe1_word;
                    if (!desc_issue_done) begin
                        descriptor_read_addr <=
                            {desc_index, 3'b000} + desc_word;
                        desc_pipe1_valid <= 1'b1;
                        desc_pipe1_index <= desc_index;
                        desc_pipe1_word <= desc_word;
                        if (desc_word == 3'd4) begin
                            desc_word <= 3'd0;
                            if (desc_index == 5'd31)
                                desc_issue_done <= 1'b1;
                            else
                                desc_index <= desc_index + 5'd1;
                        end else begin
                            desc_word <= desc_word + 3'd1;
                        end
                    end else begin
                        desc_pipe1_valid <= 1'b0;
                    end

                    if (desc_pipe2_valid) begin
                        case (desc_pipe2_word)
                            3'd0: begin
                                desc_ctrl <= descriptor_mem_q[19:0];
                                desc_ctrl_invalid <=
                                    descriptor_mem_q[31:20] != 12'd0 ||
                                    descriptor_mem_q[7:6] != 2'd0;
                            end
                            3'd1: desc_pos <= descriptor_mem_q;
                            3'd2: desc_size <= descriptor_mem_q;
                            3'd3: desc_base <= descriptor_mem_q;
                            default: begin
                                eval_raw_valid <= 1'b1;
                                eval_raw_index <= desc_pipe2_index;
                                eval_raw_ctrl <= desc_ctrl;
                                eval_raw_ctrl_invalid <= desc_ctrl_invalid;
                                eval_raw_pos <= desc_pos;
                                eval_raw_size <= desc_size;
                                eval_raw_base <= desc_base;
                                eval_raw_pitch_word <= descriptor_mem_q;
                            end
                        endcase
                        if (desc_pipe2_index == 5'd31 &&
                            desc_pipe2_word == 3'd4)
                            state <= ST_DESC_DRAIN;
                    end
                end

                ST_DESC_DRAIN: begin
                    if (eval_final_valid && eval_final_index == 5'd31 &&
                        !line_clear_active)
                        state <= ST_SELECT_SETUP;
                end

                ST_SELECT_SETUP: begin
                    select_priority <= 4'd15;
                    select_priority_mask <= priority_read_data;
                    budget_remaining <= job_budget;
                    render_count <= 6'd0;
                    state <= ST_SELECT;
                end

                // Admission runs topmost first. A sprite is either admitted in
                // full or dropped; lower-priority sprites cannot consume a
                // higher-priority sprite's reserved scanline budget.
                ST_SELECT: begin
                    if (select_priority_mask == 32'd0) begin
                        if (select_priority == 4'd0) begin
                            render_position <= render_count;
                            state <= render_count == 6'd0 ?
                                     ST_FINISH : ST_FIND;
                        end else begin
                            select_priority <= select_priority - 4'd1;
                            select_priority_mask <= priority_read_data;
                        end
                    end else begin
                        select_priority_mask <= select_priority_mask &
                                                ~selected_onehot_q;
                        render_sprite_index <= selected_index;
                        state <= ST_SELECT_META;
                    end
                end

                ST_SELECT_META: begin
                    render_order[render_count] <= render_sprite_index;
                    select_meta_width <= active_meta_width;
                    state <= ST_SELECT_DECIDE;
                end

                ST_SELECT_DECIDE: begin
                    if (budget_remaining[15:10] != 6'd0 ||
                        select_meta_width <= budget_remaining[9:0]) begin
                        budget_remaining <= budget_remaining -
                                            {6'd0, select_meta_width};
                        render_count <= render_count + 6'd1;
                    end else begin
                        overflow_line_mem <= 1'b1;
                        overflow_frame_mem <= 1'b1;
                    end
                    state <= ST_SELECT;
                end

                // Admission stored topmost-to-bottommost order. Walk it in
                // reverse so rendering remains bottom-to-top.
                ST_FIND: begin
                    if (render_position == 6'd0) begin
                        state <= ST_FINISH;
                    end else begin
                        render_position <= render_position - 6'd1;
                        render_sprite_index <=
                            render_order[render_position - 6'd1];
                        render_end_after_sprite <= render_position == 6'd1;
                        state <= ST_FIND_META;
                    end
                end

                ST_FIND_META: begin
                    render_ctrl <= active_meta_ctrl;
                    render_screen_x <= active_meta_x;
                    render_remaining <= active_meta_width;
                    render_row_base <= active_meta_row_base;
                    render_source_low <= active_meta_source_low;
                    state <= ST_ROW_SETUP;
                end

                ST_ROW_SETUP: begin
                    if (row_word_count_calc == 8'd0 ||
                        row_word_count_calc > 8'd128) begin
                        config_error <= 1'b1;
                        state <= ST_FINISH;
                    end else begin
                        row_word_base <= row_word_base_calc;
                        row_word_count <= row_word_count_calc;
                        render_word_cursor <= render_ctrl[2] ?
                            row_word_count_calc - 8'd1 : 7'd0;
                        render_nibble_cursor <= render_ctrl[2] ?
                            row_last_nibble[2:0] :
                            row_source_nibble_low[2:0];
                        row_issue_index <= 8'd0;
                        row_rsp_index <= 8'd0;
                        row_outstanding <= 7'd0;
                        row_burst_count <= 5'd0;
                        state <= ST_ROW_ISSUE;
                    end
                end

                ST_ROW_ISSUE: begin
                    if (row_accept) begin
                        row_issue_index <= row_issue_index + 8'd1;
                        row_burst_count <= row_burst_count + 5'd1;
                        if (row_issue_index + 8'd1 >= row_word_count ||
                            row_burst_count == 5'd31)
                            state <= ST_ROW_DRAIN;
                    end
                end

                ST_ROW_DRAIN: begin
                    if (row_outstanding == 7'd0 ||
                        (mem_rsp_valid && row_outstanding == 7'd1)) begin
                        if (row_issue_index >= row_word_count)
                            state <= ST_RENDER_PREP;
                        else
                            state <= ST_ROW_YIELD;
                    end
                end

                ST_ROW_YIELD: begin
                    row_burst_count <= 5'd0;
                    state <= ST_ROW_ISSUE;
                end

                ST_RENDER_PREP: begin
                    render_chunk_count <= prep_count;
                    row_read_a <= prep_row_word_index;
                    row_read_b <= prep_row_word_index + 7'd1;
                    prep_nibble_offset_q <= prep_nibble_offset;
                    prep_need_word1_q <= prep_need_word1;
                    prep_screen_lane_q <= render_screen_x[2:0];
                    state <= ST_PATTERN_READ;
                end

                ST_PATTERN_READ: state <= ST_LINE_READ;

                ST_LINE_READ: begin
                    ascending_pattern_q <= ascending_pattern;
                    collision_compose_q <= collision_build_q;
                    state <= ST_PATTERN_ALIGN;
                end

                ST_PATTERN_ALIGN: begin
                    compose_nibbles_q <= aligned_lane_nibbles;
                    compose_lane_mask_q <= aligned_lane_mask;
                    state <= ST_LINE_WRITE;
                end

                ST_LINE_WRITE: begin
                    collision_bitmap_mem <= collision_bitmap_mem |
                                            composed_collision_bits;
                    if (composed_collision_bits != 32'd0)
                        collision_line_event_mem <= 1'b1;

                    if (render_remaining <= render_chunk_count) begin
                        if (render_end_after_sprite)
                            state <= ST_FINISH;
                        else begin
                            // The next metadata word has been prefetched while
                            // the current sprite rendered. Retire and launch it
                            // without returning through the BRAM-read states.
                            render_position <= render_position - 6'd1;
                            render_sprite_index <=
                                render_order[render_position - 6'd1];
                            render_end_after_sprite <=
                                render_position == 6'd1;
                            render_ctrl <= active_meta_ctrl;
                            render_screen_x <= active_meta_x;
                            render_remaining <= active_meta_width;
                            render_row_base <= active_meta_row_base;
                            render_source_low <= active_meta_source_low;
                            state <= ST_ROW_SETUP;
                        end
                    end else begin
                        render_screen_x <= render_screen_x +
                                           render_chunk_count;
                        render_remaining <= render_remaining -
                                            render_chunk_count;
                        if (render_ctrl[2]) begin
                            render_word_cursor <= render_word_cursor -
                                {6'd0, cursor_reverse_crosses_word};
                            render_nibble_cursor <= cursor_reverse_next;
                        end else begin
                            render_word_cursor <= render_word_cursor +
                                {6'd0, cursor_forward_next[3]};
                            render_nibble_cursor <=
                                cursor_forward_next[2:0];
                        end
                        state <= ST_RENDER_PREP;
                    end
                end

                ST_FINISH: begin
                    busy <= 1'b0;
                    done <= 1'b1;
                    collision_published_mem <= collision_bitmap_mem;
                    overflow_published_mem <= overflow_frame_mem;
                    if (collision_line_event_mem)
                        collision_toggle_mem <= ~collision_toggle_mem;
                    state <= ST_IDLE;
                end

                default: begin
                    busy <= 1'b0;
                    config_error <= 1'b1;
                    done <= 1'b1;
                    state <= ST_IDLE;
                end
            endcase
        end
    end

    wire unused_overflow_line_mem = overflow_line_mem;
endmodule

`default_nettype wire
