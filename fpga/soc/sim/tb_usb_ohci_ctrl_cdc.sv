`timescale 1ns/1ps
`default_nettype none

module tb_usb_ohci_ctrl_cdc;
    reg cpu_clk = 1'b0;
    reg ctrl_clk = 1'b0;
    always #40 cpu_clk = ~cpu_clk;
    always #6.667 ctrl_clk = ~ctrl_clk;

    reg cpu_rst = 1'b1;
    reg ctrl_rst = 1'b1;
    reg cpu_start = 1'b0;
    reg cpu_write = 1'b0;
    reg [9:0] cpu_addr = 10'd0;
    reg [3:0] cpu_be = 4'd0;
    reg [31:0] cpu_wdata = 32'd0;
    wire cpu_busy;
    wire cpu_done;
    wire cpu_error;
    wire [31:0] cpu_rdata;
    wire wb_cyc;
    wire wb_stb;
    wire wb_we;
    wire [9:0] wb_addr;
    wire [31:0] wb_wdata;
    wire [3:0] wb_sel;
    reg wb_ack = 1'b0;
    reg [31:0] wb_rdata = 32'd0;

    reg slave_seen = 1'b0;
    reg [2:0] slave_delay = 3'd0;
    reg slave_respond = 1'b1;
    integer transaction_count = 0;

    usb_ohci_ctrl_cdc #(.CTRL_TIMEOUT_CYCLES(8)) dut (
        .cpu_clk(cpu_clk), .cpu_rst(cpu_rst),
        .cpu_start(cpu_start), .cpu_write(cpu_write),
        .cpu_addr(cpu_addr), .cpu_be(cpu_be), .cpu_wdata(cpu_wdata),
        .cpu_busy(cpu_busy), .cpu_done(cpu_done), .cpu_error(cpu_error),
        .cpu_rdata(cpu_rdata),
        .ctrl_clk(ctrl_clk), .ctrl_rst(ctrl_rst),
        .wb_cyc(wb_cyc), .wb_stb(wb_stb), .wb_we(wb_we),
        .wb_addr(wb_addr), .wb_wdata(wb_wdata), .wb_sel(wb_sel),
        .wb_ack(wb_ack), .wb_rdata(wb_rdata)
    );

    always @(posedge ctrl_clk) begin
        wb_ack <= 1'b0;
        if (ctrl_rst) begin
            slave_seen <= 1'b0;
            slave_delay <= 3'd0;
            wb_rdata <= 32'd0;
            transaction_count <= 0;
        end else begin
            if (!wb_cyc) begin
                slave_seen <= 1'b0;
                slave_delay <= 3'd0;
            end
            if (wb_cyc && wb_stb && !slave_seen) begin
                slave_seen <= 1'b1;
                slave_delay <= 3'd3;
                transaction_count <= transaction_count + 1;
                wb_rdata <= 32'hc0000000 | {22'd0, wb_addr};
            end else if (slave_delay != 0 && slave_respond) begin
                slave_delay <= slave_delay - 3'd1;
                if (slave_delay == 3'd1)
                    wb_ack <= 1'b1;
            end
        end
    end

    task automatic cpu_access(
        input write_value,
        input [9:0] address_value,
        input [3:0] enable_value,
        input [31:0] data_value,
        input expected_error
    );
        integer timeout;
        begin
            @(negedge cpu_clk);
            cpu_write = write_value;
            cpu_addr = address_value;
            cpu_be = enable_value;
            cpu_wdata = data_value;
            cpu_start = 1'b1;
            @(negedge cpu_clk);
            cpu_start = 1'b0;
            if (!cpu_busy)
                $fatal(1, "CPU request did not become busy");

            timeout = 0;
            while (!cpu_done && timeout < 100) begin
                @(negedge cpu_clk);
                timeout = timeout + 1;
            end
            if (!cpu_done)
                $fatal(1, "CPU request timed out");
            if (cpu_error !== expected_error)
                $fatal(1, "response error expected=%0d got=%0d",
                       expected_error, cpu_error);
            if (!expected_error &&
                cpu_rdata !== (32'hc0000000 | {22'd0, address_value}))
                $fatal(1, "response mismatch got=%08x", cpu_rdata);
            if (cpu_busy)
                $fatal(1, "busy remained asserted with done");
        end
    endtask

    initial begin
        repeat (4) @(posedge cpu_clk);
        cpu_rst = 1'b0;
        repeat (6) @(posedge ctrl_clk);
        ctrl_rst = 1'b0;

        cpu_access(1'b1, 10'h123, 4'b1010, 32'h89abcdef, 1'b0);
        if (!wb_we || wb_addr !== 10'h123 || wb_sel !== 4'b1010 ||
            wb_wdata !== 32'h89abcdef)
            $fatal(1, "write request bundle changed");

        cpu_access(1'b0, 10'h004, 4'b1111, 32'd0, 1'b0);
        if (transaction_count != 2)
            $fatal(1, "transaction count mismatch %0d", transaction_count);

        // A silent target is withdrawn in the Wishbone domain. The bridge
        // reports one error and remains usable for the next transaction.
        slave_respond = 1'b0;
        cpu_access(1'b1, 10'h155, 4'b1111, 32'hfeedface, 1'b1);
        repeat (2) @(posedge ctrl_clk);
        if (wb_cyc || wb_stb)
            $fatal(1, "timed-out Wishbone request remained active");
        slave_respond = 1'b1;
        cpu_access(1'b0, 10'h008, 4'b1111, 32'd0, 1'b0);
        if (transaction_count != 4)
            $fatal(1, "post-timeout transaction count mismatch %0d",
                   transaction_count);

        $display("USB OHCI CTRL CDC PASS transactions=%0d", transaction_count);
        $finish;
    end
endmodule

`default_nettype wire
