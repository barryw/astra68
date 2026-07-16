// Physical storage for the TG030's separate 256-byte direct-mapped caches.
// Keeping the arrays in SystemVerilog lets Yosys infer ECP5 distributed RAM;
// the VHDL wrapper owns all CACR, PMMU, locked-cycle, and bus semantics.
`default_nettype none

module tg68k_cache_store (
    input  wire         clk,
    input  wire         rst,
    input  wire         flush,

    input  wire [31:0]  lookup_addr,
    input  wire         lookup_insn,
    input  wire         lookup_data,
    output wire         lookup_ihit,
    output wire [31:0]  lookup_idata,
    output wire         lookup_dhit,
    output wire [31:0]  lookup_ddata,

    input  wire         store_valid,
    input  wire [31:0]  store_addr,
    input  wire [31:0]  store_data,
    input  wire         store_insn,

    input  wire         invalidate_valid,
    input  wire [31:0]  invalidate_addr,
    input  wire         invalidate_all,
    input  wire         ifreeze,
    input  wire         dfreeze,

    input  wire         fill_valid,
    input  wire [31:0]  fill_addr,
    input  wire [127:0] fill_data,
    input  wire         fill_insn
);
    localparam integer CACHE_LINES = 16;

    // The MC68030 caches have sixteen 16-byte lines. Keep one physical tag
    // per line and one valid bit per longword instead of repeating the tag in
    // all four data entries. Valid bits remain in FFs so reset and CINVA do
    // not require clearing the distributed RAM itself.
    (* ram_style = "distributed" *) reg [31:0] icache_data [0:63];
    (* ram_style = "distributed" *) reg [31:0] dcache_data [0:63];
    (* ram_style = "distributed" *) reg [23:0] icache_tag [0:CACHE_LINES-1];
    (* ram_style = "distributed" *) reg [23:0] dcache_tag [0:CACHE_LINES-1];
    reg [3:0] icache_valid [0:CACHE_LINES-1];
    reg [3:0] dcache_valid [0:CACHE_LINES-1];

    reg [127:0] istream_data = 128'd0;
    reg [27:0]  istream_tag = 28'd0;
    reg         istream_valid = 1'b0;
    reg [127:0] dstream_data = 128'd0;
    reg [27:0]  dstream_tag = 28'd0;
    reg         dstream_valid = 1'b0;

    wire [5:0] lookup_index = lookup_addr[7:2];
    wire [3:0] lookup_line = lookup_addr[7:4];
    wire [1:0] lookup_word = lookup_addr[3:2];
    wire [5:0] store_index = store_addr[7:2];
    wire [3:0] store_line = store_addr[7:4];
    wire [1:0] store_word = store_addr[3:2];
    wire [31:0] icache_lookup_data = icache_data[lookup_index];
    wire [31:0] dcache_lookup_data = dcache_data[lookup_index];
    wire [23:0] icache_lookup_tag = icache_tag[lookup_line];
    wire [23:0] dcache_lookup_tag = dcache_tag[lookup_line];
    wire [23:0] icache_store_tag = icache_tag[store_line];
    wire [23:0] dcache_store_tag = dcache_tag[store_line];

    wire icache_hit = lookup_insn && icache_valid[lookup_line][lookup_word] &&
                      icache_lookup_tag == lookup_addr[31:8];
    wire dcache_hit = lookup_data && dcache_valid[lookup_line][lookup_word] &&
                      dcache_lookup_tag == lookup_addr[31:8];
    wire istream_hit = lookup_insn && istream_valid &&
                       istream_tag == lookup_addr[31:4];
    wire dstream_hit = lookup_data && dstream_valid &&
                       dstream_tag == lookup_addr[31:4];

    function automatic [31:0] select_stream_word(
        input [127:0] value,
        input [1:0] word
    );
        case (word)
            2'd0: select_stream_word = value[127:96];
            2'd1: select_stream_word = value[95:64];
            2'd2: select_stream_word = value[63:32];
            default: select_stream_word = value[31:0];
        endcase
    endfunction

    wire [31:0] istream_word = select_stream_word(
        istream_data, lookup_addr[3:2]);
    wire [31:0] dstream_word = select_stream_word(
        dstream_data, lookup_addr[3:2]);

    assign lookup_ihit = icache_hit || istream_hit;
    assign lookup_idata = icache_hit ? icache_lookup_data : istream_word;
    assign lookup_dhit = dcache_hit || dstream_hit;
    assign lookup_ddata = dcache_hit ? dcache_lookup_data : dstream_word;

    integer reset_line;
    always @(posedge clk) begin
        if (rst || flush || invalidate_all) begin
            for (reset_line = 0; reset_line < CACHE_LINES;
                 reset_line = reset_line + 1) begin
                icache_valid[reset_line] <= 4'b0000;
                dcache_valid[reset_line] <= 4'b0000;
            end
            istream_valid <= 1'b0;
            dstream_valid <= 1'b0;
        end else begin
            if (invalidate_valid) begin
                // The wrapper presents the write address on lookup_addr, so
                // these tag checks use the existing asynchronous RAM read.
                if (icache_lookup_tag == invalidate_addr[31:8])
                    icache_valid[lookup_line] <= 4'b0000;
                if (dcache_lookup_tag == invalidate_addr[31:8])
                    dcache_valid[lookup_line] <= 4'b0000;
                if (istream_valid &&
                    istream_tag == invalidate_addr[31:4])
                    istream_valid <= 1'b0;
                if (dstream_valid &&
                    dstream_tag == invalidate_addr[31:4])
                    dstream_valid <= 1'b0;
            end

            if (fill_valid && fill_insn && !ifreeze) begin
                istream_data <= fill_data;
                istream_tag <= fill_addr[31:4];
                istream_valid <= 1'b1;
            end else if (fill_valid && !fill_insn && !dfreeze) begin
                dstream_data <= fill_data;
                dstream_tag <= fill_addr[31:4];
                dstream_valid <= 1'b1;
            end

            if (store_valid && store_insn) begin
                icache_data[store_index] <= store_data;
                if (icache_valid[store_line] == 4'b0000 ||
                    icache_store_tag != store_addr[31:8]) begin
                    icache_tag[store_line] <= store_addr[31:8];
                    icache_valid[store_line] <= 4'b0001 << store_word;
                end else begin
                    icache_valid[store_line][store_word] <= 1'b1;
                end
            end else if (istream_hit && !icache_hit && !ifreeze) begin
                icache_data[lookup_index] <= istream_word;
                if (icache_valid[lookup_line] == 4'b0000 ||
                    icache_lookup_tag != lookup_addr[31:8]) begin
                    icache_tag[lookup_line] <= lookup_addr[31:8];
                    icache_valid[lookup_line] <= 4'b0001 << lookup_word;
                end else begin
                    icache_valid[lookup_line][lookup_word] <= 1'b1;
                end
            end

            if (store_valid && !store_insn) begin
                dcache_data[store_index] <= store_data;
                if (dcache_valid[store_line] == 4'b0000 ||
                    dcache_store_tag != store_addr[31:8]) begin
                    dcache_tag[store_line] <= store_addr[31:8];
                    dcache_valid[store_line] <= 4'b0001 << store_word;
                end else begin
                    dcache_valid[store_line][store_word] <= 1'b1;
                end
            end else if (dstream_hit && !dcache_hit && !dfreeze) begin
                dcache_data[lookup_index] <= dstream_word;
                if (dcache_valid[lookup_line] == 4'b0000 ||
                    dcache_lookup_tag != lookup_addr[31:8]) begin
                    dcache_tag[lookup_line] <= lookup_addr[31:8];
                    dcache_valid[lookup_line] <= 4'b0001 << lookup_word;
                end else begin
                    dcache_valid[lookup_line][lookup_word] <= 1'b1;
                end
            end
        end
    end
endmodule

`default_nettype wire
