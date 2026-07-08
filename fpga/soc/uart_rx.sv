// 8N1 UART receiver. Oversamples by counting clks/bit; samples at mid-bit.
module uart_rx #(parameter CLK_HZ=3125000, BAUD=115200) (
    input  wire clk, input wire rst, input wire rx,
    output reg [7:0] data, output reg valid);
    localparam integer DIV = CLK_HZ/BAUD;         // 27
    reg rx_s1=1'b1, rx_s2=1'b1;                    // 2-FF synchronizer
    always @(posedge clk) begin rx_s1<=rx; rx_s2<=rx_s1; end
    localparam S_IDLE=2'd0, S_START=2'd1, S_DATA=2'd2, S_STOP=2'd3;
    reg [1:0] st=S_IDLE; reg [15:0] cnt=0; reg [2:0] bitn=0; reg [7:0] sh=0;
    always @(posedge clk) begin
        valid <= 1'b0;
        if (rst) begin st<=S_IDLE; cnt<=0; bitn<=0; end
        else case (st)
            S_IDLE:  if (!rx_s2) begin st<=S_START; cnt<=DIV/2; end   // detect start, aim mid-bit
            S_START: if (cnt==0) begin
                        if (!rx_s2) begin st<=S_DATA; cnt<=DIV-1; bitn<=0; end
                        else st<=S_IDLE;                              // false start
                     end else cnt<=cnt-1'b1;
            S_DATA:  if (cnt==0) begin
                        sh<={rx_s2, sh[7:1]}; cnt<=DIV-1;
                        if (bitn==3'd7) st<=S_STOP; else bitn<=bitn+1'b1;
                     end else cnt<=cnt-1'b1;
            S_STOP:  if (cnt==0) begin data<=sh; valid<=1'b1; st<=S_IDLE; end
                     else cnt<=cnt-1'b1;
        endcase
    end
endmodule
