`timescale 1ns/1ps
`default_nettype none

module tb_astra_hdmi_audio;
    integer frame;
    reg build_clk = 0;
    reg audio_clk = 0;
    reg build_reset = 1;
    reg audio_reset = 1;
    reg hdmi_output_active = 0;
    wire hdmi_output_requested;
    always #3 build_clk = ~build_clk;
    always #11 audio_clk = ~audio_clk;

    reg [31:0] s_axi_awaddr = 0;
    reg [2:0] s_axi_awprot = 0;
    reg s_axi_awvalid = 0;
    wire s_axi_awready;
    reg [31:0] s_axi_wdata = 0;
    reg [3:0] s_axi_wstrb = 0;
    reg s_axi_wvalid = 0;
    wire s_axi_wready;
    wire [1:0] s_axi_bresp;
    wire s_axi_bvalid;
    reg s_axi_bready = 0;
    reg [31:0] s_axi_araddr = 0;
    reg [2:0] s_axi_arprot = 0;
    reg s_axi_arvalid = 0;
    wire s_axi_arready;
    wire [31:0] s_axi_rdata;
    wire [1:0] s_axi_rresp;
    wire s_axi_rvalid;
    reg s_axi_rready = 0;
    wire [1:0][23:0] audio_sample_word;

    astra_hdmi_audio dut (.*);

    task automatic write_reg(input [7:0] address, input [31:0] value,
                             input [1:0] expected_response);
        begin
            @(negedge build_clk);
            s_axi_awaddr = {24'h43c060, address};
            s_axi_wdata = value;
            s_axi_wstrb = 4'hf;
            s_axi_awvalid = 1;
            s_axi_wvalid = 1;
            while (!s_axi_awready || !s_axi_wready) @(negedge build_clk);
            @(negedge build_clk);
            s_axi_awvalid = 0;
            s_axi_wvalid = 0;
            while (!s_axi_bvalid) @(negedge build_clk);
            if (s_axi_bresp != expected_response)
                $fatal(1, "write %02x response=%b expected=%b",
                       address, s_axi_bresp, expected_response);
            s_axi_bready = 1;
            @(negedge build_clk);
            s_axi_bready = 0;
        end
    endtask

    task automatic read_reg(input [7:0] address, input [31:0] expected);
        begin
            @(negedge build_clk);
            s_axi_araddr = {24'h43c060, address};
            s_axi_arvalid = 1;
            while (!s_axi_arready) @(negedge build_clk);
            @(negedge build_clk);
            s_axi_arvalid = 0;
            while (!s_axi_rvalid) @(negedge build_clk);
            if (s_axi_rresp != 0 || s_axi_rdata != expected)
                $fatal(1, "read %02x data=%08x expected=%08x response=%b",
                       address, s_axi_rdata, expected, s_axi_rresp);
            s_axi_rready = 1;
            @(negedge build_clk);
            s_axi_rready = 0;
        end
    endtask

    task automatic wait_sample(input [23:0] left, input [23:0] right);
        integer clocks;
        begin
            clocks = 0;
            while (audio_sample_word != {right, left} && clocks < 20) begin
                @(negedge audio_clk);
                clocks = clocks + 1;
            end
            if (audio_sample_word != {right, left})
                $fatal(1, "sample=%012x expected=%06x%06x",
                       audio_sample_word, right, left);
        end
    endtask

    initial begin
        repeat (5) @(posedge build_clk);
        build_reset = 0;
        repeat (2) @(posedge audio_clk);
        audio_reset = 0;

        read_reg(8'h00, 32'h41554430);
        read_reg(8'h04, 32'h00010000);
        read_reg(8'h24, 32'd48000);
        read_reg(8'h28, 32'd512);
        read_reg(8'h2c, 32'd0);
        read_reg(8'h30, 32'd0);
        write_reg(8'h2c, 32'd1, 2'b00);
        read_reg(8'h2c, 32'd1);
        hdmi_output_active = 1;
        read_reg(8'h30, 32'd3);
        write_reg(8'h2c, 32'd0, 2'b00);
        read_reg(8'h2c, 32'd0);
        hdmi_output_active = 0;
        write_reg(8'h14, 32'h00112233, 2'b00);
        write_reg(8'h18, 32'h00445566, 2'b00);
        write_reg(8'h14, 32'h00778899, 2'b00);
        write_reg(8'h18, 32'h00aabbcc, 2'b00);
        write_reg(8'h0c, 32'd1, 2'b00);
        wait_sample(24'h112233, 24'h445566);
        @(negedge audio_clk);
        wait_sample(24'h778899, 24'haabbcc);

        repeat (4) @(posedge audio_clk);
        if (dut.underflow_count_q == 0)
            $fatal(1, "underflow was not counted");
        write_reg(8'h0c, 32'd0, 2'b00);
        repeat (4) @(posedge audio_clk);
        for (frame = 0; frame < 512; frame = frame + 1) begin
            write_reg(8'h14, frame, 2'b00);
            write_reg(8'h18, frame + 1, 2'b00);
        end
        /* The FIFO output register holds one prefetched frame. */
        write_reg(8'h18, 32'h00000001, 2'b00);
        write_reg(8'h18, 32'h00000001, 2'b10);
        read_reg(8'h20, 32'd1);
        write_reg(8'h0c, 32'd4, 2'b10);
        write_reg(8'h34, 32'd0, 2'b11);

        $display("ASTRA HDMI AUDIO PASS underflows=%0d",
                 dut.underflow_count_q);
        $finish;
    end
endmodule

`default_nettype wire
