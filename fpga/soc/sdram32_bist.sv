// Complementary full-range SDRAM POST over the pipelined 32-bit DMA port.
`default_nettype none

module sdram32_bist #(
    parameter integer MEM_BYTES = 33554432,
    parameter integer MAX_OUTSTANDING = 16
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

    input  wire        mem_clk,
    input  wire        mem_rst,
    output wire        mem_lock,
    output wire        mem_valid,
    input  wire        mem_ready,
    output wire        mem_write,
    output wire [24:0] mem_addr,
    output wire [3:0]  mem_be,
    output wire [31:0] mem_wdata,
    input  wire        mem_rsp_valid,
    input  wire [31:0] mem_rdata
);
    localparam [24:0] LAST_WORD = MEM_BYTES - 4;
    localparam [2:0] PHASE_IDLE = 3'd0, PHASE_WRITE = 3'd1,
                     PHASE_READ = 3'd2, PHASE_DONE = 3'd3;

    reg start_toggle_cpu;
    (* async_reg = "true" *) reg [2:0] start_sync_mem;
    reg start_seen_mem;

    reg busy_mem;
    reg done_mem;
    reg [2:0] phase_mem;
    reg invert_pattern_mem;
    reg [24:0] issue_addr_mem;
    reg [24:0] retire_addr_mem;
    reg issue_done_mem;
    reg [5:0] outstanding_mem;
    reg [24:0] progress_mem;
    reg [31:0] error_count_mem;
    reg [24:0] first_fail_mem;
    reg [7:0] expected_mem;
    reg [7:0] actual_mem;
    reg [31:0] expected_word_mem;

    (* async_reg = "true" *) reg [1:0] busy_sync_cpu;
    (* async_reg = "true" *) reg [1:0] done_sync_cpu;
    reg [2:0] phase_meta_cpu;
    reg [24:0] progress_meta_cpu;
    reg [31:0] errors_meta_cpu;
    reg [24:0] first_meta_cpu;
    reg [7:0] expected_meta_cpu;
    reg [7:0] actual_meta_cpu;

    function automatic [7:0] pattern_byte(input [24:0] address);
        pattern_byte = address[7:0] ^ address[15:8] ^ address[23:16] ^
                       {7'd0, address[24]} ^ 8'ha5;
    endfunction

    function automatic [31:0] pattern_word(
        input [24:0] address,
        input invert
    );
        reg [7:0] base;
        reg [31:0] value;
        begin
            // issue_addr_mem is always 4-byte aligned, so +0..+3 cannot
            // carry out of address[7:0]. Avoid three 25-bit adders on the
            // SDRAM write-data timing path.
            base = pattern_byte(address);
            value = {base, base ^ 8'h01, base ^ 8'h02, base ^ 8'h03};
            pattern_word = invert ? ~value : value;
        end
    endfunction

    function automatic [2:0] mismatch_count(input [3:0] mismatch);
        mismatch_count = {2'd0, mismatch[0]} + {2'd0, mismatch[1]} +
                         {2'd0, mismatch[2]} + {2'd0, mismatch[3]};
    endfunction

    wire [31:0] expected_word = expected_word_mem;
    wire [3:0] mismatch = {
        mem_rdata[31:24] != expected_word[31:24],
        mem_rdata[23:16] != expected_word[23:16],
        mem_rdata[15:8]  != expected_word[15:8],
        mem_rdata[7:0]   != expected_word[7:0]
    };
    wire [2:0] mismatches = mismatch_count(mismatch);
    wire accepted = mem_valid && mem_ready;

    assign mem_lock = busy_mem;
    assign mem_valid = busy_mem && !issue_done_mem &&
                       outstanding_mem < MAX_OUTSTANDING;
    assign mem_write = phase_mem == PHASE_WRITE;
    assign mem_addr = issue_addr_mem;
    assign mem_be = 4'b1111;
    assign mem_wdata = pattern_word(issue_addr_mem, invert_pattern_mem);

    always @(posedge cpu_clk) begin
        if (cpu_rst) begin
            start_toggle_cpu <= 1'b0;
            busy_sync_cpu <= 2'b00;
            done_sync_cpu <= 2'b00;
            cpu_busy <= 1'b0;
            cpu_done <= 1'b0;
            cpu_phase <= PHASE_IDLE;
            cpu_progress <= 25'd0;
            cpu_error_count <= 32'd0;
            cpu_first_fail <= 25'd0;
            cpu_expected <= 8'd0;
            cpu_actual <= 8'd0;
            phase_meta_cpu <= PHASE_IDLE;
            progress_meta_cpu <= 25'd0;
            errors_meta_cpu <= 32'd0;
            first_meta_cpu <= 25'd0;
            expected_meta_cpu <= 8'd0;
            actual_meta_cpu <= 8'd0;
        end else begin
            if (cpu_start && !cpu_busy)
                start_toggle_cpu <= ~start_toggle_cpu;

            busy_sync_cpu <= {busy_sync_cpu[0], busy_mem};
            done_sync_cpu <= {done_sync_cpu[0], done_mem};
            cpu_busy <= busy_sync_cpu[1];
            cpu_done <= done_sync_cpu[1];

            phase_meta_cpu <= phase_mem;
            progress_meta_cpu <= progress_mem;
            errors_meta_cpu <= error_count_mem;
            first_meta_cpu <= first_fail_mem;
            expected_meta_cpu <= expected_mem;
            actual_meta_cpu <= actual_mem;
            cpu_phase <= phase_meta_cpu;
            cpu_progress <= progress_meta_cpu;
            cpu_error_count <= errors_meta_cpu;
            cpu_first_fail <= first_meta_cpu;
            cpu_expected <= expected_meta_cpu;
            cpu_actual <= actual_meta_cpu;
        end
    end

    always @(posedge mem_clk) begin
        if (mem_rst) begin
            start_sync_mem <= 3'b000;
            start_seen_mem <= 1'b0;
            busy_mem <= 1'b0;
            done_mem <= 1'b0;
            phase_mem <= PHASE_IDLE;
            invert_pattern_mem <= 1'b0;
            issue_addr_mem <= 25'd0;
            retire_addr_mem <= 25'd0;
            issue_done_mem <= 1'b0;
            outstanding_mem <= 6'd0;
            progress_mem <= 25'd0;
            error_count_mem <= 32'd0;
            first_fail_mem <= 25'd0;
            expected_mem <= 8'd0;
            actual_mem <= 8'd0;
            expected_word_mem <= pattern_word(25'd0, 1'b0);
        end else begin
            start_sync_mem <= {start_sync_mem[1:0], start_toggle_cpu};

            if (!busy_mem && start_sync_mem[2] != start_seen_mem) begin
                start_seen_mem <= start_sync_mem[2];
                busy_mem <= 1'b1;
                done_mem <= 1'b0;
                phase_mem <= PHASE_WRITE;
                invert_pattern_mem <= 1'b0;
                issue_addr_mem <= 25'd0;
                retire_addr_mem <= 25'd0;
                issue_done_mem <= 1'b0;
                outstanding_mem <= 6'd0;
                progress_mem <= 25'd0;
                error_count_mem <= 32'd0;
                first_fail_mem <= 25'd0;
                expected_mem <= 8'd0;
                actual_mem <= 8'd0;
                expected_word_mem <= pattern_word(25'd0, 1'b0);
            end else if (busy_mem) begin
                case ({accepted, mem_rsp_valid})
                    2'b10: outstanding_mem <= outstanding_mem + 6'd1;
                    2'b01: outstanding_mem <= outstanding_mem - 6'd1;
                    default: outstanding_mem <= outstanding_mem;
                endcase

                if (accepted) begin
                    if (issue_addr_mem == LAST_WORD)
                        issue_done_mem <= 1'b1;
                    else
                        issue_addr_mem <= issue_addr_mem + 25'd4;
                end

                if (mem_rsp_valid) begin
                    if (retire_addr_mem == LAST_WORD) begin
                        expected_word_mem <= pattern_word(
                            25'd0,
                            invert_pattern_mem || phase_mem == PHASE_READ
                        );
                    end else begin
                        expected_word_mem <= pattern_word(
                            retire_addr_mem + 25'd4,
                            invert_pattern_mem
                        );
                    end
                    progress_mem <= retire_addr_mem;
                    if (phase_mem == PHASE_READ && mismatches != 3'd0) begin
                        if (error_count_mem == 32'd0) begin
                            if (mismatch[3]) begin
                                first_fail_mem <= retire_addr_mem;
                                expected_mem <= expected_word[31:24];
                                actual_mem <= mem_rdata[31:24];
                            end else if (mismatch[2]) begin
                                first_fail_mem <= retire_addr_mem + 25'd1;
                                expected_mem <= expected_word[23:16];
                                actual_mem <= mem_rdata[23:16];
                            end else if (mismatch[1]) begin
                                first_fail_mem <= retire_addr_mem + 25'd2;
                                expected_mem <= expected_word[15:8];
                                actual_mem <= mem_rdata[15:8];
                            end else begin
                                first_fail_mem <= retire_addr_mem + 25'd3;
                                expected_mem <= expected_word[7:0];
                                actual_mem <= mem_rdata[7:0];
                            end
                        end
                        if (error_count_mem > 32'hffffffff - mismatches)
                            error_count_mem <= 32'hffffffff;
                        else
                            error_count_mem <= error_count_mem + mismatches;
                    end

                    if (retire_addr_mem == LAST_WORD) begin
                        issue_addr_mem <= 25'd0;
                        retire_addr_mem <= 25'd0;
                        issue_done_mem <= 1'b0;
                        progress_mem <= 25'd0;
                        if (phase_mem == PHASE_WRITE) begin
                            phase_mem <= PHASE_READ;
                        end else if (!invert_pattern_mem) begin
                            invert_pattern_mem <= 1'b1;
                            phase_mem <= PHASE_WRITE;
                        end else begin
                            busy_mem <= 1'b0;
                            done_mem <= 1'b1;
                            phase_mem <= PHASE_DONE;
                            progress_mem <= LAST_WORD;
                        end
                    end else begin
                        retire_addr_mem <= retire_addr_mem + 25'd4;
                    end
                end
            end
        end
    end

`ifndef SYNTHESIS
    initial begin
        if ((MEM_BYTES % 4) != 0 || MEM_BYTES < 4)
            $fatal(1, "sdram32_bist MEM_BYTES must be a positive multiple of four");
        if (MAX_OUTSTANDING < 1 || MAX_OUTSTANDING > 63)
            $fatal(1, "sdram32_bist MAX_OUTSTANDING must be 1..63");
        if (pattern_word(25'h00000fc, 1'b0) !=
            {pattern_byte(25'h00000fc), pattern_byte(25'h00000fd),
             pattern_byte(25'h00000fe), pattern_byte(25'h00000ff)})
            $fatal(1, "optimized BIST pattern mismatch at low-byte boundary");
        if (pattern_word(25'h1fffffc, 1'b1) !=
            ~{pattern_byte(25'h1fffffc), pattern_byte(25'h1fffffd),
              pattern_byte(25'h1fffffe), pattern_byte(25'h1ffffff)})
            $fatal(1, "optimized BIST complement mismatch at top address");
    end
`endif
endmodule

`default_nettype wire
