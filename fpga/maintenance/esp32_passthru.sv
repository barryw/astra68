// Maintenance-only FTDI-to-ESP32 serial and reset passthrough.
// Production AstraHost communication with the FPGA remains SPI-only.

module astra_esp32_passthru (
    input  logic       clk25_mhz,
    input  logic       ftdi_txd,
    output logic       ftdi_rxd,
    input  logic       ftdi_ndtr,
    input  logic       ftdi_nrts,
    output logic       wifi_rxd,
    input  logic       wifi_txd,
    output logic       wifi_en,
    output logic       wifi_gpio0,
    inout  wire        sd_clk,
    inout  wire        sd_cmd,
    inout  wire [3:0]  sd_d,
    output logic [7:0] leds
);
    logic [1:0] program_pins;

    assign wifi_rxd = ftdi_txd;
    assign ftdi_rxd = wifi_txd;

    // FTDI DTR/RTS are active-low. Releasing both ESP pins when both controls
    // are asserted avoids the reset glitch caused by non-atomic host updates.
    always_comb begin
        case ({ftdi_ndtr, ftdi_nrts})
            2'b10:   program_pins = 2'b01; // RTS: EN=0, GPIO0=1
            2'b01:   program_pins = 2'b10; // DTR: EN=1, GPIO0=0
            default: program_pins = 2'b11;
        endcase
    end

    assign wifi_en    = program_pins[1];
    assign wifi_gpio0 = program_pins[0];

    // GPIO2 and SD D0 are the same physical J3 net. Hold it low only while
    // DTR requests serial download; release it for normal boot and SD access.
    wire unused_sd_clk;
    wire unused_sd_cmd;
    wire [3:0] unused_sd_d;
    TRELLIS_IO #(.DIR("BIDIR")) sd_clk_pad (
        .B(sd_clk), .I(1'b0), .T(1'b1), .O(unused_sd_clk)
    );
    TRELLIS_IO #(.DIR("BIDIR")) sd_cmd_pad (
        .B(sd_cmd), .I(1'b0), .T(1'b1), .O(unused_sd_cmd)
    );
    TRELLIS_IO #(.DIR("BIDIR")) sd_d0_pad (
        .B(sd_d[0]), .I(1'b0), .T(ftdi_ndtr), .O(unused_sd_d[0])
    );
    TRELLIS_IO #(.DIR("BIDIR")) sd_d1_pad (
        .B(sd_d[1]), .I(1'b0), .T(1'b1), .O(unused_sd_d[1])
    );
    TRELLIS_IO #(.DIR("BIDIR")) sd_d2_pad (
        .B(sd_d[2]), .I(1'b0), .T(1'b1), .O(unused_sd_d[2])
    );
    TRELLIS_IO #(.DIR("BIDIR")) sd_d3_pad (
        .B(sd_d[3]), .I(1'b0), .T(1'b1), .O(unused_sd_d[3])
    );

    assign leds[0] = ~ftdi_ndtr;
    assign leds[1] = ~ftdi_nrts;
    assign leds[2] = wifi_en;
    assign leds[3] = wifi_gpio0;
    assign leds[4] = ftdi_txd;
    assign leds[5] = wifi_txd;
    assign leds[6] = 1'b1;
    assign leds[7] = 1'b1;

endmodule
