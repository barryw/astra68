// Single-outstanding CPU-to-OHCI control bridge. Request and response bundles
// remain stable while toggle synchronizers carry ownership between domains.
`default_nettype none

module usb_ohci_ctrl_cdc #(
    parameter integer ADDR_WIDTH = 10
) (
    input  wire                  cpu_clk,
    input  wire                  cpu_rst,
    input  wire                  cpu_start,
    input  wire                  cpu_write,
    input  wire [ADDR_WIDTH-1:0] cpu_addr,
    input  wire [3:0]            cpu_be,
    input  wire [31:0]           cpu_wdata,
    output wire                  cpu_busy,
    output reg                   cpu_done,
    output reg  [31:0]           cpu_rdata,

    input  wire                  ctrl_clk,
    input  wire                  ctrl_rst,
    output reg                   wb_cyc,
    output reg                   wb_stb,
    output reg                   wb_we,
    output reg  [ADDR_WIDTH-1:0] wb_addr,
    output reg  [31:0]           wb_wdata,
    output reg  [3:0]            wb_sel,
    input  wire                  wb_ack,
    input  wire [31:0]           wb_rdata
);
    localparam CTRL_IDLE = 1'b0;
    localparam CTRL_ACTIVE = 1'b1;

    reg                  request_write_cpu;
    reg [ADDR_WIDTH-1:0] request_addr_cpu;
    reg [3:0]            request_be_cpu;
    reg [31:0]           request_wdata_cpu;
    reg                  request_busy_cpu;
    reg                  request_toggle_cpu;
    reg                  response_seen_cpu;
    (* async_reg = "true" *) reg [1:0] response_sync_cpu;

    (* async_reg = "true" *) reg [1:0] request_sync_ctrl;
    reg request_seen_ctrl;
    reg response_toggle_ctrl;
    reg [31:0] response_data_ctrl;
    reg ctrl_state;

    assign cpu_busy = request_busy_cpu;

    always @(posedge cpu_clk) begin
        cpu_done <= 1'b0;
        if (cpu_rst) begin
            request_write_cpu <= 1'b0;
            request_addr_cpu <= {ADDR_WIDTH{1'b0}};
            request_be_cpu <= 4'd0;
            request_wdata_cpu <= 32'd0;
            request_busy_cpu <= 1'b0;
            request_toggle_cpu <= 1'b0;
            response_seen_cpu <= 1'b0;
            response_sync_cpu <= 2'b00;
            cpu_rdata <= 32'd0;
        end else begin
            response_sync_cpu <= {
                response_sync_cpu[0], response_toggle_ctrl
            };

            if (cpu_start && !request_busy_cpu) begin
                request_write_cpu <= cpu_write;
                request_addr_cpu <= cpu_addr;
                request_be_cpu <= cpu_be;
                request_wdata_cpu <= cpu_wdata;
                request_busy_cpu <= 1'b1;
                request_toggle_cpu <= ~request_toggle_cpu;
            end

            if (request_busy_cpu &&
                response_sync_cpu[1] != response_seen_cpu) begin
                response_seen_cpu <= response_sync_cpu[1];
                cpu_rdata <= response_data_ctrl;
                request_busy_cpu <= 1'b0;
                cpu_done <= 1'b1;
            end
        end
    end

    always @(posedge ctrl_clk) begin
        if (ctrl_rst) begin
            request_sync_ctrl <= 2'b00;
            request_seen_ctrl <= 1'b0;
            response_toggle_ctrl <= 1'b0;
            response_data_ctrl <= 32'd0;
            wb_cyc <= 1'b0;
            wb_stb <= 1'b0;
            wb_we <= 1'b0;
            wb_addr <= {ADDR_WIDTH{1'b0}};
            wb_wdata <= 32'd0;
            wb_sel <= 4'd0;
            ctrl_state <= CTRL_IDLE;
        end else begin
            request_sync_ctrl <= {
                request_sync_ctrl[0], request_toggle_cpu
            };

            case (ctrl_state)
                CTRL_IDLE: begin
                    wb_cyc <= 1'b0;
                    wb_stb <= 1'b0;
                    if (request_sync_ctrl[1] != request_seen_ctrl) begin
                        request_seen_ctrl <= request_sync_ctrl[1];
                        wb_we <= request_write_cpu;
                        wb_addr <= request_addr_cpu;
                        wb_wdata <= request_wdata_cpu;
                        wb_sel <= request_be_cpu;
                        wb_cyc <= 1'b1;
                        wb_stb <= 1'b1;
                        ctrl_state <= CTRL_ACTIVE;
                    end
                end
                CTRL_ACTIVE: begin
                    if (wb_ack) begin
                        response_data_ctrl <= wb_rdata;
                        response_toggle_ctrl <= ~response_toggle_ctrl;
                        wb_cyc <= 1'b0;
                        wb_stb <= 1'b0;
                        ctrl_state <= CTRL_IDLE;
                    end
                end
                default: ctrl_state <= CTRL_IDLE;
            endcase
        end
    end
endmodule

`default_nettype wire
