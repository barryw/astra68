`timescale 1ns/1ps
`default_nettype none

module astra_front_panel #(
    parameter integer CLK_HZ = 12500000,
    parameter integer SAMPLE_HZ = 1000,
    parameter integer DEBOUNCE_SAMPLES = 5
) (
    input  wire        clk,
    input  wire        rst,
    input  wire [5:0]  buttons,
    input  wire [3:0]  switches,
    input  wire        select,
    input  wire [5:0]  reg_index,
    input  wire        write_strobe,
    input  wire [31:0] write_data,
    input  wire [3:0]  byte_enable,
    output reg  [31:0] read_data,
    input  wire [7:0]  diagnostic_leds,
    output wire [7:0]  leds
);
    localparam integer INPUT_COUNT = 10;
    localparam integer SAMPLE_DIV_CALC = CLK_HZ / SAMPLE_HZ;
    localparam integer SAMPLE_DIV = SAMPLE_DIV_CALC < 1 ? 1 : SAMPLE_DIV_CALC;
    localparam integer SAMPLE_COUNTER_WIDTH = SAMPLE_DIV <= 1 ? 1 : $clog2(SAMPLE_DIV);
    localparam integer DEBOUNCE_COUNTER_WIDTH = DEBOUNCE_SAMPLES <= 1 ?
                                                1 : $clog2(DEBOUNCE_SAMPLES);
    localparam [SAMPLE_COUNTER_WIDTH-1:0] SAMPLE_COUNT_MAX =
                                              SAMPLE_COUNTER_WIDTH'(SAMPLE_DIV - 1);
    localparam [DEBOUNCE_COUNTER_WIDTH-1:0] DEBOUNCE_COUNT_MAX =
                                 DEBOUNCE_COUNTER_WIDTH'(DEBOUNCE_SAMPLES - 1);

    localparam [5:0] REG_ID            = 6'h00;
    localparam [5:0] REG_VERSION       = 6'h01;
    localparam [5:0] REG_CAPS          = 6'h02;
    localparam [5:0] REG_INPUT         = 6'h03;
    localparam [5:0] REG_RAW_INPUT     = 6'h04;
    localparam [5:0] REG_CHANGE        = 6'h05;
    localparam [5:0] REG_LED_DATA      = 6'h06;
    localparam [5:0] REG_LED_OWNERSHIP = 6'h07;
    localparam [5:0] REG_LED_SET       = 6'h08;
    localparam [5:0] REG_LED_CLEAR     = 6'h09;
    localparam [5:0] REG_LED_TOGGLE    = 6'h0a;

    wire [INPUT_COUNT-1:0] input_pins = {switches, buttons};
    (* async_reg = "true" *) reg [INPUT_COUNT-1:0] input_meta = 10'd0;
    (* async_reg = "true" *) reg [INPUT_COUNT-1:0] input_sync = 10'd0;
    reg [INPUT_COUNT-1:0] input_stable = 10'd0;
    reg [INPUT_COUNT-1:0] input_events = 10'd0;
    reg [INPUT_COUNT-1:0] input_changes = 10'd0;
    reg [DEBOUNCE_COUNTER_WIDTH-1:0] debounce_count [0:INPUT_COUNT-1];
    reg [SAMPLE_COUNTER_WIDTH-1:0] sample_counter = {SAMPLE_COUNTER_WIDTH{1'b0}};
    wire sample_tick = sample_counter == SAMPLE_COUNT_MAX;

    wire change_write = select && write_strobe && reg_index == REG_CHANGE;
    wire [INPUT_COUNT-1:0] change_clear = change_write ?
        {byte_enable[1] ? write_data[11:8] : 4'd0,
         byte_enable[0] ? write_data[5:0]  : 6'd0} : 10'd0;

    integer input_index;
    always @(posedge clk) begin
        if (rst) begin
            input_meta <= 10'd0;
            input_sync <= 10'd0;
            input_stable <= 10'd0;
            input_events <= 10'd0;
            sample_counter <= {SAMPLE_COUNTER_WIDTH{1'b0}};
            for (input_index = 0; input_index < INPUT_COUNT;
                 input_index = input_index + 1)
                debounce_count[input_index] <= {DEBOUNCE_COUNTER_WIDTH{1'b0}};
        end else begin
            input_meta <= input_pins;
            input_sync <= input_meta;
            input_events <= 10'd0;

            if (sample_tick)
                sample_counter <= {SAMPLE_COUNTER_WIDTH{1'b0}};
            else
                sample_counter <= sample_counter + 1'b1;

            if (sample_tick) begin
                for (input_index = 0; input_index < INPUT_COUNT;
                     input_index = input_index + 1) begin
                    if (input_sync[input_index] == input_stable[input_index]) begin
                        debounce_count[input_index] <=
                            {DEBOUNCE_COUNTER_WIDTH{1'b0}};
                    end else if (DEBOUNCE_SAMPLES <= 1 ||
                                 debounce_count[input_index] == DEBOUNCE_COUNT_MAX) begin
                        input_stable[input_index] <= input_sync[input_index];
                        input_events[input_index] <= 1'b1;
                        debounce_count[input_index] <=
                            {DEBOUNCE_COUNTER_WIDTH{1'b0}};
                    end else begin
                        debounce_count[input_index] <=
                            debounce_count[input_index] + 1'b1;
                    end
                end
            end
        end
    end

    always @(posedge clk) begin
        if (rst)
            input_changes <= 10'd0;
        else
            input_changes <= (input_changes & ~change_clear) | input_events;
    end

    reg [7:0] led_data = 8'd0;
    reg [7:0] led_ownership = 8'd0;
    wire led_write = select && write_strobe && byte_enable[0];

    always @(posedge clk) begin
        if (rst) begin
            led_data <= 8'd0;
            led_ownership <= 8'd0;
        end else if (led_write) begin
            case (reg_index)
                REG_LED_DATA:      led_data <= write_data[7:0];
                REG_LED_OWNERSHIP: led_ownership <= write_data[7:0];
                REG_LED_SET:       led_data <= led_data | write_data[7:0];
                REG_LED_CLEAR:     led_data <= led_data & ~write_data[7:0];
                REG_LED_TOGGLE:    led_data <= led_data ^ write_data[7:0];
                default: begin end
            endcase
        end
    end

    assign leds = (led_data & led_ownership) |
                  (diagnostic_leds & ~led_ownership);

    always @* begin
        case (reg_index)
            REG_ID:            read_data = 32'h504e4c30; // "PNL0"
            REG_VERSION:       read_data = 32'h00010000;
            REG_CAPS:          read_data = 32'h0f040608;
            REG_INPUT:         read_data = {20'd0, input_stable[9:6],
                                            2'd0, input_stable[5:0]};
            REG_RAW_INPUT:     read_data = {20'd0, input_sync[9:6],
                                            2'd0, input_sync[5:0]};
            REG_CHANGE:        read_data = {20'd0, input_changes[9:6],
                                            2'd0, input_changes[5:0]};
            REG_LED_DATA:      read_data = {24'd0, led_data};
            REG_LED_OWNERSHIP: read_data = {24'd0, led_ownership};
            default:           read_data = 32'd0;
        endcase
    end
endmodule

`default_nettype wire
