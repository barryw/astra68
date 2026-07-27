`timescale 1ns/1ps
`default_nettype none

module tb_usb_ohci_host;
    reg cpu_clk = 1'b0;
    reg ctrl_clk = 1'b0;
    reg mem_clk = 1'b0;
    reg phy_clk = 1'b0;
    always #40 cpu_clk = ~cpu_clk;
    always #20 ctrl_clk = ~ctrl_clk;
    always #8.333 mem_clk = ~mem_clk;
    always #10.417 phy_clk = ~phy_clk;

    reg cpu_rst = 1'b1;
    reg ctrl_rst = 1'b1;
    reg mem_rst = 1'b1;
    reg phy_rst = 1'b1;
    reg cpu_start = 1'b0;
    reg cpu_write = 1'b0;
    reg [9:0] cpu_addr = 10'd0;
    reg [3:0] cpu_be = 4'b1111;
    reg [31:0] cpu_wdata = 32'd0;
    wire cpu_busy;
    wire cpu_done;
    wire cpu_error;
    wire [31:0] cpu_rdata;
    wire cpu_irq;
    wire mem_lock;
    wire mem_valid;
    reg mem_ready = 1'b0;
    wire mem_write;
    wire [24:0] mem_addr;
    wire [3:0] mem_be;
    wire [31:0] mem_wdata;
    reg mem_rsp_valid = 1'b0;
    reg [31:0] mem_rdata = 32'd0;
    wire dma_fault;
    wire [31:0] dma_fault_addr;
    wire usb_dp_write;
    wire usb_dp_write_enable;
    wire usb_dm_write;
    wire usb_dm_write_enable;
    reg memory_pending = 1'b0;
    integer dma_count = 0;
    reg hcca_dma_seen = 1'b0;

    always @(posedge mem_clk) begin
        mem_ready <= 1'b0;
        mem_rsp_valid <= 1'b0;
        if (mem_rst) begin
            memory_pending <= 1'b0;
            dma_count <= 0;
            hcca_dma_seen <= 1'b0;
        end else begin
            if (mem_valid && !memory_pending) begin
                mem_ready <= 1'b1;
                memory_pending <= 1'b1;
                dma_count <= dma_count + 1;
                if (mem_addr >= 25'h1f00000 && mem_addr < 25'h1f00100)
                    hcca_dma_seen <= 1'b1;
            end else if (memory_pending) begin
                mem_rdata <= 32'd0;
                mem_rsp_valid <= 1'b1;
                memory_pending <= 1'b0;
            end
        end
    end

    usb_ohci_host dut (
        .cpu_clk(cpu_clk), .cpu_rst(cpu_rst),
        .cpu_start(cpu_start), .cpu_write(cpu_write),
        .cpu_addr(cpu_addr), .cpu_be(cpu_be), .cpu_wdata(cpu_wdata),
        .cpu_busy(cpu_busy), .cpu_done(cpu_done), .cpu_error(cpu_error),
        .cpu_rdata(cpu_rdata),
        .cpu_irq(cpu_irq), .ctrl_clk(ctrl_clk), .ctrl_rst(ctrl_rst),
        .mem_clk(mem_clk), .mem_rst(mem_rst),
        .mem_lock(mem_lock), .mem_valid(mem_valid), .mem_ready(mem_ready),
        .mem_write(mem_write), .mem_addr(mem_addr), .mem_be(mem_be),
        .mem_wdata(mem_wdata), .mem_rsp_valid(mem_rsp_valid),
        .mem_rdata(mem_rdata), .dma_fault(dma_fault),
        .dma_fault_addr(dma_fault_addr), .phy_clk(phy_clk),
        .phy_rst(phy_rst), .usb_dp_read(1'b0),
        .usb_dp_write(usb_dp_write),
        .usb_dp_write_enable(usb_dp_write_enable), .usb_dm_read(1'b0),
        .usb_dm_write(usb_dm_write),
        .usb_dm_write_enable(usb_dm_write_enable)
    );

    task automatic access(
        input write_value,
        input [11:0] byte_address,
        input [31:0] data_value,
        output [31:0] result
    );
        integer timeout;
        begin
            @(negedge cpu_clk);
            cpu_write = write_value;
            cpu_addr = byte_address[11:2];
            cpu_wdata = data_value;
            cpu_start = 1'b1;
            @(negedge cpu_clk);
            cpu_start = 1'b0;
            timeout = 0;
            while (!cpu_done && timeout < 100) begin
                @(negedge cpu_clk);
                timeout = timeout + 1;
            end
            if (!cpu_done)
                $fatal(1, "OHCI control access timed out at %03x",
                       byte_address);
            if (cpu_error)
                $fatal(1, "OHCI control access failed at %03x",
                       byte_address);
            result = cpu_rdata;
        end
    endtask

    reg [31:0] value;
    initial begin
        repeat (5) @(posedge mem_clk);
        mem_rst = 1'b0;
        repeat (5) @(posedge ctrl_clk);
        ctrl_rst = 1'b0;
        repeat (5) @(posedge phy_clk);
        phy_rst = 1'b0;
        repeat (3) @(posedge cpu_clk);
        cpu_rst = 1'b0;

        access(1'b0, 12'h000, 32'd0, value);
        if (value[7:0] !== 8'h10)
            $fatal(1, "OHCI revision mismatch %08x", value);

        access(1'b0, 12'h048, 32'd0, value);
        if (value[7:0] !== 8'h01)
            $fatal(1, "root-hub port count mismatch %08x", value);

        access(1'b1, 12'h018, 32'h03f00000, value);
        access(1'b0, 12'h018, 32'd0, value);
        if (value !== 32'h03f00000)
            $fatal(1, "HCCA readback mismatch %08x", value);

        access(1'b0, 12'hf00, 32'd0, value);
        if (value !== 32'h41555342)
            $fatal(1, "Astra USB extension ID mismatch %08x", value);
        access(1'b0, 12'hf10, 32'd0, value);
        if (value !== 32'h03f00000)
            $fatal(1, "USB DMA pool base mismatch %08x", value);

        if (mem_valid || mem_lock || dma_fault || dma_fault_addr != 0)
            $fatal(1, "idle controller unexpectedly issued DMA");

        // Enter HCFS=Operational with all schedules disabled. A conforming
        // OHCI controller still maintains the HCCA frame state every 1 ms.
        access(1'b1, 12'h004, 32'h00000080, value);
        begin : wait_for_hcca_dma
            repeat (100000) begin
                @(posedge mem_clk);
                if (hcca_dma_seen)
                    disable wait_for_hcca_dma;
            end
        end
        if (!hcca_dma_seen || dma_count == 0)
            $fatal(1, "operational OHCI did not originate HCCA DMA");
        if (dma_fault)
            $fatal(1, "valid HCCA DMA raised a bridge fault at %08x",
                   dma_fault_addr);

        $display("USB OHCI HOST PASS revision=%02x ports=%0d dma=%0d",
                 8'h10, 1, dma_count);
        $finish;
    end
endmodule

`default_nettype wire
