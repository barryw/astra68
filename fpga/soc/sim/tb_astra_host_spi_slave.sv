// Unit tests for astra_host_spi_slave.sv.
//
// Exercises the SPI mode-0 byte transport used by AstraHost.

`timescale 1ns/1ps

module tb_astra_host_spi_slave;

    localparam logic [7:0] SPI_WRITE_OP    = 8'h57;
    localparam logic [7:0] SPI_READ_OP     = 8'h52;
    localparam logic [7:0] SPI_TOKEN_EMPTY = 8'h00;
    localparam logic [7:0] SPI_TOKEN_DATA  = 8'h01;

    logic clk = 0;
    always #10 clk = ~clk;  // 50 MHz test clock

    logic rst;
    logic spi_sck;
    logic spi_cs_n;
    logic spi_mosi;
    wire  spi_miso;
    wire  spi_miso_oe;

    wire [7:0] rx_data;
    wire       rx_valid;
    logic      rx_ready;

    logic [7:0] tx_data;
    logic       tx_start;
    wire        tx_busy;

    wire rx_overflow;
    wire tx_overflow;
    wire tx_underflow;
    wire selected_seen;

    int pass_count = 0;
    int fail_count = 0;
    int test_num   = 0;
    logic [7:0] got;
    logic       has_data;
    logic [7:0] value;
    int         fill_index;

    astra_host_spi_slave #(
        .ADDR_WIDTH(3),
        .IDLE_BYTE(8'hA5)
    ) dut (
        .clk(clk),
        .rst(rst),
        .spi_sck(spi_sck),
        .spi_cs_n(spi_cs_n),
        .spi_mosi(spi_mosi),
        .spi_miso(spi_miso),
        .spi_miso_oe(spi_miso_oe),
        .rx_data(rx_data),
        .rx_valid(rx_valid),
        .rx_ready(rx_ready),
        .tx_data(tx_data),
        .tx_start(tx_start),
        .tx_busy(tx_busy),
        .rx_overflow(rx_overflow),
        .tx_overflow(tx_overflow),
        .tx_underflow(tx_underflow),
        .selected_seen(selected_seen)
    );

    task automatic check(input string name, input logic condition);
        test_num++;
        if (condition) begin
            $display("  PASS [%0d] %s", test_num, name);
            pass_count++;
        end else begin
            $display("  FAIL [%0d] %s", test_num, name);
            fail_count++;
        end
    endtask

    task automatic check_eq8(input string name, input logic [7:0] actual,
                             input logic [7:0] expected);
        test_num++;
        if (actual === expected) begin
            $display("  PASS [%0d] %s (=0x%02X)", test_num, name, actual);
            pass_count++;
        end else begin
            $display("  FAIL [%0d] %s (got 0x%02X, want 0x%02X)",
                     test_num, name, actual, expected);
            fail_count++;
        end
    endtask

    task automatic queue_tx(input logic [7:0] value);
        while (tx_busy) @(posedge clk);
        @(negedge clk);
        tx_data  = value;
        tx_start = 1'b1;
        @(posedge clk);
        @(negedge clk);
        tx_start = 1'b0;
        repeat (2) @(posedge clk);
    endtask

    task automatic spi_select();
        @(negedge clk);
        spi_sck  = 1'b0;
        spi_cs_n = 1'b0;
        repeat (4) @(posedge clk);
    endtask

    task automatic spi_deselect();
        @(negedge clk);
        spi_sck  = 1'b0;
        spi_cs_n = 1'b1;
        repeat (4) @(posedge clk);
    endtask

    task automatic spi_transfer(input logic [7:0] din, output logic [7:0] dout);
        dout = 8'h00;
        for (int i = 7; i >= 0; i--) begin
            @(negedge clk);
            spi_mosi = din[i];
            repeat (10) @(posedge clk);
            dout = {dout[6:0], spi_miso};
            @(negedge clk);
            spi_sck = 1'b1;
            repeat (10) @(posedge clk);
            @(negedge clk);
            spi_sck = 1'b0;
            repeat (10) @(posedge clk);
        end
    endtask

    task automatic spi_write_byte(input logic [7:0] value);
        spi_select();
        spi_transfer(SPI_WRITE_OP, got);
        spi_transfer(value, got);
        spi_deselect();
    endtask

    task automatic spi_write_two(input logic [7:0] first,
                                 input logic [7:0] second);
        spi_select();
        spi_transfer(SPI_WRITE_OP, got);
        spi_transfer(first, got);
        spi_transfer(second, got);
        spi_deselect();
    endtask

    task automatic spi_poll_byte(output logic has_data,
                                 output logic [7:0] value);
        logic [7:0] opcode_response;
        logic [7:0] token;
        value = 8'h00;

        spi_select();
        spi_transfer(SPI_READ_OP, opcode_response);
        spi_transfer(8'h00, token);
        if (token == SPI_TOKEN_DATA) begin
            has_data = 1'b1;
            spi_transfer(8'h00, value);
        end else begin
            has_data = 1'b0;
        end
        spi_deselect();
    endtask

    task automatic expect_rx(input logic [7:0] expected);
        int timeout = 200;
        while (!rx_valid && timeout > 0) begin
            @(posedge clk);
            timeout--;
        end
        check("rx_valid asserted", rx_valid);
        if (rx_valid) begin
            check_eq8("rx byte", rx_data, expected);
            @(negedge clk);
            rx_ready = 1'b1;
            @(posedge clk);
            @(negedge clk);
            rx_ready = 1'b0;
            @(posedge clk);
        end
    endtask

    task automatic expect_no_rx(input string name);
        repeat (80) begin
            @(posedge clk);
            if (rx_valid) begin
                check(name, 1'b0);
                @(negedge clk);
                rx_ready = 1'b1;
                @(posedge clk);
                @(negedge clk);
                rx_ready = 1'b0;
                return;
            end
        end
        check(name, 1'b1);
    endtask

    initial begin
        rst       = 1'b1;
        spi_sck   = 1'b0;
        spi_cs_n  = 1'b1;
        spi_mosi  = 1'b0;
        rx_ready  = 1'b0;
        tx_data   = 8'h00;
        tx_start  = 1'b0;

        repeat (8) @(posedge clk);
        rst = 1'b0;
        repeat (8) @(posedge clk);

        check("MISO is high-Z while unselected", !spi_miso_oe);
        spi_select();
        check("MISO OE asserts while selected", spi_miso_oe);
        spi_cs_n = 1'b1;
        #1;
        check("MISO OE drops immediately when CS rises", !spi_miso_oe);
        repeat (8) @(posedge clk);

        rst = 1'b1;
        repeat (4) @(posedge clk);
        rst = 1'b0;
        repeat (8) @(posedge clk);

        $display("Test: empty READ poll returns EMPTY and queues no dummy MOSI");
        spi_poll_byte(has_data, value);
        check("empty read has no data", !has_data);
        check("read polling did not enqueue dummy MOSI", !rx_valid);
        check("underflow recorded for empty read", tx_underflow);
        check("SPI select was seen", selected_seen);

        $display("Test: WRITE transaction captures payload bytes only");
        spi_write_byte(8'h5A);
        expect_rx(8'h5A);
        expect_no_rx("single-byte write produces no duplicate RX byte");

        $display("Test: READ transaction returns token-framed payload bytes");
        queue_tx(8'hC3);
        queue_tx(8'hA5);
        queue_tx(8'h7E);
        spi_poll_byte(has_data, value);
        check("first read has data", has_data);
        check_eq8("first queued payload", value, 8'hC3);
        spi_poll_byte(has_data, value);
        check("second read has data", has_data);
        check_eq8("0xA5 payload survives framing", value, 8'hA5);
        spi_poll_byte(has_data, value);
        check("third read has data", has_data);
        check_eq8("third queued payload", value, 8'h7E);
        check("read polling still did not enqueue dummy MOSI", !rx_valid);

        $display("Test: TX FIFO exposes its full declared depth");
        for (fill_index = 0; fill_index < 8; fill_index++)
            queue_tx(8'h80 + fill_index[7:0]);
        check("TX FIFO applies backpressure at depth eight", tx_busy);
        for (fill_index = 0; fill_index < 8; fill_index++) begin
            spi_poll_byte(has_data, value);
            check("full-depth read has data", has_data);
            check_eq8("full-depth payload remains ordered", value,
                      8'h80 + fill_index[7:0]);
        end
        repeat (8) @(posedge clk);
        check("TX FIFO releases backpressure after drain", !tx_busy);

        $display("Test: WRITE transaction can carry multiple bytes");
        spi_write_two(8'h12, 8'h34);
        expect_rx(8'h12);
        expect_rx(8'h34);
        expect_no_rx("multi-byte write produces no duplicate RX byte after tail");

        check("RX FIFO did not overflow", !rx_overflow);
        check("TX FIFO did not overflow", !tx_overflow);

        if (fail_count == 0) begin
            $display("All %0d AstraHost SPI tests passed", pass_count);
            $finish;
        end else begin
            $display("%0d debug SPI tests failed out of %0d",
                     fail_count, pass_count + fail_count);
            $fatal;
        end
    end

endmodule
