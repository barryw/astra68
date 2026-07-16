// Two-layer 4bpp tile scanline builder. Map entries and pattern rows are
// fetched as ordered streams; scanout only touches ping-pong BRAM line buffers.
`default_nettype none

module vega_tile_builder (
    input  wire        mem_clk,
    input  wire        mem_rst,
    input  wire        start,
    input  wire        build_bank,
    input  wire [9:0]  line_y,
    input  wire [9:0]  line_width,

    input  wire [31:0] tile0_ctrl,
    input  wire [24:0] tile0_map,
    input  wire [24:0] tile0_set,
    input  wire [7:0]  tile0_size,
    input  wire [31:0] tile0_scroll,
    input  wire [31:0] tile1_ctrl,
    input  wire [24:0] tile1_map,
    input  wire [24:0] tile1_set,
    input  wire [7:0]  tile1_size,
    input  wire [31:0] tile1_scroll,

    output reg         busy,
    output reg         done,
    output reg         config_error,

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
    output reg  [17:0] tile0_pair,
    output reg  [17:0] tile1_pair
);
    localparam [3:0] ST_IDLE = 4'd0;
    localparam [3:0] ST_CLEAR = 4'd1;
    localparam [3:0] ST_LAYER = 4'd2;
    localparam [3:0] ST_SETUP = 4'd3;
    localparam [3:0] ST_MAP_STREAM = 4'd4;
    localparam [3:0] ST_PATTERN_STREAM = 4'd5;
    localparam [3:0] ST_COMPOSE = 4'd6;
    localparam [3:0] ST_FINISH = 4'd7;
    localparam [3:0] ST_SETUP_COMMIT = 4'd8;
    localparam [3:0] ST_SETUP_VALIDATE = 4'd9;

    localparam [4:0] TAG_DEPTH = 5'd16;

    function automatic [3:0] pattern_nibble(
        input        tile16,
        input [3:0]  source_x,
        input [31:0] pattern0,
        input [31:0] pattern1
    );
        begin
            if (tile16 && source_x[3])
                pattern_nibble = pattern1[31 - source_x[2:0] * 4 -: 4];
            else
                pattern_nibble = pattern0[31 - source_x[2:0] * 4 -: 4];
        end
    endfunction

    // CPU-domain configuration is stable for normal display programming. Two
    // sampling stages make each scanline use one coherent, settled snapshot.
    reg [31:0] t0_ctrl_m1, t0_ctrl_m2;
    reg [24:0] t0_map_m1, t0_map_m2;
    reg [24:0] t0_set_m1, t0_set_m2;
    reg [7:0]  t0_size_m1, t0_size_m2;
    reg [31:0] t0_scroll_m1, t0_scroll_m2;
    reg [31:0] t1_ctrl_m1, t1_ctrl_m2;
    reg [24:0] t1_map_m1, t1_map_m2;
    reg [24:0] t1_set_m1, t1_set_m2;
    reg [7:0]  t1_size_m1, t1_size_m2;
    reg [31:0] t1_scroll_m1, t1_scroll_m2;

    always @(posedge mem_clk) begin
        t0_ctrl_m1 <= tile0_ctrl; t0_ctrl_m2 <= t0_ctrl_m1;
        t0_map_m1 <= tile0_map; t0_map_m2 <= t0_map_m1;
        t0_set_m1 <= tile0_set; t0_set_m2 <= t0_set_m1;
        t0_size_m1 <= tile0_size; t0_size_m2 <= t0_size_m1;
        t0_scroll_m1 <= tile0_scroll; t0_scroll_m2 <= t0_scroll_m1;
        t1_ctrl_m1 <= tile1_ctrl; t1_ctrl_m2 <= t1_ctrl_m1;
        t1_map_m1 <= tile1_map; t1_map_m2 <= t1_map_m1;
        t1_set_m1 <= tile1_set; t1_set_m2 <= t1_set_m1;
        t1_size_m1 <= tile1_size; t1_size_m2 <= t1_size_m1;
        t1_scroll_m1 <= tile1_scroll; t1_scroll_m2 <= t1_scroll_m1;
    end

    reg [31:0] work_ctrl;
    reg [24:0] work_map;
    reg [24:0] work_set;
    reg [7:0]  work_size;
    reg [15:0] work_scroll_x;
    reg [15:0] work_scroll_y;
    reg        work_layer;
    reg        job_bank;
    reg [9:0]  job_y;
    reg [9:0]  job_width;
    reg [1:0]  config_wait;
    reg [3:0]  state;
    reg        setup_config_valid;
    reg signed [17:0] setup_first_raw_tx;
    reg signed [17:0] setup_world_y;
    reg [9:0]  setup_map_ty;
    reg        setup_y_valid;
    reg [17:0] setup_source_tile_count;
    reg        setup_slot_phase;
    reg [24:0] setup_map_row_byte_base;
    reg [10:0] setup_map_width;
    reg [9:0]  setup_map_x_mask;
    reg        setup_config_shape_valid;
    reg [32:0] setup_map_end;
    reg [32:0] setup_set_end;

    wire work_tile16 = work_ctrl[2];
    wire [3:0] work_tile_shift = work_tile16 ? 4'd4 : 4'd3;
    wire [3:0] work_log_w = work_size[3:0];
    wire [3:0] work_log_h = work_size[7:4];
    wire [10:0] work_map_width = 11'd1 << work_log_w;
    wire [10:0] work_map_height = 11'd1 << work_log_h;
    wire [9:0] work_map_x_mask = work_map_width[9:0] - 10'd1;
    wire [9:0] work_map_y_mask = work_map_height[9:0] - 10'd1;
    wire [4:0] work_map_span_shift = {1'b0, work_log_w} +
                                     {1'b0, work_log_h} + 5'd1;
    wire [32:0] work_map_end = {8'd0, work_map} +
                               (33'd1 << work_map_span_shift);
    wire [32:0] work_set_end = {8'd0, work_set} +
                               (work_tile16 ? 33'd131072 : 33'd32768);
    wire work_config_shape_valid =
        work_log_w <= 4'd10 && work_log_h <= 4'd10 &&
        work_ctrl[31:20] == 12'd0 && work_ctrl[15:6] == 10'd0 &&
        work_map[1:0] == 2'b00 && work_set[1:0] == 2'b00;

    wire signed [17:0] scroll_x_ext = {{2{work_scroll_x[15]}},
                                       work_scroll_x};
    wire signed [17:0] scroll_y_ext = {{2{work_scroll_y[15]}},
                                       work_scroll_y};
    wire signed [17:0] first_raw_tx_calc = scroll_x_ext >>> work_tile_shift;
    wire signed [17:0] last_world_x_calc = scroll_x_ext +
        $signed({8'd0, job_width}) - 18'sd1;
    wire signed [17:0] last_raw_tx_calc =
        last_world_x_calc >>> work_tile_shift;
    wire [17:0] source_tile_count_calc =
        last_raw_tx_calc - first_raw_tx_calc + 18'd1;
    wire signed [17:0] world_y_calc = scroll_y_ext +
                                      $signed({8'd0, job_y});
    wire signed [17:0] raw_ty_calc = world_y_calc >>> work_tile_shift;
    wire y_valid_calc = work_ctrl[5] ||
        (raw_ty_calc >= 0 &&
         raw_ty_calc < $signed({7'd0, work_map_height}));
    wire [9:0] map_ty_calc = raw_ty_calc[9:0] & work_map_y_mask;
    wire [9:0] first_map_tx_calc = first_raw_tx_calc[9:0] &
                                    work_map_x_mask;
    wire [20:0] map_row_index_calc =
        {11'd0, map_ty_calc} << work_log_w;
    wire [20:0] first_map_index_calc = map_row_index_calc +
        {11'd0, first_map_tx_calc};
    wire [24:0] map_row_byte_base_calc = work_map +
        {3'd0, map_row_index_calc, 1'b0};
    wire [24:0] first_map_byte_addr_calc = work_map +
        {3'd0, first_map_index_calc, 1'b0};

    reg signed [17:0] first_raw_tx;
    reg signed [17:0] line_world_y;
    reg [9:0] map_ty;
    reg       line_y_valid;
    reg [6:0] source_tile_count;
    reg       slot_phase;
    reg signed [17:0] map_raw_tx;
    reg [24:0] map_row_byte_base;
    reg [10:0] map_width;
    reg [9:0]  map_x_mask;
    reg [6:0] map_issue_slot;
    reg [6:0] map_slots_remaining;
    reg [6:0] pattern_issue_slot;
    reg       stream_issue_part;
    reg       stream_issue_done;
    reg [4:0] stream_burst_count;
    reg       stream_pause;
    reg [1:0] pattern_pair_wait;
    reg       pattern_pair_loaded;
    reg [33:0] pattern_pair_hold;

    wire map_x_valid = work_ctrl[3] ||
        (map_raw_tx >= 0 &&
         map_raw_tx < $signed({7'd0, map_width}));
    wire map_slot_valid = line_y_valid && map_x_valid;
    wire [9:0] map_tx = map_raw_tx[9:0] & map_x_mask;
    wire [24:0] map_byte_addr = map_row_byte_base +
                                {14'd0, map_tx, 1'b0};
    wire [24:0] map_request_addr = {map_byte_addr[24:2], 2'b00};
    wire map_byte_half = map_row_byte_base[1] ^ map_tx[0];
    wire [7:0] map_storage_slot = {1'b0, map_issue_slot} +
                                  {7'd0, slot_phase};
    wire map_pair_request = map_slots_remaining > 7'd1 &&
        map_slot_valid &&
        !map_byte_half &&
        map_tx != map_x_mask &&
        map_issue_slot[0] == slot_phase;
    wire [7:0] map_issue_advance = map_pair_request ? 8'd2 : 8'd1;

    // One scanline can touch at most 91 source tiles (720 pixels with an
    // unaligned 8-pixel tile). Entries and fetched pattern rows are retained
    // until the layer is composed.
    // Validity and the 16-bit map descriptor are packed in 17-bit halves. The
    // phase bit aligns cache pairs with 32-bit map fetches, so one response can
    // write both descriptors. Mirroring supplies the compositor's two reads
    // while keeping both tables in ECP5 block RAM.
    (* ram_style = "block" *) reg [33:0] slot_pair_a [0:63];
    (* ram_style = "block" *) reg [33:0] slot_pair_b [0:63];

    // Each pattern copy has one streamed write port and one compositor read
    // port. Mirroring supplies two simultaneous compositor reads.
    (* ram_style = "block" *) reg [31:0] slot_pattern0_a [0:127];
    (* ram_style = "block" *) reg [31:0] slot_pattern0_b [0:127];
    (* ram_style = "block" *) reg [31:0] slot_pattern1_a [0:127];
    (* ram_style = "block" *) reg [31:0] slot_pattern1_b [0:127];

    wire [7:0] pattern_storage_slot = {1'b0, pattern_issue_slot} +
                                      {7'd0, slot_phase};
    wire [16:0] pattern_issue_data = pattern_storage_slot[0] ?
                                     pattern_pair_hold[33:17] :
                                     pattern_pair_hold[16:0];
    wire        pattern_issue_valid = pattern_issue_data[16];
    wire [15:0] pattern_issue_entry = pattern_issue_data[15:0];
    wire [3:0] natural_tile_row = work_tile16 ? line_world_y[3:0] :
                                                    {1'b0, line_world_y[2:0]};
    wire [3:0] pattern_tile_row = pattern_issue_entry[15] ?
        (work_tile16 ? 4'd15 - natural_tile_row :
                       {1'b0, 3'd7 - natural_tile_row[2:0]}) :
        natural_tile_row;
    wire [24:0] pattern_request_base = work_set +
        ({15'd0, pattern_issue_entry[9:0]} << (work_tile16 ? 7 : 5)) +
        ({21'd0, pattern_tile_row} << (work_tile16 ? 3 : 2));
    wire [24:0] pattern_request_addr = pattern_request_base +
                                       (stream_issue_part ? 25'd4 : 25'd0);

    (* ram_style = "distributed" *) reg [8:0] tag_fifo [0:15];
    reg [3:0] tag_write_ptr;
    reg [3:0] tag_read_ptr;
    reg [4:0] tag_count;
    reg       tag_outstanding;
    wire [8:0] tag_read_data = tag_fifo[tag_read_ptr];
    wire [6:0] tag_read_slot = tag_read_data[6:0];
    wire tag_read_aux = tag_read_data[7];
    wire tag_read_pair = tag_read_data[8];
    // Requests stop at 16 entries and cannot increment again until a response
    // retires one, so bit 4 is the exact full flag for this 16-entry FIFO.
    wire tag_full = tag_count[4];

    wire map_request_valid = state == ST_MAP_STREAM &&
        !stream_issue_done && !stream_pause && map_slot_valid && !tag_full;
    wire pattern_request_valid = state == ST_PATTERN_STREAM &&
        pattern_pair_loaded && !stream_issue_done && !stream_pause &&
        pattern_issue_valid && !tag_full;
    assign mem_valid = map_request_valid || pattern_request_valid;
    // Keep arbitration ownership independent of the current map comparator.
    // Feeding map_request_valid into mem_lock creates a request/grant/ready
    // combinational path through the shared Vega arbiter. Invalid clipped map
    // slots are consumed locally in one cycle, so retaining ownership while
    // the map stream is issuing does not add memory traffic. Pattern streams
    // still yield during their BRAM pair-load gaps.
    assign mem_lock =
        (state == ST_MAP_STREAM &&
         (tag_outstanding ||
          (!stream_issue_done && !stream_pause && !tag_full))) ||
        (state == ST_PATTERN_STREAM &&
         (tag_outstanding ||
          (!stream_issue_done && !stream_pause &&
           pattern_pair_loaded && !tag_full)));
    assign mem_write = 1'b0;
    assign mem_addr = state == ST_MAP_STREAM ? map_request_addr :
                                               pattern_request_addr;
    assign mem_be = 4'b1111;
    assign mem_wdata = 32'd0;
    wire mem_accept = mem_valid && mem_ready;

    (* ram_style = "block" *) reg [35:0] tile0_line [0:511];
    (* ram_style = "block" *) reg [35:0] tile1_line [0:511];
    reg [1:0] line_valid [0:1];
    wire [8:0] pixel_line_addr = {display_bank, pixel_x[9:2]};
    reg [35:0] tile0_quad_pixel;
    reg [35:0] tile1_quad_pixel;
    reg        tile_pair_select_pixel;
    always @(posedge pixel_clk) begin
        tile0_quad_pixel <= tile0_line[pixel_line_addr];
        tile1_quad_pixel <= tile1_line[pixel_line_addr];
        tile_pair_select_pixel <= pixel_x[1];
    end
    always @* begin
        tile0_pair = line_valid[display_bank][0] ?
            (tile_pair_select_pixel ? tile0_quad_pixel[17:0] :
                                      tile0_quad_pixel[35:18]) : 18'd0;
        tile1_pair = line_valid[display_bank][1] ?
            (tile_pair_select_pixel ? tile1_quad_pixel[17:0] :
                                      tile1_quad_pixel[35:18]) : 18'd0;
    end

    reg [6:0] pattern_read_a;
    reg [6:0] pattern_read_b;
    reg [31:0] pattern0_a_q;
    reg [31:0] pattern0_b_q;
    reg [31:0] pattern1_a_q;
    reg [31:0] pattern1_b_q;
    reg [5:0] entry_read_a;
    reg [5:0] entry_read_b;
    reg [33:0] entry_pair_a_q;
    reg [33:0] entry_pair_b_q;
    always @(posedge mem_clk) begin
        pattern0_a_q <= slot_pattern0_a[pattern_read_a];
        pattern0_b_q <= slot_pattern0_b[pattern_read_b];
        pattern1_a_q <= slot_pattern1_a[pattern_read_a];
        pattern1_b_q <= slot_pattern1_b[pattern_read_b];
        entry_pair_a_q <= slot_pair_a[entry_read_a];
        entry_pair_b_q <= slot_pair_b[entry_read_b];
    end

    reg [6:0] compose_issue_chunk;
    reg [6:0] compose_chunk_count;
    reg       compose_read_pending;
    reg       compose_active;
    reg       compose_finish_pending;
    reg       compose_half;
    reg [6:0] compose_read_chunk;
    reg [6:0] compose_write_chunk;
    reg signed [17:0] compose_world_x_read;
    reg signed [17:0] compose_raw_tx_a_read;
    reg        compose_entry_a_odd_read;
    reg        compose_entry_b_odd_read;
    reg signed [17:0] compose_world_x_q;
    reg signed [17:0] compose_raw_tx_a_q;
    reg        compose_entry_a_odd_q;
    reg        compose_entry_b_odd_q;

    wire [9:0] compose_screen_x = {compose_issue_chunk, 3'b000};
    wire signed [17:0] compose_world_x = scroll_x_ext +
        $signed({8'd0, compose_screen_x});
    wire signed [17:0] compose_raw_tx_a =
        compose_world_x >>> work_tile_shift;
    wire signed [17:0] compose_raw_tx_b =
        (compose_world_x + 18'sd7) >>> work_tile_shift;
    wire [6:0] compose_slot_a = compose_raw_tx_a - first_raw_tx;
    wire [6:0] compose_slot_b = compose_raw_tx_b - first_raw_tx;
    wire [7:0] compose_storage_slot_a = {1'b0, compose_slot_a} +
                                        {7'd0, slot_phase};
    wire [7:0] compose_storage_slot_b = {1'b0, compose_slot_b} +
                                        {7'd0, slot_phase};
    wire [16:0] compose_entry_a_data = compose_entry_a_odd_q ?
                                      entry_pair_a_q[33:17] :
                                      entry_pair_a_q[16:0];
    wire [16:0] compose_entry_b_data = compose_entry_b_odd_q ?
                                      entry_pair_b_q[33:17] :
                                      entry_pair_b_q[16:0];

    function automatic [8:0] compose_staged_pixel(
        input [15:0] entry,
        input        entry_valid,
        input [3:0]  source_x,
        input [31:0] pattern0,
        input [31:0] pattern1,
        input tile16,
        input transparency_enable,
        input [3:0] transparent_index,
        input screen_valid
    );
        reg [3:0] lane_nibble;
        reg lane_opaque;
        begin
            lane_nibble = pattern_nibble(tile16, source_x,
                                          pattern0, pattern1);
            lane_opaque = entry_valid &&
                (!transparency_enable || lane_nibble != transparent_index);
            if (screen_valid && entry_valid)
                compose_staged_pixel = {lane_opaque, 1'b0,
                                        entry[12:10], lane_nibble};
            else
                compose_staged_pixel = 9'd0;
        end
    endfunction

    wire [2:0] compose_lane_a = {compose_half, 2'b00};
    wire [2:0] compose_lane_b = {compose_half, 2'b00} + 3'd1;
    wire [2:0] compose_lane_c = {compose_half, 2'b00} + 3'd2;
    wire [2:0] compose_lane_d = {compose_half, 2'b00} + 3'd3;
    wire [9:0] compose_screen_base = {compose_write_chunk, 3'b000};
    wire [8:0] compose_line_addr = {
        job_bank, compose_write_chunk, compose_half};
    wire [3:0] compose_base_source_x = work_tile16 ?
        compose_world_x_q[3:0] : {1'b0, compose_world_x_q[2:0]};
    wire [4:0] compose_source_sum_a =
        {1'b0, compose_base_source_x} + {2'b00, compose_lane_a};
    wire [4:0] compose_source_sum_b =
        {1'b0, compose_base_source_x} + {2'b00, compose_lane_b};
    wire [4:0] compose_source_sum_c =
        {1'b0, compose_base_source_x} + {2'b00, compose_lane_c};
    wire [4:0] compose_source_sum_d =
        {1'b0, compose_base_source_x} + {2'b00, compose_lane_d};
    wire compose_use_entry_b_a = work_tile16 ?
        compose_source_sum_a[4] : compose_source_sum_a[3];
    wire compose_use_entry_b_b = work_tile16 ?
        compose_source_sum_b[4] : compose_source_sum_b[3];
    wire compose_use_entry_b_c = work_tile16 ?
        compose_source_sum_c[4] : compose_source_sum_c[3];
    wire compose_use_entry_b_d = work_tile16 ?
        compose_source_sum_d[4] : compose_source_sum_d[3];
    wire [16:0] compose_stage_entry_a = compose_use_entry_b_a ?
        compose_entry_b_data : compose_entry_a_data;
    wire [16:0] compose_stage_entry_b = compose_use_entry_b_b ?
        compose_entry_b_data : compose_entry_a_data;
    wire [16:0] compose_stage_entry_c = compose_use_entry_b_c ?
        compose_entry_b_data : compose_entry_a_data;
    wire [16:0] compose_stage_entry_d = compose_use_entry_b_d ?
        compose_entry_b_data : compose_entry_a_data;
    wire [31:0] compose_stage_pattern0_a = compose_use_entry_b_a ?
        pattern0_b_q : pattern0_a_q;
    wire [31:0] compose_stage_pattern1_a = compose_use_entry_b_a ?
        pattern1_b_q : pattern1_a_q;
    wire [31:0] compose_stage_pattern0_b = compose_use_entry_b_b ?
        pattern0_b_q : pattern0_a_q;
    wire [31:0] compose_stage_pattern1_b = compose_use_entry_b_b ?
        pattern1_b_q : pattern1_a_q;
    wire [31:0] compose_stage_pattern0_c = compose_use_entry_b_c ?
        pattern0_b_q : pattern0_a_q;
    wire [31:0] compose_stage_pattern1_c = compose_use_entry_b_c ?
        pattern1_b_q : pattern1_a_q;
    wire [31:0] compose_stage_pattern0_d = compose_use_entry_b_d ?
        pattern0_b_q : pattern0_a_q;
    wire [31:0] compose_stage_pattern1_d = compose_use_entry_b_d ?
        pattern1_b_q : pattern1_a_q;
    wire [3:0] compose_source_x_unflipped_a = work_tile16 ?
        compose_source_sum_a[3:0] : {1'b0, compose_source_sum_a[2:0]};
    wire [3:0] compose_source_x_unflipped_b = work_tile16 ?
        compose_source_sum_b[3:0] : {1'b0, compose_source_sum_b[2:0]};
    wire [3:0] compose_source_x_unflipped_c = work_tile16 ?
        compose_source_sum_c[3:0] : {1'b0, compose_source_sum_c[2:0]};
    wire [3:0] compose_source_x_unflipped_d = work_tile16 ?
        compose_source_sum_d[3:0] : {1'b0, compose_source_sum_d[2:0]};
    wire [3:0] compose_stage_source_x_a = compose_stage_entry_a[14] ?
        (work_tile16 ? 4'd15 - compose_source_x_unflipped_a :
                       {1'b0, 3'd7 - compose_source_x_unflipped_a[2:0]}) :
        compose_source_x_unflipped_a;
    wire [3:0] compose_stage_source_x_b = compose_stage_entry_b[14] ?
        (work_tile16 ? 4'd15 - compose_source_x_unflipped_b :
                       {1'b0, 3'd7 - compose_source_x_unflipped_b[2:0]}) :
        compose_source_x_unflipped_b;
    wire [3:0] compose_stage_source_x_c = compose_stage_entry_c[14] ?
        (work_tile16 ? 4'd15 - compose_source_x_unflipped_c :
                       {1'b0, 3'd7 - compose_source_x_unflipped_c[2:0]}) :
        compose_source_x_unflipped_c;
    wire [3:0] compose_stage_source_x_d = compose_stage_entry_d[14] ?
        (work_tile16 ? 4'd15 - compose_source_x_unflipped_d :
                       {1'b0, 3'd7 - compose_source_x_unflipped_d[2:0]}) :
        compose_source_x_unflipped_d;
    wire compose_stage_screen_valid_a =
        compose_screen_base + {7'd0, compose_lane_a} < job_width;
    wire compose_stage_screen_valid_b =
        compose_screen_base + {7'd0, compose_lane_b} < job_width;
    wire compose_stage_screen_valid_c =
        compose_screen_base + {7'd0, compose_lane_c} < job_width;
    wire compose_stage_screen_valid_d =
        compose_screen_base + {7'd0, compose_lane_d} < job_width;

    reg        compose_pipe_valid;
    reg        compose_pipe_layer;
    reg [8:0]  compose_pipe_line_addr;
    reg        compose_pipe_tile16;
    reg        compose_pipe_transparency;
    reg [3:0]  compose_pipe_transparent_index;
    reg [15:0] compose_pipe_entry_a;
    reg [15:0] compose_pipe_entry_b;
    reg [15:0] compose_pipe_entry_c;
    reg [15:0] compose_pipe_entry_d;
    reg        compose_pipe_entry_valid_a;
    reg        compose_pipe_entry_valid_b;
    reg        compose_pipe_entry_valid_c;
    reg        compose_pipe_entry_valid_d;
    reg [3:0]  compose_pipe_source_x_a;
    reg [3:0]  compose_pipe_source_x_b;
    reg [3:0]  compose_pipe_source_x_c;
    reg [3:0]  compose_pipe_source_x_d;
    reg [31:0] compose_pipe_pattern0_a;
    reg [31:0] compose_pipe_pattern1_a;
    reg [31:0] compose_pipe_pattern0_b;
    reg [31:0] compose_pipe_pattern1_b;
    reg [31:0] compose_pipe_pattern0_c;
    reg [31:0] compose_pipe_pattern1_c;
    reg [31:0] compose_pipe_pattern0_d;
    reg [31:0] compose_pipe_pattern1_d;
    reg        compose_pipe_screen_valid_a;
    reg        compose_pipe_screen_valid_b;
    reg        compose_pipe_screen_valid_c;
    reg        compose_pipe_screen_valid_d;

    wire [8:0] compose_pixel_a = compose_staged_pixel(
        compose_pipe_entry_a, compose_pipe_entry_valid_a,
        compose_pipe_source_x_a, compose_pipe_pattern0_a,
        compose_pipe_pattern1_a, compose_pipe_tile16,
        compose_pipe_transparency, compose_pipe_transparent_index,
        compose_pipe_screen_valid_a);
    wire [8:0] compose_pixel_b = compose_staged_pixel(
        compose_pipe_entry_b, compose_pipe_entry_valid_b,
        compose_pipe_source_x_b, compose_pipe_pattern0_b,
        compose_pipe_pattern1_b, compose_pipe_tile16,
        compose_pipe_transparency, compose_pipe_transparent_index,
        compose_pipe_screen_valid_b);
    wire [8:0] compose_pixel_c = compose_staged_pixel(
        compose_pipe_entry_c, compose_pipe_entry_valid_c,
        compose_pipe_source_x_c, compose_pipe_pattern0_c,
        compose_pipe_pattern1_c, compose_pipe_tile16,
        compose_pipe_transparency, compose_pipe_transparent_index,
        compose_pipe_screen_valid_c);
    wire [8:0] compose_pixel_d = compose_staged_pixel(
        compose_pipe_entry_d, compose_pipe_entry_valid_d,
        compose_pipe_source_x_d, compose_pipe_pattern0_d,
        compose_pipe_pattern1_d, compose_pipe_tile16,
        compose_pipe_transparency, compose_pipe_transparent_index,
        compose_pipe_screen_valid_d);

    always @(posedge mem_clk) begin
        if (mem_rst) begin
            compose_pipe_valid <= 1'b0;
            compose_pipe_layer <= 1'b0;
            compose_pipe_line_addr <= 9'd0;
            compose_pipe_tile16 <= 1'b0;
            compose_pipe_transparency <= 1'b0;
            compose_pipe_transparent_index <= 4'd0;
            compose_pipe_entry_a <= 16'd0;
            compose_pipe_entry_b <= 16'd0;
            compose_pipe_entry_c <= 16'd0;
            compose_pipe_entry_d <= 16'd0;
            compose_pipe_entry_valid_a <= 1'b0;
            compose_pipe_entry_valid_b <= 1'b0;
            compose_pipe_entry_valid_c <= 1'b0;
            compose_pipe_entry_valid_d <= 1'b0;
            compose_pipe_source_x_a <= 4'd0;
            compose_pipe_source_x_b <= 4'd0;
            compose_pipe_source_x_c <= 4'd0;
            compose_pipe_source_x_d <= 4'd0;
            compose_pipe_pattern0_a <= 32'd0;
            compose_pipe_pattern1_a <= 32'd0;
            compose_pipe_pattern0_b <= 32'd0;
            compose_pipe_pattern1_b <= 32'd0;
            compose_pipe_pattern0_c <= 32'd0;
            compose_pipe_pattern1_c <= 32'd0;
            compose_pipe_pattern0_d <= 32'd0;
            compose_pipe_pattern1_d <= 32'd0;
            compose_pipe_screen_valid_a <= 1'b0;
            compose_pipe_screen_valid_b <= 1'b0;
            compose_pipe_screen_valid_c <= 1'b0;
            compose_pipe_screen_valid_d <= 1'b0;
        end else begin
            if (compose_pipe_valid) begin
                if (!compose_pipe_layer)
                    tile0_line[compose_pipe_line_addr] <= {
                        compose_pixel_a, compose_pixel_b,
                        compose_pixel_c, compose_pixel_d};
                else
                    tile1_line[compose_pipe_line_addr] <= {
                        compose_pixel_a, compose_pixel_b,
                        compose_pixel_c, compose_pixel_d};
            end
            compose_pipe_valid <= compose_active;
            if (compose_active) begin
                compose_pipe_layer <= work_layer;
                compose_pipe_line_addr <= compose_line_addr;
                compose_pipe_tile16 <= work_tile16;
                compose_pipe_transparency <= work_ctrl[1];
                compose_pipe_transparent_index <= work_ctrl[19:16];
                compose_pipe_entry_a <= compose_stage_entry_a[15:0];
                compose_pipe_entry_b <= compose_stage_entry_b[15:0];
                compose_pipe_entry_c <= compose_stage_entry_c[15:0];
                compose_pipe_entry_d <= compose_stage_entry_d[15:0];
                compose_pipe_entry_valid_a <= compose_stage_entry_a[16];
                compose_pipe_entry_valid_b <= compose_stage_entry_b[16];
                compose_pipe_entry_valid_c <= compose_stage_entry_c[16];
                compose_pipe_entry_valid_d <= compose_stage_entry_d[16];
                compose_pipe_source_x_a <= compose_stage_source_x_a;
                compose_pipe_source_x_b <= compose_stage_source_x_b;
                compose_pipe_source_x_c <= compose_stage_source_x_c;
                compose_pipe_source_x_d <= compose_stage_source_x_d;
                compose_pipe_pattern0_a <= compose_stage_pattern0_a;
                compose_pipe_pattern1_a <= compose_stage_pattern1_a;
                compose_pipe_pattern0_b <= compose_stage_pattern0_b;
                compose_pipe_pattern1_b <= compose_stage_pattern1_b;
                compose_pipe_pattern0_c <= compose_stage_pattern0_c;
                compose_pipe_pattern1_c <= compose_stage_pattern1_c;
                compose_pipe_pattern0_d <= compose_stage_pattern0_d;
                compose_pipe_pattern1_d <= compose_stage_pattern1_d;
                compose_pipe_screen_valid_a <=
                    compose_stage_screen_valid_a;
                compose_pipe_screen_valid_b <=
                    compose_stage_screen_valid_b;
                compose_pipe_screen_valid_c <=
                    compose_stage_screen_valid_c;
                compose_pipe_screen_valid_d <=
                    compose_stage_screen_valid_d;
            end
        end
    end

    always @(posedge mem_clk) begin
        done <= 1'b0;
        if (mem_rst) begin
            state <= ST_IDLE;
            busy <= 1'b0;
            done <= 1'b0;
            config_error <= 1'b0;
            work_ctrl <= 32'd0;
            work_map <= 25'd0;
            work_set <= 25'd0;
            work_size <= 8'd0;
            work_scroll_x <= 16'd0;
            work_scroll_y <= 16'd0;
            work_layer <= 1'b0;
            job_bank <= 1'b0;
            job_y <= 10'd0;
            job_width <= 10'd0;
            config_wait <= 2'd0;
            setup_config_valid <= 1'b0;
            setup_first_raw_tx <= 18'sd0;
            setup_world_y <= 18'sd0;
            setup_map_ty <= 10'd0;
            setup_y_valid <= 1'b0;
            setup_source_tile_count <= 18'd0;
            setup_slot_phase <= 1'b0;
            setup_map_row_byte_base <= 25'd0;
            setup_map_width <= 11'd0;
            setup_map_x_mask <= 10'd0;
            setup_config_shape_valid <= 1'b0;
            setup_map_end <= 33'd0;
            setup_set_end <= 33'd0;
            line_valid[0] <= 2'b00;
            line_valid[1] <= 2'b00;
            first_raw_tx <= 18'sd0;
            line_world_y <= 18'sd0;
            map_ty <= 10'd0;
            line_y_valid <= 1'b0;
            source_tile_count <= 7'd0;
            slot_phase <= 1'b0;
            map_raw_tx <= 18'sd0;
            map_row_byte_base <= 25'd0;
            map_width <= 11'd0;
            map_x_mask <= 10'd0;
            map_issue_slot <= 7'd0;
            map_slots_remaining <= 7'd0;
            pattern_issue_slot <= 7'd0;
            stream_issue_part <= 1'b0;
            stream_issue_done <= 1'b0;
            stream_burst_count <= 5'd0;
            stream_pause <= 1'b0;
            pattern_pair_wait <= 2'd0;
            pattern_pair_loaded <= 1'b0;
            pattern_pair_hold <= 34'd0;
            tag_write_ptr <= 4'd0;
            tag_read_ptr <= 4'd0;
            tag_count <= 5'd0;
            tag_outstanding <= 1'b0;
            pattern_read_a <= 7'd0;
            pattern_read_b <= 7'd0;
            entry_read_a <= 6'd0;
            entry_read_b <= 6'd0;
            compose_issue_chunk <= 7'd0;
            compose_chunk_count <= 7'd0;
            compose_read_pending <= 1'b0;
            compose_active <= 1'b0;
            compose_finish_pending <= 1'b0;
            compose_half <= 1'b0;
            compose_read_chunk <= 7'd0;
            compose_write_chunk <= 7'd0;
            compose_world_x_read <= 18'sd0;
            compose_raw_tx_a_read <= 18'sd0;
            compose_entry_a_odd_read <= 1'b0;
            compose_entry_b_odd_read <= 1'b0;
            compose_world_x_q <= 18'sd0;
            compose_raw_tx_a_q <= 18'sd0;
            compose_entry_a_odd_q <= 1'b0;
            compose_entry_b_odd_q <= 1'b0;
        end else begin
            case (state)
                ST_IDLE: begin
                    busy <= 1'b0;
                    if (start) begin
                        busy <= 1'b1;
                        config_error <= 1'b0;
                        job_bank <= build_bank;
                        job_y <= line_y;
                        job_width <= line_width;
                        line_valid[build_bank] <= 2'b00;
                        work_layer <= 1'b0;
                        config_wait <= 2'd0;
                        state <= ST_CLEAR;
                    end
                end

                ST_CLEAR: begin
                    if (config_wait == 2'd2) begin
                        work_layer <= 1'b0;
                        state <= ST_LAYER;
                    end else begin
                        config_wait <= config_wait + 2'd1;
                    end
                end

                ST_LAYER: begin
                    if (!work_layer) begin
                        work_ctrl <= t0_ctrl_m2;
                        work_map <= t0_map_m2;
                        work_set <= t0_set_m2;
                        work_size <= t0_size_m2;
                        work_scroll_x <= t0_scroll_m2[15:0];
                        work_scroll_y <= t0_scroll_m2[31:16];
                    end else begin
                        work_ctrl <= t1_ctrl_m2;
                        work_map <= t1_map_m2;
                        work_set <= t1_set_m2;
                        work_size <= t1_size_m2;
                        work_scroll_x <= t1_scroll_m2[15:0];
                        work_scroll_y <= t1_scroll_m2[31:16];
                    end
                    state <= ST_SETUP;
                end

                ST_SETUP: begin
                    if (!work_ctrl[0] || job_width == 10'd0) begin
                        if (!work_layer) begin
                            work_layer <= 1'b1;
                            state <= ST_LAYER;
                        end else begin
                            state <= ST_FINISH;
                        end
                    end else begin
                        setup_config_shape_valid <=
                            work_config_shape_valid;
                        setup_map_end <= work_map_end;
                        setup_set_end <= work_set_end;
                        setup_first_raw_tx <= first_raw_tx_calc;
                        setup_world_y <= world_y_calc;
                        setup_map_ty <= map_ty_calc;
                        setup_y_valid <= y_valid_calc;
                        setup_source_tile_count <= source_tile_count_calc;
                        setup_slot_phase <= first_map_byte_addr_calc[1];
                        setup_map_row_byte_base <= map_row_byte_base_calc;
                        setup_map_width <= work_map_width;
                        setup_map_x_mask <= work_map_x_mask;
                        state <= ST_SETUP_VALIDATE;
                    end
                end

                ST_SETUP_VALIDATE: begin
                    setup_config_valid <= setup_config_shape_valid &&
                        setup_map_end <= 33'h02000000 &&
                        setup_set_end <= 33'h02000000;
                    state <= ST_SETUP_COMMIT;
                end

                ST_SETUP_COMMIT: begin
                    if (!setup_config_valid ||
                        setup_source_tile_count > 18'd91) begin
                        config_error <= 1'b1;
                        if (!work_layer) begin
                            work_layer <= 1'b1;
                            state <= ST_LAYER;
                        end else begin
                            state <= ST_FINISH;
                        end
                    end else begin
                        first_raw_tx <= setup_first_raw_tx;
                        line_world_y <= setup_world_y;
                        map_ty <= setup_map_ty;
                        line_y_valid <= setup_y_valid;
                        source_tile_count <= setup_source_tile_count[6:0];
                        slot_phase <= setup_slot_phase;
                        map_raw_tx <= setup_first_raw_tx;
                        map_row_byte_base <= setup_map_row_byte_base;
                        map_width <= setup_map_width;
                        map_x_mask <= setup_map_x_mask;
                        map_issue_slot <= 7'd0;
                        map_slots_remaining <=
                            setup_source_tile_count[6:0];
                        stream_issue_part <= 1'b0;
                        stream_issue_done <= 1'b0;
                        stream_burst_count <= 5'd0;
                        stream_pause <= 1'b0;
                        tag_write_ptr <= 4'd0;
                        tag_read_ptr <= 4'd0;
                        tag_count <= 5'd0;
                        tag_outstanding <= 1'b0;
                        state <= ST_MAP_STREAM;
                    end
                end

                ST_MAP_STREAM: begin
                    if (stream_pause && !tag_outstanding) begin
                        stream_pause <= 1'b0;
                        stream_burst_count <= 5'd0;
                    end
                    case ({mem_accept, mem_rsp_valid})
                        2'b10: begin
                            tag_count <= tag_count + 5'd1;
                            tag_outstanding <= 1'b1;
                        end
                        2'b01: begin
                            tag_count <= tag_count - 5'd1;
                            tag_outstanding <= tag_count != 5'd1;
                        end
                        default: tag_count <= tag_count;
                    endcase
                    if (mem_accept) begin
                        if (stream_burst_count == 5'd31)
                            stream_pause <= 1'b1;
                        else
                            stream_burst_count <= stream_burst_count + 5'd1;
                        tag_fifo[tag_write_ptr] <= {
                            map_pair_request, map_byte_half,
                            map_issue_slot};
                        tag_write_ptr <= tag_write_ptr + 4'd1;
                    end
                    if (mem_rsp_valid) begin
                        if (tag_read_pair) begin
                            slot_pair_a[(tag_read_slot +
                                         slot_phase) >> 1] <= {
                                1'b1,
                                tag_read_aux ? mem_rdata[31:16] :
                                                       mem_rdata[15:0],
                                1'b1,
                                tag_read_aux ? mem_rdata[15:0] :
                                                       mem_rdata[31:16]
                            };
                            slot_pair_b[(tag_read_slot +
                                         slot_phase) >> 1] <= {
                                1'b1,
                                tag_read_aux ? mem_rdata[31:16] :
                                                       mem_rdata[15:0],
                                1'b1,
                                tag_read_aux ? mem_rdata[15:0] :
                                                       mem_rdata[31:16]
                            };
                        end else if ((tag_read_slot +
                                      slot_phase) & 7'd1) begin
                            slot_pair_a[(tag_read_slot +
                                         slot_phase) >> 1][33:17] <= {
                                1'b1,
                                tag_read_aux ? mem_rdata[15:0] :
                                                       mem_rdata[31:16]
                            };
                            slot_pair_b[(tag_read_slot +
                                         slot_phase) >> 1][33:17] <= {
                                1'b1,
                                tag_read_aux ? mem_rdata[15:0] :
                                                       mem_rdata[31:16]
                            };
                        end else begin
                            slot_pair_a[(tag_read_slot +
                                         slot_phase) >> 1][16:0] <= {
                                1'b1,
                                tag_read_aux ? mem_rdata[15:0] :
                                                       mem_rdata[31:16]
                            };
                            slot_pair_b[(tag_read_slot +
                                         slot_phase) >> 1][16:0] <= {
                                1'b1,
                                tag_read_aux ? mem_rdata[15:0] :
                                                       mem_rdata[31:16]
                            };
                        end
                        tag_read_ptr <= tag_read_ptr + 4'd1;
                    end

                    if (!stream_issue_done) begin
                        if (!map_slot_valid) begin
                            if (!mem_rsp_valid) begin
                                if (map_storage_slot[0]) begin
                                    slot_pair_a[map_storage_slot[6:1]][33:17]
                                        <= 17'd0;
                                    slot_pair_b[map_storage_slot[6:1]][33:17]
                                        <= 17'd0;
                                end else begin
                                    slot_pair_a[map_storage_slot[6:1]][16:0]
                                        <= 17'd0;
                                    slot_pair_b[map_storage_slot[6:1]][16:0]
                                        <= 17'd0;
                                end
                                if (map_slots_remaining == 7'd1) begin
                                    stream_issue_done <= 1'b1;
                                    map_slots_remaining <= 7'd0;
                                end else begin
                                    map_issue_slot <=
                                        map_issue_slot + 7'd1;
                                    map_slots_remaining <=
                                        map_slots_remaining - 7'd1;
                                end
                                map_raw_tx <= map_raw_tx + 18'sd1;
                            end
                        end else if (mem_accept) begin
                            if (map_slots_remaining <=
                                map_issue_advance[6:0]) begin
                                stream_issue_done <= 1'b1;
                                map_slots_remaining <= 7'd0;
                            end else begin
                                map_issue_slot <= map_issue_slot +
                                    map_issue_advance[6:0];
                                map_slots_remaining <=
                                    map_slots_remaining -
                                    map_issue_advance[6:0];
                            end
                            map_raw_tx <= map_raw_tx +
                                $signed({10'd0, map_issue_advance});
                        end
                    end

                    if (stream_issue_done &&
                        (tag_count == 5'd0 ||
                         (mem_rsp_valid && tag_count == 5'd1))) begin
                        pattern_issue_slot <= 7'd0;
                        stream_issue_part <= 1'b0;
                        stream_issue_done <= 1'b0;
                        tag_write_ptr <= 4'd0;
                        tag_read_ptr <= 4'd0;
                        tag_count <= 5'd0;
                        tag_outstanding <= 1'b0;
                        stream_burst_count <= 5'd0;
                        stream_pause <= 1'b0;
                        entry_read_a <= {1'b0, slot_phase} >> 1;
                        pattern_pair_wait <= 2'd1;
                        pattern_pair_loaded <= 1'b0;
                        state <= ST_PATTERN_STREAM;
                    end
                end

                ST_PATTERN_STREAM: begin
                    if (stream_pause && !tag_outstanding) begin
                        stream_pause <= 1'b0;
                        stream_burst_count <= 5'd0;
                    end
                    case ({mem_accept, mem_rsp_valid})
                        2'b10: begin
                            tag_count <= tag_count + 5'd1;
                            tag_outstanding <= 1'b1;
                        end
                        2'b01: begin
                            tag_count <= tag_count - 5'd1;
                            tag_outstanding <= tag_count != 5'd1;
                        end
                        default: tag_count <= tag_count;
                    endcase
                    if (mem_accept) begin
                        if (stream_burst_count == 5'd31)
                            stream_pause <= 1'b1;
                        else
                            stream_burst_count <= stream_burst_count + 5'd1;
                        tag_fifo[tag_write_ptr] <= {
                            1'b0, stream_issue_part, pattern_issue_slot};
                        tag_write_ptr <= tag_write_ptr + 4'd1;
                    end
                    if (mem_rsp_valid) begin
                        if (tag_read_aux)
                            begin
                                slot_pattern1_a[tag_read_slot] <=
                                    mem_rdata;
                                slot_pattern1_b[tag_read_slot] <=
                                    mem_rdata;
                            end
                        else begin
                            slot_pattern0_a[tag_read_slot] <=
                                mem_rdata;
                            slot_pattern0_b[tag_read_slot] <=
                                mem_rdata;
                        end
                        tag_read_ptr <= tag_read_ptr + 4'd1;
                    end

                    if (pattern_pair_wait != 2'd0) begin
                        pattern_pair_wait <= pattern_pair_wait - 2'd1;
                    end else if (!pattern_pair_loaded) begin
                        pattern_pair_hold <= entry_pair_a_q;
                        pattern_pair_loaded <= 1'b1;
                        pattern_pair_wait <= 2'd1;
                        entry_read_a <= entry_read_a + 6'd1;
                    end else if (!stream_issue_done) begin
                        if (!pattern_issue_valid) begin
                            stream_issue_part <= 1'b0;
                            if (pattern_issue_slot + 7'd1 >=
                                source_tile_count) begin
                                stream_issue_done <= 1'b1;
                            end else begin
                                if (pattern_storage_slot[0]) begin
                                    pattern_pair_hold <= entry_pair_a_q;
                                    entry_read_a <= entry_read_a + 6'd1;
                                end
                                pattern_issue_slot <=
                                    pattern_issue_slot + 7'd1;
                            end
                        end else if (mem_accept) begin
                            if (work_tile16 && !stream_issue_part) begin
                                stream_issue_part <= 1'b1;
                            end else begin
                                stream_issue_part <= 1'b0;
                                if (pattern_issue_slot + 7'd1 >=
                                    source_tile_count) begin
                                    stream_issue_done <= 1'b1;
                                end else begin
                                    if (pattern_storage_slot[0]) begin
                                        pattern_pair_hold <= entry_pair_a_q;
                                        entry_read_a <= entry_read_a + 6'd1;
                                    end
                                    pattern_issue_slot <=
                                        pattern_issue_slot + 7'd1;
                                end
                            end
                        end
                    end

                    if (stream_issue_done &&
                        (tag_count == 5'd0 ||
                         (mem_rsp_valid && tag_count == 5'd1))) begin
                        compose_issue_chunk <= 7'd0;
                        compose_chunk_count <= (job_width + 10'd7) >> 3;
                        compose_read_pending <= 1'b0;
                        compose_active <= 1'b0;
                        compose_finish_pending <= 1'b0;
                        compose_half <= 1'b0;
                        pattern_pair_loaded <= 1'b0;
                        pattern_pair_wait <= 2'd0;
                        tag_count <= 5'd0;
                        tag_outstanding <= 1'b0;
                        stream_burst_count <= 5'd0;
                        stream_pause <= 1'b0;
                        state <= ST_COMPOSE;
                    end
                end

                ST_COMPOSE: begin
                    if (compose_finish_pending) begin
                        compose_finish_pending <= 1'b0;
                        line_valid[job_bank][work_layer] <= 1'b1;
                        if (!work_layer) begin
                            work_layer <= 1'b1;
                            state <= ST_LAYER;
                        end else begin
                            state <= ST_FINISH;
                        end
                    end else if (compose_active) begin
                        if (!compose_half) begin
                            compose_half <= 1'b1;
                            if (compose_issue_chunk <
                                compose_chunk_count) begin
                                pattern_read_a <= compose_slot_a;
                                pattern_read_b <= compose_slot_b;
                                entry_read_a <=
                                    compose_storage_slot_a[6:1];
                                entry_read_b <=
                                    compose_storage_slot_b[6:1];
                                compose_read_chunk <=
                                    compose_issue_chunk;
                                compose_world_x_read <= compose_world_x;
                                compose_raw_tx_a_read <=
                                    compose_raw_tx_a;
                                compose_entry_a_odd_read <=
                                    compose_storage_slot_a[0];
                                compose_entry_b_odd_read <=
                                    compose_storage_slot_b[0];
                                compose_issue_chunk <=
                                    compose_issue_chunk + 7'd1;
                                compose_read_pending <= 1'b1;
                            end
                        end else begin
                            if (compose_read_pending) begin
                                compose_write_chunk <=
                                    compose_read_chunk;
                                compose_world_x_q <=
                                    compose_world_x_read;
                                compose_raw_tx_a_q <=
                                    compose_raw_tx_a_read;
                                compose_entry_a_odd_q <=
                                    compose_entry_a_odd_read;
                                compose_entry_b_odd_q <=
                                    compose_entry_b_odd_read;
                                compose_read_pending <= 1'b0;
                                compose_half <= 1'b0;
                            end else begin
                                compose_active <= 1'b0;
                                compose_finish_pending <= 1'b1;
                            end
                        end
                    end else if (compose_read_pending) begin
                        compose_write_chunk <= compose_read_chunk;
                        compose_world_x_q <= compose_world_x_read;
                        compose_raw_tx_a_q <= compose_raw_tx_a_read;
                        compose_entry_a_odd_q <=
                            compose_entry_a_odd_read;
                        compose_entry_b_odd_q <=
                            compose_entry_b_odd_read;
                        compose_read_pending <= 1'b0;
                        compose_active <= 1'b1;
                        compose_half <= 1'b0;
                    end else if (compose_issue_chunk <
                                 compose_chunk_count) begin
                            pattern_read_a <= compose_slot_a;
                            pattern_read_b <= compose_slot_b;
                            entry_read_a <= compose_storage_slot_a[6:1];
                            entry_read_b <= compose_storage_slot_b[6:1];
                            compose_read_chunk <= compose_issue_chunk;
                            compose_world_x_read <= compose_world_x;
                            compose_raw_tx_a_read <= compose_raw_tx_a;
                            compose_entry_a_odd_read <=
                                compose_storage_slot_a[0];
                            compose_entry_b_odd_read <=
                                compose_storage_slot_b[0];
                            compose_issue_chunk <=
                                compose_issue_chunk + 7'd1;
                            compose_read_pending <= 1'b1;
                    end else begin
                        if (!work_layer) begin
                            work_layer <= 1'b1;
                            state <= ST_LAYER;
                        end else begin
                            state <= ST_FINISH;
                        end
                    end
                end

                ST_FINISH: begin
                    busy <= 1'b0;
                    done <= 1'b1;
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
endmodule

`default_nettype wire
