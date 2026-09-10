`timescale 1ns/1ps
`default_nettype none

module tb_astra_display_axis_scaler;
    reg clk = 1'b0;
    always #3 clk = ~clk;
    reg reset = 1'b1;
    reg sample_valid = 1'b0;
    reg sequence_start = 1'b0;
    reg [11:0] physical_position = 12'd0;
    reg [11:0] viewport_origin = 12'd0;
    reg [11:0] viewport_extent = 12'd1920;
    reg [11:0] source_origin = 12'd0;
    reg [11:0] source_extent = 12'd1920;
    wire output_valid;
    wire output_active;
    wire [11:0] source_position;

    astra_display_axis_scaler dut (.*);

    task automatic check_mapping(
        input integer physical_total,
        input integer view_origin,
        input integer view_extent,
        input integer source_start,
        input integer source_size
    );
        integer position;
        integer expected;
        begin
            viewport_origin = view_origin;
            viewport_extent = view_extent;
            source_origin = source_start;
            source_extent = source_size;
            for (position = 0; position < physical_total;
                 position = position + 1) begin
                @(negedge clk);
                sample_valid = 1'b1;
                sequence_start = position == 0;
                physical_position = position;
                @(posedge clk);
                #1;
                if (!output_valid)
                    $fatal(1, "position %0d did not produce output", position);
                if (position < view_origin ||
                    position >= view_origin + view_extent) begin
                    if (output_active)
                        $fatal(1, "position %0d escaped viewport", position);
                end else begin
                    expected = source_start +
                        ((position - view_origin) * source_size) / view_extent;
                    if (!output_active || source_position != expected)
                        $fatal(1,
                            "mapping %0d -> %0d expected %0d active=%0d",
                            position, source_position, expected, output_active);
                end
            end
            @(negedge clk);
            sample_valid = 1'b0;
            sequence_start = 1'b0;
        end
    endtask

    initial begin
        repeat (3) @(negedge clk);
        reset = 1'b0;

        check_mapping(1920, 0, 1920, 0, 1920);
        check_mapping(1920, 0, 1920, 0, 320);
        check_mapping(1920, 210, 1500, 0, 320);
        check_mapping(1920, 0, 1920, 40, 240);

        $display("ASTRA DISPLAY AXIS SCALER PASS");
        $finish;
    end
endmodule

`default_nettype wire
