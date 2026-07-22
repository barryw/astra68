// Boot-only 90x30 text plane for the 720x480 HDMI POST display.
// CPU writes ASCII bytes in its own clock domain; scanout reads the character
// RAM and CP437 font in the 27 MHz pixel domain. Glyphs are 8x8 doubled to 8x16.
`default_nettype none

module post_console #(
    parameter FONT_HEX = "post_fonts.hex"
) (
    input  wire        cpu_clk,
    input  wire [11:0] cpu_addr,
    input  wire [7:0]  cpu_wdata,
    input  wire        cpu_we,
    output reg  [7:0]  cpu_rdata,

    input  wire        pixel_clk,
    input  wire        pixel_rst,
    input  wire [9:0]  pixel_x,
    input  wire [9:0]  pixel_y,
    output reg  [23:0] rgb
);
    localparam integer COLS = 90;
    localparam integer ROWS = 30;
    localparam integer CELLS = COLS * ROWS;
    localparam integer FONT_BYTES = 256 * 8;

    (* ram_style = "block" *) reg [7:0] cell_mem [0:4095];
    // Infer exactly one CP437 bank so no unused BRAM address pins exist.
    (* rom_style = "block" *) reg [7:0] font_rom [0:FONT_BYTES-1];

    integer init_index;
    initial begin
        for (init_index = 0; init_index < 4096; init_index = init_index + 1)
            cell_mem[init_index] = 8'h20;
        $readmemh(FONT_HEX, font_rom, 0, FONT_BYTES - 1);
    end

    // Port A: CPU writes/reads one character byte per address.
    always @(posedge cpu_clk) begin
        if (cpu_we && cpu_addr < CELLS) cell_mem[cpu_addr] <= cpu_wdata;
        cpu_rdata <= cpu_addr < CELLS ? cell_mem[cpu_addr] : 8'h20;
    end

    function automatic [11:0] text_addr(input [4:0] row, input [6:0] col);
        // row * 90 + col = row * (64 + 16 + 8 + 2) + col.
        text_addr = {1'b0, row, 6'b0} + {3'b0, row, 4'b0} +
                    {4'b0, row, 3'b0} + {6'b0, row, 1'b0} +
                    {5'b0, col};
    endfunction

    wire       scan_active = pixel_x < 10'd720 && pixel_y < 10'd480;
    wire [6:0] scan_col = pixel_x[9:3];
    wire [4:0] scan_row = pixel_y[8:4];
    wire [2:0] scan_glyph_col = pixel_x[2:0];
    wire [2:0] scan_glyph_row = pixel_y[3:1];
    wire [11:0] scan_addr = text_addr(scan_row, scan_col);

    reg [7:0] cell_q;
    reg [7:0] font_q;
    reg [2:0] glyph_col_d1;
    reg [2:0] glyph_row_d1;
    reg [2:0] glyph_col_d2;
    reg       active_d1;
    reg       active_d2;

    always @(posedge pixel_clk) begin
        if (pixel_rst) begin
            cell_q <= 8'h20;
            font_q <= 8'h00;
            glyph_col_d1 <= 3'd0;
            glyph_row_d1 <= 3'd0;
            glyph_col_d2 <= 3'd0;
            active_d1 <= 1'b0;
            active_d2 <= 1'b0;
        end else begin
            cell_q <= cell_mem[scan_addr];
            glyph_col_d1 <= scan_glyph_col;
            glyph_row_d1 <= scan_glyph_row;
            active_d1 <= scan_active;

            font_q <= font_rom[{cell_q, glyph_row_d1}];
            glyph_col_d2 <= glyph_col_d1;
            active_d2 <= active_d1;
        end
    end

    // Restrained POST palette: near-black background and cool white text.
    always @* begin
        if (!active_d2)
            rgb = 24'h000000;
        else if (font_q[7 - glyph_col_d2])
            rgb = 24'he8edf2;
        else
            rgb = 24'h101820;
    end
endmodule

`default_nettype wire
