// Replays a captured DE25 render batch window through the command processor
// and the host-aperture router: the window is served from "host" memory at
// HOST_READ_LATENCY, everything else from Media RAM at READ_LATENCY.
//   +batch=<hex bytes of the window from offset 0x800000>
//   +commands=<n>  wait for n completions (default: batch header count)
`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module tb_astra_render_replay;
    parameter integer READ_LATENCY = 25;
    parameter integer WRITE_LATENCY = 25;
    parameter integer HOST_READ_LATENCY = 60;
    parameter integer MAX_CYCLES = 200000000;
    // The DE25 F2SDRAM path loses narrow read bursts longer than this.
    parameter integer HOST_LOST_BEATS = 128;
    localparam [31:0] ARENA_BASE = 32'h40000000;
    localparam [31:0] MEDIA_BYTES = 32'h01000000;
    localparam [31:0] WINDOW = 32'h00800000;
    localparam [31:0] HOST_PHYS = 32'hbcd00000;
    localparam integer BATCH_MAX = 32'h00800000;
    localparam integer BATCH_LOAD = 32'h00040000;

    reg clk = 1'b0;
    reg reset = 1'b1;
    always #3.03 clk = ~clk;

    reg [7:0] batch [0:BATCH_LOAD-1];
    reg [10:0] submission_producer = 11'd0;
    reg [31:0] submission_offset = 32'd0;
    reg [31:0] completion_offset = 32'd0;
    reg [31:0] generation = 32'd1;
    wire [10:0] submission_consumer, completion_producer;
    wire busy, completion_irq, engine_reset_active, configuration_fault;
    wire [31:0] retired_fence, commands_submitted, commands_completed,
        commands_failed, backpressure_cycles, timeout_count, reset_count,
        last_fault_detail;
    wire [2:0] arid, rid, awid, bid;
    wire [31:0] araddr, awaddr;
    wire [7:0] arlen, awlen, wstrb;
    wire [2:0] arsize, awsize, arprot, awprot;
    wire [1:0] arburst, awburst, rresp, bresp;
    wire [3:0] arcache, arqos, awcache, awqos;
    wire arvalid, arready, rlast, rvalid, rready, awvalid, awready;
    wire [63:0] rdata, wdata;
    wire wlast, wvalid, wready, bvalid, bready;

    astra_render_command_processor #(
        .ARENA_BASE(ARENA_BASE), .ARENA_LIMIT(32'h60000000),
        .AXI_ID_WIDTH(3), .CYCLES_PER_US(165)
    ) dut (
        .clk(clk), .reset(reset), .enable(!reset),
        .queue_rebase(1'b0), .soft_reset(1'b0),
        .submission_ring_offset(submission_offset),
        .submission_producer(submission_producer),
        .submission_consumer(submission_consumer),
        .completion_ring_offset(completion_offset),
        .completion_producer(completion_producer),
        .completion_consumer(completion_producer),
        .resource_generation(generation),
        .protected0_valid(1'b0), .protected0_offset(32'd0),
        .protected0_bytes(32'd0), .protected1_valid(1'b0),
        .protected1_offset(32'd0), .protected1_bytes(32'd0),
        .busy(busy), .completion_irq(completion_irq),
        .engine_reset_active(engine_reset_active),
        .configuration_fault(configuration_fault),
        .retired_fence(retired_fence),
        .commands_submitted(commands_submitted),
        .commands_completed(commands_completed),
        .commands_failed(commands_failed),
        .backpressure_cycles(backpressure_cycles),
        .timeout_count(timeout_count), .reset_count(reset_count),
        .last_fault_detail(last_fault_detail),
        .m_axi_arid(arid), .m_axi_araddr(araddr), .m_axi_arlen(arlen),
        .m_axi_arsize(arsize), .m_axi_arburst(arburst),
        .m_axi_arcache(arcache), .m_axi_arprot(arprot),
        .m_axi_arqos(arqos), .m_axi_arvalid(arvalid),
        .m_axi_arready(arready), .m_axi_rid(rid), .m_axi_rdata(rdata),
        .m_axi_rresp(rresp), .m_axi_rlast(rlast),
        .m_axi_rvalid(rvalid), .m_axi_rready(rready),
        .m_axi_awid(awid), .m_axi_awaddr(awaddr), .m_axi_awlen(awlen),
        .m_axi_awsize(awsize), .m_axi_awburst(awburst),
        .m_axi_awcache(awcache), .m_axi_awprot(awprot),
        .m_axi_awqos(awqos), .m_axi_awvalid(awvalid),
        .m_axi_awready(awready), .m_axi_wdata(wdata),
        .m_axi_wstrb(wstrb), .m_axi_wlast(wlast),
        .m_axi_wvalid(wvalid), .m_axi_wready(wready),
        .m_axi_bid(bid), .m_axi_bresp(bresp), .m_axi_bvalid(bvalid),
        .m_axi_bready(bready)
    );

    wire [2:0] m_arid, m_rid, h_arid, h_rid;
    wire [31:0] m_araddr, h_araddr;
    wire [7:0] m_arlen, h_arlen;
    wire [2:0] m_arsize, h_arsize;
    wire [1:0] m_rresp, h_rresp;
    wire m_arvalid, m_arready, m_rlast, m_rvalid, m_rready;
    wire h_arvalid, h_arready, h_rlast, h_rvalid, h_rready;
    wire [63:0] m_rdata, h_rdata;
    astra_render_host_reads #(.ARENA_BASE(ARENA_BASE), .AXI_ID_WIDTH(3))
    host_reads_i (
        .clk(clk), .reset(reset), .host_base(HOST_PHYS),
        .s_arid(arid), .s_araddr(araddr), .s_arlen(arlen),
        .s_arsize(arsize), .s_arburst(arburst), .s_arcache(arcache),
        .s_arprot(arprot), .s_arvalid(arvalid), .s_arready(arready),
        .s_rid(rid), .s_rdata(rdata), .s_rresp(rresp), .s_rlast(rlast),
        .s_rvalid(rvalid), .s_rready(rready),
        .m_arid(m_arid), .m_araddr(m_araddr), .m_arlen(m_arlen),
        .m_arsize(m_arsize), .m_arburst(), .m_arcache(), .m_arprot(),
        .m_arvalid(m_arvalid), .m_arready(m_arready), .m_rid(m_rid),
        .m_rdata(m_rdata), .m_rresp(m_rresp), .m_rlast(m_rlast),
        .m_rvalid(m_rvalid), .m_rready(m_rready),
        .h_arid(h_arid), .h_araddr(h_araddr), .h_arlen(h_arlen),
        .h_arsize(h_arsize), .h_arburst(), .h_arcache(), .h_arprot(),
        .h_arvalid(h_arvalid), .h_arready(h_arready), .h_rid(h_rid),
        .h_rdata(h_rdata), .h_rresp(h_rresp), .h_rlast(h_rlast),
        .h_rvalid(h_rvalid), .h_rready(h_rready)
    );
    astra_render_axi_latency_model #(
        .AXI_ID_WIDTH(3), .MEMORY_BYTES(BATCH_MAX), .BASE_ADDRESS(HOST_PHYS),
        .READ_LATENCY(HOST_READ_LATENCY), .LOST_READ_BEATS(HOST_LOST_BEATS)
    ) host_i (
        .clk(clk), .reset(reset),
        .s_axi_arid(h_arid), .s_axi_araddr(h_araddr), .s_axi_arlen(h_arlen),
        .s_axi_arvalid(h_arvalid), .s_axi_arready(h_arready),
        .s_axi_rid(h_rid), .s_axi_rdata(h_rdata), .s_axi_rresp(h_rresp),
        .s_axi_rlast(h_rlast), .s_axi_rvalid(h_rvalid),
        .s_axi_rready(h_rready),
        .s_axi_awid(3'd0), .s_axi_awaddr(32'd0), .s_axi_awlen(8'd0),
        .s_axi_awvalid(1'b0), .s_axi_awready(),
        .s_axi_wdata(64'd0), .s_axi_wstrb(8'd0), .s_axi_wlast(1'b0),
        .s_axi_wvalid(1'b0), .s_axi_wready(), .s_axi_bid(), .s_axi_bresp(),
        .s_axi_bvalid(), .s_axi_bready(1'b1),
        .read_transactions(), .write_transactions(), .read_beats(),
        .write_beats()
    );
    astra_render_axi_latency_model #(
        .AXI_ID_WIDTH(3), .MEMORY_BYTES(MEDIA_BYTES),
        .BASE_ADDRESS(ARENA_BASE),
        .READ_LATENCY(READ_LATENCY), .WRITE_LATENCY(WRITE_LATENCY)
    ) memory_i (
        .clk(clk), .reset(reset),
        .s_axi_arid(m_arid), .s_axi_araddr(m_araddr), .s_axi_arlen(m_arlen),
        .s_axi_arvalid(m_arvalid), .s_axi_arready(m_arready),
        .s_axi_rid(m_rid), .s_axi_rdata(m_rdata), .s_axi_rresp(m_rresp),
        .s_axi_rlast(m_rlast), .s_axi_rvalid(m_rvalid),
        .s_axi_rready(m_rready),
        .s_axi_awid(awid), .s_axi_awaddr(awaddr), .s_axi_awlen(awlen),
        .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(wstrb), .s_axi_wlast(wlast),
        .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bid(bid), .s_axi_bresp(bresp), .s_axi_bvalid(bvalid),
        .s_axi_bready(bready),
        .read_transactions(), .write_transactions(), .read_beats(),
        .write_beats()
    );

    // Every AR is logged with its port, size and alignment.
    integer trace = 0;
    always @(posedge clk) if (trace != 0) begin
        if (h_arvalid && h_arready)
            $display("%0t H AR id=%0d a=%h len=%0d size=%0d", $time, h_arid,
                     h_araddr, h_arlen, h_arsize);
        if (m_arvalid && m_arready)
            $display("%0t M AR id=%0d a=%h len=%0d size=%0d", $time, m_arid,
                     m_araddr, m_arlen, m_arsize);
    end

    function automatic [31:0] be32(input integer offset);
        be32 = {batch[offset], batch[offset + 1], batch[offset + 2],
                batch[offset + 3]};
    endfunction

    reg [8*256-1:0] batch_path;
    integer i, cycles, commands, last_completed;
    initial begin
        if (!$value$plusargs("batch=%s", batch_path))
            $fatal(1, "+batch=<hex> required");
        if ($test$plusargs("trace")) trace = 1;
        for (i = 0; i < BATCH_LOAD; i = i + 1) batch[i] = 8'h00;
        $readmemh(batch_path, batch);
        for (i = 0; i < BATCH_LOAD; i = i + 1)
            host_i.write_byte(HOST_PHYS + i, batch[i]);
        if (be32(0) != 32'h41524254)
            $fatal(1, "not a render batch window");
        commands = be32(12);
        if (!$value$plusargs("commands=%d", commands)) commands = be32(12);
        submission_offset = be32(16);
        completion_offset = be32(20);
        generation = be32(24);
        repeat (8) @(posedge clk);
        reset = 1'b0;
        repeat (8) @(posedge clk);
        submission_producer = be32(12);
        cycles = 0;
        last_completed = 0;
        while (completion_producer < commands && cycles < MAX_CYCLES) begin
            @(posedge clk);
            cycles = cycles + 1;
            if (commands_failed != 0) begin
                $display("replay: command failed: completed=%0d failed=%0d fault=%h",
                         commands_completed, commands_failed,
                         last_fault_detail);
                $fatal(1, "RENDER REPLAY COMMAND FAILED");
            end
            if (commands_completed != last_completed) begin
                $display("%0t completed=%0d failed=%0d state=%0d", $time,
                         commands_completed, commands_failed, dut.state);
                last_completed = commands_completed;
            end
        end
        $display("replay: host latency %0d, %0d cycles, submitted=%0d completed=%0d failed=%0d consumer=%0d busy=%0d state=%0d fault=%h",
                 HOST_READ_LATENCY, cycles, commands_submitted,
                 commands_completed, commands_failed, submission_consumer,
                 busy, dut.state, last_fault_detail);
        if (completion_producer < commands)
            $fatal(1, "RENDER REPLAY WEDGED");
        $display("RENDER REPLAY PASS");
        $finish;
    end
endmodule

`default_nettype wire
