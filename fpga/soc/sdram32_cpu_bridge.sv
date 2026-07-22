// CPU-to-SDRAM CDC bridge with a small physical read-line buffer. A cacheable
// miss fetches one 16-byte line inside a single CDC transaction; uncached,
// locked, and write cycles remain exactly one native request. The request and
// response bundles remain stable until their toggles cross the synchronizers.
`default_nettype none

module sdram32_cpu_bridge (
    input  wire        cpu_clk,
    input  wire        cpu_rst,
    input  wire        cpu_start,
    input  wire [24:0] cpu_addr,
    input  wire        cpu_write,
    input  wire [3:0]  cpu_be,
    input  wire [31:0] cpu_wdata,
    input  wire        cpu_lock,
    input  wire        cpu_cacheable,
    input  wire        cpu_instruction,
    input  wire        cpu_postable,
    input  wire        cpu_cache_flush,
    output wire        cpu_busy,
    output reg         cpu_done,
    output reg  [31:0] cpu_rdata,
    output reg  [31:0] cpu_line_hits,
    output reg  [31:0] cpu_line_misses,
    output reg  [31:0] cpu_posted_writes,
    output reg         cpu_fill_valid,
    output reg  [24:0] cpu_fill_addr,
    output reg  [127:0] cpu_fill_data,
    output reg         cpu_fill_instruction,

    input  wire        mem_clk,
    input  wire        mem_rst,
    output reg         mem_valid,
    input  wire        mem_ready,
    output wire        mem_write,
    output wire [24:0] mem_addr,
    output wire [3:0]  mem_be,
    output wire [31:0] mem_wdata,
    output wire        mem_lock,
    output wire        mem_cache_quiescent,
    input  wire        mem_rsp_valid,
    input  wire [31:0] mem_rdata
);
    localparam integer LINE_COUNT = 16;
    localparam [1:0] MEM_IDLE = 2'd0, MEM_REQUEST = 2'd1,
                     MEM_RESPONSE = 2'd2, MEM_LINE = 2'd3;

    reg [127:0] line_data [0:LINE_COUNT-1];
    reg [16:0] line_tag [0:LINE_COUNT-1];
    reg [LINE_COUNT-1:0] line_valid;
    reg [1:0] cache_epoch;
    reg cache_flush_d;

    wire posted_request = cpu_write && cpu_postable && !cpu_lock;

    wire [3:0] cpu_line_index = cpu_addr[7:4];
    wire [16:0] cpu_line_tag = cpu_addr[24:8];
    wire cache_read = cpu_cacheable && !cpu_cache_flush &&
                      !cpu_write && !cpu_lock;
    wire cpu_line_hit = cache_read && line_valid[cpu_line_index] &&
                        line_tag[cpu_line_index] == cpu_line_tag;

    function automatic [31:0] select_line_word(
        input [127:0] value,
        input [1:0] word
    );
        case (word)
            2'd0: select_line_word = value[127:96];
            2'd1: select_line_word = value[95:64];
            2'd2: select_line_word = value[63:32];
            default: select_line_word = value[31:0];
        endcase
    endfunction

    reg [24:0] addr_cpu;
    reg write_cpu;
    reg [3:0] be_cpu;
    reg [31:0] wdata_cpu;
    reg line_cpu;
    reg [1:0] requested_word_cpu;
    reg instruction_cpu;
    reg [3:0] fill_index_cpu;
    reg [16:0] fill_tag_cpu;
    reg [1:0] fill_epoch_cpu;
    reg request_busy_cpu;
    reg posted_cpu;
    reg req_toggle_cpu;
    reg rsp_seen_cpu;
    (* async_reg = "true" *) reg [1:0] rsp_sync_cpu;

    (* async_reg = "true" *) reg [1:0] req_sync_mem;
    (* async_reg = "true" *) reg [1:0] lock_sync_mem;
    (* async_reg = "true" *) reg [1:0] flush_ack_sync_mem;
    reg req_seen_mem;
    reg rsp_toggle_mem;
    reg [31:0] rdata_mem;
    reg [127:0] rline_mem;
    reg line_mem;
    reg [2:0] line_issue_mem;
    reg [2:0] line_rsp_mem;
    reg [1:0] mem_state;

    // A DMA flush is also a bus fence. New CPU SDRAM accesses remain stalled
    // until DMA releases the fence, while an already-posted request drains.
    assign cpu_busy = request_busy_cpu || cpu_cache_flush;
    wire flush_ack_cpu = cpu_cache_flush && !cpu_rst && !request_busy_cpu;
    assign mem_write = write_cpu;
    assign mem_addr = line_mem ?
                      addr_cpu + {20'd0, line_issue_mem, 2'b00} : addr_cpu;
    assign mem_be = be_cpu;
    assign mem_wdata = wdata_cpu;
    assign mem_lock = lock_sync_mem[1];
    // This round-trip acknowledgement proves the CPU clock domain has seen
    // the fence and that every request accepted before it has completed.
    assign mem_cache_quiescent = flush_ack_sync_mem[1];

    always @(posedge cpu_clk) begin
        cpu_done <= 1'b0;
        cpu_fill_valid <= 1'b0;
        if (cpu_rst) begin
            addr_cpu <= 25'd0;
            write_cpu <= 1'b0;
            be_cpu <= 4'd0;
            wdata_cpu <= 32'd0;
            line_cpu <= 1'b0;
            requested_word_cpu <= 2'd0;
            instruction_cpu <= 1'b0;
            fill_index_cpu <= 4'd0;
            fill_tag_cpu <= 17'd0;
            fill_epoch_cpu <= 2'd0;
            request_busy_cpu <= 1'b0;
            posted_cpu <= 1'b0;
            req_toggle_cpu <= 1'b0;
            rsp_seen_cpu <= 1'b0;
            rsp_sync_cpu <= 2'b00;
            cpu_rdata <= 32'd0;
            cpu_line_hits <= 32'd0;
            cpu_line_misses <= 32'd0;
            cpu_posted_writes <= 32'd0;
            cpu_fill_addr <= 25'd0;
            cpu_fill_data <= 128'd0;
            cpu_fill_instruction <= 1'b0;
            line_valid <= {LINE_COUNT{1'b0}};
            cache_epoch <= 2'd0;
            cache_flush_d <= 1'b0;
        end else begin
            cache_flush_d <= cpu_cache_flush;
            if (cpu_cache_flush) begin
                line_valid <= {LINE_COUNT{1'b0}};
                if (!cache_flush_d)
                    cache_epoch <= cache_epoch + 2'd1;
            end
            rsp_sync_cpu <= {rsp_sync_cpu[0], rsp_toggle_mem};
            if (cpu_start && !request_busy_cpu && !cpu_cache_flush) begin
                if (cpu_line_hit) begin
                    cpu_rdata <= select_line_word(
                        line_data[cpu_line_index], cpu_addr[3:2]);
                    cpu_done <= 1'b1;
                    cpu_line_hits <= cpu_line_hits + 32'd1;
                end else begin
                    addr_cpu <= cache_read ? {cpu_addr[24:4], 4'b0000} :
                                             {cpu_addr[24:2], 2'b00};
                    write_cpu <= cpu_write;
                    be_cpu <= cpu_be;
                    wdata_cpu <= cpu_wdata;
                    line_cpu <= cache_read;
                    requested_word_cpu <= cpu_addr[3:2];
                    instruction_cpu <= cpu_instruction;
                    fill_index_cpu <= cpu_line_index;
                    fill_tag_cpu <= cpu_line_tag;
                    fill_epoch_cpu <= cache_epoch;
                    req_toggle_cpu <= ~req_toggle_cpu;
                    request_busy_cpu <= 1'b1;
                    posted_cpu <= posted_request;
                    if (posted_request) begin
                        cpu_done <= 1'b1;
                        cpu_posted_writes <= cpu_posted_writes + 32'd1;
                    end else if (cache_read) begin
                        cpu_line_misses <= cpu_line_misses + 32'd1;
                    end
                end
                if (cpu_write && line_valid[cpu_line_index] &&
                    line_tag[cpu_line_index] == cpu_line_tag)
                    line_valid[cpu_line_index] <= 1'b0;
            end
            if (request_busy_cpu && rsp_sync_cpu[1] != rsp_seen_cpu) begin
                rsp_seen_cpu <= rsp_sync_cpu[1];
                if (posted_cpu) begin
                    posted_cpu <= 1'b0;
                end else if (line_cpu) begin
                    cpu_rdata <= select_line_word(rline_mem,
                                                  requested_word_cpu);
                    cpu_fill_valid <= 1'b1;
                    cpu_fill_addr <= addr_cpu;
                    cpu_fill_data <= rline_mem;
                    cpu_fill_instruction <= instruction_cpu;
                    line_data[fill_index_cpu] <= rline_mem;
                    line_tag[fill_index_cpu] <= fill_tag_cpu;
                    if (fill_epoch_cpu == cache_epoch && !cpu_cache_flush)
                        line_valid[fill_index_cpu] <= 1'b1;
                end else begin
                    cpu_rdata <= rdata_mem;
                end
                request_busy_cpu <= 1'b0;
                if (!posted_cpu)
                    cpu_done <= 1'b1;
            end
        end
    end

    always @(posedge mem_clk) begin
        if (mem_rst) begin
            req_sync_mem <= 2'b00;
            lock_sync_mem <= 2'b00;
            flush_ack_sync_mem <= 2'b00;
            req_seen_mem <= 1'b0;
            rsp_toggle_mem <= 1'b0;
            rdata_mem <= 32'd0;
            rline_mem <= 128'd0;
            line_mem <= 1'b0;
            line_issue_mem <= 3'd0;
            line_rsp_mem <= 3'd0;
            mem_valid <= 1'b0;
            mem_state <= MEM_IDLE;
        end else begin
            req_sync_mem <= {req_sync_mem[0], req_toggle_cpu};
            lock_sync_mem <= {lock_sync_mem[0], cpu_lock};
            flush_ack_sync_mem <= {
                flush_ack_sync_mem[0], flush_ack_cpu
            };
            case (mem_state)
                MEM_IDLE: begin
                    mem_valid <= 1'b0;
                    if (req_sync_mem[1] != req_seen_mem) begin
                        req_seen_mem <= req_sync_mem[1];
                        line_mem <= line_cpu;
                        line_issue_mem <= 3'd0;
                        line_rsp_mem <= 3'd0;
                        mem_valid <= 1'b1;
                        if (line_cpu)
                            mem_state <= MEM_LINE;
                        else
                            mem_state <= MEM_REQUEST;
                    end
                end
                MEM_REQUEST: begin
                    if (mem_ready) begin
                        mem_valid <= 1'b0;
                        mem_state <= MEM_RESPONSE;
                    end
                end
                MEM_RESPONSE: begin
                    if (mem_rsp_valid) begin
                        rdata_mem <= mem_rdata;
                        rsp_toggle_mem <= ~rsp_toggle_mem;
                        mem_state <= MEM_IDLE;
                    end
                end
                MEM_LINE: begin
                    // The native controller accepts queued requests and
                    // returns responses in order. Keep issuing until all four
                    // words are queued while independently assembling replies.
                    if (mem_valid && mem_ready) begin
                        if (line_issue_mem == 3'd3)
                            mem_valid <= 1'b0;
                        line_issue_mem <= line_issue_mem + 3'd1;
                    end
                    if (mem_rsp_valid) begin
                        case (line_rsp_mem)
                            3'd0: rline_mem[127:96] <= mem_rdata;
                            3'd1: rline_mem[95:64] <= mem_rdata;
                            3'd2: rline_mem[63:32] <= mem_rdata;
                            default: rline_mem[31:0] <= mem_rdata;
                        endcase
                        if (line_rsp_mem == 3'd3) begin
                            rsp_toggle_mem <= ~rsp_toggle_mem;
                            mem_state <= MEM_IDLE;
                        end else begin
                            line_rsp_mem <= line_rsp_mem + 3'd1;
                        end
                    end
                end
                default: mem_state <= MEM_IDLE;
            endcase
        end
    end
endmodule

`default_nettype wire
