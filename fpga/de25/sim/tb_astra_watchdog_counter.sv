`timescale 1ns/1ps
module tb_astra_watchdog_counter;
    reg clock = 0;
    reg watchdog_reset_n = 0;
    wire [28:0] count;

    always #5 clock = ~clock;
    astra_watchdog_counter dut (
        .clock(clock), .watchdog_reset_n(watchdog_reset_n), .count(count)
    );

    initial begin
        repeat (5) @(negedge clock);
        if (count !== 0) $fatal(1, "power-up reset counted as watchdog");
        watchdog_reset_n = 1;
        repeat (5) @(negedge clock);
        watchdog_reset_n = 0;
        repeat (8) @(negedge clock);
        if (count !== 1) $fatal(1, "first watchdog not counted exactly once");
        watchdog_reset_n = 1;
        repeat (5) @(negedge clock);
        watchdog_reset_n = 0;
        repeat (5) @(negedge clock);
        if (count !== 2) $fatal(1, "second watchdog not counted");
        dut.count_q = 29'h1fffffff;
        watchdog_reset_n = 1;
        repeat (5) @(negedge clock);
        watchdog_reset_n = 0;
        repeat (5) @(negedge clock);
        if (count !== 29'h1fffffff)
            $fatal(1, "watchdog counter wrapped");
        $display("ASTRA WATCHDOG COUNTER PASS");
        $finish;
    end
endmodule
