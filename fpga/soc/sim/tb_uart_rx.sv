`timescale 1ns/1ps
module tb_uart_rx;
  localparam CLK_HZ=3125000, BAUD=115200;
  localparam integer BITT = CLK_HZ/BAUD;           // clks per bit = 27
  reg clk=0, rst=1, rx=1; wire [7:0] data; wire valid;
  uart_rx #(.CLK_HZ(CLK_HZ), .BAUD(BAUD)) dut(.clk(clk),.rst(rst),.rx(rx),.data(data),.valid(valid));
  always #16 clk=~clk;                             // ~31.25ns => 32MHz-ish; timing is clk-count based
  integer i; reg [7:0] got; integer ngot=0;
  task send(input [7:0] b);
    integer k;
    begin
      rx=0; repeat(BITT) @(posedge clk);           // start bit
      for(k=0;k<8;k=k+1) begin rx=b[k]; repeat(BITT) @(posedge clk); end
      rx=1; repeat(BITT) @(posedge clk);           // stop bit
      repeat(BITT) @(posedge clk);
    end
  endtask
  always @(posedge clk) if(valid) begin got<=data; ngot=ngot+1; end
  initial begin
    repeat(4) @(posedge clk); rst=0; repeat(4) @(posedge clk);
    send(8'h55);
    if (got!==8'h55) begin $display("FAIL got=%02x want=55", got); $fatal; end
    send(8'hA3);
    if (got!==8'hA3) begin $display("FAIL got=%02x want=A3", got); $fatal; end
    if (ngot!==2) begin $display("FAIL ngot=%0d want=2", ngot); $fatal; end
    $display("PASS uart_rx"); $finish;
  end
endmodule
