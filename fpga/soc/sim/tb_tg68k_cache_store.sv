`timescale 1ns/1ps

module tb_tg68k_cache_store;
    reg clk = 1'b0;
    always #5 clk = ~clk;

    reg rst = 1'b1;
    reg flush = 1'b0;
    reg [31:0] lookup_addr = 32'd0;
    reg lookup_insn = 1'b0;
    reg lookup_data = 1'b0;
    wire lookup_ihit;
    wire [31:0] lookup_idata;
    wire lookup_dhit;
    wire [31:0] lookup_ddata;
    reg store_valid = 1'b0;
    reg [31:0] store_addr = 32'd0;
    reg [31:0] store_data = 32'd0;
    reg store_insn = 1'b0;
    reg invalidate_valid = 1'b0;
    reg [31:0] invalidate_addr = 32'd0;
    reg invalidate_all = 1'b0;
    reg ifreeze = 1'b0;
    reg dfreeze = 1'b0;
    reg fill_valid = 1'b0;
    reg [31:0] fill_addr = 32'd0;
    reg [127:0] fill_data = 128'd0;
    reg fill_insn = 1'b0;

    tg68k_cache_store dut (.*);

    task automatic store_word(
        input [31:0] address,
        input [31:0] data,
        input insn
    );
        begin
            @(negedge clk);
            lookup_addr = address;
            store_addr = address;
            store_data = data;
            store_insn = insn;
            store_valid = 1'b1;
            @(negedge clk);
            store_valid = 1'b0;
        end
    endtask

    task automatic expect_lookup(
        input [31:0] address,
        input insn,
        input expected_hit,
        input [31:0] expected_data
    );
        begin
            lookup_addr = address;
            lookup_insn = insn;
            lookup_data = !insn;
            #1;
            if ((insn ? lookup_ihit : lookup_dhit) !== expected_hit)
                $fatal(1, "lookup hit mismatch addr=%08x expected=%b actual=%b",
                       address, expected_hit,
                       insn ? lookup_ihit : lookup_dhit);
            if (expected_hit &&
                (insn ? lookup_idata : lookup_ddata) !== expected_data)
                $fatal(1, "lookup data mismatch addr=%08x expected=%08x actual=%08x",
                       address, expected_data,
                       insn ? lookup_idata : lookup_ddata);
            @(negedge clk);
            lookup_insn = 1'b0;
            lookup_data = 1'b0;
        end
    endtask

    initial begin
        repeat (2) @(posedge clk);
        rst = 1'b0;

        expect_lookup(32'h02000100, 1'b1, 1'b0, 32'd0);
        store_word(32'h02000100, 32'h11223344, 1'b1);
        expect_lookup(32'h02000100, 1'b1, 1'b1, 32'h11223344);
        expect_lookup(32'h02000100, 1'b0, 1'b0, 32'd0);
        store_word(32'h02000100, 32'haabbccdd, 1'b0);
        expect_lookup(32'h02000100, 1'b0, 1'b1, 32'haabbccdd);

        // Fill a line, select each word, and promote a stream hit into RAM.
        @(negedge clk);
        fill_addr = 32'h02000200;
        fill_data = 128'h01020304_11121314_21222324_31323334;
        fill_insn = 1'b0;
        fill_valid = 1'b1;
        @(negedge clk);
        fill_valid = 1'b0;
        expect_lookup(32'h02000208, 1'b0, 1'b1, 32'h21222324);

        // Replace the stream buffer; the promoted word must remain cached.
        @(negedge clk);
        fill_addr = 32'h02000300;
        fill_data = 128'h41424344_51525354_61626364_71727374;
        fill_valid = 1'b1;
        @(negedge clk);
        fill_valid = 1'b0;
        expect_lookup(32'h02000208, 1'b0, 1'b1, 32'h21222324);

        // A frozen cache retains old entries but does not accept a new line.
        dfreeze = 1'b1;
        @(negedge clk);
        fill_addr = 32'h02000400;
        fill_data = 128'h81828384_91929394_a1a2a3a4_b1b2b3b4;
        fill_valid = 1'b1;
        @(negedge clk);
        fill_valid = 1'b0;
        dfreeze = 1'b0;
        expect_lookup(32'h02000400, 1'b0, 1'b0, 32'd0);

        // A same-index write with another tag must not evict the resident line.
        lookup_addr = 32'h02010208;
        invalidate_addr = 32'h02010208;
        invalidate_valid = 1'b1;
        @(negedge clk);
        invalidate_valid = 1'b0;
        expect_lookup(32'h02000208, 1'b0, 1'b1, 32'h21222324);

        // A matching write invalidates the line and its stream buffer.
        lookup_addr = 32'h02000208;
        invalidate_addr = 32'h02000208;
        invalidate_valid = 1'b1;
        @(negedge clk);
        invalidate_valid = 1'b0;
        expect_lookup(32'h02000208, 1'b0, 1'b0, 32'd0);

        flush = 1'b1;
        @(negedge clk);
        flush = 1'b0;
        expect_lookup(32'h02000100, 1'b1, 1'b0, 32'd0);

        $display("TG68K CACHE STORE PASS");
        $finish;
    end
endmodule
