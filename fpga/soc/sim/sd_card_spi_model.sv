`timescale 1ns/1ps
`default_nettype none

// Minimal SDHC SPI model for the stage-0 boot path. It implements only the
// initialization and single-block read commands used by the immutable loader.
module sd_card_spi_model #(
    parameter IMAGE_FILE = "sdcard.img"
) (
    input  wire cs_n,
    input  wire sclk,
    input  wire mosi,
    output reg  miso
);
    integer image_fd;
    integer command_count;
    integer receive_bits;
    integer tx_count;
    integer tx_index;
    integer tx_bit;
    integer read_count;
    integer seek_status;
    integer index;
    reg [7:0] receive_shift;
    reg [7:0] command [0:5];
    reg [7:0] tx_buffer [0:519];
    reg [7:0] sector_data [0:511];
    reg idle;
    reg app_command;

    task queue_byte(input [7:0] value);
        begin
            if (tx_count >= 520) $fatal(1, "SD response overflow");
            tx_buffer[tx_count] = value;
            tx_count = tx_count + 1;
        end
    endtask

    task queue_sector(input [31:0] lba);
        begin
            seek_status = $fseek(image_fd, lba * 512, 0);
            if (seek_status != 0)
                $fatal(1, "SD image seek failed for LBA %0d", lba);
            read_count = $fread(sector_data, image_fd, 0, 512);
            if (read_count != 512)
                $fatal(1, "SD image short read for LBA %0d: %0d", lba,
                       read_count);
            queue_byte(8'hff);
            queue_byte(8'hfe);
            for (index = 0; index < 512; index = index + 1)
                queue_byte(sector_data[index]);
            queue_byte(8'hff);
            queue_byte(8'hff);
        end
    endtask

    task process_command;
        reg [5:0] number;
        reg [31:0] argument;
        begin
            number = command[0][5:0];
            argument = {command[1], command[2], command[3], command[4]};
            tx_count = 0;
            tx_index = 0;
            case (number)
                6'd0: begin
                    idle = 1'b1;
                    app_command = 1'b0;
                    queue_byte(8'h01);
                end
                6'd8: begin
                    queue_byte(idle ? 8'h01 : 8'h00);
                    queue_byte(8'h00);
                    queue_byte(8'h00);
                    queue_byte(8'h01);
                    queue_byte(8'haa);
                end
                6'd55: begin
                    app_command = 1'b1;
                    queue_byte(idle ? 8'h01 : 8'h00);
                end
                6'd41: begin
                    if (app_command) begin
                        idle = 1'b0;
                        app_command = 1'b0;
                        queue_byte(8'h00);
                    end else begin
                        queue_byte(8'h05);
                    end
                end
                6'd58: begin
                    queue_byte(idle ? 8'h01 : 8'h00);
                    queue_byte(8'h40);
                    queue_byte(8'hff);
                    queue_byte(8'h80);
                    queue_byte(8'h00);
                end
                6'd16: queue_byte(idle ? 8'h01 : 8'h00);
                6'd17: begin
                    if (idle) begin
                        queue_byte(8'h01);
                    end else begin
                        queue_byte(8'h00);
                        queue_sector(argument);
                    end
                end
                default: queue_byte(idle ? 8'h05 : 8'h04);
            endcase
            tx_bit = 8; // Load response bit 7 on the command's trailing edge.
        end
    endtask

    initial begin
        image_fd = $fopen(IMAGE_FILE, "rb");
        if (image_fd == 0) $fatal(1, "cannot open SD image %s", IMAGE_FILE);
        miso = 1'b1;
        command_count = 0;
        receive_bits = 0;
        receive_shift = 8'hff;
        tx_count = 0;
        tx_index = 0;
        tx_bit = 0;
        idle = 1'b1;
        app_command = 1'b0;
    end

    always @(cs_n) begin
        miso = 1'b1;
        command_count = 0;
        receive_bits = 0;
        receive_shift = 8'hff;
        tx_count = 0;
        tx_index = 0;
        tx_bit = 0;
    end

    always @(posedge sclk) begin
        if (!cs_n) begin
            receive_shift = {receive_shift[6:0], mosi};
            receive_bits = receive_bits + 1;
            if (receive_bits == 8) begin
                if (command_count == 0) begin
                    if (receive_shift[7:6] == 2'b01) begin
                        command[0] = receive_shift;
                        command_count = 1;
                    end
                end else begin
                    command[command_count] = receive_shift;
                    command_count = command_count + 1;
                    if (command_count == 6) begin
                        process_command();
                        command_count = 0;
                    end
                end
                receive_bits = 0;
                receive_shift = 8'hff;
            end
        end
    end

    always @(negedge sclk) begin
        if (!cs_n && tx_count != 0) begin
            if (tx_bit == 8) begin
                tx_bit = 7;
                miso = tx_buffer[tx_index][7];
            end else if (tx_bit != 0) begin
                tx_bit = tx_bit - 1;
                miso = tx_buffer[tx_index][tx_bit];
            end else if (tx_index + 1 < tx_count) begin
                tx_index = tx_index + 1;
                tx_bit = 7;
                miso = tx_buffer[tx_index][7];
            end else begin
                tx_count = 0;
                miso = 1'b1;
            end
        end else begin
            miso = 1'b1;
        end
    end
endmodule

`default_nettype wire
