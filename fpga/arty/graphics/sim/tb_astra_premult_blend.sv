`timescale 1ns/1ps
`default_nettype none

module tb_astra_premult_blend;
    reg clk = 1'b0;
    always #2.5 clk = ~clk;
    reg reset = 1'b1;
    reg input_valid = 1'b0;
    reg [31:0] destination = 32'd0;
    reg [31:0] source = 32'd0;
    reg [7:0] opacity = 8'd255;
    reg apply = 1'b0;
    wire output_valid;
    wire [31:0] output_value;

    reg opaque_valid = 1'b0;
    reg [23:0] opaque_destination = 24'd0;
    reg [31:0] opaque_source = 32'd0;
    reg opaque_apply = 1'b0;
    wire opaque_output_valid;
    wire [23:0] opaque_output;

    astra_blend_premult_pipeline build_blend (
        .clk(clk),
        .reset(reset),
        .input_valid(input_valid),
        .destination_premult_argb(destination),
        .source_straight_argb(source),
        .opacity(opacity),
        .apply_source(apply),
        .output_valid(output_valid),
        .output_premult_argb(output_value)
    );

    astra_blend_premult_opaque_pipeline pixel_blend (
        .pixel_clk(clk),
        .pixel_reset(reset),
        .input_valid(opaque_valid),
        .destination_rgb(opaque_destination),
        .source_premult_argb(opaque_source),
        .apply_source(opaque_apply),
        .output_valid(opaque_output_valid),
        .output_rgb(opaque_output)
    );

    task automatic check_build(
        input [31:0] destination_value,
        input [31:0] source_value,
        input [7:0] opacity_value,
        input apply_value,
        input [31:0] expected
    );
        begin
            destination <= destination_value;
            source <= source_value;
            opacity <= opacity_value;
            apply <= apply_value;
            input_valid <= 1'b1;
            @(posedge clk);
            input_valid <= 1'b0;
            @(posedge output_valid);
            #1;
            if (output_value !== expected) begin
                $display("FAIL build blend got=%08x expected=%08x",
                         output_value, expected);
                $fatal(1);
            end
            @(posedge clk);
        end
    endtask

    task automatic check_opaque(
        input [23:0] destination_value,
        input [31:0] source_value,
        input apply_value,
        input [23:0] expected
    );
        begin
            opaque_destination <= destination_value;
            opaque_source <= source_value;
            opaque_apply <= apply_value;
            opaque_valid <= 1'b1;
            @(posedge clk);
            opaque_valid <= 1'b0;
            @(posedge opaque_output_valid);
            #1;
            if (opaque_output !== expected) begin
                $display("FAIL opaque blend got=%06x expected=%06x",
                         opaque_output, expected);
                $fatal(1);
            end
            @(posedge clk);
        end
    endtask

    initial begin
        repeat (4) @(posedge clk);
        reset <= 1'b0;
        repeat (2) @(posedge clk);

        check_build(32'h11223344, 32'hffffffff, 8'd255,
                    1'b0, 32'h11223344);
        check_build(32'd0, 32'h80ff0000, 8'd255,
                    1'b1, 32'h80800000);
        check_build(32'h80800000, 32'h800000ff, 8'd255,
                    1'b1, 32'hc0400080);
        check_build(32'd0, 32'hff00ff00, 8'd128,
                    1'b1, 32'h80008000);
        check_opaque(24'h000000, 32'hc0400080, 1'b1, 24'h400080);
        check_opaque(24'h123456, 32'd0, 1'b1, 24'h123456);

        $display("ASTRA PREMULT BLEND PASS");
        $finish;
    end
endmodule

`default_nettype wire
