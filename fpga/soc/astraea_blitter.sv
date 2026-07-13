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
    localparam integer CHUNK_INDEX_BITS = CHUNK_UNITS <= 1 ? 1 : $clog2(CHUNK_UNITS);
    localparam [5:0] CHUNK_COUNT = CHUNK_UNITS;

    localparam [3:0] ST_IDLE        = 4'd0;
    localparam [3:0] ST_PREP        = 4'd1;
    localparam [3:0] ST_READ_ISSUE  = 4'd2;
    localparam [3:0] ST_READ_DRAIN  = 4'd3;
    localparam [3:0] ST_WRITE_ISSUE = 4'd4;
    localparam [3:0] ST_WRITE_DRAIN = 4'd5;
    localparam [3:0] ST_FILL_ISSUE  = 4'd6;
    localparam [3:0] ST_FILL_DRAIN  = 4'd7;
    localparam [3:0] ST_NEXT        = 4'd8;
    localparam [3:0] ST_ADDR        = 4'd9;

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

    reg [31:0] reg_ctrl;
    reg [31:0] reg_irq_en;
    reg [31:0] reg_irq_stat;
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
    reg [15:0] cfg_src_pitch_cpu;
    reg [15:0] cfg_dst_pitch_cpu;
    reg [31:0] cfg_dim_cpu;
    reg [31:0] cfg_op_cpu;
    reg [31:0] cfg_color_cpu;
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
    reg [3:0] state_mem;

    wire busy_visible = start_pending_cpu | busy_sync_cpu[1];
    assign cpu_busy = busy_visible;
    assign cache_flush = busy_visible;
    assign cpu_irq = (reg_irq_en[0] | reg_blit_irq_en) & reg_irq_stat[0];

    always @* begin
        cpu_rdata = 32'd0;
        case (cpu_reg)
            REG_ID:              cpu_rdata = 32'h41535452; // "ASTR"
            REG_VERSION:         cpu_rdata = 32'h00010000;
            REG_CTRL:            cpu_rdata = reg_ctrl;
            REG_STATUS:          cpu_rdata = {30'd0, done_sticky_cpu, busy_visible};
            REG_IRQ_EN:          cpu_rdata = reg_irq_en;
            REG_IRQ_STAT:        cpu_rdata = reg_irq_stat;
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
            reg_irq_en <= 32'd0;
            reg_irq_stat <= 32'd0;
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
            cfg_src_pitch_cpu <= 16'd0;
            cfg_dst_pitch_cpu <= 16'd0;
            cfg_dim_cpu <= 32'd0;
            cfg_op_cpu <= 32'd0;
            cfg_color_cpu <= 32'd0;
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
                reg_irq_stat[0] <= 1'b1;
                error_cpu <= error_sync_cpu;
                cpu_done <= 1'b1;
            end

            if (cpu_write_stb) begin
                case (cpu_reg)
                    REG_CTRL: reg_ctrl <= merge_be(reg_ctrl, cpu_wdata, cpu_be);
                    REG_IRQ_EN: reg_irq_en <= merge_be(reg_irq_en, cpu_wdata, cpu_be);
                    REG_IRQ_STAT: reg_irq_stat <= reg_irq_stat &
                                                  ~merge_be(32'd0, cpu_wdata, cpu_be);
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
                                cfg_src_pitch_cpu <= reg_src_pitch[15:0];
                                cfg_dst_pitch_cpu <= reg_dst_pitch[15:0];
                                cfg_dim_cpu <= reg_dim;
                                cfg_op_cpu <= reg_op;
                                cfg_color_cpu <= reg_color;
                                start_toggle_cpu <= ~start_toggle_cpu;
                                start_pending_cpu <= 1'b1;
                                done_sticky_cpu <= 1'b0;
                                reg_irq_stat[0] <= 1'b0;
                                error_cpu <= 8'd0;
                            end
                        end
                    end
                    default: begin end
                endcase
            end
        end
    end

    reg [2:0] mode_mem;
    reg [1:0] elem_size_mem;
    reg       reverse_x_mem;
    reg       reverse_y_mem;
    reg       word_mode_mem;
    reg [24:0] src_row_mem;
    reg [24:0] dst_row_mem;
    reg [15:0] src_pitch_mem;
    reg [15:0] dst_pitch_mem;
    reg [15:0] rows_remaining_mem;
    reg [17:0] total_units_mem;
    reg [17:0] units_done_mem;
    reg [5:0] chunk_count_mem;
    reg [5:0] chunk_last_mem;
    reg [5:0] issue_count_mem;
    reg [5:0] response_count_mem;
    reg [31:0] color_mem;
    reg [17:0] chunk_start_index_mem;
    reg [17:0] issue_unit_index_mem;
    reg [24:0] issue_src_ptr_mem;
    reg [24:0] issue_dst_ptr_mem;
    reg [24:0] response_src_ptr_mem;
    reg [31:0] chunk_data [0:CHUNK_UNITS-1];

    wire [1:0] cfg_elem_size = cfg_op_cpu[5:4];
    wire [17:0] cfg_row_bytes = {2'd0, cfg_dim_cpu[15:0]} << cfg_elem_size;
    wire cfg_word_mode = cfg_row_bytes[1:0] == 2'b00 &&
                         cfg_dst_cpu[1:0] == 2'b00 &&
                         (cfg_op_cpu[2:0] == MODE_FILL || cfg_src_cpu[1:0] == 2'b00);
    wire [24:0] cfg_row_offset_src =
        ({9'd0, cfg_dim_cpu[31:16]} - 25'd1) *
        {9'd0, cfg_src_pitch_cpu};
    wire [24:0] cfg_row_offset_dst =
        ({9'd0, cfg_dim_cpu[31:16]} - 25'd1) *
        {9'd0, cfg_dst_pitch_cpu};

    wire [24:0] chunk_start_offset = word_mode_mem ?
        {5'd0, chunk_start_index_mem, 2'b00} :
        {7'd0, chunk_start_index_mem};
    wire [24:0] unit_step = word_mode_mem ? 25'd4 : 25'd1;
    wire [7:0] issue_fill_byte = fill_byte(elem_size_mem,
                                            issue_unit_index_mem[1:0], color_mem);
    wire [31:0] fill_word = elem_size_mem == 2'd0 ? {4{color_mem[7:0]}} :
                            elem_size_mem == 2'd1 ? {2{color_mem[15:0]}} :
                            color_mem;

    wire issuing_read = state_mem == ST_READ_ISSUE;
    wire issuing_copy_write = state_mem == ST_WRITE_ISSUE;
    wire issuing_fill = state_mem == ST_FILL_ISSUE;
    wire accepted = mem_valid && mem_ready;
    wire [17:0] remaining_units = total_units_mem - units_done_mem;
    wire issue_is_last = issue_count_mem == chunk_last_mem;

    assign mem_lock = busy_mem;
    assign mem_valid = busy_mem &&
                       (issuing_read || issuing_copy_write || issuing_fill) &&
                       issue_count_mem < chunk_count_mem;
    assign mem_write = issuing_copy_write || issuing_fill;
    assign mem_addr = issuing_read ? {issue_src_ptr_mem[24:2], 2'b00} :
                      {issue_dst_ptr_mem[24:2], 2'b00};
    assign mem_be = word_mode_mem ? 4'b1111 :
                    byte_enable(issuing_read ? issue_src_ptr_mem[1:0] :
                                              issue_dst_ptr_mem[1:0]);
    assign mem_wdata = issuing_fill ?
        (word_mode_mem ? fill_word :
         place_byte(issue_dst_ptr_mem[1:0], issue_fill_byte)) :
        (word_mode_mem ? chunk_data[issue_count_mem[CHUNK_INDEX_BITS-1:0]] :
         place_byte(issue_dst_ptr_mem[1:0],
                    chunk_data[issue_count_mem[CHUNK_INDEX_BITS-1:0]][7:0]));

    integer i;
    always @(posedge mem_clk) begin
        if (mem_rst) begin
            start_sync_mem <= 2'b00;
            start_seen_mem <= 1'b0;
            busy_mem <= 1'b0;
            done_toggle_mem <= 1'b0;
            error_mem <= 8'd0;
            state_mem <= ST_IDLE;
            mode_mem <= MODE_COPY;
            elem_size_mem <= 2'd0;
            reverse_x_mem <= 1'b0;
            reverse_y_mem <= 1'b0;
            word_mode_mem <= 1'b0;
            src_row_mem <= 25'd0;
            dst_row_mem <= 25'd0;
            src_pitch_mem <= 16'd0;
            dst_pitch_mem <= 16'd0;
            rows_remaining_mem <= 16'd0;
            total_units_mem <= 18'd0;
            units_done_mem <= 18'd0;
            chunk_count_mem <= 6'd0;
            chunk_last_mem <= 6'd0;
            issue_count_mem <= 6'd0;
            response_count_mem <= 6'd0;
            color_mem <= 32'd0;
            chunk_start_index_mem <= 18'd0;
            issue_unit_index_mem <= 18'd0;
            issue_src_ptr_mem <= 25'd0;
            issue_dst_ptr_mem <= 25'd0;
            response_src_ptr_mem <= 25'd0;
            for (i = 0; i < CHUNK_UNITS; i = i + 1)
                chunk_data[i] <= 32'd0;
        end else begin
            start_sync_mem <= {start_sync_mem[0], start_toggle_cpu};

            if (!busy_mem && start_sync_mem[1] != start_seen_mem) begin
                start_seen_mem <= start_sync_mem[1];
                error_mem <= 8'd0;
                if (cfg_op_cpu[2:0] > MODE_FILL || cfg_elem_size > 2'd2) begin
                    error_mem <= 8'd1;
                    done_toggle_mem <= ~done_toggle_mem;
                    state_mem <= ST_IDLE;
                end else if (cfg_dim_cpu[15:0] == 16'd0 ||
                             cfg_dim_cpu[31:16] == 16'd0) begin
                    done_toggle_mem <= ~done_toggle_mem;
                    state_mem <= ST_IDLE;
                end else begin
                    busy_mem <= 1'b1;
                    state_mem <= ST_PREP;
                    mode_mem <= cfg_op_cpu[2:0];
                    elem_size_mem <= cfg_elem_size;
                    reverse_x_mem <= cfg_op_cpu[8];
                    reverse_y_mem <= cfg_op_cpu[9];
                    word_mode_mem <= cfg_word_mode;
                    src_pitch_mem <= cfg_src_pitch_cpu;
                    dst_pitch_mem <= cfg_dst_pitch_cpu;
                    rows_remaining_mem <= cfg_dim_cpu[31:16];
                    src_row_mem <= cfg_op_cpu[9] ?
                                   cfg_src_cpu + cfg_row_offset_src : cfg_src_cpu;
                    dst_row_mem <= cfg_op_cpu[9] ?
                                   cfg_dst_cpu + cfg_row_offset_dst : cfg_dst_cpu;
                    total_units_mem <= cfg_word_mode ?
                                       (cfg_row_bytes >> 2) : cfg_row_bytes;
                    units_done_mem <= 18'd0;
                    color_mem <= cfg_color_cpu;
                end
            end else if (busy_mem) begin
                case (state_mem)
                    ST_PREP: begin
                        if (remaining_units > {{12{1'b0}}, CHUNK_COUNT})
                            chunk_count_mem <= CHUNK_COUNT;
                        else
                            chunk_count_mem <= remaining_units[5:0];
                        chunk_start_index_mem <= reverse_x_mem ?
                            total_units_mem - 18'd1 - units_done_mem :
                            units_done_mem;
                        state_mem <= ST_ADDR;
                    end

                    ST_ADDR: begin
                        issue_count_mem <= 6'd0;
                        response_count_mem <= 6'd0;
                        chunk_last_mem <= chunk_count_mem - 6'd1;
                        issue_unit_index_mem <= chunk_start_index_mem;
                        issue_src_ptr_mem <= src_row_mem + chunk_start_offset;
                        issue_dst_ptr_mem <= dst_row_mem + chunk_start_offset;
                        response_src_ptr_mem <= src_row_mem + chunk_start_offset;
                        state_mem <= mode_mem == MODE_COPY ?
                                     ST_READ_ISSUE : ST_FILL_ISSUE;
                    end

                    ST_READ_ISSUE: begin
                        if (accepted) begin
                            issue_count_mem <= issue_count_mem + 6'd1;
                            issue_src_ptr_mem <= reverse_x_mem ?
                                issue_src_ptr_mem - unit_step :
                                issue_src_ptr_mem + unit_step;
                            if (issue_is_last)
                                state_mem <= ST_READ_DRAIN;
                        end
                        if (mem_rsp_valid) begin
                            chunk_data[response_count_mem[CHUNK_INDEX_BITS-1:0]] <= word_mode_mem ?
                                mem_rdata :
                                {24'd0, select_byte(response_src_ptr_mem[1:0], mem_rdata)};
                            response_count_mem <= response_count_mem + 6'd1;
                            response_src_ptr_mem <= reverse_x_mem ?
                                response_src_ptr_mem - unit_step :
                                response_src_ptr_mem + unit_step;
                        end
                    end

                    ST_READ_DRAIN: begin
                        if (response_count_mem == chunk_count_mem) begin
                            issue_count_mem <= 6'd0;
                            response_count_mem <= 6'd0;
                            state_mem <= ST_WRITE_ISSUE;
                        end else if (mem_rsp_valid) begin
                            chunk_data[response_count_mem[CHUNK_INDEX_BITS-1:0]] <= word_mode_mem ?
                                mem_rdata :
                                {24'd0, select_byte(response_src_ptr_mem[1:0], mem_rdata)};
                            response_count_mem <= response_count_mem + 6'd1;
                            response_src_ptr_mem <= reverse_x_mem ?
                                response_src_ptr_mem - unit_step :
                                response_src_ptr_mem + unit_step;
                        end
                    end

                    ST_WRITE_ISSUE, ST_FILL_ISSUE: begin
                        if (accepted) begin
                            issue_count_mem <= issue_count_mem + 6'd1;
                            issue_dst_ptr_mem <= reverse_x_mem ?
                                issue_dst_ptr_mem - unit_step :
                                issue_dst_ptr_mem + unit_step;
                            if (state_mem == ST_FILL_ISSUE)
                                issue_unit_index_mem <= reverse_x_mem ?
                                    issue_unit_index_mem - 18'd1 :
                                    issue_unit_index_mem + 18'd1;
                            if (issue_is_last)
                                state_mem <= state_mem == ST_WRITE_ISSUE ?
                                             ST_WRITE_DRAIN : ST_FILL_DRAIN;
                        end
                        if (mem_rsp_valid)
                            response_count_mem <= response_count_mem + 6'd1;
                    end

                    ST_WRITE_DRAIN, ST_FILL_DRAIN: begin
                        if (response_count_mem == chunk_count_mem) begin
                            state_mem <= ST_NEXT;
                        end else if (mem_rsp_valid) begin
                            response_count_mem <= response_count_mem + 6'd1;
                        end
                    end

                    ST_NEXT: begin
                        if (units_done_mem + {12'd0, chunk_count_mem} ==
                            total_units_mem) begin
                            if (rows_remaining_mem == 16'd1) begin
                                busy_mem <= 1'b0;
                                done_toggle_mem <= ~done_toggle_mem;
                                state_mem <= ST_IDLE;
                            end else begin
                                rows_remaining_mem <= rows_remaining_mem - 16'd1;
                                src_row_mem <= reverse_y_mem ?
                                               src_row_mem - {9'd0, src_pitch_mem} :
                                               src_row_mem + {9'd0, src_pitch_mem};
                                dst_row_mem <= reverse_y_mem ?
                                               dst_row_mem - {9'd0, dst_pitch_mem} :
                                               dst_row_mem + {9'd0, dst_pitch_mem};
                                units_done_mem <= 18'd0;
                                state_mem <= ST_PREP;
                            end
                        end else begin
                            units_done_mem <= units_done_mem + {12'd0, chunk_count_mem};
                            state_mem <= ST_PREP;
                        end
                    end

                    default: begin
                        busy_mem <= 1'b0;
                        error_mem <= 8'd2;
                        done_toggle_mem <= ~done_toggle_mem;
                        state_mem <= ST_IDLE;
                    end
                endcase
            end
        end
    end

`ifndef SYNTHESIS
    initial begin
        if (CHUNK_UNITS < 1 || CHUNK_UNITS > 32)
            $fatal(1, "astraea_blitter CHUNK_UNITS must be 1..32");
    end
`endif
endmodule

`default_nettype wire
