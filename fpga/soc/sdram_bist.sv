// Full-range SDRAM power-on self-test using the controller's byte-wide port B.
// The CPU starts the test and reads synchronized status; the march itself runs
// at the SDRAM clock so exhaustive POST is not throttled by the CPU CDC bridge.
`default_nettype none

module sdram_bist #(
    parameter integer MEM_BYTES = 33554432
) (
    input  wire        cpu_clk,
    input  wire        cpu_rst,
    input  wire        cpu_start,
    output reg         cpu_busy,
    output reg         cpu_done,
    output reg  [2:0]  cpu_phase,
    output reg  [24:0] cpu_progress,
    output reg  [31:0] cpu_error_count,
    output reg  [24:0] cpu_first_fail,
    output reg  [7:0]  cpu_expected,
    output reg  [7:0]  cpu_actual,

    input  wire        sdram_clk,
    input  wire        sdram_rst,
    output reg  [24:0] sdram_addr,
    output reg  [7:0]  sdram_din,
    output reg         sdram_we,
    output reg         sdram_oe,
    input  wire [7:0]  sdram_dout,
    input  wire        sdram_done
);
    localparam [24:0] LAST_BYTE = MEM_BYTES - 1;
    localparam [2:0] B_IDLE = 3'd0, B_WRITE_START = 3'd1,
                     B_WRITE_WAIT = 3'd2, B_READ_START = 3'd3,
                     B_READ_WAIT = 3'd4, B_READ_CAPTURE = 3'd5,
                     B_DONE = 3'd6;

    reg start_toggle_cpu;
    (* async_reg = "true" *) reg [2:0] start_sync_sd;
    reg start_seen_sd;

    reg [2:0] state_sd;
    reg busy_sd;
    reg done_sd;
    reg [2:0] phase_sd;
    reg [24:0] progress_sd;
    reg [31:0] error_count_sd;
    reg [24:0] first_fail_sd;
    reg [7:0] expected_sd;
    reg [7:0] actual_sd;
    reg invert_pattern_sd;

    (* async_reg = "true" *) reg [1:0] busy_sync_cpu;
    (* async_reg = "true" *) reg [1:0] done_sync_cpu;
    reg [2:0] phase_meta_cpu;
    reg [24:0] progress_meta_cpu;
    reg [31:0] errors_meta_cpu;
    reg [24:0] first_meta_cpu;
    reg [7:0] expected_meta_cpu;
    reg [7:0] actual_meta_cpu;

    function automatic [7:0] pattern(input [24:0] address);
        pattern = address[7:0] ^ address[15:8] ^ address[23:16] ^
                  {7'd0, address[24]} ^ 8'ha5;
    endfunction

    always @(posedge cpu_clk) begin
        if (cpu_rst) begin
            start_toggle_cpu <= 1'b0;
            busy_sync_cpu <= 2'b00;
            done_sync_cpu <= 2'b00;
            cpu_busy <= 1'b0;
            cpu_done <= 1'b0;
            cpu_phase <= 3'd0;
            cpu_progress <= 25'd0;
            cpu_error_count <= 32'd0;
            cpu_first_fail <= 25'd0;
            cpu_expected <= 8'd0;
            cpu_actual <= 8'd0;
            phase_meta_cpu <= 3'd0;
            progress_meta_cpu <= 25'd0;
            errors_meta_cpu <= 32'd0;
            first_meta_cpu <= 25'd0;
            expected_meta_cpu <= 8'd0;
            actual_meta_cpu <= 8'd0;
        end else begin
            if (cpu_start && !cpu_busy) start_toggle_cpu <= ~start_toggle_cpu;

            busy_sync_cpu <= {busy_sync_cpu[0], busy_sd};
            done_sync_cpu <= {done_sync_cpu[0], done_sd};
            cpu_busy <= busy_sync_cpu[1];
            cpu_done <= done_sync_cpu[1];

            phase_meta_cpu <= phase_sd;
            progress_meta_cpu <= progress_sd;
            errors_meta_cpu <= error_count_sd;
            first_meta_cpu <= first_fail_sd;
            expected_meta_cpu <= expected_sd;
            actual_meta_cpu <= actual_sd;
            cpu_phase <= phase_meta_cpu;
            cpu_progress <= progress_meta_cpu;
            cpu_error_count <= errors_meta_cpu;
            cpu_first_fail <= first_meta_cpu;
            cpu_expected <= expected_meta_cpu;
            cpu_actual <= actual_meta_cpu;
        end
    end

    always @(posedge sdram_clk) begin
        if (sdram_rst) begin
            start_sync_sd <= 3'b000;
            start_seen_sd <= 1'b0;
            state_sd <= B_IDLE;
            busy_sd <= 1'b0;
            done_sd <= 1'b0;
            phase_sd <= 3'd0;
            progress_sd <= 25'd0;
            error_count_sd <= 32'd0;
            first_fail_sd <= 25'd0;
            expected_sd <= 8'd0;
            actual_sd <= 8'd0;
            invert_pattern_sd <= 1'b0;
            sdram_addr <= 25'd0;
            sdram_din <= 8'd0;
            sdram_we <= 1'b0;
            sdram_oe <= 1'b0;
        end else begin
            start_sync_sd <= {start_sync_sd[1:0], start_toggle_cpu};
            case (state_sd)
                B_IDLE, B_DONE: begin
                    sdram_we <= 1'b0;
                    sdram_oe <= 1'b0;
                    if (start_sync_sd[2] != start_seen_sd) begin
                        start_seen_sd <= start_sync_sd[2];
                        busy_sd <= 1'b1;
                        done_sd <= 1'b0;
                        phase_sd <= 3'd1;
                        progress_sd <= 25'd0;
                        error_count_sd <= 32'd0;
                        first_fail_sd <= 25'd0;
                        expected_sd <= 8'd0;
                        actual_sd <= 8'd0;
                        invert_pattern_sd <= 1'b0;
                        state_sd <= B_WRITE_START;
                    end
                end
                B_WRITE_START: begin
                    sdram_addr <= progress_sd;
                    sdram_din <= invert_pattern_sd ? ~pattern(progress_sd) :
                                                        pattern(progress_sd);
                    sdram_we <= 1'b1;
                    sdram_oe <= 1'b0;
                    state_sd <= B_WRITE_WAIT;
                end
                B_WRITE_WAIT: if (sdram_done) begin
                    sdram_we <= 1'b0;
                    if (progress_sd == LAST_BYTE) begin
                        progress_sd <= 25'd0;
                        phase_sd <= 3'd2;
                        state_sd <= B_READ_START;
                    end else begin
                        progress_sd <= progress_sd + 25'd1;
                        state_sd <= B_WRITE_START;
                    end
                end
                B_READ_START: begin
                    sdram_addr <= progress_sd;
                    sdram_we <= 1'b0;
                    sdram_oe <= 1'b1;
                    state_sd <= B_READ_WAIT;
                end
                B_READ_WAIT: if (sdram_done) begin
                    sdram_oe <= 1'b0;
                    state_sd <= B_READ_CAPTURE;
                end
                B_READ_CAPTURE: begin
                    if (sdram_dout != (invert_pattern_sd ? ~pattern(progress_sd) :
                                                          pattern(progress_sd))) begin
                        if (error_count_sd == 32'd0) begin
                            first_fail_sd <= progress_sd;
                            expected_sd <= invert_pattern_sd ? ~pattern(progress_sd) :
                                                               pattern(progress_sd);
                            actual_sd <= sdram_dout;
                        end
                        if (error_count_sd != 32'hffffffff)
                            error_count_sd <= error_count_sd + 32'd1;
                    end
                    if (progress_sd == LAST_BYTE) begin
                        if (!invert_pattern_sd) begin
                            invert_pattern_sd <= 1'b1;
                            progress_sd <= 25'd0;
                            phase_sd <= 3'd1;
                            state_sd <= B_WRITE_START;
                        end else begin
                            busy_sd <= 1'b0;
                            done_sd <= 1'b1;
                            phase_sd <= 3'd3;
                            state_sd <= B_DONE;
                        end
                    end else begin
                        progress_sd <= progress_sd + 25'd1;
                        state_sd <= B_READ_START;
                    end
                end
                default: state_sd <= B_IDLE;
            endcase
        end
    end
endmodule

`default_nettype wire
