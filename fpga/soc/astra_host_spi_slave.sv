// SPI slave byte transport for AstraHost.
//
// Mode 0, MSB first. The ESP32 is the SPI master. Transactions start with an
// operation byte:
//   WRITE_OP: subsequent MOSI bytes are queued into the bridge RX stream.
//   READ_OP:  MISO returns DATA/EMPTY tokens; DATA is followed by a payload.
//
// The SPI shifter runs in the SCK domain and crosses into clk through small
// async FIFOs. This hardware-proven physical layer comes from NovaVM; Astra's
// commands, boot contract, and runtime services are independent.

module astra_host_spi_slave #(
    parameter int ADDR_WIDTH = 9,
    parameter int RX_ADDR_WIDTH = ADDR_WIDTH,
    parameter int TX_ADDR_WIDTH = ADDR_WIDTH,
    parameter logic [7:0] IDLE_BYTE = 8'hA5,
    parameter logic [7:0] WRITE_OP = 8'h57,       // 'W'
    parameter logic [7:0] READ_OP = 8'h52,        // 'R'
    parameter logic [7:0] READ_TOKEN_EMPTY = 8'h00,
    parameter logic [7:0] READ_TOKEN_DATA = 8'h01
) (
    input  logic       clk,
    input  logic       rst,

    input  logic       spi_sck,
    input  logic       spi_cs_n,
    input  logic       spi_mosi,
    (* syn_useioff = 1, ioff_dir = "output" *)
    output logic       spi_miso,
    output logic       spi_miso_oe,

    output logic [7:0] rx_data,
    output logic       rx_valid,
    input  logic       rx_ready,

    input  logic [7:0] tx_data,
    input  logic       tx_start,
    output logic       tx_busy,

    output logic       rx_overflow,
    output logic       tx_overflow,
    output logic       tx_underflow,
    output logic       selected_seen
);

    typedef enum logic [1:0] {
        SPI_MODE_OPCODE = 2'd0,
        SPI_MODE_WRITE  = 2'd1,
        SPI_MODE_READ   = 2'd2
    } spi_mode_t;

    // ---------------------------------------------------------------------
    // ESP/SCK domain: receive MOSI bytes and consume MISO response bytes.
    // ---------------------------------------------------------------------
    spi_mode_t spi_mode_sck;
    logic [7:0] rx_shift_sck;
    logic [2:0] rx_bit_count_sck;
    logic [7:0] tx_shift_sck;
    logic [2:0] tx_bit_count_sck;
    logic       read_data_phase_sck;
    logic [7:0] tx_prefetch_data_sck;
    logic       tx_prefetch_valid_sck;
    logic       tx_pop_pending_sck;
    logic       selected_seen_sck;
    logic       tx_underflow_sck;

    wire        rx_fifo_wr_ready;

    wire [7:0]  tx_fifo_rd_data;
    wire        tx_fifo_rd_valid;
    wire        tx_fifo_rd_ready;
    wire        spi_selected_reset = rst || spi_cs_n;

    assign spi_miso_oe = !spi_cs_n;

    wire [7:0] rx_next_byte_sck = {rx_shift_sck[6:0], spi_mosi};
    wire rx_fifo_wr_valid = !spi_cs_n &&
                             rx_bit_count_sck == 3'd7 &&
                             spi_mode_sck == SPI_MODE_WRITE;
    wire [7:0] rx_fifo_wr_data = rx_next_byte_sck;

    always_ff @(posedge spi_sck or posedge spi_selected_reset) begin
        if (spi_selected_reset) begin
            spi_mode_sck       <= SPI_MODE_OPCODE;
            rx_shift_sck       <= 8'h00;
            rx_bit_count_sck   <= 3'd0;
        end else begin
            rx_shift_sck <= rx_next_byte_sck;

            if (rx_bit_count_sck == 3'd7) begin
                rx_bit_count_sck <= 3'd0;
                unique case (spi_mode_sck)
                    SPI_MODE_OPCODE: begin
                        if (rx_next_byte_sck == WRITE_OP)
                            spi_mode_sck <= SPI_MODE_WRITE;
                        else if (rx_next_byte_sck == READ_OP)
                            spi_mode_sck <= SPI_MODE_READ;
                    end

                    SPI_MODE_WRITE: begin
                    end

                    default: ;
                endcase
            end else begin
                rx_bit_count_sck <= rx_bit_count_sck + 1'b1;
            end
        end
    end

    wire opcode_read_boundary_sck = rx_bit_count_sck == 3'd7 &&
                                     spi_mode_sck == SPI_MODE_OPCODE &&
                                     rx_next_byte_sck == READ_OP;
    wire tx_read_active_sck = spi_mode_sck == SPI_MODE_READ ||
                               opcode_read_boundary_sck;
    wire tx_payload_boundary_sck = tx_bit_count_sck == 3'd7 &&
                                    tx_read_active_sck &&
                                    read_data_phase_sck;
    wire tx_prefetch_allowed_sck = !spi_cs_n &&
                                    tx_read_active_sck &&
                                    !tx_prefetch_valid_sck &&
                                    !tx_pop_pending_sck &&
                                    tx_fifo_rd_valid &&
                                    !tx_payload_boundary_sck;
    wire tx_token_has_data_sck = tx_prefetch_valid_sck ||
                                  tx_prefetch_allowed_sck;
    wire [7:0] tx_token_data_sck = tx_prefetch_valid_sck
        ? tx_prefetch_data_sck
        : tx_fifo_rd_data;

    wire tx_underflow_event_sck = !spi_cs_n &&
                                   tx_bit_count_sck == 3'd7 &&
                                   tx_read_active_sck &&
                                   !tx_token_has_data_sck;

    always_ff @(posedge spi_sck or posedge spi_selected_reset) begin
        if (spi_selected_reset) begin
            tx_shift_sck        <= IDLE_BYTE;
            tx_bit_count_sck    <= 3'd0;
            read_data_phase_sck <= 1'b0;
            spi_miso            <= 1'b1;
        end else begin
            if (tx_bit_count_sck == 3'd7) begin
                tx_bit_count_sck <= 3'd0;

                if (tx_read_active_sck) begin
                    if (read_data_phase_sck) begin
                        tx_shift_sck <= tx_token_has_data_sck
                            ? tx_token_data_sck
                            : IDLE_BYTE;
                        spi_miso <= tx_token_has_data_sck
                            ? tx_token_data_sck[7]
                            : IDLE_BYTE[7];
                        read_data_phase_sck <= 1'b0;
                    end else if (tx_token_has_data_sck) begin
                        tx_shift_sck <= READ_TOKEN_DATA;
                        spi_miso <= READ_TOKEN_DATA[7];
                        read_data_phase_sck <= 1'b1;
                    end else begin
                        tx_shift_sck <= READ_TOKEN_EMPTY;
                        spi_miso <= READ_TOKEN_EMPTY[7];
                        read_data_phase_sck <= 1'b0;
                    end
                end else begin
                    tx_shift_sck <= IDLE_BYTE;
                    spi_miso <= IDLE_BYTE[7];
                    read_data_phase_sck <= 1'b0;
                end
            end else begin
                tx_bit_count_sck <= tx_bit_count_sck + 1'b1;
                tx_shift_sck <= {tx_shift_sck[6:0], 1'b0};
                spi_miso <= tx_shift_sck[6];
            end
        end
    end

    always_ff @(posedge spi_sck or posedge rst) begin
        if (rst) begin
            tx_prefetch_data_sck  <= 8'h00;
            tx_prefetch_valid_sck <= 1'b0;
        end else if (!spi_cs_n) begin
            if (tx_payload_boundary_sck && tx_prefetch_valid_sck)
                tx_prefetch_valid_sck <= 1'b0;
            if (tx_prefetch_allowed_sck) begin
                tx_prefetch_data_sck  <= tx_fifo_rd_data;
                tx_prefetch_valid_sck <= 1'b1;
            end
        end
    end

    always_ff @(posedge spi_sck or posedge spi_selected_reset) begin
        if (spi_selected_reset)
            tx_pop_pending_sck <= 1'b0;
        else
            tx_pop_pending_sck <= tx_prefetch_allowed_sck;
    end

    always_ff @(posedge spi_sck or posedge rst) begin
        if (rst) begin
            tx_underflow_sck  <= 1'b0;
            selected_seen_sck <= 1'b0;
        end else if (!spi_cs_n) begin
            selected_seen_sck <= 1'b1;
            if (tx_underflow_event_sck)
                tx_underflow_sck <= 1'b1;
        end
    end

    // ---------------------------------------------------------------------
    // Clock-domain crossings.
    // ---------------------------------------------------------------------
    astra_host_async_byte_fifo #(
        .ADDR_WIDTH(RX_ADDR_WIDTH)
    ) rx_fifo (
        .wr_clk    (spi_sck),
        .wr_rst    (rst),
        .wr_data   (rx_fifo_wr_data),
        .wr_valid  (rx_fifo_wr_valid),
        .wr_ready  (rx_fifo_wr_ready),
        .rd_clk    (clk),
        .rd_rst    (rst),
        .rd_data   (rx_data),
        .rd_valid  (rx_valid),
        .rd_ready  (rx_ready),
        .overflow  (rx_overflow),
        .underflow ()
    );

    wire tx_fifo_wr_ready;
    assign tx_fifo_rd_ready = !spi_cs_n && tx_pop_pending_sck;

    astra_host_async_byte_fifo #(
        .ADDR_WIDTH(TX_ADDR_WIDTH)
    ) tx_fifo (
        .wr_clk    (clk),
        .wr_rst    (rst),
        .wr_data   (tx_data),
        .wr_valid  (tx_start),
        .wr_ready  (tx_fifo_wr_ready),
        .rd_clk    (spi_sck),
        .rd_rst    (rst),
        .rd_data   (tx_fifo_rd_data),
        .rd_valid  (tx_fifo_rd_valid),
        .rd_ready  (tx_fifo_rd_ready),
        .overflow  (tx_overflow),
        .underflow ()
    );

    assign tx_busy = !tx_fifo_wr_ready;

    // Sticky status bits are diagnostic only. Synchronize SCK-domain flags
    // into clk so LEDs/debug consumers do not see raw async nodes.
    logic [2:0] selected_seen_sync;
    logic [2:0] tx_underflow_sync;
    always_ff @(posedge clk) begin
        if (rst) begin
            selected_seen_sync <= 3'b000;
            tx_underflow_sync  <= 3'b000;
            selected_seen      <= 1'b0;
            tx_underflow       <= 1'b0;
        end else begin
            selected_seen_sync <= {selected_seen_sync[1:0], selected_seen_sck};
            tx_underflow_sync  <= {tx_underflow_sync[1:0], tx_underflow_sck};
            selected_seen      <= selected_seen_sync[2];
            tx_underflow       <= tx_underflow_sync[2];
        end
    end

endmodule
