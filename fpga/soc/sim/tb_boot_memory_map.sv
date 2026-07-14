`timescale 1ns/1ps
`default_nettype none

module tb_boot_memory_map;
    reg [31:0] address = 32'd0;
    reg overlay = 1'b0;
    wire boot;
    wire stage2;
    wire sdram;
    wire [24:0] physical;

    boot_memory_map #(.SD_BOOT_ENABLE(1'b1)) dut (
        .address(address), .overlay_sdram(overlay),
        .boot_bram_select(boot), .stage2_select(stage2),
        .sdram_select(sdram), .sdram_address(physical)
    );

    task check_map(input [31:0] value, input exp_boot, input exp_stage2,
                   input exp_sdram, input [24:0] exp_physical);
        begin
            address = value;
            #1;
            if (boot !== exp_boot || stage2 !== exp_stage2 ||
                sdram !== exp_sdram || physical !== exp_physical)
                $fatal(1, "map mismatch addr=%08x overlay=%b boot=%b stage2=%b sdram=%b phys=%07x",
                       address, overlay, boot, stage2, sdram, physical);
        end
    endtask

    initial begin
        check_map(32'h00000000, 1, 0, 0, 25'h0000000);
        check_map(32'h00001fff, 1, 0, 0, 25'h0001fff);
        check_map(32'h00002000, 0, 0, 0, 25'h0002000);
        check_map(32'hfffc0000, 1, 0, 0, 25'h1fc0000);
        check_map(32'hfffc1fff, 1, 0, 0, 25'h1fc1fff);
        check_map(32'hffe00000, 0, 1, 1, 25'h1e00000);
        check_map(32'hffe3ffff, 0, 1, 1, 25'h1e3ffff);
        check_map(32'h02012340, 0, 0, 1, 25'h0012340);

        overlay = 1'b1;
        check_map(32'h00000000, 0, 1, 1, 25'h1e00000);
        check_map(32'h0003ffff, 0, 1, 1, 25'h1e3ffff);
        check_map(32'hfffc0400, 1, 0, 0, 25'h1fc0400);
        check_map(32'hffe00400, 0, 1, 1, 25'h1e00400);

        $display("PASS SD boot memory map and reset overlay");
        $finish;
    end
endmodule

`default_nettype wire
