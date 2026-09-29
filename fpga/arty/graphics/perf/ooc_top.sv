// Out-of-context area wrapper for perf/run_ooc_q.sh: every processor input
// is a register loaded from one shift chain and every output folds into one
// XOR, so synthesis trims nothing.
`default_nettype none
module ooc_top (input wire clk, input wire reset, input wire sin, output reg sout);
    localparam integer IN_BITS = 3 + 32 + 11 + 32 + 11 + 32 + 1 + 32 + 32 + 1 + 32 + 32
        + 1 + 6 + 64 + 2 + 1 + 1 + 1 + 1 + 6 + 2 + 1;
    reg [IN_BITS-1:0] in_q;
    always @(posedge clk) in_q <= {in_q[IN_BITS-2:0], sin};
    wire [10:0] submission_consumer, completion_producer;
    wire busy, completion_irq, engine_reset_active, configuration_fault;
    wire [31:0] retired_fence, commands_submitted, commands_completed, commands_failed,
        backpressure_cycles, timeout_count, reset_count, last_fault_detail;
    wire [5:0] arid, awid; wire [31:0] araddr, awaddr; wire [7:0] arlen, awlen, wstrb;
    wire [2:0] arsize, awsize, arprot, awprot; wire [1:0] arburst, awburst;
    wire [3:0] arcache, arqos, awcache, awqos; wire arvalid, rready, awvalid, wlast, wvalid, bready;
    wire [63:0] wdata;
    astra_render_command_processor #(.CYCLES_PER_US(165)) dut (
        .clk(clk), .reset(reset),
        .enable(in_q[0]), .queue_rebase(in_q[1]), .soft_reset(in_q[2]),
        .submission_ring_offset(in_q[34:3]), .submission_producer(in_q[45:35]),
        .submission_consumer(submission_consumer),
        .completion_ring_offset(in_q[77:46]), .completion_producer(completion_producer),
        .completion_consumer(in_q[88:78]), .resource_generation(in_q[120:89]),
        .protected0_valid(in_q[121]), .protected0_offset(in_q[153:122]),
        .protected0_bytes(in_q[185:154]), .protected1_valid(in_q[186]),
        .protected1_offset(in_q[218:187]), .protected1_bytes(in_q[250:219]),
        .busy(busy), .completion_irq(completion_irq), .engine_reset_active(engine_reset_active),
        .configuration_fault(configuration_fault), .retired_fence(retired_fence),
        .commands_submitted(commands_submitted), .commands_completed(commands_completed),
        .commands_failed(commands_failed), .backpressure_cycles(backpressure_cycles),
        .timeout_count(timeout_count), .reset_count(reset_count), .last_fault_detail(last_fault_detail),
        .m_axi_arid(arid), .m_axi_araddr(araddr), .m_axi_arlen(arlen), .m_axi_arsize(arsize),
        .m_axi_arburst(arburst), .m_axi_arcache(arcache), .m_axi_arprot(arprot), .m_axi_arqos(arqos),
        .m_axi_arvalid(arvalid), .m_axi_arready(in_q[251]), .m_axi_rid(in_q[257:252]),
        .m_axi_rdata(in_q[321:258]), .m_axi_rresp(in_q[323:322]), .m_axi_rlast(in_q[324]),
        .m_axi_rvalid(in_q[325]), .m_axi_rready(rready),
        .m_axi_awid(awid), .m_axi_awaddr(awaddr), .m_axi_awlen(awlen), .m_axi_awsize(awsize),
        .m_axi_awburst(awburst), .m_axi_awcache(awcache), .m_axi_awprot(awprot), .m_axi_awqos(awqos),
        .m_axi_awvalid(awvalid), .m_axi_awready(in_q[326]), .m_axi_wdata(wdata), .m_axi_wstrb(wstrb),
        .m_axi_wlast(wlast), .m_axi_wvalid(wvalid), .m_axi_wready(in_q[327]),
        .m_axi_bid(in_q[333:328]), .m_axi_bresp(in_q[335:334]), .m_axi_bvalid(in_q[336]),
        .m_axi_bready(bready));
    always @(posedge clk) sout <= ^{submission_consumer, completion_producer, busy, completion_irq,
        engine_reset_active, configuration_fault, retired_fence, commands_submitted,
        commands_completed, commands_failed, backpressure_cycles, timeout_count, reset_count,
        last_fault_detail, arid, araddr, arlen, arsize, arburst, arcache, arprot, arqos, arvalid,
        rready, awid, awaddr, awlen, awsize, awburst, awcache, awprot, awqos, awvalid, wdata,
        wstrb, wlast, wvalid, bready};
endmodule
`default_nettype wire
