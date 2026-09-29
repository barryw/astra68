// Copyright (c) 2026 Astra68 contributors
//
// Saturation, clear, and simultaneous-fault behaviour of the shared record.
`timescale 1ns/1ps
`default_nettype none

module tb_astra_access_fault_record;
    reg clk = 1'b0;
    reg reset = 1'b1;
    reg clear = 1'b0;
    reg write_fault = 1'b0;
    reg read_fault = 1'b0;
    wire [31:0] count_word;
    wire [31:0] first_word;
    integer index;

    always #5 clk = ~clk;

    astra_access_fault_record dut (
        .clk(clk), .reset(reset), .clear(clear),
        .write_fault(write_fault), .write_reason(2'b10),
        .write_offset(16'h0248),
        .read_fault(read_fault), .read_reason(2'b11),
        .read_offset(16'h1234),
        .count_word(count_word), .first_word(first_word)
    );

    task automatic step;
        begin
            @(posedge clk);
            #1;
        end
    endtask

    initial begin
        step();
        reset = 1'b0;
        step();
        if (count_word != 0 || first_word != 0)
            $fatal(1, "record not clear after reset");

        // Simultaneous faults count twice; the store is the first fault.
        write_fault = 1'b1;
        read_fault = 1'b1;
        step();
        if (count_word != 2 || first_word != 32'h80020248)
            $fatal(1, "simultaneous faults %0d/%08x", count_word, first_word);

        // Saturate at 0xffff; FIRST never moves.
        write_fault = 1'b0;
        for (index = 0; index < 70000; index = index + 1)
            step();
        read_fault = 1'b0;
        if (count_word != 32'h0000ffff || first_word != 32'h80020248)
            $fatal(1, "saturation %08x/%08x", count_word, first_word);

        // A load fault concurrent with the clearing store starts a new record.
        clear = 1'b1;
        read_fault = 1'b1;
        step();
        clear = 1'b0;
        read_fault = 1'b0;
        if (count_word != 1 || first_word != 32'h00031234)
            $fatal(1, "clear with fault %0d/%08x", count_word, first_word);
        clear = 1'b1;
        step();
        clear = 1'b0;
        if (count_word != 0 || first_word != 0)
            $fatal(1, "clear %0d/%08x", count_word, first_word);
        $display("ASTRA ACCESS FAULT RECORD PASS");
        $finish;
    end
endmodule

`default_nettype wire
