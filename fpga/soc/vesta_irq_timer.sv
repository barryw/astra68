`timescale 1ns/1ps
`default_nettype none

// Vesta interrupt controller and the two CPU-clock timers. The register index
// is relative to VESTA_BASE + 0x300, so IRQ_PENDING is index 0 and TIMER0_LOAD
// is index 0x40.
module vesta_irq_timer (
    input  wire        clk,
    input  wire        rst,

    input  wire        select,
    input  wire [7:0]  reg_index,
    input  wire        write_strobe,
    input  wire [31:0] write_data,
    input  wire [3:0]  byte_enable,
    output reg  [31:0] read_data,

    input  wire [31:0] source_level,
    output wire [2:0]  cpu_ipln_n,

    input  wire        iack_strobe,
    input  wire [2:0]  iack_level,
    output reg  [7:0]  iack_vector,
    output reg          iack_valid
);
    localparam [7:0] REG_IRQ_PENDING = 8'h00;
    localparam [7:0] REG_IRQ_ENABLE  = 8'h01;
    localparam [7:0] REG_IRQ_SOFT    = 8'h02;
    localparam [7:0] REG_IRQ_ACK     = 8'h03;
    localparam [7:0] REG_IRQ_CURRENT = 8'h04;
    localparam [7:0] REG_IRQ_CFG     = 8'h20;
    localparam [7:0] REG_TIMER0      = 8'h40;
    localparam [7:0] REG_TIMER1      = 8'h44;

    localparam [31:0] IRQ_CFG_MASK = 32'h0001ff07;
    localparam [31:0] TIMER_CTRL_MASK = 32'h000000f7;
    localparam [7:0] SPURIOUS_VECTOR = 8'd24;

    reg [31:0] irq_enable;
    reg [31:0] soft_pending;
    reg [31:0] edge_pending;
    reg [31:0] source_previous;
    reg [31:0] irq_cfg [0:31];

    reg [31:0] timer_load [0:1];
    reg [31:0] timer_value [0:1];
    reg [31:0] timer_ctrl [0:1];
    reg [31:0] timer_status [0:1];
    reg [15:0] timer_divider [0:1];

    wire [1:0] timer_irq = {
        timer_status[1][0] & timer_ctrl[1][2],
        timer_status[0][0] & timer_ctrl[0][2]
    };
    wire [31:0] combined_source = source_level |
        {30'd0, timer_irq};
    wire [31:0] source_rise = combined_source & ~source_previous;

    reg [31:0] pending_raw;
    reg [31:0] pending_enabled;
    reg [31:0] edge_capture;
    reg [31:0] pending_level_1;
    reg [31:0] pending_level_2;
    reg [31:0] pending_level_3;
    reg [31:0] pending_level_4;
    reg [31:0] pending_level_5;
    reg [31:0] pending_level_6;
    reg [31:0] pending_level_7;
    reg [31:0] active_pending;
    reg [31:0] iack_pending;
    reg [2:0] active_level;
    reg [4:0] active_source;
    reg [7:0] active_vector;
    reg active_valid;
    reg [4:0] iack_source;
    reg [7:0] iack_vector_comb;
    reg iack_valid_comb;
    reg iack_active;
    reg [7:0] iack_vector_latched;
    reg iack_valid_latched;
    reg [2:0] cpu_ipl_level;
    integer scan_index;

    function automatic [31:0] merge_bytes(
        input [31:0] old_value,
        input [31:0] new_value,
        input [3:0] enables
    );
        begin
            merge_bytes = old_value;
            if (enables[3]) merge_bytes[31:24] = new_value[31:24];
            if (enables[2]) merge_bytes[23:16] = new_value[23:16];
            if (enables[1]) merge_bytes[15:8] = new_value[15:8];
            if (enables[0]) merge_bytes[7:0] = new_value[7:0];
        end
    endfunction

    function automatic [31:0] enabled_write_bits(
        input [31:0] value,
        input [3:0] enables
    );
        begin
            enabled_write_bits = {
                enables[3] ? value[31:24] : 8'd0,
                enables[2] ? value[23:16] : 8'd0,
                enables[1] ? value[15:8] : 8'd0,
                enables[0] ? value[7:0] : 8'd0
            };
        end
    endfunction

    function automatic prescale_terminal(
        input [15:0] count,
        input [3:0] scale
    );
        begin
            case (scale)
                4'd0:  prescale_terminal = 1'b1;
                4'd1:  prescale_terminal = count[0];
                4'd2:  prescale_terminal = &count[1:0];
                4'd3:  prescale_terminal = &count[2:0];
                4'd4:  prescale_terminal = &count[3:0];
                4'd5:  prescale_terminal = &count[4:0];
                4'd6:  prescale_terminal = &count[5:0];
                4'd7:  prescale_terminal = &count[6:0];
                4'd8:  prescale_terminal = &count[7:0];
                4'd9:  prescale_terminal = &count[8:0];
                4'd10: prescale_terminal = &count[9:0];
                4'd11: prescale_terminal = &count[10:0];
                4'd12: prescale_terminal = &count[11:0];
                4'd13: prescale_terminal = &count[12:0];
                4'd14: prescale_terminal = &count[13:0];
                default: prescale_terminal = &count[14:0];
            endcase
        end
    endfunction

    // A nested reduction/mux tree keeps lowest-source selection logarithmic.
    // A procedural first-match loop maps to a 32-element serial priority chain
    // in Yosys and cannot meet the CPU clock once routing is included.
    function automatic [4:0] lowest_set32(input [31:0] value);
        reg [4:0] base;
        reg [3:0] nibble;
        begin
            base = 5'd0;
            nibble = value[3:0];
            if (|value[15:0]) begin
                if (|value[7:0]) begin
                    if (|value[3:0]) begin
                        base = 5'd0;
                        nibble = value[3:0];
                    end else begin
                        base = 5'd4;
                        nibble = value[7:4];
                    end
                end else if (|value[11:8]) begin
                    base = 5'd8;
                    nibble = value[11:8];
                end else begin
                    base = 5'd12;
                    nibble = value[15:12];
                end
            end else if (|value[23:16]) begin
                if (|value[19:16]) begin
                    base = 5'd16;
                    nibble = value[19:16];
                end else begin
                    base = 5'd20;
                    nibble = value[23:20];
                end
            end else if (|value[27:24]) begin
                base = 5'd24;
                nibble = value[27:24];
            end else begin
                base = 5'd28;
                nibble = value[31:28];
            end

            if (value == 0)
                lowest_set32 = 5'd0;
            else begin
                casez (nibble)
                    4'b???1: lowest_set32 = base;
                    4'b??10: lowest_set32 = base + 5'd1;
                    4'b?100: lowest_set32 = base + 5'd2;
                    default: lowest_set32 = base + 5'd3;
                endcase
            end
        end
    endfunction

    always @* begin
        pending_raw = 32'd0;
        edge_capture = 32'd0;
        for (scan_index = 0; scan_index < 32; scan_index = scan_index + 1) begin
            edge_capture[scan_index] = source_rise[scan_index] &
                                       irq_cfg[scan_index][16];
            pending_raw[scan_index] = soft_pending[scan_index] |
                (irq_cfg[scan_index][16] ? edge_pending[scan_index] :
                                           combined_source[scan_index]);
        end
        pending_enabled = pending_raw & irq_enable;

        pending_level_1 = 32'd0;
        pending_level_2 = 32'd0;
        pending_level_3 = 32'd0;
        pending_level_4 = 32'd0;
        pending_level_5 = 32'd0;
        pending_level_6 = 32'd0;
        pending_level_7 = 32'd0;
        for (scan_index = 0; scan_index < 32; scan_index = scan_index + 1) begin
            pending_level_1[scan_index] = pending_enabled[scan_index] &&
                                           irq_cfg[scan_index][2:0] == 3'd1;
            pending_level_2[scan_index] = pending_enabled[scan_index] &&
                                           irq_cfg[scan_index][2:0] == 3'd2;
            pending_level_3[scan_index] = pending_enabled[scan_index] &&
                                           irq_cfg[scan_index][2:0] == 3'd3;
            pending_level_4[scan_index] = pending_enabled[scan_index] &&
                                           irq_cfg[scan_index][2:0] == 3'd4;
            pending_level_5[scan_index] = pending_enabled[scan_index] &&
                                           irq_cfg[scan_index][2:0] == 3'd5;
            pending_level_6[scan_index] = pending_enabled[scan_index] &&
                                           irq_cfg[scan_index][2:0] == 3'd6;
            pending_level_7[scan_index] = pending_enabled[scan_index] &&
                                           irq_cfg[scan_index][2:0] == 3'd7;
        end

        active_level = 3'd0;
        active_pending = 32'd0;
        active_source = 5'd0;
        active_vector = 8'd0;
        active_valid = 1'b0;
        if (|pending_level_7) begin
            active_level = 3'd7;
            active_pending = pending_level_7;
        end else if (|pending_level_6) begin
            active_level = 3'd6;
            active_pending = pending_level_6;
        end else if (|pending_level_5) begin
            active_level = 3'd5;
            active_pending = pending_level_5;
        end else if (|pending_level_4) begin
            active_level = 3'd4;
            active_pending = pending_level_4;
        end else if (|pending_level_3) begin
            active_level = 3'd3;
            active_pending = pending_level_3;
        end else if (|pending_level_2) begin
            active_level = 3'd2;
            active_pending = pending_level_2;
        end else if (|pending_level_1) begin
            active_level = 3'd1;
            active_pending = pending_level_1;
        end
        active_valid = active_level != 0;
        if (active_valid) begin
            active_source = lowest_set32(active_pending);
            active_vector = irq_cfg[active_source][15:8];
        end

        iack_vector_comb = SPURIOUS_VECTOR;
        case (iack_level)
            3'd1: iack_pending = pending_level_1;
            3'd2: iack_pending = pending_level_2;
            3'd3: iack_pending = pending_level_3;
            3'd4: iack_pending = pending_level_4;
            3'd5: iack_pending = pending_level_5;
            3'd6: iack_pending = pending_level_6;
            3'd7: iack_pending = pending_level_7;
            default: iack_pending = 32'd0;
        endcase
        iack_valid_comb = |iack_pending;
        iack_source = lowest_set32(iack_pending);
        if (iack_valid_comb) begin
            iack_vector_comb = irq_cfg[iack_source][15:8];
        end

        if (iack_strobe && iack_active) begin
            iack_vector = iack_vector_latched;
            iack_valid = iack_valid_latched;
        end else begin
            iack_vector = iack_vector_comb;
            iack_valid = iack_valid_comb;
        end
    end

    // Interrupt sources and configuration may fan through the complete
    // priority encoder. Register the selected level at the controller boundary
    // so that path cannot continue through the TG68K microstate logic in the
    // same CPU cycle. This also gives the CPU a synchronous IPL input.
    assign cpu_ipln_n = ~cpu_ipl_level;

    always @* begin
        read_data = 32'd0;
        if (select) begin
            case (reg_index)
                REG_IRQ_PENDING: read_data = pending_raw;
                REG_IRQ_ENABLE:  read_data = irq_enable;
                REG_IRQ_SOFT:    read_data = soft_pending;
                REG_IRQ_ACK:     read_data = 32'd0;
                REG_IRQ_CURRENT: read_data = {
                    active_valid, 7'd0, active_vector, 3'd0,
                    active_source, 5'd0, active_level
                };
                REG_TIMER0 + 0: read_data = timer_load[0];
                REG_TIMER0 + 1: read_data = timer_value[0];
                REG_TIMER0 + 2: read_data = timer_ctrl[0];
                REG_TIMER0 + 3: read_data = timer_status[0];
                REG_TIMER1 + 0: read_data = timer_load[1];
                REG_TIMER1 + 1: read_data = timer_value[1];
                REG_TIMER1 + 2: read_data = timer_ctrl[1];
                REG_TIMER1 + 3: read_data = timer_status[1];
                default: begin
                    if (reg_index >= REG_IRQ_CFG &&
                        reg_index < REG_IRQ_CFG + 32)
                        read_data = irq_cfg[reg_index[4:0]];
                end
            endcase
        end
    end

    integer reset_index;
    always @(posedge clk) begin
        if (rst) begin
            irq_enable <= 32'd0;
            soft_pending <= 32'd0;
            edge_pending <= 32'd0;
            source_previous <= 32'd0;
            cpu_ipl_level <= 3'd0;
            iack_active <= 1'b0;
            iack_vector_latched <= SPURIOUS_VECTOR;
            iack_valid_latched <= 1'b0;
            for (reset_index = 0; reset_index < 32;
                 reset_index = reset_index + 1)
                irq_cfg[reset_index] <= 32'd0;
            for (reset_index = 0; reset_index < 2;
                 reset_index = reset_index + 1) begin
                timer_load[reset_index] <= 32'd0;
                timer_value[reset_index] <= 32'd0;
                timer_ctrl[reset_index] <= 32'd0;
                timer_status[reset_index] <= 32'd0;
                timer_divider[reset_index] <= 16'd0;
            end
        end else begin
            cpu_ipl_level <= active_level;
            source_previous <= combined_source;

            if (!iack_strobe) begin
                iack_active <= 1'b0;
            end else if (!iack_active) begin
                iack_active <= 1'b1;
                iack_vector_latched <= iack_vector_comb;
                iack_valid_latched <= iack_valid_comb;
            end

            if (select && write_strobe && reg_index == REG_IRQ_ACK) begin
                edge_pending <= (edge_pending &
                    ~enabled_write_bits(write_data, byte_enable)) | edge_capture;
                soft_pending <= soft_pending &
                    ~enabled_write_bits(write_data, byte_enable);
            end else begin
                edge_pending <= edge_pending | edge_capture;
                if (select && write_strobe && reg_index == REG_IRQ_SOFT)
                    soft_pending <= merge_bytes(soft_pending, write_data,
                                                byte_enable);
            end

            if (select && write_strobe && reg_index == REG_IRQ_ENABLE)
                irq_enable <= merge_bytes(irq_enable, write_data, byte_enable);

            if (select && write_strobe && reg_index >= REG_IRQ_CFG &&
                reg_index < REG_IRQ_CFG + 32)
                irq_cfg[reg_index[4:0]] <=
                    merge_bytes(irq_cfg[reg_index[4:0]], write_data,
                                byte_enable) & IRQ_CFG_MASK;

            if (timer_ctrl[0][0]) begin
                if (prescale_terminal(timer_divider[0],
                                      timer_ctrl[0][7:4])) begin
                    timer_divider[0] <= 16'd0;
                    if (timer_value[0] <= 1) begin
                        timer_status[0][0] <= 1'b1;
                        if (timer_ctrl[0][1])
                            timer_value[0] <= timer_load[0];
                        else begin
                            timer_value[0] <= 32'd0;
                            timer_ctrl[0][0] <= 1'b0;
                        end
                    end else begin
                        timer_value[0] <= timer_value[0] - 1'b1;
                    end
                end else begin
                    timer_divider[0] <= timer_divider[0] + 1'b1;
                end
            end

            if (timer_ctrl[1][0]) begin
                if (prescale_terminal(timer_divider[1],
                                      timer_ctrl[1][7:4])) begin
                    timer_divider[1] <= 16'd0;
                    if (timer_value[1] <= 1) begin
                        timer_status[1][0] <= 1'b1;
                        if (timer_ctrl[1][1])
                            timer_value[1] <= timer_load[1];
                        else begin
                            timer_value[1] <= 32'd0;
                            timer_ctrl[1][0] <= 1'b0;
                        end
                    end else begin
                        timer_value[1] <= timer_value[1] - 1'b1;
                    end
                end else begin
                    timer_divider[1] <= timer_divider[1] + 1'b1;
                end
            end

            if (select && write_strobe) begin
                case (reg_index)
                    REG_TIMER0 + 0:
                        timer_load[0] <= merge_bytes(timer_load[0], write_data,
                                                     byte_enable);
                    REG_TIMER0 + 2: begin
                        timer_ctrl[0] <= merge_bytes(timer_ctrl[0], write_data,
                                                     byte_enable) &
                                         TIMER_CTRL_MASK;
                        if (byte_enable[0]) begin
                            timer_divider[0] <= 16'd0;
                            if (write_data[0])
                                timer_value[0] <= timer_load[0];
                        end
                    end
                    REG_TIMER0 + 3:
                        timer_status[0] <= timer_status[0] &
                            ~enabled_write_bits(write_data, byte_enable);
                    REG_TIMER1 + 0:
                        timer_load[1] <= merge_bytes(timer_load[1], write_data,
                                                     byte_enable);
                    REG_TIMER1 + 2: begin
                        timer_ctrl[1] <= merge_bytes(timer_ctrl[1], write_data,
                                                     byte_enable) &
                                         TIMER_CTRL_MASK;
                        if (byte_enable[0]) begin
                            timer_divider[1] <= 16'd0;
                            if (write_data[0])
                                timer_value[1] <= timer_load[1];
                        end
                    end
                    REG_TIMER1 + 3:
                        timer_status[1] <= timer_status[1] &
                            ~enabled_write_bits(write_data, byte_enable);
                    default: begin end
                endcase
            end

            // Expiration wins over a simultaneous RW1C write.
            if (timer_ctrl[0][0] &&
                prescale_terminal(timer_divider[0], timer_ctrl[0][7:4]) &&
                timer_value[0] <= 1)
                timer_status[0][0] <= 1'b1;
            if (timer_ctrl[1][0] &&
                prescale_terminal(timer_divider[1], timer_ctrl[1][7:4]) &&
                timer_value[1] <= 1)
                timer_status[1][0] <= 1'b1;
        end
    end
endmodule

`default_nettype wire
