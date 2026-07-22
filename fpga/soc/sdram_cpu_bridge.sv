// 68k CPU-domain transfer adapter for the byte-wide MiST SDRAM port.
//
// One CPU transfer is captured while AS is held active. Active big-endian
// byte lanes are issued sequentially to the 60 MHz SDRAM domain through a
// request/acknowledge toggle crossing. The source bundle remains stable until
// its acknowledge returns, so only the toggles themselves need synchronizers.
`default_nettype none

module sdram_cpu_bridge (
    input  wire        cpu_clk,
    input  wire        cpu_rst,
    input  wire        cpu_start,
    input  wire [24:0] cpu_addr,
    input  wire        cpu_write,
    input  wire [3:0]  cpu_be,
    input  wire [31:0] cpu_wdata,
    output reg         cpu_busy,
    output reg         cpu_done,
    output reg  [31:0] cpu_rdata,

    input  wire        sdram_clk,
    input  wire        sdram_rst,
    output reg  [24:0] sdram_addr,
    output reg  [7:0]  sdram_din,
    output reg         sdram_we,
    output reg         sdram_oe,
    input  wire [7:0]  sdram_dout,
    input  wire        sdram_done
);
    localparam [1:0] CPU_IDLE = 2'd0, CPU_SCAN = 2'd1, CPU_WAIT = 2'd2;
    localparam [1:0] SD_IDLE = 2'd0, SD_ACTIVE = 2'd1, SD_CAPTURE = 2'd2;

    reg [1:0] cpu_state;
    reg [2:0] byte_index;
    reg [24:0] base_addr_cpu;
    reg [3:0] be_cpu;
    reg [31:0] wdata_cpu;
    reg write_cpu;

    // Stable CPU-domain byte request bundle.
    reg [24:0] byte_addr_cpu;
    reg [7:0] byte_wdata_cpu;
    reg byte_write_cpu;
    reg req_toggle_cpu;
    reg ack_seen_cpu;
    (* async_reg = "true" *) reg [2:0] ack_sync_cpu;

    // SDRAM-domain request state and result bundle.
    (* async_reg = "true" *) reg [2:0] req_sync_sd;
    reg req_seen_sd;
    reg ack_toggle_sd;
    reg [7:0] result_sd;
    reg [1:0] sd_state;
    reg sd_write;

    wire lane_active =
        (byte_index == 3'd0) ? be_cpu[3] :
        (byte_index == 3'd1) ? be_cpu[2] :
        (byte_index == 3'd2) ? be_cpu[1] :
        (byte_index == 3'd3) ? be_cpu[0] : 1'b0;

    function automatic [7:0] select_write_byte(
        input [31:0] value,
        input [2:0] index
    );
        case (index)
            3'd0: select_write_byte = value[31:24];
            3'd1: select_write_byte = value[23:16];
            3'd2: select_write_byte = value[15:8];
            default: select_write_byte = value[7:0];
        endcase
    endfunction

    always @(posedge cpu_clk) begin
        cpu_done <= 1'b0;
        if (cpu_rst) begin
            cpu_state <= CPU_IDLE;
            cpu_busy <= 1'b0;
            cpu_rdata <= 32'd0;
            byte_index <= 3'd0;
            base_addr_cpu <= 25'd0;
            be_cpu <= 4'd0;
            wdata_cpu <= 32'd0;
            write_cpu <= 1'b0;
            byte_addr_cpu <= 25'd0;
            byte_wdata_cpu <= 8'd0;
            byte_write_cpu <= 1'b0;
            req_toggle_cpu <= 1'b0;
            ack_seen_cpu <= 1'b0;
            ack_sync_cpu <= 3'b000;
        end else begin
            ack_sync_cpu <= {ack_sync_cpu[1:0], ack_toggle_sd};
            case (cpu_state)
                CPU_IDLE: begin
                    cpu_busy <= 1'b0;
                    if (cpu_start) begin
                        base_addr_cpu <= {cpu_addr[24:2], 2'b00};
                        be_cpu <= cpu_be;
                        wdata_cpu <= cpu_wdata;
                        write_cpu <= cpu_write;
                        cpu_rdata <= 32'd0;
                        byte_index <= 3'd0;
                        cpu_busy <= 1'b1;
                        cpu_state <= CPU_SCAN;
                    end
                end
                CPU_SCAN: begin
                    if (byte_index == 3'd4) begin
                        cpu_busy <= 1'b0;
                        cpu_done <= 1'b1;
                        cpu_state <= CPU_IDLE;
                    end else if (!lane_active) begin
                        byte_index <= byte_index + 3'd1;
                    end else begin
                        byte_addr_cpu <= base_addr_cpu + byte_index;
                        byte_wdata_cpu <= select_write_byte(wdata_cpu, byte_index);
                        byte_write_cpu <= write_cpu;
                        req_toggle_cpu <= ~req_toggle_cpu;
                        cpu_state <= CPU_WAIT;
                    end
                end
                CPU_WAIT: begin
                    if (ack_sync_cpu[2] != ack_seen_cpu) begin
                        ack_seen_cpu <= ack_sync_cpu[2];
                        if (!write_cpu) begin
                            case (byte_index)
                                3'd0: cpu_rdata[31:24] <= result_sd;
                                3'd1: cpu_rdata[23:16] <= result_sd;
                                3'd2: cpu_rdata[15:8] <= result_sd;
                                default: cpu_rdata[7:0] <= result_sd;
                            endcase
                        end
                        byte_index <= byte_index + 3'd1;
                        cpu_state <= CPU_SCAN;
                    end
                end
                default: cpu_state <= CPU_IDLE;
            endcase
        end
    end

    always @(posedge sdram_clk) begin
        if (sdram_rst) begin
            req_sync_sd <= 3'b000;
            req_seen_sd <= 1'b0;
            ack_toggle_sd <= 1'b0;
            result_sd <= 8'd0;
            sd_state <= SD_IDLE;
            sd_write <= 1'b0;
            sdram_addr <= 25'd0;
            sdram_din <= 8'd0;
            sdram_we <= 1'b0;
            sdram_oe <= 1'b0;
        end else begin
            req_sync_sd <= {req_sync_sd[1:0], req_toggle_cpu};
            case (sd_state)
                SD_IDLE: begin
                    sdram_we <= 1'b0;
                    sdram_oe <= 1'b0;
                    if (req_sync_sd[2] != req_seen_sd) begin
                        req_seen_sd <= req_sync_sd[2];
                        sdram_addr <= byte_addr_cpu;
                        sdram_din <= byte_wdata_cpu;
                        sd_write <= byte_write_cpu;
                        sdram_we <= byte_write_cpu;
                        sdram_oe <= !byte_write_cpu;
                        sd_state <= SD_ACTIVE;
                    end
                end
                SD_ACTIVE: begin
                    if (sdram_done) begin
                        sdram_we <= 1'b0;
                        sdram_oe <= 1'b0;
                        if (sd_write) begin
                            ack_toggle_sd <= ~ack_toggle_sd;
                            sd_state <= SD_IDLE;
                        end else begin
                            sd_state <= SD_CAPTURE;
                        end
                    end
                end
                SD_CAPTURE: begin
                    result_sd <= sdram_dout;
                    ack_toggle_sd <= ~ack_toggle_sd;
                    sd_state <= SD_IDLE;
                end
                default: sd_state <= SD_IDLE;
            endcase
        end
    end
endmodule

`default_nettype wire
