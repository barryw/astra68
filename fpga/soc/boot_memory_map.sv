`timescale 1ns/1ps
`default_nettype none

module boot_memory_map #(
    parameter SD_BOOT_ENABLE = 1'b0,
    parameter SDRAM_ENABLE = 1'b1
) (
    input  wire [31:0] address,
    input  wire        overlay_sdram,
    output wire        boot_bram_select,
    output wire        stage2_select,
    output wire        sdram_select,
    output wire [24:0] sdram_address
);
    wire low_rom_window = address[31:18] == 14'd0;
    wire high_stage2_window = address[31:18] == 14'h3ff8;
    wire high_stage0_window = address[31:13] == 19'h7ffe0;
    wire low_stage0_window = address[31:13] == 19'd0;
    wire native_sdram = address[31:25] == 7'b0000001;

    assign boot_bram_select = SD_BOOT_ENABLE ?
        (high_stage0_window || (!overlay_sdram && low_stage0_window)) :
        (high_stage2_window || low_rom_window);
    assign stage2_select = SD_BOOT_ENABLE &&
        (high_stage2_window || (overlay_sdram && low_rom_window));
    assign sdram_select = SDRAM_ENABLE && (native_sdram || stage2_select);
    assign sdram_address = (SD_BOOT_ENABLE && overlay_sdram && low_rom_window) ?
        (25'h1e00000 + {7'd0, address[17:0]}) : address[24:0];
endmodule

`default_nettype wire
