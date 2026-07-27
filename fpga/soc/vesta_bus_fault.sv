// Sticky diagnostics for physical bus failures. The first unacknowledged
// failure owns the record; later failures only increment LOST so the original
// cause remains available to the vector-2 handler and retained trace.
`default_nettype none

module vesta_bus_fault #(
    parameter integer TIMEOUT_CYCLES = 2048
) (
    input  wire        clk,
    input  wire        rst,
    input  wire        select,
    input  wire [2:0]  reg_index,
    input  wire        write_strobe,
    input  wire [31:0] write_data,
    input  wire [3:0]  byte_enable,
    output reg  [31:0] read_data,

    input  wire        fault_strobe,
    input  wire [31:0] fault_status,
    input  wire [31:0] fault_address,
    input  wire [31:0] fault_target,
    input  wire [63:0] fault_cycles,
    output wire        fault_valid,

    input  wire        cycle_active,
    input  wire        cycle_complete,
    output reg         timeout_active
);
    localparam [2:0] REG_STATUS = 3'd0;
    localparam [2:0] REG_ADDRESS = 3'd1;
    localparam [2:0] REG_TARGET = 3'd2;
    localparam [2:0] REG_CYCLES_LO = 3'd3;
    localparam [2:0] REG_CYCLES_HI = 3'd4;
    localparam [2:0] REG_LOST = 3'd5;
    localparam [2:0] REG_TIMEOUT = 3'd6;
    localparam [2:0] REG_ACK = 3'd7;

    localparam [31:0] STATUS_VALID = 32'h00000001;

    reg [31:0] status_record;
    reg [31:0] address_record;
    reg [31:0] target_record;
    reg [63:0] cycles_record;
    reg [31:0] lost_record;

    localparam integer TIMEOUT_WIDTH = TIMEOUT_CYCLES <= 1 ? 1 :
                                       $clog2(TIMEOUT_CYCLES);
    localparam [TIMEOUT_WIDTH-1:0] TIMEOUT_LAST =
        TIMEOUT_WIDTH'(TIMEOUT_CYCLES - 1);
    reg [TIMEOUT_WIDTH-1:0] timeout_count;
    reg cycle_terminated;

    wire acknowledge = select && write_strobe &&
                       reg_index == REG_ACK && byte_enable[0] &&
                       write_data[0];

    assign fault_valid = status_record[0];

    // Once either DSACK/BERR completes a cycle or the deadline expires, do
    // not restart the watchdog until AS is released. Completion wins when it
    // arrives on the final permitted clock.
    always @(posedge clk) begin
        if (rst || !cycle_active) begin
            timeout_count <= {TIMEOUT_WIDTH{1'b0}};
            cycle_terminated <= 1'b0;
            timeout_active <= 1'b0;
        end else if (!cycle_terminated) begin
            if (cycle_complete) begin
                timeout_count <= {TIMEOUT_WIDTH{1'b0}};
                cycle_terminated <= 1'b1;
            end else if (timeout_count >= TIMEOUT_LAST) begin
                cycle_terminated <= 1'b1;
                timeout_active <= 1'b1;
            end else begin
                timeout_count <= timeout_count + 1'b1;
            end
        end
    end

    always @(posedge clk) begin
        if (rst) begin
            status_record <= 32'd0;
            address_record <= 32'd0;
            target_record <= 32'd0;
            cycles_record <= 64'd0;
            lost_record <= 32'd0;
        end else if (fault_strobe && (!fault_valid || acknowledge)) begin
            status_record <= (fault_status & ~STATUS_VALID) | STATUS_VALID;
            address_record <= fault_address;
            target_record <= fault_target;
            cycles_record <= fault_cycles;
            lost_record <= 32'd0;
        end else begin
            if (fault_strobe && lost_record != 32'hffffffff)
                lost_record <= lost_record + 32'd1;
            if (acknowledge) begin
                status_record[0] <= 1'b0;
                lost_record <= 32'd0;
            end
        end
    end

    always @* begin
        read_data = 32'd0;
        if (select) begin
            case (reg_index)
                REG_STATUS: read_data = status_record;
                REG_ADDRESS: read_data = address_record;
                REG_TARGET: read_data = target_record;
                REG_CYCLES_LO: read_data = cycles_record[31:0];
                REG_CYCLES_HI: read_data = cycles_record[63:32];
                REG_LOST: read_data = lost_record;
                REG_TIMEOUT: read_data = TIMEOUT_CYCLES;
                REG_ACK: read_data = 32'd0;
                default: read_data = 32'd0;
            endcase
        end
    end

`ifndef SYNTHESIS
    initial begin
        if (TIMEOUT_CYCLES < 1)
            $fatal(1, "TIMEOUT_CYCLES must be positive");
    end
`endif
endmodule

`default_nettype wire
