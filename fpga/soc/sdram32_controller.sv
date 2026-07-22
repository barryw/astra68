// Astra 32-bit native request wrapper around ultraembedded's SDRAM core.
// Requests use canonical 68k byte order; the upstream core uses AXI lane order,
// so bytes and strobes are reversed once at this boundary.
`default_nettype none

module sdram32_controller #(
    parameter integer SDRAM_MHZ = 60,
    // ECP5/ULX3S hardware needs three internal clocks at 60 MHz. A value of
    // two captures the second 16-bit burst beat as the low halfword.
    parameter integer SDRAM_READ_LATENCY = 3
) (
    input  wire        clk,
    input  wire        rst,

    input  wire        cpu_valid,
    output wire        cpu_ready,
    input  wire        cpu_write,
    input  wire [24:0] cpu_addr,
    input  wire [3:0]  cpu_be,
    input  wire [31:0] cpu_wdata,
    input  wire        cpu_lock,
    output wire        cpu_rsp_valid,
    output wire [31:0] cpu_rdata,

    // Real-time display traffic has priority over ordinary CPU and DMA
    // traffic. The client drops video_lock at bounded burst boundaries so the
    // processor still receives service while a scanline is being prepared.
    input  wire        video_lock,
    input  wire        video_valid,
    output wire        video_ready,
    input  wire        video_write,
    input  wire [24:0] video_addr,
    input  wire [3:0]  video_be,
    input  wire [31:0] video_wdata,
    output wire        video_rsp_valid,
    output wire [31:0] video_rdata,

    // A burst owner holds dma_lock from before its first request through its
    // final response. This allows pipelining without a response-tag FIFO.
    input  wire        dma_lock,
    input  wire        dma_valid,
    output wire        dma_ready,
    input  wire        dma_write,
    input  wire [24:0] dma_addr,
    input  wire [3:0]  dma_be,
    input  wire [31:0] dma_wdata,
    output wire        dma_rsp_valid,
    output wire [31:0] dma_rdata,

    input  wire [15:0] sdram_data_in,
    output wire [15:0] sdram_data_out,
    output wire        sdram_data_oe,
    output wire        sdram_clk,
    output wire        sdram_cke,
    output wire        sdram_cs,
    output wire        sdram_ras,
    output wire        sdram_cas,
    output wire        sdram_we,
    output wire [1:0]  sdram_dqm,
    output wire [12:0] sdram_addr,
    output wire [1:0]  sdram_ba
);
    localparam [1:0] OWNER_NONE  = 2'd0;
    localparam [1:0] OWNER_CPU   = 2'd1;
    localparam [1:0] OWNER_DMA   = 2'd2;
    localparam [1:0] OWNER_VIDEO = 2'd3;

    reg [1:0] owner;
    reg [5:0] outstanding;

    // A locked CPU read-modify-write remains indivisible. Otherwise display
    // fetch wins, followed by background DMA and then ordinary CPU traffic.
    wire grant_video = (owner == OWNER_VIDEO) ||
                       (owner == OWNER_NONE && video_lock && !cpu_lock);
    wire grant_dma = (owner == OWNER_DMA) ||
                     (owner == OWNER_NONE && dma_lock && !video_lock &&
                      !cpu_lock);
    wire grant_cpu = (owner == OWNER_CPU) ||
                     (owner == OWNER_NONE &&
                      (cpu_lock || (!video_lock && !dma_lock)));

    wire        selected_valid = grant_video ? video_valid :
                                         grant_dma ? dma_valid : cpu_valid;
    wire        selected_write = grant_video ? video_write :
                                         grant_dma ? dma_write : cpu_write;
    wire [24:0] selected_addr  = grant_video ? video_addr :
                                         grant_dma ? dma_addr : cpu_addr;
    wire [3:0]  selected_be    = grant_video ? video_be :
                                         grant_dma ? dma_be : cpu_be;
    wire [31:0] selected_wdata = grant_video ? video_wdata :
                                         grant_dma ? dma_wdata : cpu_wdata;

    // Two-entry FIFO between arbitration and the physical command engine.
    // Client ready depends only on registered occupancy, keeping both owner
    // selection and the core's combinational accept path off its request input.
    reg  [1:0]  request_count;
    reg         request_write;
    reg  [24:0] request_addr;
    // Physical timing replica for the SDRAM row lookup. request_addr also
    // drives the command/address output cone near the SDRAM pins; sharing that
    // register bank with the open-row comparators creates a device-spanning
    // path. Keep this copy architecturally identical and give the placer a
    // local source for the comparator without adding a pipeline cycle.
    (* keep = "true" *) reg [24:0] request_compare_addr;
    reg  [3:0]  request_be;
    reg  [31:0] request_wdata;
    reg         request_tail_write;
    reg  [24:0] request_tail_addr;
    (* keep = "true" *) reg [24:0] request_tail_compare_addr;
    reg  [3:0]  request_tail_be;
    reg  [31:0] request_tail_wdata;

    function automatic [31:0] reverse_bytes(input [31:0] value);
        reverse_bytes = {value[7:0], value[15:8], value[23:16], value[31:24]};
    endfunction

    wire request_valid = request_count != 2'd0;
    wire request_ready = request_count != 2'd2;
    wire [3:0] core_wr = request_valid && request_write ?
                         {request_be[0], request_be[1],
                          request_be[2], request_be[3]} : 4'b0000;
    wire core_rd = request_valid && !request_write;
    wire core_accept;
    wire core_ack;
    wire core_error;
    wire [31:0] core_rdata;
    wire core_consumed = request_valid && core_accept;
    wire accepted = selected_valid && request_ready;

    // The command engine registers its row-open/hit decision one cycle before
    // consuming it. Only registered FIFO entries may enter that timing cone.
    // A full FIFO presents its tail while READ/WRITE0 accepts the head, which
    // preserves one 32-bit transfer every two clocks for continuous traffic.
    // Empty and late-arriving requests wait for the normal FIFO register
    // boundary instead of bypassing it with a device-spanning client path.
    reg       core_compare_valid;
    reg [1:0] core_compare_select;
    always @* begin
        core_compare_valid = 1'b0;
        core_compare_select = 2'b01;
        if (core_accept) begin
            if (request_count == 2'd2) begin
                core_compare_valid = 1'b1;
                core_compare_select = 2'b10;
            end
        end else if (request_valid) begin
            core_compare_valid = 1'b1;
        end
    end

    assign cpu_ready = grant_cpu && request_ready && cpu_valid;
    assign dma_ready = grant_dma && request_ready && dma_valid;
    assign video_ready = grant_video && request_ready && video_valid;
    assign cpu_rsp_valid = core_ack && owner == OWNER_CPU;
    assign dma_rsp_valid = core_ack && owner == OWNER_DMA;
    assign video_rsp_valid = core_ack && owner == OWNER_VIDEO;
    assign cpu_rdata = reverse_bytes(core_rdata);
    assign dma_rdata = reverse_bytes(core_rdata);
    assign video_rdata = reverse_bytes(core_rdata);

    always @(posedge clk or posedge rst) begin
        if (rst) begin
            owner <= OWNER_NONE;
            outstanding <= 6'd0;
            request_count <= 2'd0;
            request_write <= 1'b0;
            request_addr <= 25'd0;
            // This value is intentionally different from request_addr's reset
            // value. The comparator address is irrelevant while the FIFO is
            // empty, and the first accepted request loads both banks with the
            // same address. Distinct reset behavior prevents synthesis from
            // merging the physical timing replica back into request_addr.
            request_compare_addr <= 25'h1ffffff;
            request_be <= 4'd0;
            request_wdata <= 32'd0;
            request_tail_write <= 1'b0;
            request_tail_addr <= 25'd0;
            request_tail_compare_addr <= 25'h1555555;
            request_tail_be <= 4'd0;
            request_tail_wdata <= 32'd0;
        end else begin
            case ({accepted, core_consumed})
                2'b10: begin
                    request_count <= request_count + 2'd1;
                    if (request_count == 2'd0) begin
                        request_write <= selected_write;
                        request_addr <= selected_addr;
                        request_compare_addr <= selected_addr;
                        request_be <= selected_be;
                        request_wdata <= selected_wdata;
                    end else begin
                        request_tail_write <= selected_write;
                        request_tail_addr <= selected_addr;
                        request_tail_compare_addr <= selected_addr;
                        request_tail_be <= selected_be;
                        request_tail_wdata <= selected_wdata;
                    end
                end
                2'b01: begin
                    request_count <= request_count - 2'd1;
                    if (request_count == 2'd2) begin
                        request_write <= request_tail_write;
                        request_addr <= request_tail_addr;
                        request_compare_addr <= request_tail_compare_addr;
                        request_be <= request_tail_be;
                        request_wdata <= request_tail_wdata;
                    end
                end
                2'b11: begin
                    // With occupancy one, retire the head and replace it on
                    // the same edge. A full FIFO does not assert ready.
                    request_write <= selected_write;
                    request_addr <= selected_addr;
                    request_compare_addr <= selected_addr;
                    request_be <= selected_be;
                    request_wdata <= selected_wdata;
                end
                default: request_count <= request_count;
            endcase

            case ({accepted, core_ack})
                2'b10: outstanding <= outstanding + 6'd1;
                2'b01: outstanding <= outstanding - 6'd1;
                default: outstanding <= outstanding;
            endcase

            // Locked clients already reserve arbitration before their first
            // request is valid. Acquire from those registered lock signals so
            // request-valid/data selection is not on the owner state path.
            // Ordinary unlocked CPU traffic still acquires when accepted.
            if (owner == OWNER_NONE) begin
                if (cpu_lock)
                    owner <= OWNER_CPU;
                else if (video_lock)
                    owner <= OWNER_VIDEO;
                else if (dma_lock)
                    owner <= OWNER_DMA;
                else if (accepted)
                    owner <= OWNER_CPU;
            end
            else if (owner == OWNER_CPU && outstanding == 6'd0 &&
                     !cpu_lock && !accepted)
                owner <= OWNER_NONE;
            else if (owner == OWNER_CPU && core_ack && outstanding == 6'd1 &&
                     !accepted && !cpu_lock)
                owner <= OWNER_NONE;
            else if (owner == OWNER_DMA && outstanding == 6'd0 && !dma_lock)
                owner <= OWNER_NONE;
            else if (owner == OWNER_DMA && core_ack && outstanding == 6'd1 &&
                     !accepted && !dma_lock)
                owner <= OWNER_NONE;
            else if (owner == OWNER_VIDEO && outstanding == 6'd0 &&
                     !video_lock)
                owner <= OWNER_NONE;
            else if (owner == OWNER_VIDEO && core_ack && outstanding == 6'd1 &&
                     !accepted && !video_lock)
                owner <= OWNER_NONE;
        end
    end

    // The upstream core has no error response path for SDR SDRAM.
    wire unused_core_error = core_error;

    sdram_axi_core #(
        .SDRAM_MHZ(SDRAM_MHZ),
        .SDRAM_ADDR_W(24),
        .SDRAM_COL_W(9),
        .SDRAM_READ_LATENCY(SDRAM_READ_LATENCY)
    ) core (
        .clk_i(clk),
        .rst_i(rst),
        .inport_wr_i(core_wr),
        .inport_rd_i(core_rd),
        .inport_len_i(8'd0),
        .inport_addr_i({7'd0, request_addr}),
        .inport_compare_head_addr_i({7'd0, request_compare_addr}),
        .inport_compare_tail_addr_i({7'd0, request_tail_compare_addr}),
        .inport_compare_select_i(core_compare_select),
        .inport_compare_valid_i(core_compare_valid),
        .inport_write_data_i(reverse_bytes(request_wdata)),
        .inport_accept_o(core_accept),
        .inport_ack_o(core_ack),
        .inport_error_o(core_error),
        .inport_read_data_o(core_rdata),
        .sdram_clk_o(sdram_clk),
        .sdram_cke_o(sdram_cke),
        .sdram_cs_o(sdram_cs),
        .sdram_ras_o(sdram_ras),
        .sdram_cas_o(sdram_cas),
        .sdram_we_o(sdram_we),
        .sdram_dqm_o(sdram_dqm),
        .sdram_addr_o(sdram_addr),
        .sdram_ba_o(sdram_ba),
        .sdram_data_output_o(sdram_data_out),
        .sdram_data_out_en_o(sdram_data_oe),
        .sdram_data_input_i(sdram_data_in)
    );

`ifndef SYNTHESIS
    always @(posedge clk) begin
        if (!rst && request_valid && request_compare_addr !== request_addr)
            $fatal(1, "SDRAM request timing replica diverged");
        if (!rst && request_count == 2'd2 &&
            request_tail_compare_addr !== request_tail_addr)
            $fatal(1, "SDRAM tail timing replica diverged");
        if (!rst && core_compare_valid) begin
            case (core_compare_select)
                2'b01, 2'b10: ;
                default: $fatal(1, "SDRAM compare source is not one-hot");
            endcase
        end
    end
`endif
endmodule

`default_nettype wire
