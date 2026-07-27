// Astra integration wrapper for the generated one-port, low/full-speed OHCI
// controller. The OHCI engine has its own control clock, while CPU control
// traffic and SDRAM DMA cross into and out of that domain explicitly.
`default_nettype none

module usb_ohci_host #(
    parameter integer CTRL_TIMEOUT_CYCLES = 2048
) (
    input  wire        cpu_clk,
    input  wire        cpu_rst,
    input  wire        cpu_start,
    input  wire        cpu_write,
    input  wire [9:0]  cpu_addr,
    input  wire [3:0]  cpu_be,
    input  wire [31:0] cpu_wdata,
    output wire        cpu_busy,
    output wire        cpu_done,
    output wire        cpu_error,
    output wire [31:0] cpu_rdata,
    output wire        cpu_irq,

    input  wire        ctrl_clk,
    input  wire        ctrl_rst,

    input  wire        mem_clk,
    input  wire        mem_rst,
    output wire        mem_lock,
    output wire        mem_valid,
    input  wire        mem_ready,
    output wire        mem_write,
    output wire [24:0] mem_addr,
    output wire [3:0]  mem_be,
    output wire [31:0] mem_wdata,
    input  wire        mem_rsp_valid,
    input  wire [31:0] mem_rdata,
    output wire        dma_fault,
    output wire [31:0] dma_fault_addr,

    input  wire        phy_clk,
    input  wire        phy_rst,
    input  wire        usb_dp_read,
    output wire        usb_dp_write,
    output wire        usb_dp_write_enable,
    input  wire        usb_dm_read,
    output wire        usb_dm_write,
    output wire        usb_dm_write_enable
);
    wire ctrl_cyc;
    wire ctrl_stb;
    wire ctrl_ack;
    wire ctrl_we;
    wire [9:0] ctrl_addr;
    wire [31:0] ctrl_rdata;
    wire [31:0] ctrl_wdata;
    wire [3:0] ctrl_sel;

    wire dma_cyc;
    wire dma_stb;
    wire dma_ack;
    wire dma_we;
    wire [29:0] dma_addr;
    wire [31:0] dma_rdata;
    wire [31:0] dma_wdata;
    wire [3:0] dma_sel;
    wire dma_err;
    wire [2:0] dma_cti;
    wire [1:0] dma_bte;
    wire interrupt_ctrl;
    wire dma_fault_mem;
    wire [31:0] dma_fault_addr_mem;
    wire ctrl_cpu_busy;
    wire ctrl_cpu_done;
    wire ctrl_cpu_error;
    wire [31:0] ctrl_cpu_rdata;

    wire custom_select = cpu_addr[9:4] == 6'h3c; // byte 0xF00..0xF3F
    reg custom_done = 1'b0;
    reg [31:0] custom_rdata = 32'd0;
    reg fault_clear_toggle_cpu = 1'b0;
    (* async_reg = "true" *) reg [1:0] fault_clear_sync_mem = 2'b00;
    reg fault_clear_seen_mem = 1'b0;
    reg fault_clear_mem = 1'b0;

    reg [1:0] interrupt_sync_cpu = 2'b00;
    reg [1:0] dma_fault_sync_cpu = 2'b00;
    reg [31:0] dma_fault_addr_meta_cpu = 32'd0;
    reg [31:0] dma_fault_addr_cpu = 32'd0;
    always @(posedge cpu_clk) begin
        if (cpu_rst) begin
            interrupt_sync_cpu <= 2'b00;
            dma_fault_sync_cpu <= 2'b00;
            dma_fault_addr_meta_cpu <= 32'd0;
            dma_fault_addr_cpu <= 32'd0;
        end else begin
            interrupt_sync_cpu <= {interrupt_sync_cpu[0], interrupt_ctrl};
            dma_fault_sync_cpu <= {dma_fault_sync_cpu[0], dma_fault_mem};
            dma_fault_addr_meta_cpu <= dma_fault_addr_mem;
            dma_fault_addr_cpu <= dma_fault_addr_meta_cpu;
        end
    end
    assign cpu_irq = interrupt_sync_cpu[1];
    assign dma_fault = dma_fault_sync_cpu[1];
    assign dma_fault_addr = dma_fault_addr_cpu;
    assign cpu_busy = ctrl_cpu_busy;
    assign cpu_done = ctrl_cpu_done || custom_done;
    assign cpu_error = custom_done ? 1'b0 : ctrl_cpu_error;
    assign cpu_rdata = custom_done ? custom_rdata : ctrl_cpu_rdata;

    always @(posedge cpu_clk) begin
        custom_done <= 1'b0;
        if (cpu_rst) begin
            custom_rdata <= 32'd0;
            fault_clear_toggle_cpu <= 1'b0;
        end else if (cpu_start && custom_select) begin
            case (cpu_addr[3:0])
                4'h0: custom_rdata <= 32'h41555342; // "AUSB"
                4'h1: custom_rdata <= 32'h00010000;
                4'h2: custom_rdata <= {
                    30'd0, interrupt_sync_cpu[1], dma_fault_sync_cpu[1]
                };
                4'h3: custom_rdata <= dma_fault_addr_cpu;
                4'h4: custom_rdata <= 32'h03f00000;
                4'h5: custom_rdata <= 32'h00100000;
                default: custom_rdata <= 32'd0;
            endcase
            if (cpu_write && cpu_addr[3:0] == 4'h2 &&
                cpu_be[0] && cpu_wdata[0])
                fault_clear_toggle_cpu <= ~fault_clear_toggle_cpu;
            custom_done <= 1'b1;
        end
    end

    always @(posedge mem_clk) begin
        fault_clear_mem <= 1'b0;
        if (mem_rst) begin
            fault_clear_sync_mem <= 2'b00;
            fault_clear_seen_mem <= 1'b0;
        end else begin
            fault_clear_sync_mem <= {
                fault_clear_sync_mem[0], fault_clear_toggle_cpu
            };
            if (fault_clear_sync_mem[1] != fault_clear_seen_mem) begin
                fault_clear_seen_mem <= fault_clear_sync_mem[1];
                fault_clear_mem <= 1'b1;
            end
        end
    end

    usb_ohci_ctrl_cdc #(
        .CTRL_TIMEOUT_CYCLES(CTRL_TIMEOUT_CYCLES)
    ) ctrl_cdc_i (
        .cpu_clk(cpu_clk), .cpu_rst(cpu_rst),
        .cpu_start(cpu_start && !custom_select),
        .cpu_write(cpu_write), .cpu_addr(cpu_addr), .cpu_be(cpu_be),
        .cpu_wdata(cpu_wdata), .cpu_busy(ctrl_cpu_busy),
        .cpu_done(ctrl_cpu_done), .cpu_error(ctrl_cpu_error),
        .cpu_rdata(ctrl_cpu_rdata),
        .ctrl_clk(ctrl_clk), .ctrl_rst(ctrl_rst),
        .wb_cyc(ctrl_cyc), .wb_stb(ctrl_stb), .wb_we(ctrl_we),
        .wb_addr(ctrl_addr), .wb_wdata(ctrl_wdata), .wb_sel(ctrl_sel),
        .wb_ack(ctrl_ack), .wb_rdata(ctrl_rdata)
    );

    usb_ohci_dma_bridge dma_bridge_i (
        .wb_clk(ctrl_clk), .wb_rst(ctrl_rst),
        .wb_cyc(dma_cyc), .wb_stb(dma_stb), .wb_we(dma_we),
        .wb_addr(dma_addr), .wb_wdata(dma_wdata), .wb_sel(dma_sel),
        .wb_ack(dma_ack), .wb_err(dma_err), .wb_rdata(dma_rdata),
        .mem_clk(mem_clk), .mem_rst(mem_rst),
        .mem_lock(mem_lock), .mem_valid(mem_valid), .mem_ready(mem_ready),
        .mem_write(mem_write), .mem_addr(mem_addr), .mem_be(mem_be),
        .mem_wdata(mem_wdata), .mem_rsp_valid(mem_rsp_valid),
        .mem_rdata(mem_rdata), .fault_clear(fault_clear_mem),
        .fault(dma_fault_mem),
        .fault_addr(dma_fault_addr_mem)
    );

    UsbOhciWishbone_Dw32_Pc1_Pf48000000 ohci_i (
        .io_dma_CYC(dma_cyc), .io_dma_STB(dma_stb),
        .io_dma_ACK(dma_ack), .io_dma_WE(dma_we), .io_dma_ADR(dma_addr),
        .io_dma_DAT_MISO(dma_rdata), .io_dma_DAT_MOSI(dma_wdata),
        .io_dma_SEL(dma_sel), .io_dma_ERR(dma_err),
        .io_dma_CTI(dma_cti), .io_dma_BTE(dma_bte),
        .io_ctrl_CYC(ctrl_cyc), .io_ctrl_STB(ctrl_stb),
        .io_ctrl_ACK(ctrl_ack), .io_ctrl_WE(ctrl_we),
        .io_ctrl_ADR(ctrl_addr), .io_ctrl_DAT_MISO(ctrl_rdata),
        .io_ctrl_DAT_MOSI(ctrl_wdata), .io_ctrl_SEL(ctrl_sel),
        .io_interrupt(interrupt_ctrl),
        .io_usb_0_dp_read(usb_dp_read),
        .io_usb_0_dp_write(usb_dp_write),
        .io_usb_0_dp_writeEnable(usb_dp_write_enable),
        .io_usb_0_dm_read(usb_dm_read),
        .io_usb_0_dm_write(usb_dm_write),
        .io_usb_0_dm_writeEnable(usb_dm_write_enable),
        .phy_clk(phy_clk), .phy_reset(phy_rst),
        .ctrl_clk(ctrl_clk), .ctrl_reset(ctrl_rst)
    );

    // CTI/BTE are consumed by the generated Wishbone master itself. Astra's
    // SDRAM adapter preserves each acknowledged beat in program order.
    wire _unused = &{1'b0, dma_cti, dma_bte};
endmodule

`default_nettype wire
