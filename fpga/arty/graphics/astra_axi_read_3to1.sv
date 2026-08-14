`timescale 1ns/1ps
`default_nettype none

module astra_axi_read_3to1 #(
    parameter integer AXI_ID_WIDTH = 6
) (
    input  wire                         aclk,
    input  wire                         aresetn,

    input  wire [3*AXI_ID_WIDTH-1:0]    s_axi_arid,
    input  wire [95:0]                  s_axi_araddr,
    input  wire [23:0]                  s_axi_arlen,
    input  wire [8:0]                   s_axi_arsize,
    input  wire [5:0]                   s_axi_arburst,
    input  wire [11:0]                  s_axi_arcache,
    input  wire [8:0]                   s_axi_arprot,
    input  wire [11:0]                  s_axi_arqos,
    input  wire [2:0]                   s_axi_arvalid,
    output wire [2:0]                   s_axi_arready,

    output wire [3*AXI_ID_WIDTH-1:0]    s_axi_rid,
    output wire [191:0]                 s_axi_rdata,
    output wire [5:0]                   s_axi_rresp,
    output wire [2:0]                   s_axi_rlast,
    output reg  [2:0]                   s_axi_rvalid,
    input  wire [2:0]                   s_axi_rready,

    output reg  [AXI_ID_WIDTH-1:0]      m_axi_arid,
    output reg  [31:0]                  m_axi_araddr,
    output reg  [7:0]                   m_axi_arlen,
    output reg  [2:0]                   m_axi_arsize,
    output reg  [1:0]                   m_axi_arburst,
    output reg  [3:0]                   m_axi_arcache,
    output reg  [2:0]                   m_axi_arprot,
    output reg  [3:0]                   m_axi_arqos,
    output reg                          m_axi_arvalid,
    input  wire                         m_axi_arready,

    input  wire [AXI_ID_WIDTH-1:0]      m_axi_rid,
    input  wire [63:0]                  m_axi_rdata,
    input  wire [1:0]                   m_axi_rresp,
    input  wire                         m_axi_rlast,
    input  wire                         m_axi_rvalid,
    output reg                          m_axi_rready
);
    reg [1:0] next_client_q;
    reg [1:0] grant;
    reg grant_valid;
    reg [2:0] request_valid_q;
    reg [95:0] request_araddr_q;
    reg [23:0] request_arlen_q;
    reg [8:0] request_arsize_q;
    reg [5:0] request_arburst_q;
    reg [11:0] request_arcache_q;
    reg [8:0] request_arprot_q;
    reg [11:0] request_arqos_q;
    wire [2:0] request_accept = s_axi_arvalid & s_axi_arready;

    assign s_axi_arready = ~request_valid_q;

    always @* begin
        grant = next_client_q;
        grant_valid = 1'b0;
        case (next_client_q)
            2'd0: begin
                if (request_valid_q[0]) begin grant = 2'd0; grant_valid = 1'b1; end
                else if (request_valid_q[1]) begin grant = 2'd1; grant_valid = 1'b1; end
                else if (request_valid_q[2]) begin grant = 2'd2; grant_valid = 1'b1; end
            end
            2'd1: begin
                if (request_valid_q[1]) begin grant = 2'd1; grant_valid = 1'b1; end
                else if (request_valid_q[2]) begin grant = 2'd2; grant_valid = 1'b1; end
                else if (request_valid_q[0]) begin grant = 2'd0; grant_valid = 1'b1; end
            end
            default: begin
                if (request_valid_q[2]) begin grant = 2'd2; grant_valid = 1'b1; end
                else if (request_valid_q[0]) begin grant = 2'd0; grant_valid = 1'b1; end
                else if (request_valid_q[1]) begin grant = 2'd1; grant_valid = 1'b1; end
            end
        endcase
    end

    always @* begin
        m_axi_arid = {{(AXI_ID_WIDTH-2){1'b0}}, grant};
        m_axi_araddr = 32'd0;
        m_axi_arlen = 8'd0;
        m_axi_arsize = 3'd0;
        m_axi_arburst = 2'd0;
        m_axi_arcache = 4'd0;
        m_axi_arprot = 3'd0;
        m_axi_arqos = 4'd0;
        m_axi_arvalid = grant_valid;
        case (grant)
            2'd0: begin
                m_axi_araddr = request_araddr_q[31:0];
                m_axi_arlen = request_arlen_q[7:0];
                m_axi_arsize = request_arsize_q[2:0];
                m_axi_arburst = request_arburst_q[1:0];
                m_axi_arcache = request_arcache_q[3:0];
                m_axi_arprot = request_arprot_q[2:0];
                m_axi_arqos = request_arqos_q[3:0];
            end
            2'd1: begin
                m_axi_araddr = request_araddr_q[63:32];
                m_axi_arlen = request_arlen_q[15:8];
                m_axi_arsize = request_arsize_q[5:3];
                m_axi_arburst = request_arburst_q[3:2];
                m_axi_arcache = request_arcache_q[7:4];
                m_axi_arprot = request_arprot_q[5:3];
                m_axi_arqos = request_arqos_q[7:4];
            end
            default: begin
                m_axi_araddr = request_araddr_q[95:64];
                m_axi_arlen = request_arlen_q[23:16];
                m_axi_arsize = request_arsize_q[8:6];
                m_axi_arburst = request_arburst_q[5:4];
                m_axi_arcache = request_arcache_q[11:8];
                m_axi_arprot = request_arprot_q[8:6];
                m_axi_arqos = request_arqos_q[11:8];
            end
        endcase
    end

    always @(posedge aclk) begin
        if (!aresetn) begin
            next_client_q <= 2'd0;
            request_valid_q <= 3'd0;
            request_araddr_q <= 96'd0;
            request_arlen_q <= 24'd0;
            request_arsize_q <= 9'd0;
            request_arburst_q <= 6'd0;
            request_arcache_q <= 12'd0;
            request_arprot_q <= 9'd0;
            request_arqos_q <= 12'd0;
        end else begin
            if (m_axi_arvalid && m_axi_arready) begin
                request_valid_q[grant] <= 1'b0;
                case (grant)
                    2'd0: next_client_q <= 2'd1;
                    2'd1: next_client_q <= 2'd2;
                    default: next_client_q <= 2'd0;
                endcase
            end
            if (request_accept[0]) begin
                request_valid_q[0] <= 1'b1;
                request_araddr_q[31:0] <= s_axi_araddr[31:0];
                request_arlen_q[7:0] <= s_axi_arlen[7:0];
                request_arsize_q[2:0] <= s_axi_arsize[2:0];
                request_arburst_q[1:0] <= s_axi_arburst[1:0];
                request_arcache_q[3:0] <= s_axi_arcache[3:0];
                request_arprot_q[2:0] <= s_axi_arprot[2:0];
                request_arqos_q[3:0] <= s_axi_arqos[3:0];
            end
            if (request_accept[1]) begin
                request_valid_q[1] <= 1'b1;
                request_araddr_q[63:32] <= s_axi_araddr[63:32];
                request_arlen_q[15:8] <= s_axi_arlen[15:8];
                request_arsize_q[5:3] <= s_axi_arsize[5:3];
                request_arburst_q[3:2] <= s_axi_arburst[3:2];
                request_arcache_q[7:4] <= s_axi_arcache[7:4];
                request_arprot_q[5:3] <= s_axi_arprot[5:3];
                request_arqos_q[7:4] <= s_axi_arqos[7:4];
            end
            if (request_accept[2]) begin
                request_valid_q[2] <= 1'b1;
                request_araddr_q[95:64] <= s_axi_araddr[95:64];
                request_arlen_q[23:16] <= s_axi_arlen[23:16];
                request_arsize_q[8:6] <= s_axi_arsize[8:6];
                request_arburst_q[5:4] <= s_axi_arburst[5:4];
                request_arcache_q[11:8] <= s_axi_arcache[11:8];
                request_arprot_q[8:6] <= s_axi_arprot[8:6];
                request_arqos_q[11:8] <= s_axi_arqos[11:8];
            end
        end
    end

    assign s_axi_rid = {3*AXI_ID_WIDTH{1'b0}};
    assign s_axi_rdata = {3{m_axi_rdata}};
    assign s_axi_rresp = {3{m_axi_rresp}};
    assign s_axi_rlast = {3{m_axi_rlast}};

    always @* begin
        s_axi_rvalid = 3'd0;
        m_axi_rready = 1'b0;
        case (m_axi_rid)
            {{(AXI_ID_WIDTH-2){1'b0}}, 2'd0}: begin
                s_axi_rvalid[0] = m_axi_rvalid;
                m_axi_rready = s_axi_rready[0];
            end
            {{(AXI_ID_WIDTH-2){1'b0}}, 2'd1}: begin
                s_axi_rvalid[1] = m_axi_rvalid;
                m_axi_rready = s_axi_rready[1];
            end
            {{(AXI_ID_WIDTH-2){1'b0}}, 2'd2}: begin
                s_axi_rvalid[2] = m_axi_rvalid;
                m_axi_rready = s_axi_rready[2];
            end
            default: begin end
        endcase
    end

`ifndef SYNTHESIS
    always @(posedge aclk) begin
        if (aresetn) begin
            if (request_accept[0] &&
                s_axi_arid[AXI_ID_WIDTH-1:0] != {AXI_ID_WIDTH{1'b0}})
                $fatal(1, "HP1 client 0 must use AXI ID zero");
            if (request_accept[1] &&
                s_axi_arid[2*AXI_ID_WIDTH-1:AXI_ID_WIDTH] !=
                    {AXI_ID_WIDTH{1'b0}})
                $fatal(1, "HP1 client 1 must use AXI ID zero");
            if (request_accept[2] &&
                s_axi_arid[3*AXI_ID_WIDTH-1:2*AXI_ID_WIDTH] !=
                    {AXI_ID_WIDTH{1'b0}})
                $fatal(1, "HP1 client 2 must use AXI ID zero");
        end
        if (aresetn && m_axi_arvalid && m_axi_arready) begin
            if (m_axi_arlen > 8'd15)
                $fatal(1, "HP1 request exceeds AXI3 burst limit");
        end
    end
`endif
endmodule

`default_nettype wire
