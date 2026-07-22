// Astraea top-level: global identity/IRQ surface, blitter, draw engine, and
// copper, with registered ownership for the shared native SDRAM master.
`default_nettype none

module astraea_chip (
    input  wire        cpu_clk,
    input  wire        cpu_rst,
    input  wire        cpu_write_stb,
    input  wire [15:0] cpu_addr,
    input  wire [3:0]  cpu_be,
    input  wire [31:0] cpu_wdata,
    output reg  [31:0] cpu_rdata,
    output wire        cpu_busy,
    output wire        cpu_done,
    output wire        cpu_irq,
    output wire        cache_flush,
    output wire [31:0] blitter_completed_fence,
    output wire [31:0] draw_completed_fence,

    input  wire        front_guard_valid,
    input  wire [24:0] front_guard_start,
    input  wire [25:0] front_guard_end,
    input  wire        pending_guard_valid,
    input  wire [24:0] pending_guard_start,
    input  wire [25:0] pending_guard_end,

    input  wire [9:0]  beam_x,
    input  wire [9:0]  beam_y,
    output wire        cop_move_stb,
    output wire [17:0] cop_move_addr,
    output wire [31:0] cop_move_data,

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
    localparam [31:0] ASTRAEA_VERSION = 32'h00040000;
    localparam [31:0] CAP_COPY      = 32'h00000001;
    localparam [31:0] CAP_FILL      = 32'h00000002;
    localparam [31:0] CAP_COPY_KEY  = 32'h00000004;
    localparam [31:0] CAP_COPY_MASK = 32'h00000008;
    localparam [31:0] CAP_GEOMETRY  = 32'h00000010;
    localparam [31:0] CAP_GLYPH     = 32'h00000020;
    localparam [31:0] CAP_FLOOD     = 32'h00000040;
    localparam [31:0] CAP_COPPER    = 32'h00000080;

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

    wire cpu_blitter_select = cpu_addr < 16'h0080;
    wire cpu_blitter_write = cpu_write_stb && cpu_blitter_select;
    wire cpu_draw_select = cpu_addr >= 16'h0100 && cpu_addr < 16'h0180;
    wire cpu_draw_write = cpu_write_stb && cpu_draw_select;

    wire copper_move_raw;
    wire [17:0] copper_move_addr_raw;
    wire [31:0] copper_move_data_raw;
    wire copper_irq_event;
    wire [3:0] copper_irq_sources;
    wire copper_running;
    wire copper_waiting;
    wire copper_fault;
    wire [31:0] copper_rdata;

    // Astraea-targeted MOVEs below 0x80 can launch or configure the blitter.
    // Other chip targets leave this module through cop_move_*.
    wire copper_blitter_write = copper_move_raw &&
                                 copper_move_addr_raw[17:16] == 2'b01 &&
                                 copper_move_addr_raw[15:7] == 9'd0;
    wire copper_draw_write = copper_move_raw &&
                             copper_move_addr_raw[17:16] == 2'b01 &&
                             copper_move_addr_raw[15:8] == 8'h01 &&
                             copper_move_addr_raw[7:0] < 8'h80;
    wire blitter_write_stb = cpu_blitter_write ||
                             (!cpu_blitter_write && copper_blitter_write);
    wire [4:0] blitter_reg = blitter_write_stb ?
        (cpu_blitter_write ? cpu_addr[6:2] : copper_move_addr_raw[6:2]) :
        cpu_addr[6:2];
    wire [3:0] blitter_be = cpu_blitter_write ? cpu_be : 4'b1111;
    wire [31:0] blitter_wdata = cpu_blitter_write ? cpu_wdata :
                                                     copper_move_data_raw;
    wire [31:0] blitter_rdata;
    wire blitter_busy;
    wire blitter_done;
    wire blitter_irq;
    wire blitter_cache_flush;
    wire [31:0] blitter_completed_fence_i;
    wire blitter_mem_lock;
    wire blitter_mem_valid;
    wire blitter_mem_ready;
    wire blitter_mem_write;
    wire [24:0] blitter_mem_addr;
    wire [3:0] blitter_mem_be;
    wire [31:0] blitter_mem_wdata;
    wire blitter_mem_rsp_valid;

    astraea_blitter blitter_i (
        .cpu_clk(cpu_clk), .cpu_rst(cpu_rst),
        .cpu_write_stb(blitter_write_stb), .cpu_reg(blitter_reg),
        .cpu_be(blitter_be), .cpu_wdata(blitter_wdata),
        .cpu_rdata(blitter_rdata), .cpu_busy(blitter_busy),
        .cpu_done(blitter_done), .cpu_irq(blitter_irq),
        .cache_flush(blitter_cache_flush),
        .completed_fence(blitter_completed_fence_i),
        .front_guard_valid(front_guard_valid),
        .front_guard_start(front_guard_start),
        .front_guard_end(front_guard_end),
        .pending_guard_valid(pending_guard_valid),
        .pending_guard_start(pending_guard_start),
        .pending_guard_end(pending_guard_end),
        .mem_clk(mem_clk), .mem_rst(mem_rst), .mem_lock(blitter_mem_lock),
        .mem_valid(blitter_mem_valid), .mem_ready(blitter_mem_ready),
        .mem_write(blitter_mem_write), .mem_addr(blitter_mem_addr),
        .mem_be(blitter_mem_be), .mem_wdata(blitter_mem_wdata),
        .mem_rsp_valid(blitter_mem_rsp_valid),
        .mem_rdata(mem_rdata)
    );

    wire draw_write_stb = cpu_draw_write ||
                          (!cpu_draw_write && copper_draw_write);
    wire [4:0] draw_reg = draw_write_stb ?
        (cpu_draw_write ? cpu_addr[6:2] : copper_move_addr_raw[6:2]) :
        cpu_addr[6:2];
    wire [3:0] draw_be = cpu_draw_write ? cpu_be : 4'b1111;
    wire [31:0] draw_wdata = cpu_draw_write ? cpu_wdata :
                                               copper_move_data_raw;
    wire [31:0] draw_rdata;
    wire draw_busy;
    wire draw_done;
    wire draw_irq;
    wire draw_cache_flush;
    wire [31:0] draw_completed_fence_i;
    wire draw_mem_lock;
    wire draw_mem_valid;
    wire draw_mem_ready;
    wire draw_mem_write;
    wire [24:0] draw_mem_addr;
    wire [3:0] draw_mem_be;
    wire [31:0] draw_mem_wdata;
    wire draw_mem_rsp_valid;

    astraea_draw draw_i (
        .cpu_clk(cpu_clk), .cpu_rst(cpu_rst),
        .cpu_write_stb(draw_write_stb), .cpu_reg(draw_reg),
        .cpu_be(draw_be), .cpu_wdata(draw_wdata), .cpu_rdata(draw_rdata),
        .cpu_busy(draw_busy), .cpu_done(draw_done), .cpu_irq(draw_irq),
        .cache_flush(draw_cache_flush),
        .completed_fence(draw_completed_fence_i),
        .front_guard_valid(front_guard_valid),
        .front_guard_start(front_guard_start),
        .front_guard_end(front_guard_end),
        .pending_guard_valid(pending_guard_valid),
        .pending_guard_start(pending_guard_start),
        .pending_guard_end(pending_guard_end),
        .mem_clk(mem_clk), .mem_rst(mem_rst), .mem_lock(draw_mem_lock),
        .mem_valid(draw_mem_valid), .mem_ready(draw_mem_ready),
        .mem_write(draw_mem_write), .mem_addr(draw_mem_addr),
        .mem_be(draw_mem_be), .mem_wdata(draw_mem_wdata),
        .mem_rsp_valid(draw_mem_rsp_valid), .mem_rdata(mem_rdata)
    );

    astraea_copper copper_i (
        .clk(cpu_clk), .rst(cpu_rst),
        .cpu_write_stb(cpu_write_stb && !cpu_blitter_select &&
                       !cpu_draw_select),
        .cpu_addr(cpu_addr), .cpu_be(cpu_be), .cpu_wdata(cpu_wdata),
        .cpu_rdata(copper_rdata),
        .beam_x_async(beam_x), .beam_y_async(beam_y),
        .move_stb(copper_move_raw), .move_addr(copper_move_addr_raw),
        .move_data(copper_move_data_raw), .irq_event(copper_irq_event),
        .irq_sources(copper_irq_sources), .running(copper_running),
        .waiting(copper_waiting), .fault(copper_fault)
    );

    localparam [1:0] MEM_OWNER_NONE = 2'd0;
    localparam [1:0] MEM_OWNER_BLIT = 2'd1;
    localparam [1:0] MEM_OWNER_DRAW = 2'd2;
    reg [1:0] mem_owner;

    always @(posedge mem_clk) begin
        if (mem_rst) begin
            mem_owner <= MEM_OWNER_NONE;
        end else begin
            case (mem_owner)
                MEM_OWNER_NONE: begin
                    if (blitter_mem_lock)
                        mem_owner <= MEM_OWNER_BLIT;
                    else if (draw_mem_lock)
                        mem_owner <= MEM_OWNER_DRAW;
                end
                MEM_OWNER_BLIT: begin
                    if (!blitter_mem_lock)
                        mem_owner <= MEM_OWNER_NONE;
                end
                MEM_OWNER_DRAW: begin
                    if (!draw_mem_lock)
                        mem_owner <= MEM_OWNER_NONE;
                end
                default: mem_owner <= MEM_OWNER_NONE;
            endcase
        end
    end

    wire mem_use_blitter = mem_owner == MEM_OWNER_BLIT;
    wire mem_use_draw = mem_owner == MEM_OWNER_DRAW;
    assign mem_lock = mem_owner != MEM_OWNER_NONE;
    assign mem_valid = mem_use_blitter ? blitter_mem_valid :
                       mem_use_draw ? draw_mem_valid : 1'b0;
    assign mem_write = mem_use_blitter ? blitter_mem_write :
                       mem_use_draw ? draw_mem_write : 1'b0;
    assign mem_addr = mem_use_blitter ? blitter_mem_addr :
                      mem_use_draw ? draw_mem_addr : 25'd0;
    assign mem_be = mem_use_blitter ? blitter_mem_be :
                    mem_use_draw ? draw_mem_be : 4'd0;
    assign mem_wdata = mem_use_blitter ? blitter_mem_wdata :
                       mem_use_draw ? draw_mem_wdata : 32'd0;
    assign blitter_mem_ready = mem_use_blitter && mem_ready;
    assign draw_mem_ready = mem_use_draw && mem_ready;
    assign blitter_mem_rsp_valid = mem_use_blitter && mem_rsp_valid;
    assign draw_mem_rsp_valid = mem_use_draw && mem_rsp_valid;

    assign cpu_busy = blitter_busy || draw_busy;
    assign cpu_done = blitter_done || draw_done;
    assign cache_flush = blitter_cache_flush || draw_cache_flush;
    assign blitter_completed_fence = blitter_completed_fence_i;
    assign draw_completed_fence = draw_completed_fence_i;

    assign cop_move_stb = copper_move_raw &&
                          copper_move_addr_raw[17:16] != 2'b01;
    assign cop_move_addr = copper_move_addr_raw;
    assign cop_move_data = copper_move_data_raw;

    reg copper_irq_enable;
    reg copper_irq_pending;
    reg [3:0] copper_irq_source_pending;
    reg draw_irq_enable;
    reg draw_irq_pending;

    wire global_irq_en_write = blitter_write_stb && blitter_reg == 5'h04;
    wire global_irq_stat_write = blitter_write_stb && blitter_reg == 5'h05;
    wire [31:0] merged_irq_enable = merge_be(
        {28'd0, draw_irq_enable, 1'b0, copper_irq_enable, 1'b0},
        blitter_wdata, blitter_be);

    always @(posedge cpu_clk) begin
        if (cpu_rst) begin
            copper_irq_enable <= 1'b0;
            copper_irq_pending <= 1'b0;
            copper_irq_source_pending <= 4'd0;
            draw_irq_enable <= 1'b0;
            draw_irq_pending <= 1'b0;
        end else begin
            if (global_irq_en_write) begin
                copper_irq_enable <= merged_irq_enable[1];
                draw_irq_enable <= merged_irq_enable[3];
            end
            if (global_irq_stat_write && blitter_be[0] && blitter_wdata[1]) begin
                copper_irq_pending <= 1'b0;
                copper_irq_source_pending <= 4'd0;
            end
            if (global_irq_stat_write && blitter_be[0] && blitter_wdata[3])
                draw_irq_pending <= 1'b0;
            if (cpu_write_stb && cpu_addr == 16'h0090 && cpu_be[0])
                copper_irq_source_pending <= copper_irq_source_pending &
                                             ~cpu_wdata[3:0];
            if (copper_irq_event) begin
                copper_irq_pending <= 1'b1;
                copper_irq_source_pending <= copper_irq_source_pending |
                                             copper_irq_sources;
            end
            if (draw_done)
                draw_irq_pending <= 1'b1;
        end
    end

    assign cpu_irq = blitter_irq | draw_irq |
                     (copper_irq_enable && copper_irq_pending) |
                     (draw_irq_enable && draw_irq_pending);

    always @* begin
        if (cpu_addr == 16'h0004)
            cpu_rdata = ASTRAEA_VERSION;
        else if (cpu_addr == 16'h0014)
            cpu_rdata = blitter_rdata |
                        {28'd0, draw_irq_pending, 1'b0,
                         copper_irq_pending, 1'b0};
        else if (cpu_addr == 16'h0018)
            cpu_rdata = CAP_COPY | CAP_FILL | CAP_COPY_KEY | CAP_COPY_MASK |
                        CAP_GEOMETRY | CAP_GLYPH | CAP_FLOOD | CAP_COPPER;
        else if (cpu_addr == 16'h0090)
            cpu_rdata = {28'd0, copper_irq_source_pending};
        else if (cpu_blitter_select)
            cpu_rdata = blitter_rdata;
        else if (cpu_draw_select)
            cpu_rdata = draw_rdata;
        else
            cpu_rdata = copper_rdata;
    end

    wire unused_copper_state = copper_running ^ copper_waiting ^ copper_fault;
endmodule

`default_nettype wire
