`timescale 1ns/1ps
`default_nettype none

module tb_astra_axi_lite_1to2;
    reg clk = 1'b0;
    reg reset = 1'b1;
    always #3 clk = ~clk;

    reg [31:0] s_awaddr = 0;
    reg [2:0] s_awprot = 0;
    reg s_awvalid = 0;
    wire s_awready;
    reg [31:0] s_wdata = 0;
    reg [3:0] s_wstrb = 0;
    reg s_wvalid = 0;
    wire s_wready;
    wire [1:0] s_bresp;
    wire s_bvalid;
    reg s_bready = 0;
    reg [31:0] s_araddr = 0;
    reg [2:0] s_arprot = 0;
    reg s_arvalid = 0;
    wire s_arready;
    wire [31:0] s_rdata;
    wire [1:0] s_rresp;
    wire s_rvalid;
    reg s_rready = 0;

    wire [31:0] m0_awaddr, m1_awaddr;
    wire [2:0] m0_awprot, m1_awprot;
    wire m0_awvalid, m1_awvalid;
    reg m0_awready = 1, m1_awready = 1;
    wire [31:0] m0_wdata, m1_wdata;
    wire [3:0] m0_wstrb, m1_wstrb;
    wire m0_wvalid, m1_wvalid;
    reg m0_wready = 1, m1_wready = 1;
    reg [1:0] m0_bresp = 0, m1_bresp = 0;
    reg m0_bvalid = 0, m1_bvalid = 0;
    wire m0_bready, m1_bready;
    wire [31:0] m0_araddr, m1_araddr;
    wire [2:0] m0_arprot, m1_arprot;
    wire m0_arvalid, m1_arvalid;
    reg m0_arready = 1, m1_arready = 1;
    reg [31:0] m0_rdata = 0, m1_rdata = 0;
    reg [1:0] m0_rresp = 0, m1_rresp = 0;
    reg m0_rvalid = 0, m1_rvalid = 0;
    wire m0_rready, m1_rready;

    astra_axi_lite_1to2 dut (.*);

    reg [1:0] write_seen0 = 0;
    reg [1:0] write_seen1 = 0;
    always @(posedge clk) begin
        if (reset) begin
            write_seen0 <= 0;
            write_seen1 <= 0;
            m0_bvalid <= 0;
            m1_bvalid <= 0;
            m0_rvalid <= 0;
            m1_rvalid <= 0;
        end else begin
            if (m0_awvalid && m0_awready)
                write_seen0[0] <= 1;
            if (m0_wvalid && m0_wready)
                write_seen0[1] <= 1;
            if (&write_seen0 && !m0_bvalid) begin
                m0_bvalid <= 1;
                m0_bresp <= 2'b00;
            end
            if (m0_bvalid && m0_bready) begin
                m0_bvalid <= 0;
                write_seen0 <= 0;
            end
            if (m1_awvalid && m1_awready)
                write_seen1[0] <= 1;
            if (m1_wvalid && m1_wready)
                write_seen1[1] <= 1;
            if (&write_seen1 && !m1_bvalid) begin
                m1_bvalid <= 1;
                m1_bresp <= 2'b00;
            end
            if (m1_bvalid && m1_bready) begin
                m1_bvalid <= 0;
                write_seen1 <= 0;
            end
            if (m0_arvalid && m0_arready) begin
                m0_rdata <= 32'h10000000 | m0_araddr;
                m0_rresp <= 0;
                m0_rvalid <= 1;
            end else if (m0_rvalid && m0_rready)
                m0_rvalid <= 0;
            if (m1_arvalid && m1_arready) begin
                m1_rdata <= 32'h20000000 | m1_araddr;
                m1_rresp <= 0;
                m1_rvalid <= 1;
            end else if (m1_rvalid && m1_rready)
                m1_rvalid <= 0;
        end
    end

    task automatic send_aw(input [31:0] address);
        begin
            @(negedge clk);
            s_awaddr = address;
            s_awvalid = 1;
            while (!s_awready) @(negedge clk);
            @(negedge clk);
            s_awvalid = 0;
        end
    endtask

    task automatic send_w(input [31:0] data);
        begin
            @(negedge clk);
            s_wdata = data;
            s_wstrb = 4'hf;
            s_wvalid = 1;
            while (!s_wready) @(negedge clk);
            @(negedge clk);
            s_wvalid = 0;
        end
    endtask

    task automatic check_write(input [31:0] address, input integer w_first);
        begin
            $display("write start %08x order=%0d", address, w_first);
            if (w_first) begin
                send_w(32'h55aa0000 | address);
                send_aw(address);
            end else begin
                send_aw(address);
                send_w(32'h55aa0000 | address);
            end
            while (!s_bvalid) @(negedge clk);
            if (s_bresp != 0)
                $fatal(1, "write failed address=%08x", address);
            s_bready = 1;
            @(negedge clk);
            s_bready = 0;
            $display("write done %08x", address);
        end
    endtask

    task automatic check_read(input [31:0] address, input [31:0] expected);
        begin
            $display("read start %08x", address);
            @(negedge clk);
            s_araddr = address;
            s_arvalid = 1;
            while (!s_arready) @(negedge clk);
            @(negedge clk);
            s_arvalid = 0;
            while (!s_rvalid) @(negedge clk);
            if (s_rresp != 0 || s_rdata != expected)
                $fatal(1, "read failed address=%08x data=%08x expected=%08x",
                       address, s_rdata, expected);
            s_rready = 1;
            @(negedge clk);
            s_rready = 0;
            $display("read done %08x", address);
        end
    endtask

    initial begin
        fork
            begin
                repeat (500) @(posedge clk);
                $fatal(1, "split timeout aw=%b w=%b wr=%b bv=%b addr=%08x select=%b input-select=%b seen0=%b b0=%b br0=%b seen1=%b b1=%b br1=%b ar=%b rr=%b rv=%b",
                    dut.aw_buffer_valid_q, dut.w_buffer_valid_q,
                    dut.write_response_q, s_bvalid, dut.awaddr_q,
                    dut.aw_select_q, dut.s_aw_select, write_seen0, m0_bvalid,
                    m0_bready, write_seen1, m1_bvalid, m1_bready,
                    dut.ar_buffer_valid_q,
                    dut.read_response_q, s_rvalid);
            end
        join_none
        repeat (5) @(posedge clk);
        reset = 0;
        check_write(32'h00000018, 0);
        check_write(32'h00004008, 1);
        check_write(32'h00008000, 0);
        check_read(32'h00000020, 32'h10000020);
        check_read(32'h00004004, 32'h20004004);
        check_read(32'h0000fffc, 32'h2000fffc);
        $display("ASTRA AXI-LITE SPLIT PASS");
        $finish;
    end
endmodule

`default_nettype wire
