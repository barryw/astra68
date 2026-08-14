`timescale 1ns/1ps
`default_nettype none

module tb_astra_axi_read_3to1;
    reg clk = 1'b0;
    always #5 clk = ~clk;

    reg resetn = 1'b0;
    reg [17:0] s_arid = 18'd0;
    reg [95:0] s_araddr = 96'd0;
    reg [23:0] s_arlen = 24'd0;
    reg [8:0] s_arsize = {3{3'd3}};
    reg [5:0] s_arburst = {3{2'd1}};
    reg [11:0] s_arcache = {3{4'd3}};
    reg [8:0] s_arprot = 9'd0;
    reg [11:0] s_arqos = 12'd0;
    reg [2:0] s_arvalid = 3'd0;
    wire [2:0] s_arready;
    wire [17:0] s_rid;
    wire [191:0] s_rdata;
    wire [5:0] s_rresp;
    wire [2:0] s_rlast;
    wire [2:0] s_rvalid;
    reg [2:0] s_rready = 3'b111;
    wire [5:0] m_arid;
    wire [31:0] m_araddr;
    wire [7:0] m_arlen;
    wire [2:0] m_arsize;
    wire [1:0] m_arburst;
    wire [3:0] m_arcache;
    wire [2:0] m_arprot;
    wire [3:0] m_arqos;
    wire m_arvalid;
    reg m_arready = 1'b1;
    reg [5:0] m_rid = 6'd0;
    reg [63:0] m_rdata = 64'd0;
    reg [1:0] m_rresp = 2'd0;
    reg m_rlast = 1'b0;
    reg m_rvalid = 1'b0;
    wire m_rready;

    astra_axi_read_3to1 dut (
        .aclk(clk), .aresetn(resetn),
        .s_axi_arid(s_arid), .s_axi_araddr(s_araddr),
        .s_axi_arlen(s_arlen), .s_axi_arsize(s_arsize),
        .s_axi_arburst(s_arburst), .s_axi_arcache(s_arcache),
        .s_axi_arprot(s_arprot), .s_axi_arqos(s_arqos),
        .s_axi_arvalid(s_arvalid), .s_axi_arready(s_arready),
        .s_axi_rid(s_rid), .s_axi_rdata(s_rdata),
        .s_axi_rresp(s_rresp), .s_axi_rlast(s_rlast),
        .s_axi_rvalid(s_rvalid), .s_axi_rready(s_rready),
        .m_axi_arid(m_arid), .m_axi_araddr(m_araddr),
        .m_axi_arlen(m_arlen), .m_axi_arsize(m_arsize),
        .m_axi_arburst(m_arburst), .m_axi_arcache(m_arcache),
        .m_axi_arprot(m_arprot), .m_axi_arqos(m_arqos),
        .m_axi_arvalid(m_arvalid), .m_axi_arready(m_arready),
        .m_axi_rid(m_rid), .m_axi_rdata(m_rdata),
        .m_axi_rresp(m_rresp), .m_axi_rlast(m_rlast),
        .m_axi_rvalid(m_rvalid), .m_axi_rready(m_rready)
    );

    task automatic expect_output(input [1:0] client, input [31:0] address);
        begin
            #1;
            if (!m_arvalid || m_arid != client || m_araddr != address)
                $fatal(1, "output mismatch client=%0d id=%0d addr=%08x",
                    client, m_arid, m_araddr);
            @(posedge clk);
            #1;
        end
    endtask

    initial begin
        s_araddr = {32'h3000, 32'h2000, 32'h1000};
        repeat (3) @(posedge clk);
        @(negedge clk);
        resetn = 1'b1;

        // Each client deposits independently; the arbiter drains one request
        // per cycle without a combinational ready path between clients.
        s_arvalid = 3'b111;
        #1;
        if (s_arready != 3'b111 || m_arvalid)
            $fatal(1, "empty input buffers were not independently ready");
        @(posedge clk);
        @(negedge clk);
        s_arvalid = 3'd0;
        expect_output(2'd0, 32'h1000);
        expect_output(2'd1, 32'h2000);
        expect_output(2'd2, 32'h3000);

        // Backpressure holds the selected request and does not rotate fairness.
        @(negedge clk);
        s_araddr[31:0] = 32'h4000;
        s_araddr[63:32] = 32'h5000;
        s_arvalid = 3'b011;
        m_arready = 1'b0;
        #1;
        if (s_arready != 3'b111 || m_arvalid)
            $fatal(1, "empty buffers were not ready before backpressure test");
        @(posedge clk);
        @(negedge clk);
        s_arvalid = 3'd0;
        #1;
        if (!m_arvalid || m_arid != 6'd0 || m_araddr != 32'h4000 ||
            s_arready[1:0] != 2'b00)
            $fatal(1, "address backpressure changed buffered request");
        repeat (2) @(posedge clk);
        @(negedge clk);
        m_arready = 1'b1;
        expect_output(2'd0, 32'h4000);
        expect_output(2'd1, 32'h5000);

        // Interleaved responses route by the rewritten ID.
        m_rvalid = 1'b1;
        m_rlast = 1'b1;
        m_rresp = 2'b10;
        m_rdata = 64'h0123456789abcdef;
        m_rid = 6'd2;
        s_rready[2] = 1'b0;
        #1;
        if (s_rvalid != 3'b100 || m_rready ||
            s_rdata[191:128] != m_rdata || s_rresp[5:4] != m_rresp ||
            !s_rlast[2])
            $fatal(1, "client 2 response/backpressure mismatch");
        s_rready[2] = 1'b1;
        #1;
        if (!m_rready)
            $fatal(1, "client 2 response did not resume");
        m_rid = 6'd0;
        #1;
        if (s_rvalid != 3'b001 || !m_rready)
            $fatal(1, "client 0 response mismatch");
        m_rid = 6'd3;
        #1;
        if (s_rvalid != 3'd0 || m_rready)
            $fatal(1, "unknown response ID was accepted");

        $display("ASTRA AXI READ 3TO1 PASS");
        $finish;
    end
endmodule

`default_nettype wire
