// Astraea raster coprocessor. Instructions live in deterministic dual-port
// block RAM; execution and chipset register writes use the CPU clock domain.
`default_nettype none

module astraea_copper (
    input  wire        clk,
    input  wire        rst,

    input  wire        cpu_write_stb,
    input  wire [15:0] cpu_addr,
    input  wire [3:0]  cpu_be,
    input  wire [31:0] cpu_wdata,
    output reg  [31:0] cpu_rdata,

    input  wire [9:0]  beam_x_async,
    input  wire [9:0]  beam_y_async,

    output reg         move_stb,
    output reg  [17:0] move_addr,
    output reg  [31:0] move_data,
    output reg         irq_event,
    output reg  [3:0]  irq_sources,
    output wire        running,
    output wire        waiting,
    output wire        fault
);
    localparam [2:0] OP_END  = 3'd0;
    localparam [2:0] OP_MOVE = 3'd1;
    localparam [2:0] OP_WAIT = 3'd2;
    localparam [2:0] OP_SKIP = 3'd3;
    localparam [2:0] OP_IRQ  = 3'd4;
    localparam [2:0] OP_JUMP = 3'd5;

    localparam [1:0] EXEC_STOPPED = 2'd0;
    localparam [1:0] EXEC_FETCH   = 2'd1;
    localparam [1:0] EXEC_RUN     = 2'd2;
    localparam [1:0] EXEC_EVAL    = 2'd3;

    function automatic [31:0] merge_be(
        input [31:0] old_value,
        input [31:0] new_value,
        input [3:0] enables
    );
        reg [31:0] value;
        begin
            value = old_value;
            if (enables[3]) value[31:24] = new_value[31:24];
            if (enables[2]) value[23:16] = new_value[23:16];
            if (enables[1]) value[15:8] = new_value[15:8];
            if (enables[0]) value[7:0] = new_value[7:0];
            merge_be = value;
        end
    endfunction

    function automatic beam_reached(
        input [15:0] target_y,
        input [15:0] target_x,
        input [9:0] current_y,
        input [9:0] current_x
    );
        begin
            beam_reached = {6'd0, current_y} > target_y ||
                           ({6'd0, current_y} == target_y &&
                            {6'd0, current_x} >= target_x);
        end
    endfunction

    reg [31:0] reg_ctrl;
    reg [10:0] reg_start;
    reg [10:0] pc;
    reg [1:0] exec_state;
    reg fault_sticky;
    reg [31:0] instruction_w0;
    reg [31:0] instruction_w1;
    reg [31:0] execute_w0;
    reg [31:0] execute_w1;

    assign running = exec_state != EXEC_STOPPED;
    assign waiting = exec_state == EXEC_EVAL && execute_w0[31:29] == OP_WAIT;
    assign fault = fault_sticky;

    // Two 32-bit banks infer eight ECP5 DP16KD blocks total. Port A is CPU
    // read/write and port B is the copper instruction fetch.
    (* ram_style = "block" *) reg [31:0] instruction_w0_mem [0:2047];
    (* ram_style = "block" *) reg [31:0] instruction_w1_mem [0:2047];
    reg [31:0] cpu_ram_w0_q;
    reg [31:0] cpu_ram_w1_q;

    wire cpu_ram_select = cpu_addr[15:14] == 2'b01;
    wire [10:0] cpu_ram_index = cpu_addr[13:3];
    wire cpu_ram_half = cpu_addr[2];
    wire cpu_ram_write = cpu_write_stb && cpu_ram_select;

    integer init_index;
    initial begin
        for (init_index = 0; init_index < 2048; init_index = init_index + 1) begin
            instruction_w0_mem[init_index] = 32'd0;
            instruction_w1_mem[init_index] = 32'd0;
        end
    end

    always @(posedge clk) begin
        cpu_ram_w0_q <= instruction_w0_mem[cpu_ram_index];
        cpu_ram_w1_q <= instruction_w1_mem[cpu_ram_index];
        // A 68030 long store arrives as two 16-bit bus portions. Byte enables
        // keep the software ABI atomic while allowing the physical bus to
        // populate each 32-bit instruction word over both cycles.
        if (cpu_ram_write && !cpu_ram_half) begin
            if (cpu_be[3])
                instruction_w0_mem[cpu_ram_index][31:24] <= cpu_wdata[31:24];
            if (cpu_be[2])
                instruction_w0_mem[cpu_ram_index][23:16] <= cpu_wdata[23:16];
            if (cpu_be[1])
                instruction_w0_mem[cpu_ram_index][15:8] <= cpu_wdata[15:8];
            if (cpu_be[0])
                instruction_w0_mem[cpu_ram_index][7:0] <= cpu_wdata[7:0];
        end
        if (cpu_ram_write && cpu_ram_half) begin
            if (cpu_be[3])
                instruction_w1_mem[cpu_ram_index][31:24] <= cpu_wdata[31:24];
            if (cpu_be[2])
                instruction_w1_mem[cpu_ram_index][23:16] <= cpu_wdata[23:16];
            if (cpu_be[1])
                instruction_w1_mem[cpu_ram_index][15:8] <= cpu_wdata[15:8];
            if (cpu_be[0])
                instruction_w1_mem[cpu_ram_index][7:0] <= cpu_wdata[7:0];
        end
    end

    always @(posedge clk) begin
        instruction_w0 <= instruction_w0_mem[pc];
        instruction_w1 <= instruction_w1_mem[pc];
    end

    reg [9:0] beam_x_meta;
    reg [9:0] beam_x;
    reg [9:0] beam_y_meta;
    reg [9:0] beam_y;
    reg [1:0] vblank_sync;
    reg       vblank_seen;

    wire ctrl_write = cpu_write_stb && cpu_addr[15:2] == 14'h0020;
    wire start_write = cpu_write_stb && cpu_addr[15:2] == 14'h0021;
    wire strobe_write = cpu_write_stb && cpu_addr[15:2] == 14'h0023 &&
                        cpu_be[0] && cpu_wdata[0];
    wire [31:0] merged_ctrl = merge_be(reg_ctrl, cpu_wdata, cpu_be);
    wire [31:0] merged_start = merge_be({21'd0, reg_start}, cpu_wdata,
                                        cpu_be);
    wire enabled_effective = ctrl_write ? merged_ctrl[0] : reg_ctrl[0];
    // Restart as the beam leaves vertical blanking. Restarting on entry would
    // make every visible-line WAIT compare true at y>=480 and collapse the
    // complete raster list into the blanking interval.
    wire frame_start_event = !vblank_sync[1] && vblank_seen;
    wire restart_vblank = frame_start_event && enabled_effective &&
                          (ctrl_write ? merged_ctrl[1] : reg_ctrl[1]);
    wire wait_satisfied = beam_reached(execute_w0[15:0],
                                       execute_w1[15:0], beam_y, beam_x);
    wire move_encoding_valid = execute_w0[28:18] == 11'd0 &&
                               execute_w0[1:0] == 2'b00;

    always @(posedge clk) begin
        move_stb <= 1'b0;
        irq_event <= 1'b0;
        if (rst) begin
            reg_ctrl <= 32'd0;
            reg_start <= 11'd0;
            pc <= 11'd0;
            exec_state <= EXEC_STOPPED;
            fault_sticky <= 1'b0;
            execute_w0 <= 32'd0;
            execute_w1 <= 32'd0;
            move_addr <= 18'd0;
            move_data <= 32'd0;
            irq_sources <= 4'd0;
            beam_x_meta <= 10'd0;
            beam_x <= 10'd0;
            beam_y_meta <= 10'd0;
            beam_y <= 10'd0;
            vblank_sync <= 2'b00;
            vblank_seen <= 1'b0;
        end else begin
            beam_x_meta <= beam_x_async;
            beam_x <= beam_x_meta;
            beam_y_meta <= beam_y_async;
            beam_y <= beam_y_meta;
            vblank_sync <= {vblank_sync[0], beam_y_async >= 10'd480};
            vblank_seen <= vblank_sync[1];

            if (ctrl_write)
                reg_ctrl <= merged_ctrl & 32'h00000003;
            if (start_write)
                reg_start <= merged_start[10:0];

            if (!enabled_effective) begin
                exec_state <= EXEC_STOPPED;
            end else if (strobe_write || restart_vblank) begin
                pc <= reg_start;
                exec_state <= EXEC_FETCH;
                fault_sticky <= 1'b0;
            end else begin
                case (exec_state)
                    EXEC_STOPPED: begin end
                    EXEC_FETCH: exec_state <= EXEC_RUN;
                    EXEC_RUN: begin
                        execute_w0 <= instruction_w0;
                        execute_w1 <= instruction_w1;
                        exec_state <= EXEC_EVAL;
                    end
                    EXEC_EVAL: begin
                        case (execute_w0[31:29])
                            OP_END: exec_state <= EXEC_STOPPED;
                            OP_MOVE: begin
                                if (move_encoding_valid) begin
                                    move_stb <= 1'b1;
                                    move_addr <= execute_w0[17:0];
                                    move_data <= execute_w1;
                                    pc <= pc + 11'd1;
                                    exec_state <= EXEC_FETCH;
                                end else begin
                                    fault_sticky <= 1'b1;
                                    exec_state <= EXEC_STOPPED;
                                end
                            end
                            OP_WAIT: begin
                                if (wait_satisfied) begin
                                    pc <= pc + 11'd1;
                                    exec_state <= EXEC_FETCH;
                                end
                            end
                            OP_SKIP: begin
                                pc <= pc + (wait_satisfied ? 11'd2 : 11'd1);
                                exec_state <= EXEC_FETCH;
                            end
                            OP_IRQ: begin
                                irq_sources <= execute_w0[3:0];
                                irq_event <= 1'b1;
                                pc <= pc + 11'd1;
                                exec_state <= EXEC_FETCH;
                            end
                            OP_JUMP: begin
                                pc <= execute_w0[10:0];
                                exec_state <= EXEC_FETCH;
                            end
                            default: begin
                                fault_sticky <= 1'b1;
                                exec_state <= EXEC_STOPPED;
                            end
                        endcase
                    end
                    default: begin
                        fault_sticky <= 1'b1;
                        exec_state <= EXEC_STOPPED;
                    end
                endcase
            end
        end
    end

    always @* begin
        cpu_rdata = 32'd0;
        if (cpu_ram_select) begin
            cpu_rdata = cpu_ram_half ? cpu_ram_w1_q : cpu_ram_w0_q;
        end else begin
            case (cpu_addr[15:2])
                14'h0020: cpu_rdata = reg_ctrl;
                14'h0021: cpu_rdata = {21'd0, reg_start};
                14'h0022: cpu_rdata = {13'd0, fault_sticky, waiting,
                                        running, 5'd0, pc};
                default: cpu_rdata = 32'd0;
            endcase
        end
    end
endmodule

`default_nettype wire
