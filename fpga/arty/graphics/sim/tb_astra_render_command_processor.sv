`timescale 1ns/1ps
`default_nettype none

`include "astra_render_protocol.vh"

module tb_astra_render_command_processor;
    localparam [31:0] ARENA_BYTES = 32'h00100000;
    localparam [31:0] SUBMISSION_OFFSET = 32'h00000000;
    localparam [31:0] COMPLETION_OFFSET = 32'h00010000;
    localparam [31:0] DESTINATION_DESCRIPTOR = 32'h00020000;
    localparam [31:0] SOURCE_DESCRIPTOR = 32'h00020020;
    localparam [31:0] AUXILIARY_DESCRIPTOR = 32'h00020040;
    localparam [31:0] DESTINATION_DATA = 32'h00030000;
    localparam [31:0] SOURCE_DATA = 32'h00040000;
    localparam [31:0] AUXILIARY_DATA = 32'h00050000;
    localparam [31:0] SOURCE_PALETTE = 32'h00060000;
    localparam [31:0] GLYPH_DESCRIPTORS = 32'h00070000;
    localparam [31:0] GENERATION = 32'h12345678;
    localparam [31:0] IGNORE_COUNT = 32'hffffffff;

    reg clk = 1'b0;
    reg reset = 1'b1;
    always #2.5 clk = ~clk;

    reg enable = 1'b1;
    reg queue_rebase = 1'b0;
    reg soft_reset = 1'b0;
    reg [31:0] submission_ring_offset = SUBMISSION_OFFSET;
    reg [10:0] submission_producer = 11'd0;
    wire [10:0] submission_consumer;
    reg [31:0] completion_ring_offset = COMPLETION_OFFSET;
    wire [10:0] completion_producer;
    reg [10:0] completion_consumer = 11'd0;
    reg [31:0] resource_generation = GENERATION;
    reg protected0_valid = 1'b0;
    reg [31:0] protected0_offset = 32'd0;
    reg [31:0] protected0_bytes = 32'd0;
    reg protected1_valid = 1'b0;
    reg [31:0] protected1_offset = 32'd0;
    reg [31:0] protected1_bytes = 32'd0;

    wire busy;
    wire completion_irq;
    wire engine_reset_active;
    wire configuration_fault;
    wire [31:0] retired_fence;
    wire [31:0] commands_submitted;
    wire [31:0] commands_completed;
    wire [31:0] commands_failed;
    wire [31:0] backpressure_cycles;
    wire [31:0] timeout_count;
    wire [31:0] reset_count;
    wire [31:0] last_fault_detail;

    wire [5:0] arid;
    wire [31:0] araddr;
    wire [7:0] arlen;
    wire [2:0] arsize;
    wire [1:0] arburst;
    wire [3:0] arcache;
    wire [2:0] arprot;
    wire [3:0] arqos;
    wire arvalid;
    wire arready;
    wire [5:0] rid;
    wire [63:0] rdata;
    wire [1:0] rresp;
    wire rlast;
    wire rvalid;
    wire rready;
    wire [5:0] awid;
    wire [31:0] awaddr;
    wire [7:0] awlen;
    wire [2:0] awsize;
    wire [1:0] awburst;
    wire [3:0] awcache;
    wire [2:0] awprot;
    wire [3:0] awqos;
    wire awvalid;
    wire awready;
    wire [63:0] wdata;
    wire [7:0] wstrb;
    wire wlast;
    wire wvalid;
    wire wready;
    wire [5:0] bid;
    wire [1:0] bresp;
    wire bvalid;
    wire bready;

    reg stall_reads = 1'b0;
    reg stall_writes = 1'b0;
    reg inject_read_error = 1'b0;
    reg inject_write_error = 1'b0;
    wire [31:0] read_transactions;
    wire [31:0] write_transactions;
    wire memory_reset = reset;

    astra_render_command_processor #(
        .ARENA_BASE(32'd0),
        .ARENA_LIMIT(ARENA_BYTES),
        .CYCLES_PER_US(1),
        .RESET_HOLD_CYCLES(4)
    ) dut (
        .clk(clk), .reset(reset), .enable(enable),
        .queue_rebase(queue_rebase), .soft_reset(soft_reset),
        .submission_ring_offset(submission_ring_offset),
        .submission_producer(submission_producer),
        .submission_consumer(submission_consumer),
        .completion_ring_offset(completion_ring_offset),
        .completion_producer(completion_producer),
        .completion_consumer(completion_consumer),
        .resource_generation(resource_generation),
        .protected0_valid(protected0_valid),
        .protected0_offset(protected0_offset),
        .protected0_bytes(protected0_bytes),
        .protected1_valid(protected1_valid),
        .protected1_offset(protected1_offset),
        .protected1_bytes(protected1_bytes),
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

    astra_render_axi_memory_model #(
        .MEMORY_BYTES(ARENA_BYTES)
    ) memory_i (
        .clk(clk), .reset(memory_reset), .stall_reads(stall_reads),
        .stall_writes(stall_writes),
        .inject_read_error(inject_read_error),
        .inject_write_error(inject_write_error),
        .s_axi_arid(arid), .s_axi_araddr(araddr), .s_axi_arlen(arlen),
        .s_axi_arsize(arsize), .s_axi_arburst(arburst),
        .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rid(rid), .s_axi_rdata(rdata), .s_axi_rresp(rresp),
        .s_axi_rlast(rlast), .s_axi_rvalid(rvalid),
        .s_axi_rready(rready), .s_axi_awid(awid),
        .s_axi_awaddr(awaddr), .s_axi_awlen(awlen),
        .s_axi_awsize(awsize), .s_axi_awburst(awburst),
        .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(wstrb),
        .s_axi_wlast(wlast), .s_axi_wvalid(wvalid),
        .s_axi_wready(wready), .s_axi_bid(bid),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid),
        .s_axi_bready(bready), .read_transactions(read_transactions),
        .write_transactions(write_transactions)
    );

    task automatic write_be32(input [31:0] address, input [31:0] value);
        begin
            memory_i.write_byte(address, value[31:24]);
            memory_i.write_byte(address + 32'd1, value[23:16]);
            memory_i.write_byte(address + 32'd2, value[15:8]);
            memory_i.write_byte(address + 32'd3, value[7:0]);
        end
    endtask

    function automatic [31:0] read_be32(input [31:0] address);
        begin
            read_be32 = {memory_i.read_byte(address),
                         memory_i.read_byte(address + 32'd1),
                         memory_i.read_byte(address + 32'd2),
                         memory_i.read_byte(address + 32'd3)};
        end
    endfunction

    task automatic check_byte(input [31:0] address, input [7:0] expected);
        reg [7:0] actual;
        begin
            actual = memory_i.read_byte(address);
            if (actual !== expected)
                $fatal(1, "memory[%08x]=%02x expected=%02x",
                       address, actual, expected);
        end
    endtask

    task automatic write_surface(
        input [31:0] descriptor,
        input [31:0] generation,
        input [31:0] data_offset,
        input [31:0] data_bytes,
        input [31:0] pitch,
        input [15:0] width,
        input [15:0] height,
        input [7:0] format,
        input [7:0] flags
    );
        begin
            write_be32(descriptor, (`ASTRA_RENDER_ABI_VERSION << 16) |
                       `ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES);
            write_be32(descriptor + 32'd4, generation);
            write_be32(descriptor + 32'd8, data_offset);
            write_be32(descriptor + 32'd12, data_bytes);
            write_be32(descriptor + 32'd16, pitch);
            write_be32(descriptor + 32'd20, {width, height});
            write_be32(descriptor + 32'd24, {format, flags, 16'd0});
            write_be32(descriptor + 32'd28, 32'd0);
        end
    endtask

    function automatic [31:0] command_address(input [10:0] pointer);
        begin
            command_address = SUBMISSION_OFFSET +
                ({22'd0, pointer[9:0]} << 6);
        end
    endfunction

    task automatic write_command_word(
        input [10:0] pointer,
        input integer word_number,
        input [31:0] value
    );
        begin
            write_be32(command_address(pointer) + word_number * 4, value);
        end
    endtask

    task automatic clear_command(input [10:0] pointer);
        integer word_number;
        begin
            for (word_number = 0; word_number < 16;
                 word_number = word_number + 1)
                write_command_word(pointer, word_number, 32'd0);
        end
    endtask

    task automatic write_fill_command(
        input [10:0] pointer,
        input [31:0] command_sequence,
        input [31:0] generation,
        input [31:0] deadline_us,
        input [31:0] descriptor,
        input signed [15:0] destination_x,
        input signed [15:0] destination_y,
        input [15:0] width,
        input [15:0] height,
        input [31:0] color
    );
        begin
            clear_command(pointer);
            write_command_word(pointer, 0,
                (`ASTRA_RENDER_ABI_VERSION << 16) |
                `ASTRA_RENDER_COMMAND_BYTES);
            write_command_word(pointer, 1,
                `ASTRA_RENDER_OP_FILL << 16);
            write_command_word(pointer, 2, command_sequence);
            write_command_word(pointer, 3, generation);
            write_command_word(pointer, 4, deadline_us);
            write_command_word(pointer, 6, {16'sd0, 16'sd0});
            write_command_word(pointer, 7, {16'sd16, 16'sd16});
            write_command_word(pointer, 8, descriptor);
            write_command_word(pointer, 12,
                {destination_x, destination_y});
            write_command_word(pointer, 14, {width, height});
            write_command_word(pointer, 15, color);
        end
    endtask

    task automatic write_blit_command(
        input [10:0] pointer,
        input [31:0] command_sequence,
        input [31:0] deadline_us,
        input [31:0] destination_descriptor,
        input [31:0] source_descriptor,
        input signed [15:0] source_x,
        input signed [15:0] source_y,
        input signed [15:0] destination_x,
        input signed [15:0] destination_y,
        input [15:0] source_width,
        input [15:0] source_height,
        input [15:0] destination_width,
        input [15:0] destination_height
    );
        begin
            clear_command(pointer);
            write_command_word(pointer, 0,
                (`ASTRA_RENDER_ABI_VERSION << 16) |
                `ASTRA_RENDER_COMMAND_BYTES);
            write_command_word(pointer, 1,
                `ASTRA_RENDER_OP_BLIT << 16);
            write_command_word(pointer, 2, command_sequence);
            write_command_word(pointer, 3, GENERATION);
            write_command_word(pointer, 4, deadline_us);
            write_command_word(pointer, 6, {16'sd0, 16'sd0});
            write_command_word(pointer, 7, {16'sd16, 16'sd16});
            write_command_word(pointer, 8, destination_descriptor);
            write_command_word(pointer, 9, source_descriptor);
            write_command_word(pointer, 11, {source_x, source_y});
            write_command_word(pointer, 12,
                {destination_x, destination_y});
            write_command_word(pointer, 13,
                {source_width, source_height});
            write_command_word(pointer, 14,
                {destination_width, destination_height});
        end
    endtask

    task automatic write_geometry_command(
        input [10:0] pointer,
        input [31:0] command_sequence,
        input [15:0] opcode,
        input [15:0] flags,
        input signed [15:0] p0_x,
        input signed [15:0] p0_y,
        input signed [15:0] p1_x,
        input signed [15:0] p1_y,
        input [31:0] radii_or_origin,
        input [63:0] pattern,
        input [31:0] background,
        input [31:0] foreground
    );
        begin
            clear_command(pointer);
            write_command_word(pointer, 0,
                (`ASTRA_RENDER_ABI_VERSION << 16) |
                `ASTRA_RENDER_COMMAND_BYTES);
            write_command_word(pointer, 1, {opcode, flags});
            write_command_word(pointer, 2, command_sequence);
            write_command_word(pointer, 3, GENERATION);
            write_command_word(pointer, 4, 32'd100000);
            write_command_word(pointer, 6, {16'sd0, 16'sd0});
            write_command_word(pointer, 7, {16'sd16, 16'sd16});
            write_command_word(pointer, 8, DESTINATION_DESCRIPTOR);
            write_command_word(pointer, 9, pattern[63:32]);
            write_command_word(pointer, 10, pattern[31:0]);
            write_command_word(pointer, 11, {p0_x, p0_y});
            write_command_word(pointer, 12, {p1_x, p1_y});
            write_command_word(pointer, 13, radii_or_origin);
            write_command_word(pointer, 14, background);
            write_command_word(pointer, 15, foreground);
        end
    endtask

    task automatic write_flood_command(
        input [10:0] pointer,
        input [31:0] command_sequence,
        input [31:0] destination_descriptor,
        input [31:0] workspace_descriptor,
        input signed [15:0] seed_x,
        input signed [15:0] seed_y,
        input [31:0] replacement
    );
        begin
            clear_command(pointer);
            write_command_word(pointer, 0,
                (`ASTRA_RENDER_ABI_VERSION << 16) |
                `ASTRA_RENDER_COMMAND_BYTES);
            write_command_word(pointer, 1,
                `ASTRA_RENDER_OP_FLOOD_FILL << 16);
            write_command_word(pointer, 2, command_sequence);
            write_command_word(pointer, 3, GENERATION);
            write_command_word(pointer, 4, 32'd100000);
            write_command_word(pointer, 6, {16'sd0, 16'sd0});
            write_command_word(pointer, 7, {16'sd16, 16'sd16});
            write_command_word(pointer, 8, destination_descriptor);
            write_command_word(pointer, 10, workspace_descriptor);
            write_command_word(pointer, 11, {seed_x, seed_y});
            write_command_word(pointer, 15, replacement);
        end
    endtask

    task automatic write_glyph_command(
        input [10:0] pointer,
        input [31:0] command_sequence,
        input [15:0] flags,
        input [31:0] destination_descriptor,
        input [31:0] source_descriptor,
        input [31:0] descriptor_array,
        input [12:0] descriptor_count,
        input [31:0] foreground,
        input [31:0] background,
        input [7:0] transparent_index
    );
        begin
            clear_command(pointer);
            write_command_word(pointer, 0,
                (`ASTRA_RENDER_ABI_VERSION << 16) |
                `ASTRA_RENDER_COMMAND_BYTES);
            write_command_word(pointer, 1,
                (`ASTRA_RENDER_OP_GLYPH_RUN << 16) | flags);
            write_command_word(pointer, 2, command_sequence);
            write_command_word(pointer, 3, GENERATION);
            write_command_word(pointer, 4, 32'd100000);
            write_command_word(pointer, 6, {16'sd0, 16'sd0});
            write_command_word(pointer, 7, {16'sd16, 16'sd16});
            write_command_word(pointer, 8, destination_descriptor);
            write_command_word(pointer, 9, source_descriptor);
            write_command_word(pointer, 10, descriptor_array);
            write_command_word(pointer, 11, {19'd0, descriptor_count});
            write_command_word(pointer, 12, foreground);
            write_command_word(pointer, 13, background);
            write_command_word(pointer, 14, {24'd0, transparent_index});
        end
    endtask

    task automatic write_glyph_descriptor(
        input [31:0] address,
        input [31:0] source_offset,
        input [15:0] source_x,
        input [15:0] source_y,
        input signed [15:0] destination_x,
        input signed [15:0] destination_y,
        input [15:0] width,
        input [15:0] height
    );
        begin
            write_be32(address, source_offset);
            write_be32(address + 32'd4, {source_x, source_y});
            write_be32(address + 32'd8,
                       {destination_x, destination_y});
            write_be32(address + 32'd12, {width, height});
        end
    endtask

    task automatic pulse_rebase;
        begin
            @(negedge clk);
            queue_rebase = 1'b1;
            @(negedge clk);
            queue_rebase = 1'b0;
            repeat (2) @(posedge clk);
        end
    endtask

    task automatic publish(input [10:0] new_producer);
        begin
            @(negedge clk);
            submission_producer = new_producer;
        end
    endtask

    task automatic wait_for_state(input [5:0] expected_state);
        integer elapsed;
        begin
            elapsed = 0;
            while (dut.state != expected_state) begin
                @(negedge clk);
                elapsed = elapsed + 1;
                if (elapsed > 200000)
                    $fatal(1, "state timeout expected=%0d actual=%0d",
                           expected_state, dut.state);
            end
        end
    endtask

    task automatic wait_for_completion(
        input [10:0] expected_producer,
        input [15:0] expected_opcode,
        input [15:0] expected_status,
        input [31:0] expected_sequence,
        input [31:0] expected_count,
        input [31:0] expected_generation
    );
        integer elapsed;
        reg [31:0] address;
        reg [31:0] actual;
        begin
            elapsed = 0;
            while (completion_producer != expected_producer) begin
                @(posedge clk);
                elapsed = elapsed + 1;
                if (elapsed > 300000)
                    $fatal(1, "completion timeout state=%0d sub=%0d/%0d comp=%0d/%0d reset=%0d",
                           dut.state, submission_consumer,
                           submission_producer, completion_producer,
                           expected_producer, engine_reset_active);
            end
            @(negedge clk);
            address = COMPLETION_OFFSET +
                ({22'd0, (expected_producer - 11'd1) & 11'h3ff} << 5);
            actual = read_be32(address);
            if (actual !== ((`ASTRA_RENDER_ABI_VERSION << 16) |
                            `ASTRA_RENDER_COMPLETION_BYTES))
                $fatal(1, "completion header=%08x", actual);
            actual = read_be32(address + 32'd4);
            if (actual !== {expected_opcode, expected_status})
                $fatal(1, "completion opcode/status=%08x expected=%04x/%04x sequence=%08x state=%0d blitter=%0d writer=%0d ingress=%0d staged=%0d",
                       actual, expected_opcode, expected_status,
                       expected_sequence, dut.state, dut.blitter_i.state,
                       dut.writer_busy, dut.pixel_writer_i.ingress_valid,
                       dut.pixel_writer_i.pixel_stage_count);
            actual = read_be32(address + 32'd8);
            if (actual !== expected_sequence)
                $fatal(1, "completion sequence=%08x expected=%08x",
                       actual, expected_sequence);
            actual = read_be32(address + 32'd12);
            if (expected_count != IGNORE_COUNT && actual !== expected_count)
                $fatal(1, "completion count=%0d expected=%0d status=%0d",
                       actual, expected_count, expected_status);
            if (read_be32(address + 32'd20) <
                read_be32(address + 32'd16))
                $fatal(1, "completion timestamps reversed");
            actual = read_be32(address + 32'd28);
            if (actual !== expected_generation)
                $fatal(1, "completion generation=%08x expected=%08x",
                       actual, expected_generation);
            completion_consumer = expected_producer;
        end
    endtask

    integer row;
    integer column;
    integer sub_pointer;
    integer comp_pointer;
    integer before_reads;
    integer before_backpressure;
    integer before_timeouts;
    integer before_resets;
    integer before_blitter_dispatches;
    integer before_geometry_dispatches;
    integer before_flood_dispatches;
    integer before_glyph_dispatches;
    integer blitter_dispatches = 0;
    integer geometry_dispatches = 0;
    integer flood_dispatches = 0;
    integer glyph_dispatches = 0;
    reg [7:0] expected_byte;

    always @(posedge clk) begin
        if (reset)
            blitter_dispatches <= 0;
        else if (dut.blitter_start)
            blitter_dispatches <= blitter_dispatches + 1;
    end

    always @(posedge clk) begin
        if (reset)
            flood_dispatches <= 0;
        else if (dut.flood_start)
            flood_dispatches <= flood_dispatches + 1;
    end

    always @(posedge clk) begin
        if (reset)
            geometry_dispatches <= 0;
        else if (dut.geometry_start)
            geometry_dispatches <= geometry_dispatches + 1;
    end

    always @(posedge clk) begin
        if (reset)
            glyph_dispatches <= 0;
        else if (dut.glyph_start)
            glyph_dispatches <= glyph_dispatches + 1;
    end

    initial begin
        memory_i.clear_memory(8'ha5);
        repeat (8) @(posedge clk);
        reset = 1'b0;

        // The command address is deterministic before admission decides
        // whether work is available. Preload it at the existing combine
        // boundary so queue occupancy does not drive a 32-bit register CE.
        force dut.state = 6'd54;
        force dut.submission_command_address_q = 32'h02001240;
        @(posedge clk);
        #1;
        if (dut.manager_araddr != 32'h02001240)
            $fatal(1, "admission address was not preloaded");
        release dut.submission_command_address_q;
        release dut.state;
        reset = 1'b1;
        repeat (2) @(posedge clk);
        reset = 1'b0;

        // The shared response boundary must absorb one additional beat even
        // while the selected engine is stalled. This keeps HP2 RREADY local
        // to registered storage instead of feeding engine decode back into
        // the PS-facing slice.
        force dut.command_dispatched_q = 1'b1;
        force dut.command_is_geometry_q = 1'b0;
        force dut.command_is_flood_q = 1'b0;
        force dut.command_is_glyph_q = 1'b1;
        force dut.engine_response_valid_q = 1'b1;
        force dut.glyph_rready = 1'b0;
        #1;
        if (!rready)
            $fatal(1, "engine response boundary did not absorb stalled beat");
        force dut.engine_response_spill_valid_q = 1'b1;
        #1;
        if (rready)
            $fatal(1, "engine response boundary accepted beyond capacity");
        release dut.engine_response_spill_valid_q;
        release dut.glyph_rready;
        release dut.engine_response_valid_q;
        release dut.command_is_glyph_q;
        release dut.command_is_flood_q;
        release dut.command_is_geometry_q;
        release dut.command_dispatched_q;
        reset = 1'b1;
        repeat (2) @(posedge clk);
        reset = 1'b0;

        pulse_rebase();
        sub_pointer = 0;
        comp_pointer = 0;

        // End-to-end INDEX8 fill through both rings and both AXI paths.
        write_surface(DESTINATION_DESCRIPTOR, GENERATION,
            DESTINATION_DATA, 32'd256, 32'd16, 16'd16, 16'd16,
            `ASTRA_RENDER_FORMAT_INDEX8, `ASTRA_RENDER_SURFACE_WRITE);
        write_fill_command(sub_pointer[10:0], 32'd1, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR, 16'sd2, 16'sd3, 16'd4, 16'd3,
            32'h0000005a);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_OK, 32'd1, 32'd12, GENERATION);
        for (row = 0; row < 16; row = row + 1)
            for (column = 0; column < 16; column = column + 1) begin
                expected_byte = row >= 3 && row < 6 &&
                                column >= 2 && column < 6 ? 8'h5a : 8'ha5;
                check_byte(DESTINATION_DATA + row * 16 + column,
                           expected_byte);
            end
        if (retired_fence != 32'd1)
            $fatal(1, "first fence did not retire");

        // Every v1 destination format traverses the command path in canonical
        // big-endian byte order.
        write_surface(DESTINATION_DESCRIPTOR, GENERATION,
            DESTINATION_DATA + 32'h1000, 32'd512, 32'd32,
            16'd16, 16'd16, `ASTRA_RENDER_FORMAT_RGB565,
            `ASTRA_RENDER_SURFACE_WRITE);
        write_fill_command(sub_pointer[10:0], 32'd2, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR, 16'sd1, 16'sd1, 16'd1, 16'd1,
            32'h00001234);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_OK, 32'd2, 32'd1, GENERATION);
        check_byte(DESTINATION_DATA + 32'h1022, 8'h12);
        check_byte(DESTINATION_DATA + 32'h1023, 8'h34);

        write_surface(DESTINATION_DESCRIPTOR, GENERATION,
            DESTINATION_DATA + 32'h2000, 32'd1024, 32'd64,
            16'd16, 16'd16, `ASTRA_RENDER_FORMAT_XRGB8888,
            `ASTRA_RENDER_SURFACE_WRITE);
        write_fill_command(sub_pointer[10:0], 32'd3, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR, 16'sd0, 16'sd0, 16'd1, 16'd1,
            32'hff112233);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_OK, 32'd3, 32'd1, GENERATION);
        check_byte(DESTINATION_DATA + 32'h2000, 8'hff);
        check_byte(DESTINATION_DATA + 32'h2001, 8'h11);
        check_byte(DESTINATION_DATA + 32'h2002, 8'h22);
        check_byte(DESTINATION_DATA + 32'h2003, 8'h33);

        write_surface(DESTINATION_DESCRIPTOR, GENERATION,
            DESTINATION_DATA + 32'h3000, 32'd1024, 32'd64,
            16'd16, 16'd16, `ASTRA_RENDER_FORMAT_ARGB8888,
            `ASTRA_RENDER_SURFACE_WRITE);
        write_fill_command(sub_pointer[10:0], 32'd4, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR, 16'sd0, 16'sd0, 16'd1, 16'd1,
            32'h80402010);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_OK, 32'd4, 32'd1, GENERATION);
        check_byte(DESTINATION_DATA + 32'h3000, 8'h80);
        check_byte(DESTINATION_DATA + 32'h3001, 8'h40);
        check_byte(DESTINATION_DATA + 32'h3002, 8'h20);
        check_byte(DESTINATION_DATA + 32'h3003, 8'h10);

        // Same-surface overlap must traverse backward like memmove.
        write_surface(DESTINATION_DESCRIPTOR, GENERATION,
            DESTINATION_DATA, 32'd16, 32'd16, 16'd16, 16'd1,
            `ASTRA_RENDER_FORMAT_INDEX8,
            `ASTRA_RENDER_SURFACE_READ | `ASTRA_RENDER_SURFACE_WRITE);
        for (column = 0; column < 16; column = column + 1)
            memory_i.write_byte(DESTINATION_DATA + column, column[7:0]);
        write_blit_command(sub_pointer[10:0], 32'd5, 32'd1000,
            DESTINATION_DESCRIPTOR, DESTINATION_DESCRIPTOR,
            16'sd0, 16'sd0, 16'sd2, 16'sd0,
            16'd8, 16'd1, 16'd8, 16'd1);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_BLIT,
            `ASTRA_RENDER_STATUS_OK, 32'd5, 32'd8, GENERATION);
        check_byte(DESTINATION_DATA + 32'd0, 8'd0);
        check_byte(DESTINATION_DATA + 32'd1, 8'd1);
        for (column = 0; column < 8; column = column + 1)
            check_byte(DESTINATION_DATA + 32'd2 + column, column[7:0]);

        // Rejections before dispatch must not inherit the prior pixel count.
        write_fill_command(sub_pointer[10:0], 32'd6, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR, 16'sd0, 16'sd0, 16'd1, 16'd1, 32'h44);
        write_command_word(sub_pointer[10:0], 0,
            (32'd2 << 16) | `ASTRA_RENDER_COMMAND_BYTES);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_BAD_VERSION, 32'd6, 32'd0, GENERATION);

        write_fill_command(sub_pointer[10:0], 32'd7, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR, 16'sd0, 16'sd0, 16'd1, 16'd1, 32'h44);
        write_command_word(sub_pointer[10:0], 0,
            (`ASTRA_RENDER_ABI_VERSION << 16) | 32'd60);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_BAD_SIZE, 32'd7, 32'd0, GENERATION);

        write_fill_command(sub_pointer[10:0], 32'd8, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR, 16'sd0, 16'sd0, 16'd1, 16'd1, 32'h44);
        write_command_word(sub_pointer[10:0], 1, 32'h77770000);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], 16'h7777,
            `ASTRA_RENDER_STATUS_BAD_OPCODE, 32'd8, 32'd0, GENERATION);

        write_fill_command(sub_pointer[10:0], 32'd9, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR, 16'sd0, 16'sd0, 16'd1, 16'd1, 32'h44);
        write_command_word(sub_pointer[10:0], 1,
            (`ASTRA_RENDER_OP_FILL << 16) | 32'd1);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_BAD_FLAGS, 32'd9, 32'd0, GENERATION);

        write_fill_command(sub_pointer[10:0], 32'd5, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR, 16'sd0, 16'sd0, 16'd1, 16'd1, 32'h44);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_BAD_SEQUENCE, 32'd5, 32'd0, GENERATION);

        write_fill_command(sub_pointer[10:0], 32'd10, GENERATION + 32'd1,
            32'd1000, DESTINATION_DESCRIPTOR, 16'sd0, 16'sd0,
            16'd1, 16'd1, 32'h44);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_BAD_GENERATION, 32'd10, 32'd0,
            GENERATION + 32'd1);

        write_fill_command(sub_pointer[10:0], 32'd11, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR, 16'sd0, 16'sd0, 16'd1, 16'd1, 32'h44);
        write_command_word(sub_pointer[10:0], 6, {16'sd8, 16'sd0});
        write_command_word(sub_pointer[10:0], 7, {16'sd4, 16'sd16});
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_BAD_CLIP, 32'd11, 32'd0, GENERATION);

        write_fill_command(sub_pointer[10:0], 32'd12, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR + 32'd1, 16'sd0, 16'sd0,
            16'd1, 16'd1, 32'h44);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_BAD_DESCRIPTOR, 32'd12, 32'd0,
            GENERATION);

        write_surface(DESTINATION_DESCRIPTOR, GENERATION + 32'd1,
            DESTINATION_DATA, 32'd256, 32'd16, 16'd16, 16'd16,
            `ASTRA_RENDER_FORMAT_INDEX8, `ASTRA_RENDER_SURFACE_WRITE);
        write_fill_command(sub_pointer[10:0], 32'd13, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR, 16'sd0, 16'sd0, 16'd1, 16'd1, 32'h44);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_BAD_DESCRIPTOR, 32'd13, 32'd0,
            GENERATION);

        // Active scanout allocations are protected from asynchronous writes.
        write_surface(DESTINATION_DESCRIPTOR, GENERATION,
            DESTINATION_DATA, 32'd256, 32'd16, 16'd16, 16'd16,
            `ASTRA_RENDER_FORMAT_INDEX8, `ASTRA_RENDER_SURFACE_WRITE);
        protected0_valid = 1'b1;
        protected0_offset = DESTINATION_DATA;
        protected0_bytes = 32'd256;
        write_fill_command(sub_pointer[10:0], 32'd14, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR, 16'sd0, 16'sd0, 16'd1, 16'd1, 32'h44);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_BAD_RANGE, 32'd14, 32'd0, GENERATION);
        protected0_valid = 1'b0;

        // Distinct descriptors may not alias.
        write_surface(DESTINATION_DESCRIPTOR, GENERATION,
            DESTINATION_DATA, 32'd256, 32'd16, 16'd16, 16'd16,
            `ASTRA_RENDER_FORMAT_INDEX8, `ASTRA_RENDER_SURFACE_WRITE);
        write_surface(SOURCE_DESCRIPTOR, GENERATION,
            DESTINATION_DATA + 32'd128, 32'd256, 32'd16,
            16'd16, 16'd16, `ASTRA_RENDER_FORMAT_INDEX8,
            `ASTRA_RENDER_SURFACE_READ);
        write_blit_command(sub_pointer[10:0], 32'd15, 32'd1000,
            DESTINATION_DESCRIPTOR, SOURCE_DESCRIPTOR,
            16'sd0, 16'sd0, 16'sd0, 16'sd0,
            16'd1, 16'd1, 16'd1, 16'd1);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_BLIT,
            `ASTRA_RENDER_STATUS_BAD_RANGE, 32'd15, 32'd0, GENERATION);

        write_surface(SOURCE_DESCRIPTOR, GENERATION, SOURCE_DATA,
            32'd256, 32'd16, 16'd16, 16'd16,
            `ASTRA_RENDER_FORMAT_INDEX8, `ASTRA_RENDER_SURFACE_READ);
        for (row = 0; row < 4; row = row + 1)
            for (column = 0; column < 4; column = column + 1)
                memory_i.write_byte(SOURCE_DATA + row * 16 + column,
                                    row * 16 + column);
        write_blit_command(sub_pointer[10:0], 32'd16, 32'd1000,
            DESTINATION_DESCRIPTOR, SOURCE_DESCRIPTOR,
            16'sd0, 16'sd0, 16'sd0, 16'sd0,
            16'd4, 16'd4, 16'd8, 16'd8);
        write_command_word(sub_pointer[10:0], 1,
            (`ASTRA_RENDER_OP_BLIT << 16) |
            `ASTRA_RENDER_FLAG_BLIT_REFLECT_X |
            `ASTRA_RENDER_FLAG_BLIT_REFLECT_Y);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_BLIT,
            `ASTRA_RENDER_STATUS_OK, 32'd16, 32'd64, GENERATION);
        for (row = 0; row < 8; row = row + 1)
            for (column = 0; column < 8; column = column + 1) begin
                expected_byte = (3 - row / 2) * 16 +
                                (3 - column / 2);
                if (memory_i.read_byte(DESTINATION_DATA + row * 16 + column)
                    !== expected_byte)
                    $fatal(1, "scaled command pixel (%0d,%0d) mismatch",
                           column, row);
            end

        // An untrusted command fetch never dispatches and reports no guessed
        // opcode or sequence.
        write_fill_command(sub_pointer[10:0], 32'd17, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR, 16'sd0, 16'sd0, 16'd1, 16'd1, 32'h44);
        inject_read_error = 1'b1;
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_state(6'd15);
        inject_read_error = 1'b0;
        wait_for_completion(comp_pointer[10:0], 16'd0,
            `ASTRA_RENDER_STATUS_AXI_READ, 32'd0, 32'd0, GENERATION);

        // Destination response errors become failed completions; the ring
        // itself remains writable after the error is contained.
        write_fill_command(sub_pointer[10:0], 32'd17, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR, 16'sd0, 16'sd0, 16'd4, 16'd4, 32'h55);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_state(6'd14);
        inject_write_error = 1'b1;
        wait_for_state(6'd15);
        inject_write_error = 1'b0;
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_AXI_WRITE, 32'd17, IGNORE_COUNT,
            GENERATION);

        // A stalled engine times out, resets only its AXI paths, emits one
        // completion, and accepts the next command.
        before_timeouts = timeout_count;
        before_resets = reset_count;
        write_fill_command(sub_pointer[10:0], 32'd18, GENERATION, 32'd200,
            DESTINATION_DESCRIPTOR, 16'sd0, 16'sd0, 16'd16, 16'd16,
            32'h66);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_state(6'd14);
        wait (dut.pixel_writer_i.outstanding_count != 0);
        stall_writes = 1'b1;
        wait (engine_reset_active == 1'b1);
        stall_writes = 1'b0;
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_TIMEOUT, 32'd18, 32'd0, GENERATION);
        if (timeout_count != before_timeouts + 1 ||
            reset_count != before_resets + 1)
            $fatal(1, "timeout/reset counters did not advance exactly once");

        // Explicit cancellation follows the same recovery path with RESET.
        write_fill_command(sub_pointer[10:0], 32'd19, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR, 16'sd0, 16'sd0, 16'd16, 16'd16,
            32'h77);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_state(6'd14);
        wait (dut.pixel_writer_i.outstanding_count != 0);
        stall_writes = 1'b1;
        @(negedge clk);
        soft_reset = 1'b1;
        @(negedge clk);
        soft_reset = 1'b0;
        wait (engine_reset_active == 1'b1);
        stall_writes = 1'b0;
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_RESET, 32'd19, 32'd0, GENERATION);

        // Cancellation during validation is still pre-dispatch even though
        // the validation states are numerically above ST_EXECUTE.
        write_fill_command(sub_pointer[10:0], 32'd20, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR, 16'sd0, 16'sd0, 16'd1, 16'd1, 32'h78);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_state(6'd29);
        soft_reset = 1'b1;
        @(negedge clk);
        soft_reset = 1'b0;
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_RESET, 32'd20, 32'd0, GENERATION);

        // A full completion ring applies backpressure without reading the
        // submission or overwriting the unread completion.
        write_fill_command(sub_pointer[10:0], 32'd21, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR, 16'sd0, 16'sd0, 16'd1, 16'd1, 32'h88);
        completion_consumer = (comp_pointer + 1024) & 2047;
        before_reads = read_transactions;
        before_backpressure = backpressure_cycles;
        sub_pointer = sub_pointer + 1;
        publish(sub_pointer[10:0]);
        repeat (40) @(posedge clk);
        if (read_transactions != before_reads ||
            completion_producer != comp_pointer[10:0] ||
            backpressure_cycles <= before_backpressure)
            $fatal(1, "completion-full backpressure failed");
        completion_consumer = comp_pointer[10:0];
        comp_pointer = comp_pointer + 1;
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_OK, 32'd21, 32'd1, GENERATION);

        // Palette and MASK1 are independently validated resources and compose
        // through the same alpha path used by direct-color blits.
        write_surface(DESTINATION_DESCRIPTOR, GENERATION,
            DESTINATION_DATA, 32'd256, 32'd64, 16'd16, 16'd4,
            `ASTRA_RENDER_FORMAT_XRGB8888, `ASTRA_RENDER_SURFACE_WRITE);
        write_surface(SOURCE_DESCRIPTOR, GENERATION,
            SOURCE_DATA, 32'd256, 32'd16, 16'd16, 16'd16,
            `ASTRA_RENDER_FORMAT_INDEX8, `ASTRA_RENDER_SURFACE_READ);
        write_be32(SOURCE_DESCRIPTOR + 32'd28, SOURCE_PALETTE);
        write_surface(AUXILIARY_DESCRIPTOR, GENERATION,
            AUXILIARY_DATA, 32'd8, 32'd2, 16'd16, 16'd4,
            `ASTRA_RENDER_FORMAT_MASK1, `ASTRA_RENDER_SURFACE_READ);
        memory_i.write_byte(SOURCE_DATA, 8'h02);
        memory_i.write_byte(AUXILIARY_DATA, 8'h80);
        write_be32(SOURCE_PALETTE + 32'd8, 32'h80800000);
        write_be32(DESTINATION_DATA, 32'hff0000ff);
        write_blit_command(sub_pointer[10:0], 32'd22, 32'd1000,
            DESTINATION_DESCRIPTOR, SOURCE_DESCRIPTOR,
            16'sd0, 16'sd0, 16'sd0, 16'sd0,
            16'd1, 16'd1, 16'd1, 16'd1);
        write_command_word(sub_pointer[10:0], 1,
            (`ASTRA_RENDER_OP_BLIT << 16) |
            `ASTRA_RENDER_FLAG_BLIT_PALETTE |
            `ASTRA_RENDER_FLAG_BLIT_MASK1 |
            `ASTRA_RENDER_FLAG_BLIT_ALPHA);
        write_command_word(sub_pointer[10:0], 10, AUXILIARY_DESCRIPTOR);
        write_command_word(sub_pointer[10:0], 15, 32'h80000000);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_BLIT,
            `ASTRA_RENDER_STATUS_OK, 32'd22, 32'd1, GENERATION);
        if (read_be32(DESTINATION_DATA) != 32'hff4000bf)
            $fatal(1, "palette/mask/alpha command result=%08x",
                   read_be32(DESTINATION_DATA));

        // A MASK1 request without an auxiliary descriptor is rejected before
        // dispatch. It must not inherit or modify the prior destination.
        write_surface(SOURCE_DESCRIPTOR, GENERATION,
            SOURCE_DATA, 32'd256, 32'd16, 16'd16, 16'd16,
            `ASTRA_RENDER_FORMAT_INDEX8, `ASTRA_RENDER_SURFACE_READ);
        write_be32(DESTINATION_DATA, 32'h11223344);
        before_blitter_dispatches = blitter_dispatches;
        write_blit_command(sub_pointer[10:0], 32'd23, 32'd1000,
            DESTINATION_DESCRIPTOR, SOURCE_DESCRIPTOR,
            16'sd0, 16'sd0, 16'sd0, 16'sd0,
            16'd1, 16'd1, 16'd1, 16'd1);
        write_command_word(sub_pointer[10:0], 1,
            (`ASTRA_RENDER_OP_BLIT << 16) |
            `ASTRA_RENDER_FLAG_BLIT_MASK1);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_BLIT,
            `ASTRA_RENDER_STATUS_BAD_FLAGS, 32'd23, 32'd0, GENERATION);
        if (blitter_dispatches != before_blitter_dispatches ||
            read_be32(DESTINATION_DATA) != 32'h11223344)
            $fatal(1, "missing MASK1 descriptor reached pixel DMA");

        // The auxiliary attachment must be a valid MASK1 read surface.
        write_surface(AUXILIARY_DESCRIPTOR, GENERATION,
            AUXILIARY_DATA, 32'd64, 32'd16, 16'd16, 16'd4,
            `ASTRA_RENDER_FORMAT_INDEX8, `ASTRA_RENDER_SURFACE_READ);
        before_blitter_dispatches = blitter_dispatches;
        write_blit_command(sub_pointer[10:0], 32'd24, 32'd1000,
            DESTINATION_DESCRIPTOR, SOURCE_DESCRIPTOR,
            16'sd0, 16'sd0, 16'sd0, 16'sd0,
            16'd1, 16'd1, 16'd1, 16'd1);
        write_command_word(sub_pointer[10:0], 1,
            (`ASTRA_RENDER_OP_BLIT << 16) |
            `ASTRA_RENDER_FLAG_BLIT_MASK1);
        write_command_word(sub_pointer[10:0], 10, AUXILIARY_DESCRIPTOR);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_BLIT,
            `ASTRA_RENDER_STATUS_BAD_DESCRIPTOR, 32'd24, 32'd0,
            GENERATION);
        if (blitter_dispatches != before_blitter_dispatches ||
            read_be32(DESTINATION_DATA) != 32'h11223344)
            $fatal(1, "non-MASK1 auxiliary reached pixel DMA");

        // ROP and source-over alpha have distinct destination semantics and
        // are deliberately mutually exclusive in the v1 command contract.
        before_blitter_dispatches = blitter_dispatches;
        write_blit_command(sub_pointer[10:0], 32'd25, 32'd1000,
            DESTINATION_DESCRIPTOR, SOURCE_DESCRIPTOR,
            16'sd0, 16'sd0, 16'sd0, 16'sd0,
            16'd1, 16'd1, 16'd1, 16'd1);
        write_command_word(sub_pointer[10:0], 1,
            (`ASTRA_RENDER_OP_BLIT << 16) |
            `ASTRA_RENDER_FLAG_BLIT_ALPHA |
            `ASTRA_RENDER_FLAG_BLIT_ROP_ENABLE);
        write_command_word(sub_pointer[10:0], 15, 32'h80000000);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_BLIT,
            `ASTRA_RENDER_STATUS_BAD_FLAGS, 32'd25, 32'd0, GENERATION);
        if (blitter_dispatches != before_blitter_dispatches ||
            read_be32(DESTINATION_DATA) != 32'h11223344)
            $fatal(1, "alpha/ROP conflict reached pixel DMA");

        // Palette storage is an independently bounded resource and may not
        // alias any destination allocation.
        write_be32(SOURCE_DESCRIPTOR + 32'd28, DESTINATION_DATA);
        before_blitter_dispatches = blitter_dispatches;
        write_blit_command(sub_pointer[10:0], 32'd26, 32'd1000,
            DESTINATION_DESCRIPTOR, SOURCE_DESCRIPTOR,
            16'sd0, 16'sd0, 16'sd0, 16'sd0,
            16'd1, 16'd1, 16'd1, 16'd1);
        write_command_word(sub_pointer[10:0], 1,
            (`ASTRA_RENDER_OP_BLIT << 16) |
            `ASTRA_RENDER_FLAG_BLIT_PALETTE);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_BLIT,
            `ASTRA_RENDER_STATUS_BAD_RANGE, 32'd26, 32'd0, GENERATION);
        if (blitter_dispatches != before_blitter_dispatches ||
            read_be32(DESTINATION_DATA) != 32'h11223344)
            $fatal(1, "overlapping palette reached pixel DMA");

        // Rebase at the physical ring edge proves both pointer phases and
        // sequence half-range wrap without special-case addressing.
        submission_producer = 11'd1023;
        completion_consumer = 11'd1023;
        pulse_rebase();
        if (submission_consumer != 11'd1023 ||
            completion_producer != 11'd1023)
            $fatal(1, "queue rebase did not take exact pointers");
        sub_pointer = 1023;
        comp_pointer = 1023;
        write_fill_command(sub_pointer[10:0], 32'hffffffff, GENERATION,
            32'd1000, DESTINATION_DESCRIPTOR, 16'sd0, 16'sd0,
            16'd1, 16'd1, 32'h99);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_OK, 32'hffffffff, 32'd1, GENERATION);
        write_fill_command(sub_pointer[10:0], 32'd1, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR, 16'sd1, 16'sd0, 16'd1, 16'd1,
            32'haa);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_FILL,
            `ASTRA_RENDER_STATUS_OK, 32'd1, 32'd1, GENERATION);

        // Every write-only geometry opcode traverses the same bounded rings,
        // destination validation, shared pixel writer, and completion path.
        write_surface(DESTINATION_DESCRIPTOR, GENERATION,
            DESTINATION_DATA, 32'd256, 32'd16, 16'd16, 16'd16,
            `ASTRA_RENDER_FORMAT_INDEX8, `ASTRA_RENDER_SURFACE_WRITE);
        for (column = 0; column < 256; column = column + 1)
            memory_i.write_byte(DESTINATION_DATA + column, 8'h00);

        write_geometry_command(sub_pointer[10:0], 32'd2,
            `ASTRA_RENDER_OP_LINE, 16'd0,
            16'sd1, 16'sd1, 16'sd4, 16'sd1,
            32'd0, 64'd0, 32'd0, 32'h00000021);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_LINE,
            `ASTRA_RENDER_STATUS_OK, 32'd2, 32'd4, GENERATION);
        for (column = 1; column <= 4; column = column + 1)
            check_byte(DESTINATION_DATA + 16 + column, 8'h21);

        write_geometry_command(sub_pointer[10:0], 32'd3,
            `ASTRA_RENDER_OP_RECT,
            `ASTRA_RENDER_GEOMETRY_FLAG_FILLED,
            16'sd2, 16'sd3, 16'sd3, 16'sd4,
            32'd0, 64'd0, 32'd0, 32'h00000032);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_RECT,
            `ASTRA_RENDER_STATUS_OK, 32'd3, 32'd4, GENERATION);
        check_byte(DESTINATION_DATA + 3 * 16 + 2, 8'h32);
        check_byte(DESTINATION_DATA + 4 * 16 + 3, 8'h32);

        write_geometry_command(sub_pointer[10:0], 32'd4,
            `ASTRA_RENDER_OP_CIRCLE, 16'd0,
            16'sd8, 16'sd8, 16'sd0, 16'sd0,
            {16'd2, 16'd0}, 64'd0, 32'd0, 32'h00000043);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_CIRCLE,
            `ASTRA_RENDER_STATUS_OK, 32'd4, IGNORE_COUNT, GENERATION);
        check_byte(DESTINATION_DATA + 8 * 16 + 10, 8'h43);
        check_byte(DESTINATION_DATA + 8 * 16 + 6, 8'h43);

        write_geometry_command(sub_pointer[10:0], 32'd5,
            `ASTRA_RENDER_OP_ELLIPSE, 16'd0,
            16'sd8, 16'sd8, 16'sd0, 16'sd0,
            {16'd3, 16'd2}, 64'd0, 32'd0, 32'h00000054);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_ELLIPSE,
            `ASTRA_RENDER_STATUS_OK, 32'd5, IGNORE_COUNT, GENERATION);
        check_byte(DESTINATION_DATA + 8 * 16 + 11, 8'h54);
        check_byte(DESTINATION_DATA + 8 * 16 + 5, 8'h54);

        write_geometry_command(sub_pointer[10:0], 32'd6,
            `ASTRA_RENDER_OP_PATTERN_FILL,
            `ASTRA_RENDER_GEOMETRY_FLAG_PATTERN_OPAQUE,
            16'sd12, 16'sd12, 16'sd13, 16'sd13,
            32'd0, 64'hffffffffffffffff,
            32'h00000065, 32'h00000076);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0],
            `ASTRA_RENDER_OP_PATTERN_FILL, `ASTRA_RENDER_STATUS_OK,
            32'd6, 32'd4, GENERATION);
        check_byte(DESTINATION_DATA + 12 * 16 + 12, 8'h76);
        check_byte(DESTINATION_DATA + 13 * 16 + 13, 8'h76);

        // Meaningless geometry flags and unsupported destination formats are
        // rejected before the geometry producer or pixel DMA can start.
        before_geometry_dispatches = geometry_dispatches;
        write_geometry_command(sub_pointer[10:0], 32'd7,
            `ASTRA_RENDER_OP_LINE,
            `ASTRA_RENDER_GEOMETRY_FLAG_FILLED,
            16'sd0, 16'sd0, 16'sd1, 16'sd1,
            32'd0, 64'd0, 32'd0, 32'h00000087);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_LINE,
            `ASTRA_RENDER_STATUS_BAD_FLAGS, 32'd7, 32'd0, GENERATION);
        if (geometry_dispatches != before_geometry_dispatches)
            $fatal(1, "bad geometry flags reached execution");

        write_surface(DESTINATION_DESCRIPTOR, GENERATION,
            DESTINATION_DATA, 32'd1024, 32'd64, 16'd16, 16'd16,
            `ASTRA_RENDER_FORMAT_ARGB8888, `ASTRA_RENDER_SURFACE_WRITE);
        write_geometry_command(sub_pointer[10:0], 32'd8,
            `ASTRA_RENDER_OP_LINE, 16'd0,
            16'sd0, 16'sd0, 16'sd1, 16'sd1,
            32'd0, 64'd0, 32'd0, 32'hff112233);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0], `ASTRA_RENDER_OP_LINE,
            `ASTRA_RENDER_STATUS_BAD_DESCRIPTOR, 32'd8, 32'd0,
            GENERATION);
        if (geometry_dispatches != before_geometry_dispatches)
            $fatal(1, "unsupported geometry format reached execution");

        // Flood fill owns a read/write destination and a caller-bounded
        // read/write workspace. The workspace is ordered through writer
        // barriers before any dependent seed read.
        write_surface(DESTINATION_DESCRIPTOR, GENERATION,
            DESTINATION_DATA, 32'd256, 32'd16, 16'd16, 16'd16,
            `ASTRA_RENDER_FORMAT_INDEX8,
            `ASTRA_RENDER_SURFACE_READ | `ASTRA_RENDER_SURFACE_WRITE);
        write_surface(AUXILIARY_DESCRIPTOR, GENERATION,
            AUXILIARY_DATA, 32'd1024, 32'd64, 16'd16, 16'd16,
            `ASTRA_RENDER_FORMAT_XRGB8888,
            `ASTRA_RENDER_SURFACE_READ | `ASTRA_RENDER_SURFACE_WRITE);
        for (column = 0; column < 256; column = column + 1)
            memory_i.write_byte(DESTINATION_DATA + column, 8'd0);
        for (row = 2; row <= 8; row = row + 1)
            for (column = 2; column <= 10; column = column + 1)
                memory_i.write_byte(DESTINATION_DATA + row * 16 + column,
                                    8'd1);
        memory_i.write_byte(DESTINATION_DATA + 4 * 16 + 5, 8'd2);
        memory_i.write_byte(DESTINATION_DATA + 4 * 16 + 6, 8'd2);
        memory_i.write_byte(DESTINATION_DATA + 5 * 16 + 5, 8'd2);
        write_flood_command(sub_pointer[10:0], 32'd9,
            DESTINATION_DESCRIPTOR, AUXILIARY_DESCRIPTOR,
            16'sd3, 16'sd3, 32'd7);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0],
            `ASTRA_RENDER_OP_FLOOD_FILL, `ASTRA_RENDER_STATUS_OK,
            32'd9, 32'd60, GENERATION);
        for (row = 0; row < 16; row = row + 1)
            for (column = 0; column < 16; column = column + 1) begin
                if (column >= 2 && column <= 10 && row >= 2 && row <= 8 &&
                    !((column == 5 && row == 4) ||
                      (column == 6 && row == 4) ||
                      (column == 5 && row == 5))) begin
                    check_byte(DESTINATION_DATA + row * 16 + column, 8'd7);
                end else if (memory_i.read_byte(
                                 DESTINATION_DATA + row * 16 + column) ==
                             8'd7) begin
                    $fatal(1, "integrated flood escaped x=%0d y=%0d",
                           column, row);
                end
            end

        // One workspace entry cannot represent this topology. It must return
        // an explicit bounded failure, never overwrite adjacent arena data.
        for (row = 2; row <= 8; row = row + 1)
            for (column = 2; column <= 10; column = column + 1)
                memory_i.write_byte(DESTINATION_DATA + row * 16 + column,
                                    8'd1);
        memory_i.write_byte(DESTINATION_DATA + 4 * 16 + 5, 8'd2);
        memory_i.write_byte(DESTINATION_DATA + 4 * 16 + 6, 8'd2);
        memory_i.write_byte(DESTINATION_DATA + 5 * 16 + 5, 8'd2);
        write_surface(AUXILIARY_DESCRIPTOR, GENERATION,
            AUXILIARY_DATA, 32'd4, 32'd4, 16'd1, 16'd1,
            `ASTRA_RENDER_FORMAT_XRGB8888,
            `ASTRA_RENDER_SURFACE_READ | `ASTRA_RENDER_SURFACE_WRITE);
        write_flood_command(sub_pointer[10:0], 32'd10,
            DESTINATION_DESCRIPTOR, AUXILIARY_DESCRIPTOR,
            16'sd3, 16'sd3, 32'd7);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0],
            `ASTRA_RENDER_OP_FLOOD_FILL,
            `ASTRA_RENDER_STATUS_WORK_OVERFLOW,
            32'd10, IGNORE_COUNT, GENERATION);

        // Workspace aliasing destination storage is rejected before dispatch.
        before_flood_dispatches = flood_dispatches;
        write_surface(AUXILIARY_DESCRIPTOR, GENERATION,
            DESTINATION_DATA, 32'd256, 32'd16, 16'd4, 16'd16,
            `ASTRA_RENDER_FORMAT_XRGB8888,
            `ASTRA_RENDER_SURFACE_READ | `ASTRA_RENDER_SURFACE_WRITE);
        write_flood_command(sub_pointer[10:0], 32'd11,
            DESTINATION_DESCRIPTOR, AUXILIARY_DESCRIPTOR,
            16'sd3, 16'sd3, 32'd7);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0],
            `ASTRA_RENDER_OP_FLOOD_FILL,
            `ASTRA_RENDER_STATUS_BAD_RANGE,
            32'd11, 32'd0, GENERATION);
        if (flood_dispatches != before_flood_dispatches)
            $fatal(1, "overlapping flood workspace reached execution");

        // AFNT MASK1 runs traverse command validation, descriptor prepass,
        // clipping, shared pixel DMA, and fenced completion end to end.
        write_surface(DESTINATION_DESCRIPTOR, GENERATION,
            DESTINATION_DATA, 32'd512, 32'd32, 16'd16, 16'd16,
            `ASTRA_RENDER_FORMAT_RGB565,
            `ASTRA_RENDER_SURFACE_READ | `ASTRA_RENDER_SURFACE_WRITE);
        write_surface(SOURCE_DESCRIPTOR, GENERATION,
            SOURCE_DATA, 32'd32, 32'd2, 16'd16, 16'd16,
            `ASTRA_RENDER_FORMAT_MASK1, `ASTRA_RENDER_SURFACE_READ);
        for (column = 0; column < 512; column = column + 1)
            memory_i.write_byte(DESTINATION_DATA + column, 8'd0);
        memory_i.write_byte(SOURCE_DATA, 8'b01000000);
        write_glyph_descriptor(GLYPH_DESCRIPTORS, 32'd0,
            16'd1, 16'd0, 16'sd2, 16'sd3, 16'd2, 16'd1);
        write_glyph_command(sub_pointer[10:0], 32'd12, 16'd0,
            DESTINATION_DESCRIPTOR, SOURCE_DESCRIPTOR,
            GLYPH_DESCRIPTORS, 13'd1, 32'h0000f800, 32'd0, 8'd0);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0],
            `ASTRA_RENDER_OP_GLYPH_RUN, `ASTRA_RENDER_STATUS_OK,
            32'd12, 32'd1, GENERATION);
        check_byte(DESTINATION_DATA + 3 * 32 + 2 * 2, 8'hf8);
        check_byte(DESTINATION_DATA + 3 * 32 + 2 * 2 + 1, 8'h00);
        check_byte(DESTINATION_DATA + 3 * 32 + 3 * 2, 8'h00);
        check_byte(DESTINATION_DATA + 3 * 32 + 3 * 2 + 1, 8'h00);

        // A4 uses the exact native RGB565 coverage path through the command
        // processor rather than a software-expanded glyph surface.
        write_surface(SOURCE_DESCRIPTOR, GENERATION,
            SOURCE_DATA, 32'd128, 32'd8, 16'd16, 16'd16,
            `ASTRA_RENDER_FORMAT_A4, `ASTRA_RENDER_SURFACE_READ);
        memory_i.write_byte(SOURCE_DATA, 8'hf8);
        write_glyph_descriptor(GLYPH_DESCRIPTORS, 32'd0,
            16'd0, 16'd0, 16'sd4, 16'sd5, 16'd2, 16'd1);
        write_glyph_command(sub_pointer[10:0], 32'd13, 16'd0,
            DESTINATION_DESCRIPTOR, SOURCE_DESCRIPTOR,
            GLYPH_DESCRIPTORS, 13'd1, 32'h0000ffff, 32'd0, 8'd0);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0],
            `ASTRA_RENDER_OP_GLYPH_RUN, `ASTRA_RENDER_STATUS_OK,
            32'd13, 32'd2, GENERATION);
        check_byte(DESTINATION_DATA + 5 * 32 + 4 * 2, 8'hff);
        check_byte(DESTINATION_DATA + 5 * 32 + 4 * 2 + 1, 8'hff);
        check_byte(DESTINATION_DATA + 5 * 32 + 5 * 2, 8'h8c);
        check_byte(DESTINATION_DATA + 5 * 32 + 5 * 2 + 1, 8'h51);

        // INDEX8 is protocol value zero, so explicitly prove that command
        // validation accepts it and performs exact indexed output.
        write_surface(DESTINATION_DESCRIPTOR, GENERATION,
            DESTINATION_DATA, 32'd256, 32'd16, 16'd16, 16'd16,
            `ASTRA_RENDER_FORMAT_INDEX8,
            `ASTRA_RENDER_SURFACE_READ | `ASTRA_RENDER_SURFACE_WRITE);
        write_surface(SOURCE_DESCRIPTOR, GENERATION,
            SOURCE_DATA, 32'd256, 32'd16, 16'd16, 16'd16,
            `ASTRA_RENDER_FORMAT_INDEX8, `ASTRA_RENDER_SURFACE_READ);
        write_be32(SOURCE_DESCRIPTOR + 32'd28, SOURCE_PALETTE);
        memory_i.write_byte(SOURCE_DATA, 8'h2a);
        write_glyph_descriptor(GLYPH_DESCRIPTORS, 32'd0,
            16'd0, 16'd0, 16'sd1, 16'sd1, 16'd1, 16'd1);
        write_glyph_command(sub_pointer[10:0], 32'd14, 16'd0,
            DESTINATION_DESCRIPTOR, SOURCE_DESCRIPTOR,
            GLYPH_DESCRIPTORS, 13'd1, 32'd0, 32'd0, 8'd0);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0],
            `ASTRA_RENDER_OP_GLYPH_RUN, `ASTRA_RENDER_STATUS_OK,
            32'd14, 32'd1, GENERATION);
        check_byte(DESTINATION_DATA + 16 + 1, 8'h2a);

        // Every positioned record is checked before writer start. A valid
        // first glyph followed by a malformed zero-width glyph must reject
        // the whole run without changing the destination.
        write_surface(DESTINATION_DESCRIPTOR, GENERATION,
            DESTINATION_DATA, 32'd512, 32'd32, 16'd16, 16'd16,
            `ASTRA_RENDER_FORMAT_RGB565,
            `ASTRA_RENDER_SURFACE_READ | `ASTRA_RENDER_SURFACE_WRITE);
        write_surface(SOURCE_DESCRIPTOR, GENERATION,
            SOURCE_DATA, 32'd32, 32'd2, 16'd16, 16'd16,
            `ASTRA_RENDER_FORMAT_MASK1, `ASTRA_RENDER_SURFACE_READ);
        memory_i.write_byte(DESTINATION_DATA, 8'h12);
        memory_i.write_byte(DESTINATION_DATA + 1, 8'h34);
        memory_i.write_byte(SOURCE_DATA, 8'h80);
        write_glyph_descriptor(GLYPH_DESCRIPTORS, 32'd0,
            16'd0, 16'd0, 16'sd0, 16'sd0, 16'd1, 16'd1);
        write_glyph_descriptor(GLYPH_DESCRIPTORS + 32'd16, 32'd0,
            16'd0, 16'd0, 16'sd1, 16'sd0, 16'd0, 16'd1);
        before_glyph_dispatches = glyph_dispatches;
        write_glyph_command(sub_pointer[10:0], 32'd15, 16'd0,
            DESTINATION_DESCRIPTOR, SOURCE_DESCRIPTOR,
            GLYPH_DESCRIPTORS, 13'd2, 32'h0000ffff, 32'd0, 8'd0);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0],
            `ASTRA_RENDER_OP_GLYPH_RUN, `ASTRA_RENDER_STATUS_BAD_RANGE,
            32'd15, 32'd0, GENERATION);
        if (glyph_dispatches != before_glyph_dispatches + 1)
            $fatal(1, "malformed glyph run did not reach bounded prepass");
        check_byte(DESTINATION_DATA, 8'h12);
        check_byte(DESTINATION_DATA + 1, 8'h34);

        // Descriptor storage overlapping destination data is rejected by
        // command-range policy before the glyph engine can start.
        before_glyph_dispatches = glyph_dispatches;
        write_glyph_command(sub_pointer[10:0], 32'd16, 16'd0,
            DESTINATION_DESCRIPTOR, SOURCE_DESCRIPTOR,
            DESTINATION_DATA, 13'd1, 32'h0000ffff, 32'd0, 8'd0);
        sub_pointer = sub_pointer + 1;
        comp_pointer = comp_pointer + 1;
        publish(sub_pointer[10:0]);
        wait_for_completion(comp_pointer[10:0],
            `ASTRA_RENDER_OP_GLYPH_RUN, `ASTRA_RENDER_STATUS_BAD_RANGE,
            32'd16, 32'd0, GENERATION);
        if (glyph_dispatches != before_glyph_dispatches)
            $fatal(1, "overlapping glyph descriptors reached execution");

        // Impossible pointer distance and overlapping rings fail closed and
        // perform no AXI access. Rebase is the explicit recovery operation.
        before_reads = read_transactions;
        submission_producer = submission_consumer + 11'd1025;
        repeat (20) @(posedge clk);
        if (!configuration_fault || read_transactions != before_reads)
            $fatal(1, "invalid queue distance was not contained");
        submission_producer = submission_consumer;
        completion_consumer = completion_producer;
        pulse_rebase();
        if (configuration_fault)
            $fatal(1, "queue rebase did not clear configuration fault");

        sub_pointer = submission_consumer;
        write_fill_command(sub_pointer[10:0], 32'd2, GENERATION, 32'd1000,
            DESTINATION_DESCRIPTOR, 16'sd0, 16'sd0, 16'd1, 16'd1, 32'hbb);
        completion_ring_offset = SUBMISSION_OFFSET;
        before_reads = read_transactions;
        sub_pointer = sub_pointer + 1;
        submission_producer = sub_pointer[10:0];
        repeat (20) @(posedge clk);
        if (!configuration_fault || read_transactions != before_reads)
            $fatal(1, "overlapping rings were not contained");
        submission_producer = submission_consumer;
        completion_ring_offset = COMPLETION_OFFSET;
        completion_consumer = completion_producer;
        pulse_rebase();

        if (commands_submitted != commands_completed)
            $fatal(1, "submitted/completed mismatch %0d/%0d",
                   commands_submitted, commands_completed);
        if (commands_failed == 32'd0)
            $fatal(1, "failure accounting was not observable");
        $display("astra render command processor tests passed: submitted=%0d failed=%0d reads=%0d writes=%0d",
                 commands_submitted, commands_failed,
                 read_transactions, write_transactions);
        $finish;
    end
endmodule

`default_nettype wire
