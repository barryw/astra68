module usb_pll_probe (
    input  wire       clk25_mhz,
    input  wire       reset_n,
    output wire       ftdi_rxd,
    output wire [7:0] leds
);
    wire [3:0] root_clk;
    wire       root_locked;
    wire [3:0] cascade_reset_clk;
    wire       cascade_reset_locked;
    wire [3:0] cascade_free_clk;
    wire       cascade_free_locked;
    wire [3:0] direct_clk;
    wire       direct_locked;

    ecp5pll #(
        .in_hz(25000000),
        .out0_hz(50000000),
        .out1_hz(120000000),
        .out2_hz(60000000),
        .out3_hz(0)
    ) root_pll (
        .clk_i(clk25_mhz),
        .clk_o(root_clk),
        .locked(root_locked),
        .reset(1'b0),
        .standby(1'b0),
        .phasesel(2'b00),
        .phasedir(1'b0),
        .phasestep(1'b0),
        .phaseloadreg(1'b0)
    );

    ecp5pll #(
        .in_hz(120000000),
        .out0_hz(48000000),
        .out1_hz(0),
        .out2_hz(0),
        .out3_hz(0),
        .reset_en(1)
    ) cascade_reset_pll (
        .clk_i(root_clk[1]),
        .clk_o(cascade_reset_clk),
        .locked(cascade_reset_locked),
        .reset(!root_locked),
        .standby(1'b0),
        .phasesel(2'b00),
        .phasedir(1'b0),
        .phasestep(1'b0),
        .phaseloadreg(1'b0)
    );

    ecp5pll #(
        .in_hz(120000000),
        .out0_hz(48000000),
        .out1_hz(0),
        .out2_hz(0),
        .out3_hz(0)
    ) cascade_free_pll (
        .clk_i(root_clk[1]),
        .clk_o(cascade_free_clk),
        .locked(cascade_free_locked),
        .reset(1'b0),
        .standby(1'b0),
        .phasesel(2'b00),
        .phasedir(1'b0),
        .phasestep(1'b0),
        .phaseloadreg(1'b0)
    );

    // Exercise the production video topology as the fourth PLL.
    ecp5pll #(
        .in_hz(60000000),
        .out0_hz(135000000),
        .out1_hz(27000000),
        .out2_hz(0),
        .out3_hz(0),
        .reset_en(1)
    ) direct_pll (
        .clk_i(root_clk[2]),
        .clk_o(direct_clk),
        .locked(direct_locked),
        .reset(!root_locked),
        .standby(1'b0),
        .phasesel(2'b00),
        .phasedir(1'b0),
        .phasestep(1'b0),
        .phaseloadreg(1'b0)
    );

    reg [25:0] root_count = 26'd0;
    reg [24:0] cascade_reset_count = 25'd0;
    reg [24:0] cascade_free_count = 25'd0;
    reg [24:0] direct_count = 25'd0;

    always @(posedge root_clk[2])
        root_count <= root_count + 26'd1;
    always @(posedge cascade_reset_clk[0])
        cascade_reset_count <= cascade_reset_count + 25'd1;
    always @(posedge cascade_free_clk[0])
        cascade_free_count <= cascade_free_count + 25'd1;
    always @(posedge direct_clk[0])
        direct_count <= direct_count + 25'd1;

    // Synchronize a counter bit from each generated clock and remember whether
    // it changed. This distinguishes a locked-but-dead output from a clock that
    // has actually run, without relying on visually inspecting LED rates.
    reg [1:0] root_lock_sync = 2'b00;
    reg [1:0] cascade_reset_lock_sync = 2'b00;
    reg [1:0] cascade_free_lock_sync = 2'b00;
    reg [1:0] direct_lock_sync = 2'b00;
    reg [1:0] root_activity_sync = 2'b00;
    reg [1:0] cascade_reset_activity_sync = 2'b00;
    reg [1:0] cascade_free_activity_sync = 2'b00;
    reg [1:0] direct_activity_sync = 2'b00;
    reg [3:0] activity_previous = 4'b0000;
    reg [3:0] activity_seen = 4'b0000;

    always @(posedge clk25_mhz) begin
        root_lock_sync <= {root_lock_sync[0], root_locked};
        cascade_reset_lock_sync <= {
            cascade_reset_lock_sync[0], cascade_reset_locked
        };
        cascade_free_lock_sync <= {
            cascade_free_lock_sync[0], cascade_free_locked
        };
        direct_lock_sync <= {direct_lock_sync[0], direct_locked};

        root_activity_sync <= {root_activity_sync[0], root_count[18]};
        cascade_reset_activity_sync <= {
            cascade_reset_activity_sync[0], cascade_reset_count[18]
        };
        cascade_free_activity_sync <= {
            cascade_free_activity_sync[0], cascade_free_count[18]
        };
        direct_activity_sync <= {
            direct_activity_sync[0], direct_count[18]
        };

        activity_previous <= {
            direct_activity_sync[1],
            cascade_free_activity_sync[1],
            cascade_reset_activity_sync[1],
            root_activity_sync[1]
        };
        activity_seen <= activity_seen | (activity_previous ^ {
            direct_activity_sync[1],
            cascade_free_activity_sync[1],
            cascade_reset_activity_sync[1],
            root_activity_sync[1]
        });
    end

    wire [7:0] status = {
        activity_seen[3], direct_lock_sync[1],
        activity_seen[2], cascade_free_lock_sync[1],
        activity_seen[1], cascade_reset_lock_sync[1],
        activity_seen[0], root_lock_sync[1]
    };

    // Reproduce the production SDRAM/USB reset and ready qualification exactly.
    // This separates a logic fault in that circuit from a full-SoC routing or
    // placement interaction while retaining the real ECP5 PLL primitives.
    reg [2:0] cpu_clock_divider = 3'd0;
    wire cpu_clk = cpu_clock_divider[0];
    reg [15:0] cpu_reset_count = 16'd0;
    reg cpu_reset = 1'b1;
    reg [1:0] sd_lock_sync = 2'b00;
    reg [1:0] sd_reset_sync = 2'b11;
    reg [19:0] sd_boot_count = 20'd0;
    reg sd_ready = 1'b0;
    wire sd_locked = sd_lock_sync[1];
    wire sd_manual_reset = !sd_reset_sync[1];
    wire usb_ctrl_reset;
    wire usb_mem_reset;
    wire usb_phy_reset;
    reg [1:0] usb_ready_sync_cpu = 2'b00;

    always @(posedge clk25_mhz)
        cpu_clock_divider <= cpu_clock_divider + 3'd1;

    always @(posedge cpu_clk) begin
        if (!reset_n) begin
            cpu_reset_count <= 16'd0;
            cpu_reset <= 1'b1;
        end else if (cpu_reset_count != 16'hffff) begin
            cpu_reset_count <= cpu_reset_count + 16'd1;
        end else begin
            cpu_reset <= 1'b0;
        end
    end

    always @(posedge root_clk[2]) begin
        sd_lock_sync <= {sd_lock_sync[0], root_locked};
        sd_reset_sync <= {sd_reset_sync[0], reset_n};
        if (!sd_locked || sd_manual_reset) begin
            sd_boot_count <= 20'd0;
            sd_ready <= 1'b0;
        end else if (!sd_ready) begin
            if (sd_boot_count >= 20'hfffff)
                sd_ready <= 1'b1;
            else
                sd_boot_count <= sd_boot_count + 20'd1;
        end
    end

    probe_reset_release usb_ctrl_reset_i (
        .clk(clk25_mhz),
        .assert_reset(!reset_n || !sd_ready || !cascade_reset_locked),
        .reset(usb_ctrl_reset)
    );
    probe_reset_release usb_mem_reset_i (
        .clk(root_clk[2]),
        .assert_reset(!reset_n || !sd_ready || !cascade_reset_locked),
        .reset(usb_mem_reset)
    );
    probe_reset_release usb_phy_reset_i (
        .clk(cascade_reset_clk[0]),
        .assert_reset(!reset_n || !cascade_reset_locked || !sd_ready),
        .reset(usb_phy_reset)
    );

    always @(posedge cpu_clk) begin
        if (cpu_reset) begin
            usb_ready_sync_cpu <= 2'b00;
        end else begin
            usb_ready_sync_cpu <= {
                usb_ready_sync_cpu[0],
                sd_ready && cascade_reset_locked &&
                !usb_ctrl_reset && !usb_mem_reset && !usb_phy_reset
            };
        end
    end

    wire [7:0] qualification_status = {
        usb_ready_sync_cpu[1], !usb_phy_reset,
        !usb_mem_reset, !usb_ctrl_reset,
        cascade_reset_locked, sd_ready,
        sd_locked, root_locked
    };

    function automatic [7:0] hex_digit(input [3:0] nibble);
        if (nibble < 10)
            hex_digit = "0" + nibble;
        else
            hex_digit = "A" + (nibble - 10);
    endfunction

    localparam integer UART_MSG_LEN = 13;
    localparam integer UART_GAP_CLKS = 25000000 / 2;
    reg [7:0] uart_data = 8'h00;
    reg       uart_start = 1'b0;
    wire      uart_busy;
    reg [3:0] uart_index = 4'd0;
    reg [23:0] uart_gap = 24'd0;
    reg [7:0] reported_status = 8'h00;
    reg [7:0] reported_qualification = 8'h00;
    reg [7:0] reset_count = 8'h00;
    wire uart_reset = !reset_count[7];

    always @(posedge clk25_mhz)
        if (!reset_count[7])
            reset_count <= reset_count + 8'd1;

    uart_tx #(.CLK_HZ(25000000), .BAUD(115200)) uart_i (
        .clk(clk25_mhz),
        .rst(uart_reset),
        .data(uart_data),
        .start(uart_start),
        .tx(ftdi_rxd),
        .busy(uart_busy)
    );

    function automatic [7:0] uart_message(
        input [3:0] index,
        input [7:0] value,
        input [7:0] qualification
    );
        case (index)
            4'd0: uart_message = "P";
            4'd1: uart_message = "L";
            4'd2: uart_message = "L";
            4'd3: uart_message = "=";
            4'd4: uart_message = hex_digit(value[7:4]);
            4'd5: uart_message = hex_digit(value[3:0]);
            4'd6: uart_message = " ";
            4'd7: uart_message = "Q";
            4'd8: uart_message = "=";
            4'd9: uart_message = hex_digit(qualification[7:4]);
            4'd10: uart_message = hex_digit(qualification[3:0]);
            4'd11: uart_message = "\r";
            4'd12: uart_message = "\n";
            default: uart_message = 8'h00;
        endcase
    endfunction

    always @(posedge clk25_mhz) begin
        uart_start <= 1'b0;
        if (uart_reset) begin
            uart_index <= 4'd0;
            uart_gap <= UART_GAP_CLKS[23:0];
            reported_status <= 8'h00;
            reported_qualification <= 8'h00;
        end else if (uart_gap != 0) begin
            uart_gap <= uart_gap - 1'b1;
        end else if (!uart_busy && !uart_start) begin
            if (uart_index == 0) begin
                reported_status <= status;
                reported_qualification <= qualification_status;
            end
            uart_data <= uart_message(
                uart_index,
                uart_index == 0 ? status : reported_status,
                uart_index == 0 ? qualification_status :
                                  reported_qualification
            );
            uart_start <= 1'b1;
            if (uart_index == UART_MSG_LEN - 1) begin
                uart_index <= 4'd0;
                uart_gap <= UART_GAP_CLKS[23:0];
            end else begin
                uart_index <= uart_index + 1'b1;
            end
        end
    end

    assign leds = status;
endmodule

module probe_reset_release (
    input  wire clk,
    input  wire assert_reset,
    output wire reset
);
    reg [1:0] release_pipe = 2'b11;

    always @(posedge clk or posedge assert_reset) begin
        if (assert_reset)
            release_pipe <= 2'b11;
        else
            release_pipe <= {release_pipe[0], 1'b0};
    end

    assign reset = release_pipe[1];
endmodule
