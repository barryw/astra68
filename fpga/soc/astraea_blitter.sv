// Astraea COPY/FILL DMA engine. CPU-visible configuration is captured before
// crossing into the SDRAM domain; each chunk is fully retired before the next
// one so overlap direction and response ordering remain deterministic.
`default_nettype none

module astraea_blitter #(
    parameter integer CHUNK_UNITS = 16
) (
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
    localparam [2:0] MODE_COPY = 3'd0;
    localparam [2:0] MODE_FILL = 3'd1;
    localparam [2:0] MODE_COPY_KEY = 3'd2;
    localparam [2:0] MODE_COPY_MASK = 3'd3;
    localparam integer CHUNK_INDEX_BITS = CHUNK_UNITS <= 1 ? 1 : $clog2(CHUNK_UNITS);
    localparam [5:0] CHUNK_COUNT = CHUNK_UNITS;

    localparam [4:0] ST_IDLE          = 5'd0;
    localparam [4:0] ST_PREP          = 5'd1;
    localparam [4:0] ST_READ_ISSUE    = 5'd3;
    localparam [4:0] ST_READ_DRAIN    = 5'd2;
    localparam [4:0] ST_WRITE_ISSUE   = 5'd6;
    localparam [4:0] ST_WRITE_DRAIN   = 5'd7;
    localparam [4:0] ST_FILL_ISSUE    = 5'd5;
    localparam [4:0] ST_FILL_DRAIN    = 5'd4;
    localparam [4:0] ST_NEXT          = 5'd12;
    localparam [4:0] ST_ADDR          = 5'd13;
    localparam [4:0] ST_READ_YIELD    = 5'd15;
    localparam [4:0] ST_KM_PREP       = 5'd14;
    localparam [4:0] ST_KM_MASK_ISSUE = 5'd10;
    localparam [4:0] ST_KM_MASK_WAIT  = 5'd11;
    localparam [4:0] ST_KM_SRC_ISSUE  = 5'd9;
    localparam [4:0] ST_KM_SRC_WAIT   = 5'd8;
    localparam [4:0] ST_KM_DECIDE     = 5'd24;
    localparam [4:0] ST_KM_DST_ISSUE  = 5'd25;
    localparam [4:0] ST_KM_DST_WAIT   = 5'd27;
    localparam [4:0] ST_KM_NEXT       = 5'd26;
    localparam [4:0] ST_VAL_DST_MUL   = 5'd30;
    localparam [4:0] ST_VAL_DST_BASE  = 5'd31;
    localparam [4:0] ST_VAL_DST_END   = 5'd29;
    localparam [4:0] ST_VAL_SRC_MUL   = 5'd28;
    localparam [4:0] ST_VAL_SRC_BASE  = 5'd20;
    localparam [4:0] ST_VAL_SRC_END   = 5'd21;
    localparam [4:0] ST_VAL_MASK_MUL  = 5'd23;
    localparam [4:0] ST_VAL_MASK_BASE = 5'd22;
    localparam [4:0] ST_VAL_MASK_END  = 5'd18;
    localparam [4:0] ST_VAL_DONE      = 5'd19;

    localparam integer RF_ISSUING_READ       = 0;
    localparam integer RF_ISSUING_COPY_WRITE = 1;
    localparam integer RF_ISSUING_FILL       = 2;
    localparam integer RF_READ_RESPONSE      = 3;
    localparam integer RF_KM_ISSUING_READ    = 4;
    localparam integer RF_KM_ISSUING_WRITE   = 5;
    localparam integer RF_KM_BUS_ACTIVE      = 6;
    localparam integer RF_MEM_LOCK           = 7;
    localparam integer RF_ADDR               = 8;
    localparam integer RF_KM_PREP            = 9;
    localparam integer RF_KM_NEXT            = 10;

    localparam [4:0] REG_ID             = 5'h00;
    localparam [4:0] REG_VERSION        = 5'h01;
    localparam [4:0] REG_CTRL           = 5'h02;
    localparam [4:0] REG_STATUS         = 5'h03;
    localparam [4:0] REG_IRQ_EN         = 5'h04;
    localparam [4:0] REG_IRQ_STAT       = 5'h05;
    localparam [4:0] REG_BLIT_SRC       = 5'h10;
    localparam [4:0] REG_BLIT_DST       = 5'h11;
    localparam [4:0] REG_BLIT_MASK      = 5'h12;
    localparam [4:0] REG_BLIT_SRC_PITCH = 5'h13;
    localparam [4:0] REG_BLIT_DST_PITCH = 5'h14;
    localparam [4:0] REG_BLIT_MASK_PITCH= 5'h15;
    localparam [4:0] REG_BLIT_DIM       = 5'h16;
    localparam [4:0] REG_BLIT_OP        = 5'h17;
    localparam [4:0] REG_BLIT_COLOR     = 5'h18;
    localparam [4:0] REG_BLIT_KEY       = 5'h19;
    localparam [4:0] REG_BLIT_CTRL      = 5'h1a;
    localparam [4:0] REG_BLIT_STATUS    = 5'h1b;

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

    function automatic [3:0] byte_enable(input [1:0] address);
        byte_enable = 4'b1000 >> address;
    endfunction

    function automatic [31:0] place_byte(
        input [1:0] address,
        input [7:0] value
    );
        begin
            case (address)
                2'd0: place_byte = {value, 24'd0};
                2'd1: place_byte = {8'd0, value, 16'd0};
                2'd2: place_byte = {16'd0, value, 8'd0};
                default: place_byte = {24'd0, value};
            endcase
        end
    endfunction

    function automatic [7:0] select_byte(
        input [1:0] address,
        input [31:0] value
    );
        begin
            case (address)
                2'd0: select_byte = value[31:24];
                2'd1: select_byte = value[23:16];
                2'd2: select_byte = value[15:8];
                default: select_byte = value[7:0];
            endcase
        end
    endfunction

    function automatic [7:0] fill_byte(
        input [1:0] elem_size,
        input [1:0] byte_index,
        input [31:0] color
    );
        begin
            case (elem_size)
                2'd0: fill_byte = color[7:0];
                2'd1: fill_byte = byte_index[0] ? color[7:0] : color[15:8];
                default: begin
                    case (byte_index)
                        2'd0: fill_byte = color[31:24];
                        2'd1: fill_byte = color[23:16];
                        2'd2: fill_byte = color[15:8];
                        default: fill_byte = color[7:0];
                    endcase
                end
            endcase
        end
    endfunction

    function automatic [31:0] put_element_byte(
        input [31:0] old_value,
        input [1:0] elem_size,
        input [1:0] byte_index,
        input [7:0] byte_value
    );
        reg [31:0] value;
        begin
            value = old_value;
            case (elem_size)
                2'd0: value[7:0] = byte_value;
                2'd1: if (!byte_index[0]) value[15:8] = byte_value;
                      else value[7:0] = byte_value;
                default: begin
                    case (byte_index)
                        2'd0: value[31:24] = byte_value;
                        2'd1: value[23:16] = byte_value;
                        2'd2: value[15:8] = byte_value;
                        default: value[7:0] = byte_value;
                    endcase
                end
            endcase
            put_element_byte = value;
        end
    endfunction

    function automatic [7:0] get_element_byte(
        input [31:0] value,
        input [1:0] elem_size,
        input [1:0] byte_index
    );
        begin
            case (elem_size)
                2'd0: get_element_byte = value[7:0];
                2'd1: get_element_byte = byte_index[0] ?
                                                value[7:0] : value[15:8];
                default: begin
                    case (byte_index)
                        2'd0: get_element_byte = value[31:24];
                        2'd1: get_element_byte = value[23:16];
                        2'd2: get_element_byte = value[15:8];
                        default: get_element_byte = value[7:0];
                    endcase
                end
            endcase
        end
    endfunction

    reg [31:0] reg_ctrl;
    reg        reg_irq_en;
    reg        reg_irq_stat;
    reg [31:0] reg_src;
    reg [31:0] reg_dst;
    reg [31:0] reg_mask;
    reg [31:0] reg_src_pitch;
    reg [31:0] reg_dst_pitch;
    reg [31:0] reg_mask_pitch;
    reg [31:0] reg_dim;
    reg [31:0] reg_op;
    reg [31:0] reg_color;
    reg [31:0] reg_key;
    reg        reg_blit_irq_en;
    reg        done_sticky_cpu;
    reg [7:0]  error_cpu;

    reg [24:0] cfg_src_cpu;
    reg [24:0] cfg_dst_cpu;
    reg [24:0] cfg_mask_cpu;
    reg [15:0] cfg_src_pitch_cpu;
    reg [15:0] cfg_dst_pitch_cpu;
    reg [15:0] cfg_mask_pitch_cpu;
    reg [31:0] cfg_dim_cpu;
    reg [31:0] cfg_op_cpu;
    reg [3:0]  cfg_mode_cpu;
    reg [31:0] cfg_color_cpu;
    reg [31:0] cfg_key_cpu;
    reg        cfg_fields_valid_cpu;
    reg        cfg_op_valid_cpu;
    reg        cfg_noop_cpu;
    reg        start_toggle_cpu;
    reg        start_pending_cpu;

    reg [1:0] busy_sync_cpu;
    reg [1:0] done_sync_cpu;
    reg       done_seen_cpu;
    reg [7:0] error_meta_cpu;
    reg [7:0] error_sync_cpu;

    reg [1:0] start_sync_mem;
    reg       start_seen_mem;
    reg       busy_mem;
    reg       done_toggle_mem;
    reg [7:0] error_mem;
    reg [4:0] state_mem;
    reg [10:0] request_facts_mem;

    function automatic [10:0] request_facts_for_state(input [4:0] state);
        begin
            request_facts_for_state = 11'd0;
            case (state)
                ST_READ_ISSUE: begin
                    request_facts_for_state[RF_ISSUING_READ] = 1'b1;
                    request_facts_for_state[RF_READ_RESPONSE] = 1'b1;
                    request_facts_for_state[RF_MEM_LOCK] = 1'b1;
                end
                ST_READ_DRAIN: begin
                    request_facts_for_state[RF_READ_RESPONSE] = 1'b1;
                    request_facts_for_state[RF_MEM_LOCK] = 1'b1;
                end
                ST_WRITE_ISSUE: begin
                    request_facts_for_state[RF_ISSUING_COPY_WRITE] = 1'b1;
                    request_facts_for_state[RF_MEM_LOCK] = 1'b1;
                end
                ST_WRITE_DRAIN:
                    request_facts_for_state[RF_MEM_LOCK] = 1'b1;
                ST_FILL_ISSUE: begin
                    request_facts_for_state[RF_ISSUING_FILL] = 1'b1;
                    request_facts_for_state[RF_MEM_LOCK] = 1'b1;
                end
                ST_FILL_DRAIN:
                    request_facts_for_state[RF_MEM_LOCK] = 1'b1;
                ST_KM_MASK_ISSUE, ST_KM_SRC_ISSUE: begin
                    request_facts_for_state[RF_KM_ISSUING_READ] = 1'b1;
                    request_facts_for_state[RF_KM_BUS_ACTIVE] = 1'b1;
                    request_facts_for_state[RF_MEM_LOCK] = 1'b1;
                end
                ST_KM_DST_ISSUE: begin
                    request_facts_for_state[RF_KM_ISSUING_WRITE] = 1'b1;
                    request_facts_for_state[RF_KM_BUS_ACTIVE] = 1'b1;
                    request_facts_for_state[RF_MEM_LOCK] = 1'b1;
                end
                ST_KM_MASK_WAIT, ST_KM_SRC_WAIT, ST_KM_DST_WAIT: begin
                    request_facts_for_state[RF_KM_BUS_ACTIVE] = 1'b1;
                    request_facts_for_state[RF_MEM_LOCK] = 1'b1;
                end
                ST_ADDR:
                    request_facts_for_state[RF_ADDR] = 1'b1;
                ST_KM_PREP:
                    request_facts_for_state[RF_KM_PREP] = 1'b1;
                ST_KM_NEXT:
                    request_facts_for_state[RF_KM_NEXT] = 1'b1;
                default: begin end
            endcase
        end
    endfunction

    task automatic set_state_mem(input [4:0] next_state);
        begin
            state_mem <= next_state;
            request_facts_mem <= request_facts_for_state(next_state);
        end
    endtask

    wire busy_visible = start_pending_cpu | busy_sync_cpu[1];
    assign cpu_busy = busy_visible;
    assign cache_flush = busy_visible;
    assign cpu_irq = (reg_irq_en | reg_blit_irq_en) & reg_irq_stat;

    always @* begin
        cpu_rdata = 32'd0;
        case (cpu_reg)
            REG_ID:              cpu_rdata = 32'h41535452; // "ASTR"
            REG_VERSION:         cpu_rdata = 32'h00010000;
            REG_CTRL:            cpu_rdata = reg_ctrl;
            REG_STATUS:          cpu_rdata = {30'd0, done_sticky_cpu, busy_visible};
            REG_IRQ_EN:          cpu_rdata = {31'd0, reg_irq_en};
            REG_IRQ_STAT:        cpu_rdata = {31'd0, reg_irq_stat};
            REG_BLIT_SRC:        cpu_rdata = reg_src;
            REG_BLIT_DST:        cpu_rdata = reg_dst;
            REG_BLIT_MASK:       cpu_rdata = reg_mask;
            REG_BLIT_SRC_PITCH:  cpu_rdata = reg_src_pitch;
            REG_BLIT_DST_PITCH:  cpu_rdata = reg_dst_pitch;
            REG_BLIT_MASK_PITCH: cpu_rdata = reg_mask_pitch;
            REG_BLIT_DIM:        cpu_rdata = reg_dim;
            REG_BLIT_OP:         cpu_rdata = reg_op;
            REG_BLIT_COLOR:      cpu_rdata = reg_color;
            REG_BLIT_KEY:        cpu_rdata = reg_key;
            REG_BLIT_CTRL:       cpu_rdata = {30'd0, reg_blit_irq_en, 1'b0};
            REG_BLIT_STATUS:     cpu_rdata = {16'd0, error_cpu, 6'd0,
                                              done_sticky_cpu, busy_visible};
            default:             cpu_rdata = 32'd0;
        endcase
    end

    always @(posedge cpu_clk) begin
        cpu_done <= 1'b0;
        if (cpu_rst) begin
            reg_ctrl <= 32'd0;
            reg_irq_en <= 1'b0;
            reg_irq_stat <= 1'b0;
            reg_src <= 32'd0;
            reg_dst <= 32'd0;
            reg_mask <= 32'd0;
            reg_src_pitch <= 32'd0;
            reg_dst_pitch <= 32'd0;
            reg_mask_pitch <= 32'd0;
            reg_dim <= 32'd0;
            reg_op <= 32'd0;
            reg_color <= 32'd0;
            reg_key <= 32'd0;
            reg_blit_irq_en <= 1'b0;
            done_sticky_cpu <= 1'b0;
            error_cpu <= 8'd0;
            cfg_src_cpu <= 25'd0;
            cfg_dst_cpu <= 25'd0;
            cfg_mask_cpu <= 25'd0;
            cfg_src_pitch_cpu <= 16'd0;
            cfg_dst_pitch_cpu <= 16'd0;
            cfg_mask_pitch_cpu <= 16'd0;
            cfg_dim_cpu <= 32'd0;
            cfg_op_cpu <= 32'd0;
            cfg_mode_cpu <= 4'b0001;
            cfg_color_cpu <= 32'd0;
            cfg_key_cpu <= 32'd0;
            cfg_fields_valid_cpu <= 1'b1;
            cfg_op_valid_cpu <= 1'b1;
            cfg_noop_cpu <= 1'b1;
            start_toggle_cpu <= 1'b0;
            start_pending_cpu <= 1'b0;
            busy_sync_cpu <= 2'b00;
            done_sync_cpu <= 2'b00;
            done_seen_cpu <= 1'b0;
            error_meta_cpu <= 8'd0;
            error_sync_cpu <= 8'd0;
        end else begin
            busy_sync_cpu <= {busy_sync_cpu[0], busy_mem};
            done_sync_cpu <= {done_sync_cpu[0], done_toggle_mem};
            error_meta_cpu <= error_mem;
            error_sync_cpu <= error_meta_cpu;

            if (busy_sync_cpu[1])
                start_pending_cpu <= 1'b0;
            if (done_sync_cpu[1] != done_seen_cpu) begin
                done_seen_cpu <= done_sync_cpu[1];
                start_pending_cpu <= 1'b0;
                done_sticky_cpu <= 1'b1;
                reg_irq_stat <= 1'b1;
                error_cpu <= error_sync_cpu;
                cpu_done <= 1'b1;
            end

            if (cpu_write_stb) begin
                case (cpu_reg)
                    REG_CTRL: reg_ctrl <= merge_be(reg_ctrl, cpu_wdata, cpu_be);
                    REG_IRQ_EN: if (cpu_be[0])
                        reg_irq_en <= cpu_wdata[0];
                    REG_IRQ_STAT: if (cpu_be[0] && cpu_wdata[0])
                        reg_irq_stat <= 1'b0;
                    REG_BLIT_SRC: reg_src <= merge_be(reg_src, cpu_wdata, cpu_be);
                    REG_BLIT_DST: reg_dst <= merge_be(reg_dst, cpu_wdata, cpu_be);
                    REG_BLIT_MASK: reg_mask <= merge_be(reg_mask, cpu_wdata, cpu_be);
                    REG_BLIT_SRC_PITCH:
                        reg_src_pitch <= merge_be(reg_src_pitch, cpu_wdata, cpu_be);
                    REG_BLIT_DST_PITCH:
                        reg_dst_pitch <= merge_be(reg_dst_pitch, cpu_wdata, cpu_be);
                    REG_BLIT_MASK_PITCH:
                        reg_mask_pitch <= merge_be(reg_mask_pitch, cpu_wdata, cpu_be);
                    REG_BLIT_DIM: reg_dim <= merge_be(reg_dim, cpu_wdata, cpu_be);
                    REG_BLIT_OP: reg_op <= merge_be(reg_op, cpu_wdata, cpu_be);
                    REG_BLIT_COLOR: reg_color <= merge_be(reg_color, cpu_wdata, cpu_be);
                    REG_BLIT_KEY: reg_key <= merge_be(reg_key, cpu_wdata, cpu_be);
                    REG_BLIT_CTRL: begin
                        if (cpu_be[0]) begin
                            reg_blit_irq_en <= cpu_wdata[1];
                            if (cpu_wdata[0] && !busy_visible) begin
                                cfg_src_cpu <= reg_src[24:0];
                                cfg_dst_cpu <= reg_dst[24:0];
                                cfg_mask_cpu <= reg_mask[24:0];
                                cfg_src_pitch_cpu <= reg_src_pitch[15:0];
                                cfg_dst_pitch_cpu <= reg_dst_pitch[15:0];
                                cfg_mask_pitch_cpu <= reg_mask_pitch[15:0];
                                cfg_dim_cpu <= reg_dim;
                                cfg_op_cpu <= reg_op;
                                cfg_mode_cpu <= {
                                    reg_op[2:0] == MODE_COPY_MASK,
                                    reg_op[2:0] == MODE_COPY_KEY,
                                    reg_op[2:0] == MODE_FILL,
                                    reg_op[2:0] == MODE_COPY
                                };
                                cfg_color_cpu <= reg_color;
                                cfg_key_cpu <= reg_key;
                                cfg_fields_valid_cpu <=
                                    reg_dst[31:25] == 7'd0 &&
                                    reg_dst_pitch[31:16] == 16'd0 &&
                                    (reg_op[2:0] == MODE_FILL ||
                                     (reg_src[31:25] == 7'd0 &&
                                      reg_src_pitch[31:16] == 16'd0)) &&
                                    (reg_op[2:0] != MODE_COPY_MASK ||
                                     (reg_mask[31:25] == 7'd0 &&
                                      reg_mask_pitch[31:16] == 16'd0));
                                cfg_op_valid_cpu <=
                                    reg_op[2:0] <= MODE_COPY_MASK &&
                                    reg_op[5:4] <= 2'd2 &&
                                    !reg_op[3] &&
                                    reg_op[7:6] == 2'd0 &&
                                    reg_op[31:10] == 22'd0;
                                cfg_noop_cpu <= reg_dim[15:0] == 16'd0 ||
                                                reg_dim[31:16] == 16'd0;
                                start_toggle_cpu <= ~start_toggle_cpu;
                                start_pending_cpu <= 1'b1;
                                done_sticky_cpu <= 1'b0;
                                reg_irq_stat <= 1'b0;
                                error_cpu <= 8'd0;
                            end
                        end
                    end
                    default: begin end
                endcase
            end
        end
    end

    reg [24:0] src_row_mem;
    reg [24:0] dst_row_mem;
    reg [24:0] mask_row_mem;
    reg [24:0] cfg_src_mem;
    reg [24:0] cfg_dst_mem;
    reg [24:0] cfg_mask_mem;
    reg [15:0] cfg_src_pitch_mem;
    reg [15:0] cfg_dst_pitch_mem;
    reg [15:0] cfg_mask_pitch_mem;
    reg [31:0] cfg_dim_mem;
    reg [31:0] cfg_op_mem;
    reg [3:0]  cfg_mode_mem;
    reg [2:0]  cfg_element_bytes_mem;
    reg [17:0] cfg_row_bytes_mem;
    reg        word_mode_mem;
    reg [17:0] total_units_mem;
    reg [31:0] cfg_color_mem;
    reg [31:0] cfg_key_mem;
    reg        cfg_fields_valid_mem;
    reg        cfg_op_valid_mem;
    reg        cfg_noop_mem;
    // Number of rows after the row currently being processed.
    reg [15:0] rows_remaining_mem;
    reg [17:0] units_done_mem;
    reg [5:0] chunk_count_mem;
    reg [5:0] chunk_last_mem;
    reg       chunk_finishes_row_mem;
    reg [5:0] issue_count_mem;
    reg [5:0] response_count_mem;
    reg [17:0] chunk_start_index_mem;
    reg [17:0] issue_unit_index_mem;
    reg [24:0] issue_src_ptr_mem;
    reg [24:0] issue_dst_ptr_mem;
    reg [24:0] response_src_ptr_mem;
    // Every slot is filled by the read phase before the write phase consumes
    // it, so reset state is irrelevant. Distributed RAM avoids a 512-bit
    // register bank and its dynamic read mux without spending block RAM.
    (* ram_style = "distributed" *) reg [31:0] chunk_data [0:CHUNK_UNITS-1];

    reg [15:0] km_element_index_mem;
    reg [15:0] km_elements_remaining_mem;
    reg [1:0] km_source_byte_mem;
    reg [1:0] km_dest_byte_mem;
    reg [31:0] km_source_element_mem;
    reg [24:0] km_request_byte_addr_mem;
    reg [15:0] validate_pitch_mem;
    reg [32:0] validate_add_a_mem;
    reg [32:0] validate_add_b_mem;
    reg [24:0] src_row_offset_mem;
    reg [24:0] dst_row_offset_mem;
    reg [24:0] mask_row_offset_mem;

    wire [1:0] cfg_elem_size = cfg_op_mem[5:4];
    wire [13:0] cfg_mask_row_bytes =
        ({1'b0, cfg_dim_mem[15:0]} + 17'd7) >> 3;
    wire mode_copy_mem = cfg_mode_mem[0];
    wire mode_fill_mem = cfg_mode_mem[1];
    wire mode_copy_key_mem = cfg_mode_mem[2];
    wire mode_copy_mask_mem = cfg_mode_mem[3];
    wire cfg_uses_src = !mode_fill_mem;
    wire cfg_uses_mask = mode_copy_mask_mem;
    wire cfg_word_mode = cfg_row_bytes_mem[1:0] == 2'b00 &&
                         cfg_dst_mem[1:0] == 2'b00 &&
                         (mode_fill_mem || cfg_src_mem[1:0] == 2'b00);
    wire [1:0] elem_size_mem = cfg_elem_size;
    wire reverse_x_mem = cfg_op_mem[8];
    wire reverse_y_mem = cfg_op_mem[9];
    wire [15:0] src_pitch_mem = cfg_src_pitch_mem;
    wire [15:0] dst_pitch_mem = cfg_dst_pitch_mem;
    wire [15:0] mask_pitch_mem = cfg_mask_pitch_mem;
    wire [31:0] color_mem = cfg_color_mem;
    wire [31:0] key_mem = cfg_key_mem;
    wire [15:0] km_width_mem = cfg_dim_mem[15:0];

    wire [31:0] validate_product =
        rows_remaining_mem * validate_pitch_mem;
    wire [32:0] validate_sum = validate_add_a_mem + validate_add_b_mem;
    // Validation uses the inclusive final byte. A legal SDRAM address has no
    // bits above bit 24, which maps directly onto the carry chain and avoids a
    // second wide magnitude comparator after the address addition.
    wire validate_sum_out_of_range = |validate_sum[32:25];

    wire [24:0] chunk_start_offset = word_mode_mem ?
        {5'd0, chunk_start_index_mem, 2'b00} :
        {7'd0, chunk_start_index_mem};
    wire [24:0] unit_step = word_mode_mem ? 25'd4 : 25'd1;
    wire [7:0] issue_fill_byte = fill_byte(elem_size_mem,
                                            issue_unit_index_mem[1:0], color_mem);
    wire [31:0] fill_word = elem_size_mem == 2'd0 ? {4{color_mem[7:0]}} :
                            elem_size_mem == 2'd1 ? {2{color_mem[15:0]}} :
                            color_mem;

    wire [15:0] km_first_element = reverse_x_mem ?
                                         km_width_mem - 16'd1 : 16'd0;
    wire [17:0] km_first_byte_offset =
        {2'd0, km_first_element} << elem_size_mem;
    wire [15:0] km_next_element = reverse_x_mem ?
        km_element_index_mem - 16'd1 : km_element_index_mem + 16'd1;
    wire [2:0] km_element_bytes = cfg_element_bytes_mem;
    wire [24:0] km_next_source_element_addr = reverse_x_mem ?
        issue_src_ptr_mem - {22'd0, km_element_bytes} :
        issue_src_ptr_mem + {22'd0, km_element_bytes};
    wire [24:0] km_next_dest_element_addr = reverse_x_mem ?
        issue_dst_ptr_mem - {22'd0, km_element_bytes} :
        issue_dst_ptr_mem + {22'd0, km_element_bytes};
    wire [24:0] km_src_byte_addr = issue_src_ptr_mem +
        {23'd0, km_source_byte_mem};
    wire [31:0] km_value_mask = elem_size_mem == 2'd0 ? 32'h000000ff :
                                elem_size_mem == 2'd1 ? 32'h0000ffff :
                                                       32'hffffffff;
    wire km_key_match = (km_source_element_mem & km_value_mask) ==
                        (key_mem & km_value_mask);
    wire [7:0] km_response_byte =
        select_byte(km_request_byte_addr_mem[1:0], mem_rdata);
    wire km_mask_selected = km_response_byte[3'd7 -
                                             km_element_index_mem[2:0]];

    wire issuing_read = request_facts_mem[RF_ISSUING_READ];
    wire issuing_copy_write = request_facts_mem[RF_ISSUING_COPY_WRITE];
    wire issuing_fill = request_facts_mem[RF_ISSUING_FILL];
    wire read_response_phase = request_facts_mem[RF_READ_RESPONSE];
    wire km_issuing_read = request_facts_mem[RF_KM_ISSUING_READ];
    wire km_issuing_write = request_facts_mem[RF_KM_ISSUING_WRITE];
    wire km_bus_active = request_facts_mem[RF_KM_BUS_ACTIVE];
    wire address_phase = request_facts_mem[RF_ADDR];
    wire km_prep_phase = request_facts_mem[RF_KM_PREP];
    wire km_next_phase = request_facts_mem[RF_KM_NEXT];
    wire accepted = mem_valid && mem_ready;
    wire [17:0] remaining_units = total_units_mem - units_done_mem;
    wire issue_is_last = issue_count_mem == chunk_last_mem;

    // Ownership is needed only while a chunk phase can issue requests or has
    // responses in flight. Dropping it between fully retired phases gives the
    // real-time display port a bounded preemption point every CHUNK_UNITS.
    assign mem_lock = request_facts_mem[RF_MEM_LOCK];
    // The issue state retires on the same edge that accepts its final request,
    // so its registered fact is the exact valid window. Rechecking the counters
    // here only puts a carry chain on the shared SDRAM arbitration path.
    assign mem_valid = busy_mem &&
                       (issuing_read || issuing_copy_write || issuing_fill ||
                        km_issuing_read || km_issuing_write);
    assign mem_write = km_bus_active ? km_issuing_write :
                       issuing_copy_write || issuing_fill;
    assign mem_addr = km_bus_active ?
        {km_request_byte_addr_mem[24:2], 2'b00} :
        (issuing_read ? {issue_src_ptr_mem[24:2], 2'b00} :
                         {issue_dst_ptr_mem[24:2], 2'b00});
    assign mem_be = km_bus_active ?
        (km_issuing_write ? byte_enable(km_request_byte_addr_mem[1:0]) :
                            4'b1111) :
        (word_mode_mem ? 4'b1111 :
         byte_enable(issuing_read ? issue_src_ptr_mem[1:0] :
                                   issue_dst_ptr_mem[1:0]));
    // The captured one-hot mode is stable for the complete command. Use it
    // for write-data selection instead of decoding the live FSM; mem_wdata is
    // sampled only in the corresponding valid issue states.
    assign mem_wdata = (mode_copy_key_mem || mode_copy_mask_mem) ?
        place_byte(km_request_byte_addr_mem[1:0],
                   get_element_byte(km_source_element_mem, elem_size_mem,
                                    km_dest_byte_mem)) :
        (mode_fill_mem ?
         (word_mode_mem ? fill_word :
          place_byte(issue_dst_ptr_mem[1:0], issue_fill_byte)) :
         (word_mode_mem ?
          chunk_data[issue_count_mem[CHUNK_INDEX_BITS-1:0]] :
          place_byte(issue_dst_ptr_mem[1:0],
                     chunk_data[issue_count_mem[
                         CHUNK_INDEX_BITS-1:0]][7:0])));

    always @(posedge mem_clk) begin
        if (mem_rst) begin
            start_sync_mem <= 2'b00;
            start_seen_mem <= 1'b0;
            busy_mem <= 1'b0;
            done_toggle_mem <= 1'b0;
            error_mem <= 8'd0;
            set_state_mem(ST_IDLE);
            src_row_mem <= 25'd0;
            dst_row_mem <= 25'd0;
            mask_row_mem <= 25'd0;
            cfg_src_mem <= 25'd0;
            cfg_dst_mem <= 25'd0;
            cfg_mask_mem <= 25'd0;
            cfg_src_pitch_mem <= 16'd0;
            cfg_dst_pitch_mem <= 16'd0;
            cfg_mask_pitch_mem <= 16'd0;
            cfg_dim_mem <= 32'd0;
            cfg_op_mem <= 32'd0;
            cfg_mode_mem <= 4'b0001;
            cfg_element_bytes_mem <= 3'd1;
            cfg_row_bytes_mem <= 18'd0;
            word_mode_mem <= 1'b0;
            total_units_mem <= 18'd0;
            cfg_color_mem <= 32'd0;
            cfg_key_mem <= 32'd0;
            cfg_fields_valid_mem <= 1'b1;
            cfg_op_valid_mem <= 1'b1;
            cfg_noop_mem <= 1'b1;
            rows_remaining_mem <= 16'd0;
            units_done_mem <= 18'd0;
            chunk_count_mem <= 6'd0;
            chunk_last_mem <= 6'd0;
            chunk_finishes_row_mem <= 1'b0;
            issue_count_mem <= 6'd0;
            response_count_mem <= 6'd0;
            chunk_start_index_mem <= 18'd0;
            issue_unit_index_mem <= 18'd0;
            issue_src_ptr_mem <= 25'd0;
            issue_dst_ptr_mem <= 25'd0;
            response_src_ptr_mem <= 25'd0;
            km_element_index_mem <= 16'd0;
            km_elements_remaining_mem <= 16'd0;
            km_source_byte_mem <= 2'd0;
            km_dest_byte_mem <= 2'd0;
            km_source_element_mem <= 32'd0;
            km_request_byte_addr_mem <= 25'd0;
            validate_pitch_mem <= 16'd0;
            validate_add_a_mem <= 33'd0;
            validate_add_b_mem <= 33'd0;
            src_row_offset_mem <= 25'd0;
            dst_row_offset_mem <= 25'd0;
            mask_row_offset_mem <= 25'd0;
        end else begin
            start_sync_mem <= {start_sync_mem[0], start_toggle_cpu};

            // Keep address progression on registered bus facts. These
            // are the same state-cycle updates as the control case below, but
            // without rebuilding the 27-state decode on each pointer enable.
            if (address_phase)
                issue_src_ptr_mem <= src_row_mem + chunk_start_offset;
            else if (issuing_read && accepted)
                issue_src_ptr_mem <= reverse_x_mem ?
                    issue_src_ptr_mem - unit_step :
                    issue_src_ptr_mem + unit_step;
            else if (km_prep_phase)
                issue_src_ptr_mem <= src_row_mem +
                    {7'd0, km_first_byte_offset};
            else if (km_next_phase)
                issue_src_ptr_mem <= km_next_source_element_addr;

            if (address_phase)
                issue_dst_ptr_mem <= dst_row_mem + chunk_start_offset;
            else if ((issuing_copy_write || issuing_fill) && accepted)
                issue_dst_ptr_mem <= reverse_x_mem ?
                    issue_dst_ptr_mem - unit_step :
                    issue_dst_ptr_mem + unit_step;
            else if (km_prep_phase)
                issue_dst_ptr_mem <= dst_row_mem +
                    {7'd0, km_first_byte_offset};
            else if (km_next_phase)
                issue_dst_ptr_mem <= km_next_dest_element_addr;

            if (address_phase)
                issue_unit_index_mem <= chunk_start_index_mem;
            else if (issuing_fill && accepted)
                issue_unit_index_mem <= reverse_x_mem ?
                    issue_unit_index_mem - 18'd1 :
                    issue_unit_index_mem + 18'd1;

            if (address_phase)
                response_src_ptr_mem <= src_row_mem + chunk_start_offset;
            else if (read_response_phase && mem_rsp_valid)
                response_src_ptr_mem <= reverse_x_mem ?
                    response_src_ptr_mem - unit_step :
                    response_src_ptr_mem + unit_step;

            // The launch payload is held until completion. Capture it on the
            // first synchronizer stage so the second stage can launch from a
            // fully registered mem-domain snapshot without adding a cycle.
            if (!busy_mem && start_sync_mem[0] != start_seen_mem) begin
                cfg_src_mem <= cfg_src_cpu;
                cfg_dst_mem <= cfg_dst_cpu;
                cfg_mask_mem <= cfg_mask_cpu;
                cfg_src_pitch_mem <= cfg_src_pitch_cpu;
                cfg_dst_pitch_mem <= cfg_dst_pitch_cpu;
                cfg_mask_pitch_mem <= cfg_mask_pitch_cpu;
                cfg_dim_mem <= cfg_dim_cpu;
                cfg_op_mem <= cfg_op_cpu;
                cfg_mode_mem <= cfg_mode_cpu;
                cfg_element_bytes_mem <= 3'b001 << cfg_op_cpu[5:4];
                cfg_color_mem <= cfg_color_cpu;
                cfg_key_mem <= cfg_key_cpu;
                cfg_fields_valid_mem <= cfg_fields_valid_cpu;
                cfg_op_valid_mem <= cfg_op_valid_cpu;
                cfg_noop_mem <= cfg_noop_cpu;
            end

            if (!busy_mem && start_sync_mem[1] != start_seen_mem) begin
                start_seen_mem <= start_sync_mem[1];
                error_mem <= 8'd0;
                if (!cfg_op_valid_mem) begin
                    error_mem <= 8'd1;
                    done_toggle_mem <= ~done_toggle_mem;
                    set_state_mem(ST_IDLE);
                end else if (cfg_noop_mem) begin
                    done_toggle_mem <= ~done_toggle_mem;
                    set_state_mem(ST_IDLE);
                end else if (!cfg_fields_valid_mem) begin
                    error_mem <= 8'd1;
                    done_toggle_mem <= ~done_toggle_mem;
                    set_state_mem(ST_IDLE);
                end else begin
                    busy_mem <= 1'b1;
                    cfg_row_bytes_mem <=
                        {2'd0, cfg_dim_mem[15:0]} << cfg_op_mem[5:4];
                    rows_remaining_mem <= cfg_dim_mem[31:16] - 16'd1;
                    validate_pitch_mem <= cfg_dst_pitch_mem;
                    set_state_mem(ST_VAL_DST_MUL);
                end
            end else if (busy_mem) begin
                case (state_mem)
                    ST_VAL_DST_MUL: begin
                        word_mode_mem <= cfg_word_mode;
                        total_units_mem <= cfg_word_mode ?
                            (cfg_row_bytes_mem >> 2) : cfg_row_bytes_mem;
                        dst_row_offset_mem <= validate_product[24:0];
                        validate_add_a_mem <= {8'd0, cfg_dst_mem};
                        validate_add_b_mem <= {1'b0, validate_product};
                        set_state_mem(ST_VAL_DST_BASE);
                    end
                    ST_VAL_DST_BASE: begin
                        validate_add_a_mem <= validate_sum;
                        validate_add_b_mem <=
                            {15'd0, cfg_row_bytes_mem - 18'd1};
                        set_state_mem(ST_VAL_DST_END);
                    end
                    ST_VAL_DST_END: begin
                        if (validate_sum_out_of_range) begin
                            busy_mem <= 1'b0;
                            error_mem <= 8'd1;
                            done_toggle_mem <= ~done_toggle_mem;
                            set_state_mem(ST_IDLE);
                        end else begin
                            if (cfg_uses_src)
                                validate_pitch_mem <= cfg_src_pitch_mem;
                            set_state_mem(cfg_uses_src ? ST_VAL_SRC_MUL :
                                                         ST_VAL_DONE);
                        end
                    end
                    ST_VAL_SRC_MUL: begin
                        src_row_offset_mem <= validate_product[24:0];
                        validate_add_a_mem <= {8'd0, cfg_src_mem};
                        validate_add_b_mem <= {1'b0, validate_product};
                        set_state_mem(ST_VAL_SRC_BASE);
                    end
                    ST_VAL_SRC_BASE: begin
                        validate_add_a_mem <= validate_sum;
                        validate_add_b_mem <=
                            {15'd0, cfg_row_bytes_mem - 18'd1};
                        set_state_mem(ST_VAL_SRC_END);
                    end
                    ST_VAL_SRC_END: begin
                        if (validate_sum_out_of_range) begin
                            busy_mem <= 1'b0;
                            error_mem <= 8'd1;
                            done_toggle_mem <= ~done_toggle_mem;
                            set_state_mem(ST_IDLE);
                        end else begin
                            if (cfg_uses_mask)
                                validate_pitch_mem <= cfg_mask_pitch_mem;
                            set_state_mem(cfg_uses_mask ? ST_VAL_MASK_MUL :
                                                          ST_VAL_DONE);
                        end
                    end
                    ST_VAL_MASK_MUL: begin
                        mask_row_offset_mem <= validate_product[24:0];
                        validate_add_a_mem <= {8'd0, cfg_mask_mem};
                        validate_add_b_mem <= {1'b0, validate_product};
                        set_state_mem(ST_VAL_MASK_BASE);
                    end
                    ST_VAL_MASK_BASE: begin
                        validate_add_a_mem <= validate_sum;
                        validate_add_b_mem <=
                            {19'd0, cfg_mask_row_bytes - 14'd1};
                        set_state_mem(ST_VAL_MASK_END);
                    end
                    ST_VAL_MASK_END: begin
                        if (validate_sum_out_of_range) begin
                            busy_mem <= 1'b0;
                            error_mem <= 8'd1;
                            done_toggle_mem <= ~done_toggle_mem;
                            set_state_mem(ST_IDLE);
                        end else begin
                            set_state_mem(ST_VAL_DONE);
                        end
                    end
                    ST_VAL_DONE: begin
                        src_row_mem <= cfg_op_mem[9] ?
                            cfg_src_mem + src_row_offset_mem : cfg_src_mem;
                        dst_row_mem <= cfg_op_mem[9] ?
                            cfg_dst_mem + dst_row_offset_mem : cfg_dst_mem;
                        mask_row_mem <= cfg_op_mem[9] ?
                            cfg_mask_mem + mask_row_offset_mem : cfg_mask_mem;
                        units_done_mem <= 18'd0;
                        set_state_mem(mode_copy_mem || mode_fill_mem ?
                                      ST_PREP : ST_KM_PREP);
                    end
                    ST_PREP: begin
                        if (remaining_units > {{12{1'b0}}, CHUNK_COUNT}) begin
                            chunk_count_mem <= CHUNK_COUNT;
                            chunk_finishes_row_mem <= 1'b0;
                        end else begin
                            chunk_count_mem <= remaining_units[5:0];
                            chunk_finishes_row_mem <= 1'b1;
                        end
                        chunk_start_index_mem <= reverse_x_mem ?
                            total_units_mem - 18'd1 - units_done_mem :
                            units_done_mem;
                        set_state_mem(ST_ADDR);
                    end

                    ST_ADDR: begin
                        issue_count_mem <= 6'd0;
                        response_count_mem <= 6'd0;
                        chunk_last_mem <= chunk_count_mem - 6'd1;
                        set_state_mem(mode_copy_mem ?
                                      ST_READ_ISSUE : ST_FILL_ISSUE);
                    end

                    ST_READ_ISSUE: begin
                        if (accepted) begin
                            issue_count_mem <= issue_count_mem + 6'd1;
                            if (issue_is_last)
                                set_state_mem(ST_READ_DRAIN);
                        end
                        if (mem_rsp_valid) begin
                            chunk_data[response_count_mem[CHUNK_INDEX_BITS-1:0]] <= word_mode_mem ?
                                mem_rdata :
                                {24'd0, select_byte(response_src_ptr_mem[1:0], mem_rdata)};
                            response_count_mem <= response_count_mem + 6'd1;
                        end
                    end

                    ST_READ_DRAIN: begin
                        if (response_count_mem == chunk_count_mem) begin
                            issue_count_mem <= 6'd0;
                            response_count_mem <= 6'd0;
                            set_state_mem(ST_READ_YIELD);
                        end else if (mem_rsp_valid) begin
                            chunk_data[response_count_mem[CHUNK_INDEX_BITS-1:0]] <= word_mode_mem ?
                                mem_rdata :
                                {24'd0, select_byte(response_src_ptr_mem[1:0], mem_rdata)};
                            response_count_mem <= response_count_mem + 6'd1;
                        end
                    end

                    ST_READ_YIELD: begin
                        set_state_mem(ST_WRITE_ISSUE);
                    end

                    ST_WRITE_ISSUE, ST_FILL_ISSUE: begin
                        if (accepted) begin
                            issue_count_mem <= issue_count_mem + 6'd1;
                            if (issue_is_last)
                                set_state_mem(state_mem == ST_WRITE_ISSUE ?
                                              ST_WRITE_DRAIN : ST_FILL_DRAIN);
                        end
                        if (mem_rsp_valid)
                            response_count_mem <= response_count_mem + 6'd1;
                    end

                    ST_WRITE_DRAIN, ST_FILL_DRAIN: begin
                        if (response_count_mem == chunk_count_mem) begin
                            set_state_mem(ST_NEXT);
                        end else if (mem_rsp_valid) begin
                            response_count_mem <= response_count_mem + 6'd1;
                        end
                    end

                    ST_NEXT: begin
                        if (chunk_finishes_row_mem) begin
                            if (rows_remaining_mem == 16'd0) begin
                                busy_mem <= 1'b0;
                                done_toggle_mem <= ~done_toggle_mem;
                                set_state_mem(ST_IDLE);
                            end else begin
                                rows_remaining_mem <= rows_remaining_mem - 16'd1;
                                src_row_mem <= reverse_y_mem ?
                                               src_row_mem - {9'd0, src_pitch_mem} :
                                               src_row_mem + {9'd0, src_pitch_mem};
                                dst_row_mem <= reverse_y_mem ?
                                               dst_row_mem - {9'd0, dst_pitch_mem} :
                                               dst_row_mem + {9'd0, dst_pitch_mem};
                                units_done_mem <= 18'd0;
                                set_state_mem(ST_PREP);
                            end
                        end else begin
                            units_done_mem <= units_done_mem + {12'd0, chunk_count_mem};
                            set_state_mem(ST_PREP);
                        end
                    end

                    ST_KM_PREP: begin
                        km_element_index_mem <= km_first_element;
                        km_elements_remaining_mem <= km_width_mem;
                        km_source_byte_mem <= 2'd0;
                        km_dest_byte_mem <= 2'd0;
                        km_source_element_mem <= 32'd0;
                        if (mode_copy_mask_mem) begin
                            km_request_byte_addr_mem <= mask_row_mem +
                                {12'd0, km_first_element[15:3]};
                            set_state_mem(ST_KM_MASK_ISSUE);
                        end else begin
                            km_request_byte_addr_mem <= src_row_mem +
                                {7'd0, km_first_byte_offset};
                            set_state_mem(ST_KM_SRC_ISSUE);
                        end
                    end

                    ST_KM_MASK_ISSUE: begin
                        if (accepted)
                            set_state_mem(ST_KM_MASK_WAIT);
                    end

                    ST_KM_MASK_WAIT: begin
                        if (mem_rsp_valid) begin
                            if (km_mask_selected) begin
                                km_source_byte_mem <= 2'd0;
                                km_source_element_mem <= 32'd0;
                                km_request_byte_addr_mem <= km_src_byte_addr;
                                set_state_mem(ST_KM_SRC_ISSUE);
                            end else begin
                                set_state_mem(ST_KM_NEXT);
                            end
                        end
                    end

                    ST_KM_SRC_ISSUE: begin
                        if (accepted)
                            set_state_mem(ST_KM_SRC_WAIT);
                    end

                    ST_KM_SRC_WAIT: begin
                        if (mem_rsp_valid) begin
                            km_source_element_mem <= put_element_byte(
                                km_source_element_mem, elem_size_mem,
                                km_source_byte_mem, km_response_byte);
                            if ({1'b0, km_source_byte_mem} + 3'd1 >=
                                km_element_bytes) begin
                                set_state_mem(ST_KM_DECIDE);
                            end else begin
                                km_source_byte_mem <= km_source_byte_mem + 2'd1;
                                km_request_byte_addr_mem <=
                                    km_request_byte_addr_mem + 25'd1;
                                set_state_mem(ST_KM_SRC_ISSUE);
                            end
                        end
                    end

                    ST_KM_DECIDE: begin
                        if (mode_copy_key_mem && km_key_match) begin
                            set_state_mem(ST_KM_NEXT);
                        end else begin
                            km_dest_byte_mem <= 2'd0;
                            km_request_byte_addr_mem <=
                                issue_dst_ptr_mem;
                            set_state_mem(ST_KM_DST_ISSUE);
                        end
                    end

                    ST_KM_DST_ISSUE: begin
                        if (accepted)
                            set_state_mem(ST_KM_DST_WAIT);
                    end

                    ST_KM_DST_WAIT: begin
                        if (mem_rsp_valid) begin
                            if ({1'b0, km_dest_byte_mem} + 3'd1 >=
                                km_element_bytes) begin
                                set_state_mem(ST_KM_NEXT);
                            end else begin
                                km_dest_byte_mem <= km_dest_byte_mem + 2'd1;
                                km_request_byte_addr_mem <=
                                    km_request_byte_addr_mem + 25'd1;
                                set_state_mem(ST_KM_DST_ISSUE);
                            end
                        end
                    end

                    ST_KM_NEXT: begin
                        if (mode_copy_mask_mem)
                            km_request_byte_addr_mem <= mask_row_mem +
                                {12'd0, km_next_element[15:3]};
                        else
                            km_request_byte_addr_mem <=
                                km_next_source_element_addr;
                        if (km_elements_remaining_mem == 16'd1) begin
                            if (rows_remaining_mem == 16'd0) begin
                                busy_mem <= 1'b0;
                                done_toggle_mem <= ~done_toggle_mem;
                                set_state_mem(ST_IDLE);
                            end else begin
                                rows_remaining_mem <= rows_remaining_mem - 16'd1;
                                src_row_mem <= reverse_y_mem ?
                                    src_row_mem - {9'd0, src_pitch_mem} :
                                    src_row_mem + {9'd0, src_pitch_mem};
                                dst_row_mem <= reverse_y_mem ?
                                    dst_row_mem - {9'd0, dst_pitch_mem} :
                                    dst_row_mem + {9'd0, dst_pitch_mem};
                                mask_row_mem <= reverse_y_mem ?
                                    mask_row_mem - {9'd0, mask_pitch_mem} :
                                    mask_row_mem + {9'd0, mask_pitch_mem};
                                set_state_mem(ST_KM_PREP);
                            end
                        end else begin
                            km_elements_remaining_mem <=
                                km_elements_remaining_mem - 16'd1;
                            km_element_index_mem <= km_next_element;
                            km_source_byte_mem <= 2'd0;
                            km_source_element_mem <= 32'd0;
                            if (mode_copy_mask_mem) begin
                                set_state_mem(ST_KM_MASK_ISSUE);
                            end else begin
                                set_state_mem(ST_KM_SRC_ISSUE);
                            end
                        end
                    end

                    default: begin
                        busy_mem <= 1'b0;
                        error_mem <= 8'd2;
                        done_toggle_mem <= ~done_toggle_mem;
                        set_state_mem(ST_IDLE);
                    end
                endcase
            end
        end
    end

`ifndef SYNTHESIS
    always @(posedge mem_clk) begin
        if (!mem_rst) begin
            if (request_facts_mem !== request_facts_for_state(state_mem))
                $fatal(1, "blitter request facts/state mismatch state=%0d facts=%h",
                       state_mem, request_facts_mem);
            if ((issuing_read || issuing_copy_write || issuing_fill) &&
                issue_count_mem >= chunk_count_mem)
                $fatal(1, "blitter issue state has no request remaining count=%0d chunk=%0d",
                       issue_count_mem, chunk_count_mem);
        end
    end

    initial begin
        if (CHUNK_UNITS < 1 || CHUNK_UNITS > 32)
            $fatal(1, "astraea_blitter CHUNK_UNITS must be 1..32");
    end
`endif
endmodule

`default_nettype wire
