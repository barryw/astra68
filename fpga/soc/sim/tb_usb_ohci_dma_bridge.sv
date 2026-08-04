`timescale 1ns/1ps
`default_nettype none

module tb_usb_ohci_dma_bridge;
    reg wb_clk = 1'b0;
    reg mem_clk = 1'b0;
    always #20 wb_clk = ~wb_clk;
    always #8.333 mem_clk = ~mem_clk;

    reg wb_rst = 1'b1;
    reg mem_rst = 1'b1;
    reg wb_cyc = 1'b0;
    reg wb_stb = 1'b0;
    reg wb_we = 1'b0;
    reg [29:0] wb_addr = 30'd0;
    reg [31:0] wb_wdata = 32'd0;
    reg [3:0] wb_sel = 4'd0;
    wire wb_ack;
    wire wb_err;
    wire [31:0] wb_rdata;
    wire mem_lock;
    wire mem_valid;
    reg mem_ready = 1'b0;
    wire mem_write;
    wire [24:0] mem_addr;
    wire [3:0] mem_be;
    wire [31:0] mem_wdata;
    reg mem_rsp_valid = 1'b0;
    reg [31:0] mem_rdata = 32'd0;
    reg fault_clear = 1'b0;
    wire fault;
    wire [31:0] fault_addr;

    integer accepted_count = 0;

    usb_ohci_dma_bridge dut (
        .wb_clk(wb_clk), .wb_rst(wb_rst),
        .wb_cyc(wb_cyc), .wb_stb(wb_stb), .wb_we(wb_we),
        .wb_addr(wb_addr), .wb_wdata(wb_wdata), .wb_sel(wb_sel),
        .wb_ack(wb_ack), .wb_err(wb_err), .wb_rdata(wb_rdata),
        .mem_clk(mem_clk), .mem_rst(mem_rst),
        .mem_lock(mem_lock), .mem_valid(mem_valid), .mem_ready(mem_ready),
        .mem_write(mem_write), .mem_addr(mem_addr), .mem_be(mem_be),
        .mem_wdata(mem_wdata), .mem_rsp_valid(mem_rsp_valid),
        .mem_rdata(mem_rdata), .fault_clear(fault_clear),
        .fault(fault), .fault_addr(fault_addr)
    );

    task automatic begin_wb(
        input write_value,
        input [31:0] byte_address,
        input [3:0] select_value,
        input [31:0] data_value
    );
        begin
            @(negedge wb_clk);
            wb_we = write_value;
            wb_addr = byte_address[31:2];
            wb_sel = select_value;
            wb_wdata = data_value;
            wb_cyc = 1'b1;
            wb_stb = 1'b1;
        end
    endtask

    task automatic complete_memory(
        input [31:0] read_value
    );
        integer timeout;
        begin
            timeout = 0;
            while (!mem_valid && timeout < 100) begin
                @(negedge mem_clk);
                timeout = timeout + 1;
            end
            if (!mem_valid)
                $fatal(1, "native request did not assert");
            if (!mem_lock)
                $fatal(1, "DMA lock absent during request");

            mem_ready = 1'b1;
            @(negedge mem_clk);
            mem_ready = 1'b0;
            accepted_count = accepted_count + 1;
            repeat (2) @(negedge mem_clk);
            if (wb_ack)
                $fatal(1, "Wishbone acknowledged before native response");
            mem_rdata = read_value;
            mem_rsp_valid = 1'b1;
            @(negedge mem_clk);
            mem_rsp_valid = 1'b0;

            timeout = 0;
            while (!wb_ack && timeout < 100) begin
                @(negedge wb_clk);
                timeout = timeout + 1;
            end
            if (!wb_ack)
                $fatal(1, "Wishbone response timed out");
        end
    endtask

    initial begin
        repeat (5) @(posedge mem_clk);
        mem_rst = 1'b0;
        repeat (5) @(posedge wb_clk);
        wb_rst = 1'b0;

        begin_wb(1'b1, 32'h02000100, 4'b0101, 32'h11223344);
        complete_memory(32'd0);
        if (!mem_write || mem_addr !== 25'h0000100 ||
            mem_be !== 4'b1010 || mem_wdata !== 32'h44332211)
            $fatal(1, "write lane conversion mismatch");

        // Keep CYC/STB asserted and replace the request after ACK, matching a
        // Wishbone incrementing burst from the generated OHCI bridge.
        @(negedge wb_clk);
        wb_we = 1'b0;
        wb_addr = 32'h02000104 >> 2;
        wb_sel = 4'b1111;
        wb_wdata = 32'd0;
        if (!mem_lock)
            $fatal(1, "DMA ownership dropped between burst beats");
        complete_memory(32'h78563412);
        if (mem_write || mem_addr !== 25'h0000104)
            $fatal(1, "read address conversion mismatch");
        if (wb_rdata !== 32'h12345678)
            $fatal(1, "read byte swap mismatch got=%08x", wb_rdata);

        @(negedge wb_clk);
        wb_cyc = 1'b0;
        wb_stb = 1'b0;
        repeat (4) @(negedge mem_clk);
        if (mem_lock)
            $fatal(1, "DMA ownership did not release");

        // OHCI emits zero-select writes for masked beats in HCCA update
        // bursts. They are legal no-ops and must complete without reaching
        // the SDRAM controller, whose write command requires a byte lane.
        begin_wb(1'b1, 32'h03f00084, 4'b0000, 32'hdeadbeef);
        begin : wait_for_noop_ack
            integer timeout;
            timeout = 0;
            while (!wb_ack && timeout < 100) begin
                @(negedge wb_clk);
                if (mem_valid)
                    $fatal(1, "zero-select write reached native memory");
                timeout = timeout + 1;
            end
            if (!wb_ack)
                $fatal(1, "zero-select write did not complete");
        end
        if (wb_err || wb_rdata !== 32'd0 || fault)
            $fatal(1, "zero-select write completion mismatch");
        @(negedge wb_clk);
        wb_cyc = 1'b0;
        wb_stb = 1'b0;
        repeat (4) @(negedge mem_clk);
        if (mem_lock)
            $fatal(1, "zero-select write did not release DMA ownership");

        begin_wb(1'b0, 32'h03fffffc, 4'b1111, 32'd0);
        complete_memory(32'h04030201);
        if (mem_write || mem_addr !== 25'h1fffffc)
            $fatal(1, "last valid DMA word mismatch");
        if (wb_rdata !== 32'h01020304)
            $fatal(1, "last valid DMA read mismatch got=%08x", wb_rdata);
        @(negedge wb_clk);
        wb_cyc = 1'b0;
        wb_stb = 1'b0;
        repeat (4) @(negedge mem_clk);

        begin_wb(1'b0, 32'h01000000, 4'b1111, 32'd0);
        while (!wb_ack)
            @(negedge wb_clk);
        if (!wb_ack || wb_err || wb_rdata !== 32'd0)
            $fatal(1, "invalid DMA completion mismatch");
        if (!fault || fault_addr !== 32'h01000000)
            $fatal(1, "invalid DMA fault was not recorded");
        if (mem_valid)
            $fatal(1, "invalid DMA reached native memory");
        wb_cyc = 1'b0;
        wb_stb = 1'b0;
        @(negedge mem_clk);
        fault_clear = 1'b1;
        @(negedge mem_clk);
        fault_clear = 1'b0;
        @(negedge mem_clk);
        if (fault)
            $fatal(1, "fault clear failed");

        begin_wb(1'b0, 32'h04000000, 4'b1111, 32'd0);
        while (!wb_ack)
            @(negedge wb_clk);
        if (!fault || fault_addr !== 32'h04000000)
            $fatal(1, "upper-bound DMA fault was not recorded");
        if (mem_valid)
            $fatal(1, "upper-bound DMA reached native memory");
        wb_cyc = 1'b0;
        wb_stb = 1'b0;

        if (accepted_count != 3)
            $fatal(1, "native acceptance count mismatch %0d", accepted_count);
        $display("USB OHCI DMA BRIDGE PASS requests=%0d", accepted_count);
        $finish;
    end
endmodule

`default_nettype wire
