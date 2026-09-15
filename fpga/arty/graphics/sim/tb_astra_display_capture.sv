`timescale 1ns/1ps
`default_nettype none

module tb_astra_display_capture;
    localparam integer WIDTH = 8;
    localparam integer HEIGHT = 4;
    localparam integer PIXELS = WIDTH * HEIGHT;
    localparam integer BYTES = PIXELS * 3;
    localparam [31:0] BASE = 32'h40001000;

    reg build_clk = 1'b0;
    reg pixel_clk = 1'b0;
    always #5 build_clk = ~build_clk;
    always #7 pixel_clk = ~pixel_clk;

    reg build_reset = 1'b1;
    reg pixel_reset = 1'b1;
    reg pixel_frame_start = 1'b0;
    reg pixel_valid = 1'b0;
    reg [23:0] pixel_rgb = 24'd0;
    wire interrupt;

    reg [31:0] s_axi_awaddr = 32'd0;
    reg [2:0] s_axi_awprot = 3'd0;
    reg s_axi_awvalid = 1'b0;
    wire s_axi_awready;
    reg [31:0] s_axi_wdata = 32'd0;
    reg [3:0] s_axi_wstrb = 4'hf;
    reg s_axi_wvalid = 1'b0;
    wire s_axi_wready;
    wire [1:0] s_axi_bresp;
    wire s_axi_bvalid;
    reg s_axi_bready = 1'b1;
    reg [31:0] s_axi_araddr = 32'd0;
    reg [2:0] s_axi_arprot = 3'd0;
    reg s_axi_arvalid = 1'b0;
    wire s_axi_arready;
    wire [31:0] s_axi_rdata;
    wire [1:0] s_axi_rresp;
    wire s_axi_rvalid;
    reg s_axi_rready = 1'b1;

    wire m_axi_awid;
    wire [31:0] m_axi_awaddr;
    wire [7:0] m_axi_awlen;
    wire [2:0] m_axi_awsize;
    wire [1:0] m_axi_awburst;
    wire [3:0] m_axi_awcache;
    wire [2:0] m_axi_awprot;
    wire [3:0] m_axi_awqos;
    wire m_axi_awvalid;
    reg m_axi_awready = 1'b1;
    wire [63:0] m_axi_wdata;
    wire [7:0] m_axi_wstrb;
    wire m_axi_wlast;
    wire m_axi_wvalid;
    reg m_axi_wready = 1'b1;
    reg m_axi_bid = 1'b0;
    reg [1:0] m_axi_bresp = 2'b00;
    reg m_axi_bvalid = 1'b0;
    wire m_axi_bready;

    astra_display_capture #(
        .ARENA_BASE(32'h40000000), .ARENA_LIMIT(32'h40010000),
        .FRAME_WIDTH(WIDTH), .FRAME_HEIGHT(HEIGHT),
        .FIFO_ADDR_WIDTH(3), .BURST_BEATS(4), .AXI_ID_WIDTH(1)
    ) dut (.*);

    reg [7:0] memory [0:BYTES-1];
    reg [31:0] active_awaddr;
    integer active_beat;
    integer response_queue;
    integer response_delay;
    integer aw_count;
    reg inject_error;
    reg hold_responses;
    integer index;

    always @(posedge build_clk) begin
        if (build_reset) begin
            active_awaddr <= 32'd0;
            active_beat <= 0;
            response_queue <= 0;
            response_delay <= 0;
            aw_count <= 0;
            m_axi_bvalid <= 1'b0;
            m_axi_bresp <= 2'b00;
        end else begin
            if (m_axi_awvalid && m_axi_awready) begin
                if (m_axi_awlen != 3 || m_axi_awsize != 3 ||
                    m_axi_awburst != 2'b01)
                    $fatal(1, "bad AXI burst contract");
                active_awaddr <= m_axi_awaddr;
                active_beat <= 0;
                aw_count <= aw_count + 1;
            end
            if (m_axi_wvalid && m_axi_wready) begin
                if (m_axi_wstrb != 8'hff)
                    $fatal(1, "capture write must use every byte lane");
                for (index = 0; index < 8; index = index + 1)
                    memory[active_awaddr - BASE + active_beat * 8 + index]
                        <= m_axi_wdata[index * 8 +: 8];
                if (m_axi_wlast != (active_beat == 3))
                    $fatal(1, "bad WLAST beat=%0d wlast=%0d aw=%0d",
                           active_beat, m_axi_wlast, aw_count);
                if (m_axi_wlast) begin
                    response_queue <= response_queue + 1;
                    response_delay <= 2;
                end else begin
                    active_beat <= active_beat + 1;
                end
            end
            if (m_axi_bvalid && m_axi_bready) begin
                m_axi_bvalid <= 1'b0;
                response_queue <= response_queue - 1;
                m_axi_bresp <= 2'b00;
            end else if (!m_axi_bvalid && response_queue != 0 &&
                         !hold_responses) begin
                if (response_delay != 0)
                    response_delay <= response_delay - 1;
                else begin
                    m_axi_bvalid <= 1'b1;
                    m_axi_bresp <= inject_error ? 2'b10 : 2'b00;
                    inject_error <= 1'b0;
                end
            end
        end
    end

    task automatic write_reg(input [7:0] address, input [31:0] value);
        begin
            @(negedge build_clk);
            s_axi_awaddr = {24'd0, address};
            s_axi_wdata = value;
            s_axi_awvalid = 1'b1;
            s_axi_wvalid = 1'b1;
            while (!(s_axi_awready && s_axi_wready))
                @(negedge build_clk);
            @(negedge build_clk);
            s_axi_awvalid = 1'b0;
            s_axi_wvalid = 1'b0;
            while (!s_axi_bvalid)
                @(negedge build_clk);
            if (s_axi_bresp != 2'b00)
                $fatal(1, "register write failed at %02x", address);
            @(negedge build_clk);
        end
    endtask

    task automatic read_reg(input [7:0] address, output [31:0] value);
        begin
            @(negedge build_clk);
            s_axi_araddr = {24'd0, address};
            s_axi_arvalid = 1'b1;
            while (!s_axi_arready)
                @(negedge build_clk);
            @(negedge build_clk);
            s_axi_arvalid = 1'b0;
            while (!s_axi_rvalid)
                @(negedge build_clk);
            if (s_axi_rresp != 2'b00)
                $fatal(1, "register read failed at %02x", address);
            value = s_axi_rdata;
            @(negedge build_clk);
        end
    endtask

    task automatic send_frame;
        integer pixel;
        begin
            repeat (6) @(negedge pixel_clk);
            for (pixel = 0; pixel < PIXELS; pixel = pixel + 1) begin
                pixel_frame_start = pixel == 0;
                pixel_valid = 1'b1;
                pixel_rgb = {pixel[7:0], pixel[7:0] + 8'h40,
                             pixel[7:0] + 8'h80};
                @(negedge pixel_clk);
            end
            pixel_frame_start = 1'b0;
            pixel_valid = 1'b0;
        end
    endtask

    task automatic wait_completion;
        integer timeout;
        reg [31:0] status;
        begin
            timeout = 0;
            status = 0;
            while (!status[3] && timeout < 2000) begin
                read_reg(8'h10, status);
                timeout = timeout + 1;
            end
            if (!status[3])
                $fatal(1, "capture completion timeout status=%08x", status);
        end
    endtask

    task automatic wait_drop(input [31:0] expected);
        integer timeout;
        reg [31:0] count;
        begin
            timeout = 0;
            count = 0;
            while (count != expected && timeout < 2000) begin
                read_reg(8'h28, count);
                timeout = timeout + 1;
            end
            if (count != expected)
                $fatal(1, "capture drop timeout count=%0d", count);
        end
    endtask

    integer pixel;
    reg [31:0] value;
    initial begin
        inject_error = 1'b0;
        hold_responses = 1'b0;
        for (index = 0; index < BYTES; index = index + 1)
            memory[index] = 8'hcc;
        repeat (5) @(posedge build_clk);
        build_reset = 1'b0;
        pixel_reset = 1'b0;

        send_frame();
        repeat (20) @(posedge build_clk);
        if (aw_count != 0)
            $fatal(1, "disabled capture issued writes");

        write_reg(8'h14, BASE);
        write_reg(8'h0c, 32'h00000003);
        hold_responses = 1'b1;
        send_frame();
        repeat (20) @(posedge build_clk);
        if (interrupt)
            $fatal(1, "capture published before AXI durability responses");
        hold_responses = 1'b0;
        wait_completion();
        if (!interrupt)
            $fatal(1, "completion interrupt was not retained");
        read_reg(8'h18, value);
        if (value != BYTES)
            $fatal(1, "bad frame byte count %0d", value);
        read_reg(8'h24, value);
        if (value != 1)
            $fatal(1, "bad completed count %0d", value);
        for (pixel = 0; pixel < PIXELS; pixel = pixel + 1) begin
            if (memory[pixel * 3] != pixel[7:0] ||
                memory[pixel * 3 + 1] != ((pixel + 8'h40) & 8'hff) ||
                memory[pixel * 3 + 2] != ((pixel + 8'h80) & 8'hff))
                $fatal(1, "RGB888 mismatch at pixel %0d: %02x %02x %02x",
                    pixel, memory[pixel * 3], memory[pixel * 3 + 1],
                    memory[pixel * 3 + 2]);
        end

        write_reg(8'h0c, 32'h00000005);
        if (interrupt)
            $fatal(1, "completion acknowledge did not clear interrupt");

        inject_error = 1'b1;
        write_reg(8'h0c, 32'h00000003);
        send_frame();
        wait_drop(1);
        if (interrupt)
            $fatal(1, "failed capture was published");
        read_reg(8'h30, value);
        if (value == 0)
            $fatal(1, "AXI error was not counted");

        m_axi_awready = 1'b0;
        write_reg(8'h0c, 32'h00000003);
        send_frame();
        repeat (20) @(posedge build_clk);
        @(negedge build_clk);
        m_axi_awready = 1'b1;
        wait_drop(2);
        read_reg(8'h2c, value);
        if (value == 0)
            $fatal(1, "FIFO overflow was not counted");

        write_reg(8'h0c, 32'h00000000);
        $display("astra display capture tests passed");
        $finish;
    end
endmodule

`default_nettype wire
