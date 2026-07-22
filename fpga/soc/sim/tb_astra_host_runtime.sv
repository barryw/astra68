`timescale 1ns/1ps
`default_nettype none

module tb_astra_host_runtime;
    reg cpu_clk = 1'b0;
    reg mem_clk = 1'b0;
    reg cpu_rst = 1'b1;
    reg mem_rst = 1'b1;
    always #5 cpu_clk = ~cpu_clk;
    always #7 mem_clk = ~mem_clk;

    reg host_select = 1'b0;
    reg input_select = 1'b0;
    reg [5:0] reg_index = 6'd0;
    reg write_strobe = 1'b0;
    reg [31:0] write_data = 32'd0;
    reg [3:0] byte_enable = 4'b1111;
    wire [31:0] read_data;
    wire storage_irq;
    wire input_irq;

    wire request_valid;
    reg request_ready = 1'b0;
    wire [31:0] request_id;
    wire [7:0] request_op;
    wire [7:0] request_flags;
    wire [63:0] request_lba;
    wire [15:0] request_sectors;
    wire [31:0] request_buffer;
    wire [31:0] request_media_generation;
    wire [31:0] request_host_generation;

    reg completion_valid = 1'b0;
    wire completion_ready;
    reg [31:0] completion_id = 32'd0;
    reg [15:0] completion_status = 16'd0;
    reg [15:0] completion_sectors = 16'd0;
    reg [31:0] completion_detail = 32'd0;
    reg [31:0] completion_media_generation = 32'd0;
    reg [31:0] completion_host_generation = 32'd0;

    reg state_valid = 1'b0;
    wire state_ready;
    reg [31:0] state_host_generation = 32'd0;
    reg [31:0] state_media_generation = 32'd0;
    reg [31:0] state_flags = 32'd0;
    reg [63:0] state_media_sectors = 64'd0;
    reg [15:0] state_max_sectors = 16'd0;

    reg event_valid = 1'b0;
    wire event_ready;
    reg [31:0] event_host_generation = 32'd0;
    reg [31:0] event_header = 32'd0;
    reg [31:0] event_value = 32'd0;
    reg [31:0] event_timestamp = 32'd0;
    reg [31:0] event_device_sequence = 32'd0;

    astra_host_runtime dut (
        .cpu_clk(cpu_clk), .cpu_rst(cpu_rst),
        .host_select(host_select), .input_select(input_select),
        .reg_index(reg_index), .write_strobe(write_strobe),
        .write_data(write_data), .byte_enable(byte_enable),
        .read_data(read_data), .storage_irq(storage_irq),
        .input_irq(input_irq), .mem_clk(mem_clk), .mem_rst(mem_rst),
        .request_valid(request_valid), .request_ready(request_ready),
        .request_id(request_id), .request_op(request_op),
        .request_flags(request_flags), .request_lba(request_lba),
        .request_sectors(request_sectors), .request_buffer(request_buffer),
        .request_media_generation(request_media_generation),
        .request_host_generation(request_host_generation),
        .completion_valid(completion_valid),
        .completion_ready(completion_ready), .completion_id(completion_id),
        .completion_status(completion_status),
        .completion_sectors(completion_sectors),
        .completion_detail(completion_detail),
        .completion_media_generation(completion_media_generation),
        .completion_host_generation(completion_host_generation),
        .state_valid(state_valid), .state_ready(state_ready),
        .state_host_generation(state_host_generation),
        .state_media_generation(state_media_generation),
        .state_flags(state_flags), .state_media_sectors(state_media_sectors),
        .state_max_sectors(state_max_sectors),
        .event_valid(event_valid), .event_ready(event_ready),
        .event_host_generation(event_host_generation),
        .event_header(event_header), .event_value(event_value),
        .event_timestamp(event_timestamp),
        .event_device_sequence(event_device_sequence)
    );

    task automatic host_write(
        input [5:0] index,
        input [31:0] value,
        input [3:0] enables
    );
        begin
            @(negedge cpu_clk);
            host_select = 1'b1;
            input_select = 1'b0;
            reg_index = index;
            write_data = value;
            byte_enable = enables;
            write_strobe = 1'b1;
            @(negedge cpu_clk);
            write_strobe = 1'b0;
            host_select = 1'b0;
        end
    endtask

    task automatic input_write(input [5:0] index, input [31:0] value);
        begin
            @(negedge cpu_clk);
            host_select = 1'b0;
            input_select = 1'b1;
            reg_index = index;
            write_data = value;
            byte_enable = 4'b1111;
            write_strobe = 1'b1;
            @(negedge cpu_clk);
            write_strobe = 1'b0;
            input_select = 1'b0;
        end
    endtask

    task automatic expect_host_read(
        input [5:0] index,
        input [31:0] expected
    );
        begin
            host_select = 1'b1;
            input_select = 1'b0;
            reg_index = index;
            #1;
            if (read_data !== expected)
                $fatal(1, "host reg %0d expected=%08x actual=%08x",
                       index, expected, read_data);
            host_select = 1'b0;
        end
    endtask

    task automatic expect_input_read(
        input [5:0] index,
        input [31:0] expected
    );
        begin
            host_select = 1'b0;
            input_select = 1'b1;
            reg_index = index;
            #1;
            if (read_data !== expected)
                $fatal(1, "input reg %0d expected=%08x actual=%08x",
                       index, expected, read_data);
            input_select = 1'b0;
        end
    endtask

    initial begin
        repeat (5) @(posedge cpu_clk);
        cpu_rst = 1'b0;
        repeat (3) @(posedge mem_clk);
        mem_rst = 1'b0;

        // Publish a live, writable 4 GiB-equivalent test medium.
        wait (state_ready);
        @(negedge mem_clk);
        state_host_generation = 32'h11223344;
        state_media_generation = 32'h55667788;
        state_flags = 32'h00000007;
        state_media_sectors = 64'h0000000000800000;
        state_max_sectors = 16'd16;
        state_valid = 1'b1;
        @(negedge mem_clk);
        state_valid = 1'b0;
        wait (storage_irq);
        repeat (2) @(posedge cpu_clk);
        expect_host_read(6'h00, 32'h484f5354);
        expect_host_read(6'h01, 32'h00010000);
        expect_host_read(6'h02, 32'h00000007);
        expect_host_read(6'h03, 32'h00000007);
        expect_host_read(6'h04, 32'h55667788);
        expect_host_read(6'h05, 32'h00000000);
        expect_host_read(6'h06, 32'h00800000);
        expect_host_read(6'h16, 32'h11223344);
        expect_input_read(6'h00, 32'h494e5054);
        expect_input_read(6'h01, 32'h00010000);
        expect_input_read(6'h02, 32'h00000007);
        host_write(6'h17, 32'd1, 4'b1111);
        if (storage_irq)
            $fatal(1, "state-change IRQ did not clear");

        // Byte enables must preserve untouched staging bytes.
        host_write(6'h08, 32'haabb0000, 4'b1100);
        host_write(6'h08, 32'h0000ccdd, 4'b0011);
        host_write(6'h09, 32'h00000001, 4'b1111);
        host_write(6'h0a, 32'h00000000, 4'b1111);
        host_write(6'h0b, 32'h00123456, 4'b1111);
        host_write(6'h0c, 32'd2, 4'b1111);
        host_write(6'h0d, 32'h02004000, 4'b1111);
        host_write(6'h0e, 32'd1, 4'b1111);

        wait (request_valid);
        if (request_id != 32'haabbccdd || request_op != 8'd1 ||
            request_flags != 8'd0 || request_lba != 64'h0000000000123456 ||
            request_sectors != 16'd2 || request_buffer != 32'h02004000 ||
            request_media_generation != 32'h55667788 ||
            request_host_generation != 32'h11223344)
            $fatal(1, "request descriptor corrupted across CDC");
        @(negedge mem_clk);
        request_ready = 1'b1;
        @(negedge mem_clk);
        request_ready = 1'b0;

        // Protocol 1.0 reserves every request-flag bit. Rejecting unknown
        // semantics prevents a newer driver from silently getting weaker I/O.
        host_write(6'h09, 32'h00005a01, 4'b1111);
        host_write(6'h0e, 32'd1, 4'b1111);
        expect_host_read(6'h15, 32'h00000100);
        host_write(6'h15, 32'h00000100, 4'b1111);
        host_write(6'h09, 32'h00000001, 4'b1111);

        // Reject a DMA range that crosses the end of SDRAM.
        host_write(6'h08, 32'h00000022, 4'b1111);
        host_write(6'h0d, 32'h03ffff00, 4'b1111);
        host_write(6'h0e, 32'd1, 4'b1111);
        expect_host_read(6'h15, 32'h00000004);
        host_write(6'h15, 32'h00000004, 4'b1111);
        expect_host_read(6'h15, 32'd0);

        // Completion fields remain a coherent peek record until POP.
        wait (completion_ready);
        @(negedge mem_clk);
        completion_id = 32'haabbccdd;
        completion_status = 16'h0000;
        completion_sectors = 16'd2;
        completion_detail = 32'hcafebabe;
        completion_media_generation = 32'h55667788;
        completion_host_generation = 32'h11223344;
        completion_valid = 1'b1;
        @(negedge mem_clk);
        completion_valid = 1'b0;
        wait (storage_irq);
        repeat (2) @(posedge cpu_clk);
        expect_host_read(6'h0f, 32'haabbccdd);
        expect_host_read(6'h10, 32'h00000002);
        expect_host_read(6'h11, 32'hcafebabe);
        expect_host_read(6'h12, 32'h55667788);
        expect_host_read(6'h13, 32'h11223344);
        host_write(6'h14, 32'd1, 4'b1111);
        repeat (2) @(posedge cpu_clk);
        if (storage_irq)
            $fatal(1, "completion IRQ did not clear after POP");

        // Keyboard/pointer/gamepad records share one ordered event stream.
        wait (event_ready);
        @(negedge mem_clk);
        event_host_generation = 32'h11223344;
        event_header = 32'h0104002c;
        event_value = 32'h00000001;
        event_timestamp = 32'h10203040;
        event_device_sequence = 32'h00420017;
        event_valid = 1'b1;
        @(negedge mem_clk);
        event_valid = 1'b0;
        wait (input_irq);
        repeat (2) @(posedge cpu_clk);
        expect_input_read(6'h04, 32'h0104002c);
        expect_input_read(6'h05, 32'h00000001);
        expect_input_read(6'h06, 32'h10203040);
        expect_input_read(6'h07, 32'h00420017);
        expect_input_read(6'h08, 32'h11223344);
        input_write(6'h09, 32'd1);
        repeat (2) @(posedge cpu_clk);
        if (input_irq)
            $fatal(1, "input IRQ did not clear after POP");

        $display("PASS AstraHost queued descriptors, validation, CDC, completions, media generations, and input events");
        $finish;
    end

    initial begin
        #200000;
        $fatal(1, "AstraHost runtime test timeout request=%b completion=%b input=%b storage_irq=%b input_irq=%b",
               request_valid, completion_ready, event_ready,
               storage_irq, input_irq);
    end
endmodule

`default_nettype wire
