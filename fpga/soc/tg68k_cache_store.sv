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
    localparam integer CACHE_ENTRIES = 64;

    // Each entry is {physical tag[31:8], longword}. Valid bits remain in FFs
    // so reset and CINVA do not require clearing the distributed RAM itself.
    (* ram_style = "distributed" *) reg [55:0] icache [0:CACHE_ENTRIES-1];
    (* ram_style = "distributed" *) reg [55:0] dcache [0:CACHE_ENTRIES-1];
    reg [CACHE_ENTRIES-1:0] icache_valid = {CACHE_ENTRIES{1'b0}};
    reg [CACHE_ENTRIES-1:0] dcache_valid = {CACHE_ENTRIES{1'b0}};

    reg [127:0] istream_data = 128'd0;
    reg [27:0]  istream_tag = 28'd0;
    reg         istream_valid = 1'b0;
    reg [127:0] dstream_data = 128'd0;
    reg [27:0]  dstream_tag = 28'd0;
    reg         dstream_valid = 1'b0;

    wire [5:0] lookup_index = lookup_addr[7:2];
    wire [5:0] store_index = store_addr[7:2];
    wire [5:0] invalidate_index = invalidate_addr[7:2];
    wire [55:0] icache_entry = icache[lookup_index];
    wire [55:0] dcache_entry = dcache[lookup_index];

    wire icache_hit = lookup_insn && icache_valid[lookup_index] &&
                      icache_entry[55:32] == lookup_addr[31:8];
    wire dcache_hit = lookup_data && dcache_valid[lookup_index] &&
                      dcache_entry[55:32] == lookup_addr[31:8];
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
    assign lookup_idata = icache_hit ? icache_entry[31:0] : istream_word;
    assign lookup_dhit = dcache_hit || dstream_hit;
    assign lookup_ddata = dcache_hit ? dcache_entry[31:0] : dstream_word;

    always @(posedge clk) begin
        if (rst || flush || invalidate_all) begin
            icache_valid <= {CACHE_ENTRIES{1'b0}};
            dcache_valid <= {CACHE_ENTRIES{1'b0}};
            istream_valid <= 1'b0;
            dstream_valid <= 1'b0;
        end else begin
            if (invalidate_valid) begin
                // The wrapper presents the write address on lookup_addr, so
                // these tag checks use the existing asynchronous RAM read.
                if (icache_valid[invalidate_index] &&
                    icache_entry[55:32] == invalidate_addr[31:8])
                    icache_valid[invalidate_index] <= 1'b0;
                if (dcache_valid[invalidate_index] &&
                    dcache_entry[55:32] == invalidate_addr[31:8])
                    dcache_valid[invalidate_index] <= 1'b0;
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
                icache[store_index] <= {store_addr[31:8], store_data};
                icache_valid[store_index] <= 1'b1;
            end else if (istream_hit && !icache_hit && !ifreeze) begin
                icache[lookup_index] <= {lookup_addr[31:8], istream_word};
                icache_valid[lookup_index] <= 1'b1;
            end

            if (store_valid && !store_insn) begin
                dcache[store_index] <= {store_addr[31:8], store_data};
                dcache_valid[store_index] <= 1'b1;
            end else if (dstream_hit && !dcache_hit && !dfreeze) begin
                dcache[lookup_index] <= {lookup_addr[31:8], dstream_word};
                dcache_valid[lookup_index] <= 1'b1;
            end
        end
    end
endmodule

`default_nettype wire
