`timescale 1ns/1ps
`default_nettype none

module tb_sd_card_spi_model;
    reg cs_n = 1'b1;
    reg sclk = 1'b0;
    reg mosi = 1'b1;
    wire miso;
    reg [7:0] value;
    reg [7:0] mbr [0:511];
    integer index;

    sd_card_spi_model card (
        .cs_n(cs_n), .sclk(sclk), .mosi(mosi), .miso(miso)
    );

    task transfer(input [7:0] outgoing, output [7:0] incoming);
        integer bit_number;
        begin
            incoming = 8'd0;
            for (bit_number = 7; bit_number >= 0; bit_number = bit_number - 1) begin
                mosi = outgoing[bit_number];
                #5 sclk = 1'b1;
                #1 incoming[bit_number] = miso;
                #4 sclk = 1'b0;
                #5;
            end
        end
    endtask

    task command(input [5:0] number, input [31:0] argument,
                 input [7:0] crc, output [7:0] response);
        begin
            cs_n = 1'b0;
            transfer(8'hff, value);
            transfer({2'b01, number}, value);
            transfer(argument[31:24], value);
            transfer(argument[23:16], value);
            transfer(argument[15:8], value);
            transfer(argument[7:0], value);
            transfer(crc, value);
            transfer(8'hff, response);
        end
    endtask

    task deselect;
        begin
            transfer(8'hff, value);
            cs_n = 1'b1;
            transfer(8'hff, value);
        end
    endtask

    initial begin
        #20;
        command(0, 0, 8'h95, value);
        if (value != 8'h01) $fatal(1, "CMD0 response %02x", value);
        deselect();

        command(8, 32'h000001aa, 8'h87, value);
        if (value != 8'h01) $fatal(1, "CMD8 response %02x", value);
        transfer(8'hff, value);
        if (value != 8'h00) $fatal(1, "CMD8 R7[0] %02x", value);
        transfer(8'hff, value);
        if (value != 8'h00) $fatal(1, "CMD8 R7[1] %02x", value);
        transfer(8'hff, value);
        if (value != 8'h01) $fatal(1, "CMD8 R7[2] %02x", value);
        transfer(8'hff, value);
        if (value != 8'haa) $fatal(1, "CMD8 R7[3] %02x", value);
        deselect();

        command(55, 0, 8'h01, value);
        if (value != 8'h01) $fatal(1, "CMD55 response %02x", value);
        deselect();
        command(41, 32'h40000000, 8'h01, value);
        if (value != 8'h00) $fatal(1, "ACMD41 response %02x", value);
        deselect();

        command(17, 0, 8'h01, value);
        if (value != 8'h00) $fatal(1, "CMD17 response %02x", value);
        transfer(8'hff, value);
        if (value != 8'hff) $fatal(1, "CMD17 gap %02x", value);
        transfer(8'hff, value);
        if (value != 8'hfe) $fatal(1, "CMD17 token %02x", value);
        for (index = 0; index < 512; index = index + 1)
            transfer(8'hff, mbr[index]);
        if (mbr[446 + 4] != 8'h0c || mbr[510] != 8'h55 || mbr[511] != 8'haa)
            $fatal(1, "invalid MBR type=%02x signature=%02x%02x",
                   mbr[446 + 4], mbr[510], mbr[511]);

        $display("PASS SDHC SPI init and sector read");
        $finish;
    end
endmodule

`default_nettype wire
