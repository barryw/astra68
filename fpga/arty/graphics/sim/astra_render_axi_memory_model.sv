// Simulation-only 64-bit AXI memory used by the Astraea renderer tests.
`timescale 1ns/1ps
`default_nettype none

module astra_render_axi_memory_model #(
    parameter integer AXI_ID_WIDTH = 6,
    parameter integer MEMORY_BYTES = 1048576,
    parameter [31:0] BASE_ADDRESS = 32'd0,
    parameter integer WRITE_QUEUE_DEPTH = 64
) (
    input  wire                         clk,
    input  wire                         reset,
    input  wire                         stall_reads,
    input  wire                         stall_writes,
    input  wire                         inject_read_error,
    input  wire                         inject_write_error,

    input  wire [AXI_ID_WIDTH-1:0]      s_axi_arid,
    input  wire [31:0]                  s_axi_araddr,
    input  wire [7:0]                   s_axi_arlen,
    input  wire [2:0]                   s_axi_arsize,
    input  wire [1:0]                   s_axi_arburst,
    input  wire                         s_axi_arvalid,
    output wire                         s_axi_arready,
    output reg  [AXI_ID_WIDTH-1:0]      s_axi_rid,
    output reg  [63:0]                  s_axi_rdata,
    output reg  [1:0]                   s_axi_rresp,
    output reg                          s_axi_rlast,
    output reg                          s_axi_rvalid,
    input  wire                         s_axi_rready,

    input  wire [AXI_ID_WIDTH-1:0]      s_axi_awid,
    input  wire [31:0]                  s_axi_awaddr,
    input  wire [7:0]                   s_axi_awlen,
    input  wire [2:0]                   s_axi_awsize,
    input  wire [1:0]                   s_axi_awburst,
    input  wire                         s_axi_awvalid,
    output wire                         s_axi_awready,
    input  wire [63:0]                  s_axi_wdata,
    input  wire [7:0]                   s_axi_wstrb,
    input  wire                         s_axi_wlast,
    input  wire                         s_axi_wvalid,
    output wire                         s_axi_wready,
    output wire [AXI_ID_WIDTH-1:0]      s_axi_bid,
    output wire [1:0]                   s_axi_bresp,
    output wire                         s_axi_bvalid,
    input  wire                         s_axi_bready,

    output reg  [31:0]                  read_transactions,
    output reg  [31:0]                  write_transactions
);
    localparam integer POINTER_WIDTH = $clog2(WRITE_QUEUE_DEPTH);
    localparam integer COUNT_WIDTH = $clog2(WRITE_QUEUE_DEPTH + 1);

    reg [7:0] memory [0:MEMORY_BYTES-1];
    reg [31:0] cycle;

    function automatic [63:0] load_beat(input [31:0] address);
        integer lane;
        integer offset;
        begin
            load_beat = 64'd0;
            offset = address - BASE_ADDRESS;
            for (lane = 0; lane < 8; lane = lane + 1)
                load_beat[lane * 8 +: 8] = memory[offset + lane];
        end
    endfunction

    task automatic clear_memory(input [7:0] value);
        integer byte_index;
        begin
            for (byte_index = 0; byte_index < MEMORY_BYTES;
                 byte_index = byte_index + 1)
                memory[byte_index] = value;
        end
    endtask

    task automatic write_byte(input [31:0] address, input [7:0] value);
        begin
            memory[address - BASE_ADDRESS] = value;
        end
    endtask

    function automatic [7:0] read_byte(input [31:0] address);
        begin
            read_byte = memory[address - BASE_ADDRESS];
        end
    endfunction

    reg read_active;
    reg [31:0] read_address;
    reg [8:0] read_beats_remaining;
    reg [AXI_ID_WIDTH-1:0] read_id;
    reg read_error;
    assign s_axi_arready = !reset && !stall_reads && !read_active &&
        !s_axi_rvalid && (cycle[0] || cycle[2]);
    wire ar_accept = s_axi_arvalid && s_axi_arready;
    wire r_accept = s_axi_rvalid && s_axi_rready;

    reg [31:0] aw_address_fifo [0:WRITE_QUEUE_DEPTH-1];
    reg [8:0] aw_beats_fifo [0:WRITE_QUEUE_DEPTH-1];
    reg [AXI_ID_WIDTH-1:0] aw_id_fifo [0:WRITE_QUEUE_DEPTH-1];
    reg [POINTER_WIDTH-1:0] aw_write_pointer;
    reg [POINTER_WIDTH-1:0] aw_read_pointer;
    reg [COUNT_WIDTH-1:0] aw_count;
    reg [63:0] w_data_fifo [0:WRITE_QUEUE_DEPTH-1];
    reg [7:0] w_strobe_fifo [0:WRITE_QUEUE_DEPTH-1];
    reg w_last_fifo [0:WRITE_QUEUE_DEPTH-1];
    reg [POINTER_WIDTH-1:0] w_write_pointer;
    reg [POINTER_WIDTH-1:0] w_read_pointer;
    reg [COUNT_WIDTH-1:0] w_count;
    reg [AXI_ID_WIDTH-1:0] b_id_fifo [0:WRITE_QUEUE_DEPTH-1];
    reg [1:0] b_response_fifo [0:WRITE_QUEUE_DEPTH-1];
    reg [POINTER_WIDTH-1:0] b_write_pointer;
    reg [POINTER_WIDTH-1:0] b_read_pointer;
    reg [COUNT_WIDTH-1:0] b_count;

    reg write_active;
    reg [31:0] write_address;
    reg [8:0] write_beats_remaining;
    reg [AXI_ID_WIDTH-1:0] write_id;

    assign s_axi_awready = !reset && !stall_writes &&
        aw_count < WRITE_QUEUE_DEPTH && (cycle[0] || cycle[3]);
    assign s_axi_wready = !reset && !stall_writes &&
        w_count < WRITE_QUEUE_DEPTH && (cycle[1] || !cycle[2]);
    wire aw_accept = s_axi_awvalid && s_axi_awready;
    wire w_accept = s_axi_wvalid && s_axi_wready;
    wire load_write = !write_active && aw_count != 0;
    wire consume_write_beat = write_active && w_count != 0 &&
        !stall_writes;
    wire finish_write = consume_write_beat &&
        write_beats_remaining == 9'd1;
    assign s_axi_bid = b_id_fifo[b_read_pointer];
    assign s_axi_bresp = b_response_fifo[b_read_pointer];
    assign s_axi_bvalid = b_count != 0 && !stall_writes &&
        cycle[2:0] == 3'b111;
    wire b_accept = s_axi_bvalid && s_axi_bready;

    integer lane;
    integer write_offset;
    always @(posedge clk) begin
        if (reset) begin
            cycle <= 32'd0;
            read_active <= 1'b0;
            read_address <= 32'd0;
            read_beats_remaining <= 9'd0;
            read_id <= {AXI_ID_WIDTH{1'b0}};
            read_error <= 1'b0;
            s_axi_rid <= {AXI_ID_WIDTH{1'b0}};
            s_axi_rdata <= 64'd0;
            s_axi_rresp <= 2'b00;
            s_axi_rlast <= 1'b0;
            s_axi_rvalid <= 1'b0;
            aw_write_pointer <= {POINTER_WIDTH{1'b0}};
            aw_read_pointer <= {POINTER_WIDTH{1'b0}};
            aw_count <= {COUNT_WIDTH{1'b0}};
            w_write_pointer <= {POINTER_WIDTH{1'b0}};
            w_read_pointer <= {POINTER_WIDTH{1'b0}};
            w_count <= {COUNT_WIDTH{1'b0}};
            b_write_pointer <= {POINTER_WIDTH{1'b0}};
            b_read_pointer <= {POINTER_WIDTH{1'b0}};
            b_count <= {COUNT_WIDTH{1'b0}};
            write_active <= 1'b0;
            write_address <= 32'd0;
            write_beats_remaining <= 9'd0;
            write_id <= {AXI_ID_WIDTH{1'b0}};
            read_transactions <= 32'd0;
            write_transactions <= 32'd0;
        end else begin
            cycle <= cycle + 32'd1;

            if (ar_accept) begin
                if (s_axi_arsize != 3'b011 || s_axi_arburst != 2'b01 ||
                    s_axi_araddr[2:0] != 3'd0)
                    $fatal(1, "invalid renderer AXI read request");
                if (s_axi_araddr < BASE_ADDRESS ||
                    s_axi_araddr + ({24'd0, s_axi_arlen} + 32'd1) * 32'd8 >
                    BASE_ADDRESS + MEMORY_BYTES)
                    $fatal(1, "renderer AXI read outside memory: %08x",
                           s_axi_araddr);
                if ({1'b0, s_axi_araddr[11:0]} +
                    ({5'd0, s_axi_arlen} + 13'd1) * 13'd8 > 13'd4096)
                    $fatal(1, "renderer AXI read crossed 4KiB: %08x len=%0d",
                           s_axi_araddr, s_axi_arlen);
                read_active <= 1'b1;
                read_address <= s_axi_araddr;
                read_beats_remaining <= {1'b0, s_axi_arlen} + 9'd1;
                read_id <= s_axi_arid;
                read_error <= inject_read_error;
                read_transactions <= read_transactions + 32'd1;
            end

            if (read_active && !s_axi_rvalid && !stall_reads) begin
                s_axi_rid <= read_id;
                s_axi_rdata <= load_beat(read_address);
                s_axi_rresp <= read_error ? 2'b10 : 2'b00;
                s_axi_rlast <= read_beats_remaining == 9'd1;
                s_axi_rvalid <= 1'b1;
            end
            if (r_accept) begin
                s_axi_rvalid <= 1'b0;
                if (s_axi_rlast) begin
                    read_active <= 1'b0;
                end else begin
                    read_address <= read_address + 32'd8;
                    read_beats_remaining <= read_beats_remaining - 9'd1;
                end
            end

            if (aw_accept) begin
                if (s_axi_awsize != 3'b011 || s_axi_awburst != 2'b01 ||
                    s_axi_awaddr[2:0] != 3'd0)
                    $fatal(1, "invalid renderer AXI write request");
                if (s_axi_awaddr < BASE_ADDRESS ||
                    s_axi_awaddr + ({24'd0, s_axi_awlen} + 32'd1) * 32'd8 >
                    BASE_ADDRESS + MEMORY_BYTES)
                    $fatal(1, "renderer AXI write outside memory: %08x",
                           s_axi_awaddr);
                if ({1'b0, s_axi_awaddr[11:0]} +
                    ({5'd0, s_axi_awlen} + 13'd1) * 13'd8 > 13'd4096)
                    $fatal(1, "renderer AXI write crossed 4KiB: %08x len=%0d",
                           s_axi_awaddr, s_axi_awlen);
                aw_address_fifo[aw_write_pointer] <= s_axi_awaddr;
                aw_beats_fifo[aw_write_pointer] <=
                    {1'b0, s_axi_awlen} + 9'd1;
                aw_id_fifo[aw_write_pointer] <= s_axi_awid;
                aw_write_pointer <= aw_write_pointer + 1'b1;
            end
            if (w_accept) begin
                w_data_fifo[w_write_pointer] <= s_axi_wdata;
                w_strobe_fifo[w_write_pointer] <= s_axi_wstrb;
                w_last_fifo[w_write_pointer] <= s_axi_wlast;
                w_write_pointer <= w_write_pointer + 1'b1;
            end

            if (load_write) begin
                write_active <= 1'b1;
                write_address <= aw_address_fifo[aw_read_pointer];
                write_beats_remaining <= aw_beats_fifo[aw_read_pointer];
                write_id <= aw_id_fifo[aw_read_pointer];
                aw_read_pointer <= aw_read_pointer + 1'b1;
            end
            if (consume_write_beat) begin
                if (w_last_fifo[w_read_pointer] !=
                    (write_beats_remaining == 9'd1))
                    $fatal(1, "renderer AXI WLAST mismatch");
                write_offset = write_address - BASE_ADDRESS;
                for (lane = 0; lane < 8; lane = lane + 1)
                    if (w_strobe_fifo[w_read_pointer][lane])
                        memory[write_offset + lane] <=
                            w_data_fifo[w_read_pointer][lane * 8 +: 8];
                w_read_pointer <= w_read_pointer + 1'b1;
                if (finish_write) begin
                    write_active <= 1'b0;
                    b_id_fifo[b_write_pointer] <= write_id;
                    b_response_fifo[b_write_pointer] <=
                        inject_write_error ? 2'b10 : 2'b00;
                    b_write_pointer <= b_write_pointer + 1'b1;
                    write_transactions <= write_transactions + 32'd1;
                end else begin
                    write_address <= write_address + 32'd8;
                    write_beats_remaining <= write_beats_remaining - 9'd1;
                end
            end

            case ({aw_accept, load_write})
                2'b10: aw_count <= aw_count + 1'b1;
                2'b01: aw_count <= aw_count - 1'b1;
                default: begin end
            endcase
            case ({w_accept, consume_write_beat})
                2'b10: w_count <= w_count + 1'b1;
                2'b01: w_count <= w_count - 1'b1;
                default: begin end
            endcase
            case ({finish_write, b_accept})
                2'b10: b_count <= b_count + 1'b1;
                2'b01: b_count <= b_count - 1'b1;
                default: begin end
            endcase
            if (b_accept)
                b_read_pointer <= b_read_pointer + 1'b1;
        end
    end
endmodule

`default_nettype wire
