// Directed raster, clipping, format, guard, and completion tests for Astraea.
`timescale 1ns/1ps
`default_nettype none

module tb_astraea_draw;
    localparam integer MEM_BYTES = 65536;

    reg cpu_clk = 1'b0;
    reg mem_clk = 1'b0;
    reg cpu_rst = 1'b1;
    reg mem_rst = 1'b1;
    always #5 cpu_clk = ~cpu_clk;
    always #3 mem_clk = ~mem_clk;

    reg cpu_write_stb = 1'b0;
    reg [4:0] cpu_reg = 5'd0;
    reg [3:0] cpu_be = 4'd0;
    reg [31:0] cpu_wdata = 32'd0;
    wire [31:0] cpu_rdata;
    wire cpu_busy;
    wire cpu_done;
    wire cpu_irq;
    wire cache_flush;

    wire mem_lock;
    wire mem_valid;
    wire mem_ready;
    wire mem_write;
    wire [24:0] mem_addr;
    wire [3:0] mem_be;
    wire [31:0] mem_wdata;
    reg mem_rsp_valid = 1'b0;
    reg [31:0] mem_rdata = 32'd0;

    astraea_draw dut (
        .cpu_clk(cpu_clk), .cpu_rst(cpu_rst),
        .cpu_write_stb(cpu_write_stb), .cpu_reg(cpu_reg),
        .cpu_be(cpu_be), .cpu_wdata(cpu_wdata), .cpu_rdata(cpu_rdata),
        .cpu_busy(cpu_busy), .cpu_done(cpu_done), .cpu_irq(cpu_irq),
        .cache_flush(cache_flush), .mem_clk(mem_clk), .mem_rst(mem_rst),
        .mem_lock(mem_lock), .mem_valid(mem_valid), .mem_ready(mem_ready),
        .mem_write(mem_write), .mem_addr(mem_addr), .mem_be(mem_be),
        .mem_wdata(mem_wdata), .mem_rsp_valid(mem_rsp_valid),
        .mem_rdata(mem_rdata)
    );

    reg [7:0] memory [0:MEM_BYTES-1];
    reg [7:0] expected [0:MEM_BYTES-1];
    reg response_pending = 1'b0;
    reg [31:0] response_data = 32'd0;
    assign mem_ready = !response_pending;

    integer memory_index;
    integer setup_x;
    integer setup_y;
    integer setup_address;
    reg [31:0] register_value;
    always @(posedge mem_clk) begin
        mem_rsp_valid <= 1'b0;
        if (mem_rst) begin
            response_pending <= 1'b0;
            mem_rdata <= 32'd0;
        end else begin
            if (response_pending) begin
                mem_rsp_valid <= 1'b1;
                mem_rdata <= response_data;
                response_pending <= 1'b0;
            end
            if (mem_valid && mem_ready) begin
                if (mem_addr + 25'd3 >= MEM_BYTES)
                    $fatal(1, "draw memory address out of model: %08x", mem_addr);
                response_data <= {
                    memory[mem_addr], memory[mem_addr + 25'd1],
                    memory[mem_addr + 25'd2], memory[mem_addr + 25'd3]
                };
                if (mem_write) begin
                    if (mem_be[3]) memory[mem_addr] <= mem_wdata[31:24];
                    if (mem_be[2]) memory[mem_addr + 25'd1] <= mem_wdata[23:16];
                    if (mem_be[1]) memory[mem_addr + 25'd2] <= mem_wdata[15:8];
                    if (mem_be[0]) memory[mem_addr + 25'd3] <= mem_wdata[7:0];
                end
                response_pending <= 1'b1;
            end
        end
    end

    function automatic [31:0] packed_xy(input integer x, input integer y);
        packed_xy = {y[15:0], x[15:0]};
    endfunction

    task automatic write_reg(input [4:0] address, input [31:0] value);
        begin
            @(negedge cpu_clk);
            cpu_reg = address;
            cpu_wdata = value;
            cpu_be = 4'b1111;
            cpu_write_stb = 1'b1;
            @(negedge cpu_clk);
            cpu_write_stb = 1'b0;
            cpu_be = 4'd0;
        end
    endtask

    task automatic write_reg_be(input [4:0] address,
                                input [31:0] value,
                                input [3:0] enables);
        begin
            // Present the address for a full cycle before a partial write so
            // the synchronous command RAM read supplies the retained bytes.
            @(negedge cpu_clk);
            cpu_reg = address;
            cpu_write_stb = 1'b0;
            cpu_be = 4'd0;
            @(posedge cpu_clk);
            @(negedge cpu_clk);
            cpu_wdata = value;
            cpu_be = enables;
            cpu_write_stb = 1'b1;
            @(negedge cpu_clk);
            cpu_write_stb = 1'b0;
            cpu_be = 4'd0;
        end
    endtask

    task automatic read_reg(input [4:0] address,
                            output [31:0] value);
        begin
            @(negedge cpu_clk);
            cpu_reg = address;
            cpu_write_stb = 1'b0;
            cpu_be = 4'd0;
            @(posedge cpu_clk);
            #1 value = cpu_rdata;
        end
    endtask

    task automatic wait_command(input [7:0] expected_error,
                                input [31:0] expected_fence);
        integer timeout;
        begin
            timeout = 0;
            while (!cpu_done && timeout < 2000000) begin
                @(posedge cpu_clk);
                timeout = timeout + 1;
            end
            if (timeout == 2000000)
                $fatal(1, "draw command timeout fence=%08x",
                       expected_fence);
            cpu_reg = 5'd21;
            #1;
            if (cpu_rdata[15:8] !== expected_error)
                $fatal(1, "draw error fence=%08x expected=%0d actual=%0d",
                       expected_fence, expected_error, cpu_rdata[15:8]);
            if (!cpu_rdata[1] || cpu_rdata[0])
                $fatal(1, "draw completion status invalid: %08x", cpu_rdata);
            cpu_reg = 5'd22;
            #1;
            if (cpu_rdata !== expected_fence)
                $fatal(1, "draw fence expected=%08x actual=%08x",
                       expected_fence, cpu_rdata);
        end
    endtask

    integer fence_value = 1;
    task automatic run_command(input [31:0] operation,
                               input [7:0] expected_error);
        begin
            write_reg(5'd22, fence_value);
            write_reg(5'd19, operation);
            write_reg(5'd20, 32'h00000001);
            wait_command(expected_error, fence_value);
            fence_value = fence_value + 1;
        end
    endtask

    integer model_base;
    integer model_pitch;
    integer model_format;
    integer model_clip_x0;
    integer model_clip_y0;
    integer model_clip_x1;
    integer model_clip_y1;
    integer model_fg;
    integer model_bg;

    task automatic configure_surface(
        input integer base,
        input integer pitch,
        input integer format_rgb565,
        input integer clip_x0,
        input integer clip_y0,
        input integer clip_x1,
        input integer clip_y1,
        input integer foreground,
        input integer background
    );
        begin
            model_base = base;
            model_pitch = pitch;
            model_format = format_rgb565;
            model_clip_x0 = clip_x0;
            model_clip_y0 = clip_y0;
            model_clip_x1 = clip_x1;
            model_clip_y1 = clip_y1;
            model_fg = foreground;
            model_bg = background;
            write_reg(5'd0, base);
            write_reg(5'd1, pitch);
            write_reg(5'd2, format_rgb565);
            write_reg(5'd3, packed_xy(clip_x0, clip_y0));
            write_reg(5'd4, packed_xy(clip_x1, clip_y1));
            write_reg(5'd8, foreground);
            write_reg(5'd9, background);
        end
    endtask

    task automatic put_expected(input integer x, input integer y,
                                input integer color);
        integer address;
        begin
            if (x >= model_clip_x0 && x < model_clip_x1 &&
                y >= model_clip_y0 && y < model_clip_y1) begin
                address = model_base + y * model_pitch +
                          x * (model_format ? 2 : 1);
                if (model_format) begin
                    expected[address] = color[15:8];
                    expected[address + 1] = color[7:0];
                end else begin
                    expected[address] = color[7:0];
                end
            end
        end
    endtask

    task automatic model_line(input integer x0_in, input integer y0_in,
                              input integer x1, input integer y1);
        integer x0;
        integer y0;
        integer dx;
        integer dy;
        integer sx;
        integer sy;
        integer err;
        integer e2;
        integer finished;
        begin
            x0 = x0_in;
            y0 = y0_in;
            dx = x1 >= x0 ? x1 - x0 : x0 - x1;
            dy = y1 >= y0 ? y1 - y0 : y0 - y1;
            sx = x0 < x1 ? 1 : -1;
            sy = y0 < y1 ? 1 : -1;
            err = dx - dy;
            finished = 0;
            while (!finished) begin
                put_expected(x0, y0, model_fg);
                if (x0 == x1 && y0 == y1) begin
                    finished = 1;
                end else begin
                    e2 = 2 * err;
                    if (e2 > -dy) begin
                        err = err - dy;
                        x0 = x0 + sx;
                    end
                    if (e2 < dx) begin
                        err = err + dx;
                        y0 = y0 + sy;
                    end
                end
            end
        end
    endtask

    task automatic command_line(input integer x0, input integer y0,
                                input integer x1, input integer y1);
        begin
            write_reg(5'd5, packed_xy(x0, y0));
            write_reg(5'd6, packed_xy(x1, y1));
            model_line(x0, y0, x1, y1);
            run_command(32'd0, 8'd0);
        end
    endtask

    task automatic model_rect(input integer x0, input integer y0,
                              input integer x1, input integer y1,
                              input integer filled);
        integer xa;
        integer xb;
        integer ya;
        integer yb;
        integer x;
        integer y;
        begin
            xa = x0 < x1 ? x0 : x1;
            xb = x0 > x1 ? x0 : x1;
            ya = y0 < y1 ? y0 : y1;
            yb = y0 > y1 ? y0 : y1;
            if (filled) begin
                for (y = ya; y <= yb; y = y + 1)
                    for (x = xa; x <= xb; x = x + 1)
                        put_expected(x, y, model_fg);
            end else begin
                model_line(xa, ya, xb, ya);
                model_line(xb, ya, xb, yb);
                model_line(xb, yb, xa, yb);
                model_line(xa, yb, xa, ya);
            end
        end
    endtask

    task automatic command_rect(input integer x0, input integer y0,
                                input integer x1, input integer y1,
                                input integer filled);
        begin
            write_reg(5'd5, packed_xy(x0, y0));
            write_reg(5'd6, packed_xy(x1, y1));
            model_rect(x0, y0, x1, y1, filled);
            run_command(filled ? 32'd2 : 32'd1, 8'd0);
        end
    endtask

    task automatic model_circle(input integer cx, input integer cy,
                                input integer radius, input integer filled);
        integer x;
        integer y;
        integer err;
        integer left;
        integer right;
        integer column;
        begin
            x = radius;
            y = 0;
            err = 0;
            while (x >= y) begin
                if (filled) begin
                    left = cx - x; right = cx + x;
                    for (column = left; column <= right; column = column + 1) begin
                        put_expected(column, cy + y, model_fg);
                        put_expected(column, cy - y, model_fg);
                    end
                    left = cx - y; right = cx + y;
                    for (column = left; column <= right; column = column + 1) begin
                        put_expected(column, cy + x, model_fg);
                        put_expected(column, cy - x, model_fg);
                    end
                end else begin
                    put_expected(cx + x, cy + y, model_fg);
                    put_expected(cx + y, cy + x, model_fg);
                    put_expected(cx - y, cy + x, model_fg);
                    put_expected(cx - x, cy + y, model_fg);
                    put_expected(cx - x, cy - y, model_fg);
                    put_expected(cx - y, cy - x, model_fg);
                    put_expected(cx + y, cy - x, model_fg);
                    put_expected(cx + x, cy - y, model_fg);
                end
                y = y + 1;
                err = err + 1 + 2 * y;
                if (2 * (err - x) + 1 > 0) begin
                    x = x - 1;
                    err = err + 1 - 2 * x;
                end
            end
        end
    endtask

    task automatic command_circle(input integer cx, input integer cy,
                                  input integer radius, input integer filled);
        begin
            write_reg(5'd5, packed_xy(cx, cy));
            write_reg(5'd7, radius);
            model_circle(cx, cy, radius, filled);
            run_command(filled ? 32'd4 : 32'd3, 8'd0);
        end
    endtask

    task automatic model_ellipse(input integer cx, input integer cy,
                                 input integer rx, input integer ry,
                                 input integer filled);
        longint signed x;
        longint signed y;
        longint signed dx;
        longint signed dy;
        longint signed err;
        longint signed e2;
        integer column;
        begin
            if (rx == 0) begin
                model_line(cx, cy - ry, cx, cy + ry);
            end else if (ry == 0) begin
                model_line(cx - rx, cy, cx + rx, cy);
            end else begin
                x = -rx;
                y = 0;
                dx = (1 + 2 * x) * ry * ry;
                dy = x * x;
                err = dx + dy;
                while (x <= 0) begin
                    if (filled) begin
                        for (column = cx + x; column <= cx - x;
                             column = column + 1) begin
                            put_expected(column, cy + y, model_fg);
                            put_expected(column, cy - y, model_fg);
                        end
                    end else begin
                        put_expected(cx - x, cy + y, model_fg);
                        put_expected(cx + x, cy + y, model_fg);
                        put_expected(cx + x, cy - y, model_fg);
                        put_expected(cx - x, cy - y, model_fg);
                    end
                    e2 = 2 * err;
                    if (e2 >= dx) begin
                        x = x + 1;
                        dx = dx + 2 * ry * ry;
                        err = err + dx;
                    end
                    if (e2 <= dy) begin
                        y = y + 1;
                        dy = dy + 2 * rx * rx;
                        err = err + dy;
                    end
                end
                while (y < ry) begin
                    y = y + 1;
                    put_expected(cx, cy + y, model_fg);
                    put_expected(cx, cy - y, model_fg);
                end
            end
        end
    endtask

    task automatic command_ellipse(input integer cx, input integer cy,
                                   input integer rx, input integer ry,
                                   input integer filled);
        begin
            write_reg(5'd5, packed_xy(cx, cy));
            write_reg(5'd7, {ry[15:0], rx[15:0]});
            model_ellipse(cx, cy, rx, ry, filled);
            run_command(filled ? 32'd6 : 32'd5, 8'd0);
        end
    endtask

    task automatic model_pattern(input integer x0, input integer y0,
                                 input integer x1, input integer y1,
                                 input integer origin_x, input integer origin_y,
                                 input reg [63:0] bits, input integer opaque);
        integer xa;
        integer xb;
        integer ya;
        integer yb;
        integer x;
        integer y;
        integer bit_index;
        begin
            xa = x0 < x1 ? x0 : x1;
            xb = x0 > x1 ? x0 : x1;
            ya = y0 < y1 ? y0 : y1;
            yb = y0 > y1 ? y0 : y1;
            for (y = ya; y <= yb; y = y + 1) begin
                for (x = xa; x <= xb; x = x + 1) begin
                    bit_index = (((y - origin_y) & 7) * 8) +
                                ((x - origin_x) & 7);
                    if (bits[63 - bit_index])
                        put_expected(x, y, model_fg);
                    else if (opaque)
                        put_expected(x, y, model_bg);
                end
            end
        end
    endtask

    task automatic command_pattern(input integer x0, input integer y0,
                                   input integer x1, input integer y1,
                                   input integer origin_x, input integer origin_y,
                                   input reg [63:0] bits, input integer opaque);
        begin
            write_reg(5'd5, packed_xy(x0, y0));
            write_reg(5'd6, packed_xy(x1, y1));
            write_reg(5'd10, bits[63:32]);
            write_reg(5'd11, bits[31:0]);
            write_reg(5'd12, packed_xy(origin_x, origin_y));
            model_pattern(x0, y0, x1, y1, origin_x, origin_y, bits, opaque);
            run_command(32'd7 | (opaque ? 32'h00000100 : 32'd0), 8'd0);
        end
    endtask

    task automatic set_memory_byte(input integer address, input integer value);
        begin
            memory[address] = value[7:0];
            expected[address] = value[7:0];
        end
    endtask

    task automatic set_memory_long(input integer address,
                                   input reg [31:0] value);
        begin
            set_memory_byte(address, value[31:24]);
            set_memory_byte(address + 1, value[23:16]);
            set_memory_byte(address + 2, value[15:8]);
            set_memory_byte(address + 3, value[7:0]);
        end
    endtask

    function automatic integer expected_pixel(input integer x,
                                               input integer y);
        integer address;
        begin
            address = model_base + y * model_pitch +
                      x * (model_format ? 2 : 1);
            expected_pixel = model_format ?
                {expected[address], expected[address + 1]} : expected[address];
        end
    endfunction

    function automatic [15:0] model_blend565(
        input [15:0] foreground,
        input [15:0] destination,
        input integer coverage
    );
        integer red;
        integer green;
        integer blue;
        begin
            red = (foreground[15:11] * coverage +
                   destination[15:11] * (15 - coverage) + 7) / 15;
            green = (foreground[10:5] * coverage +
                     destination[10:5] * (15 - coverage) + 7) / 15;
            blue = (foreground[4:0] * coverage +
                    destination[4:0] * (15 - coverage) + 7) / 15;
            model_blend565 = {red[4:0], green[5:0], blue[4:0]};
        end
    endfunction

    task automatic model_glyph(
        input integer operation,
        input integer source_base,
        input integer source_pitch,
        input integer source_x,
        input integer source_y,
        input integer destination_x,
        input integer destination_y,
        input integer width,
        input integer height,
        input integer palette_base,
        input integer transparent_index,
        input integer opaque
    );
        integer x;
        integer y;
        integer source_pixel_x;
        integer source_address;
        integer source_byte;
        integer value;
        integer palette_address;
        integer destination_value;
        begin
            for (y = 0; y < height; y = y + 1) begin
                for (x = 0; x < width; x = x + 1) begin
                    source_pixel_x = source_x + x;
                    case (operation)
                        8: begin
                            source_address = source_base +
                                (source_y + y) * source_pitch +
                                (source_pixel_x >> 3);
                            source_byte = expected[source_address];
                            value = (source_byte >>
                                     (7 - (source_pixel_x & 7))) & 1;
                            if (value)
                                put_expected(destination_x + x,
                                             destination_y + y, model_fg);
                            else if (opaque)
                                put_expected(destination_x + x,
                                             destination_y + y, model_bg);
                        end
                        9: begin
                            source_address = source_base +
                                (source_y + y) * source_pitch +
                                (source_pixel_x >> 1);
                            source_byte = expected[source_address];
                            value = (source_pixel_x & 1) ?
                                    (source_byte & 15) : (source_byte >> 4);
                            if (value != 0 &&
                                destination_x + x >= model_clip_x0 &&
                                destination_x + x < model_clip_x1 &&
                                destination_y + y >= model_clip_y0 &&
                                destination_y + y < model_clip_y1) begin
                                destination_value = expected_pixel(
                                    destination_x + x, destination_y + y);
                                put_expected(destination_x + x,
                                    destination_y + y,
                                    model_blend565(model_fg,
                                                   destination_value, value));
                            end
                        end
                        10: begin
                            source_address = source_base +
                                (source_y + y) * source_pitch +
                                (source_pixel_x >> 1);
                            source_byte = expected[source_address];
                            value = (source_pixel_x & 1) ?
                                    (source_byte & 15) : (source_byte >> 4);
                            if (value != (transparent_index & 15)) begin
                                palette_address = palette_base + value * 2;
                                put_expected(destination_x + x,
                                    destination_y + y,
                                    {expected[palette_address],
                                     expected[palette_address + 1]});
                            end
                        end
                        default: begin
                            source_address = source_base +
                                (source_y + y) * source_pitch + source_pixel_x;
                            value = expected[source_address];
                            if (value != transparent_index) begin
                                palette_address = palette_base + value * 2;
                                put_expected(destination_x + x,
                                    destination_y + y,
                                    {expected[palette_address],
                                     expected[palette_address + 1]});
                            end
                        end
                    endcase
                end
            end
        end
    endtask

    task automatic command_glyph_single(
        input integer operation,
        input integer source_base,
        input integer source_pitch,
        input integer source_x,
        input integer source_y,
        input integer destination_x,
        input integer destination_y,
        input integer width,
        input integer height,
        input integer palette_base,
        input integer transparent_index,
        input integer opaque
    );
        reg [31:0] operation_word;
        begin
            write_reg(5'd13, source_base);
            write_reg(5'd14, source_pitch);
            write_reg(5'd15, {height[15:0], width[15:0]});
            write_reg(5'd16, palette_base);
            write_reg(5'd17, 32'd0);
            write_reg(5'd18, 32'd0);
            write_reg(5'd5, packed_xy(destination_x, destination_y));
            write_reg(5'd6, packed_xy(source_x, source_y));
            model_glyph(operation, source_base, source_pitch, source_x,
                        source_y, destination_x, destination_y, width, height,
                        palette_base, transparent_index, opaque);
            operation_word = operation | (opaque ? 32'h00000100 : 32'd0) |
                             ((transparent_index & 255) << 16);
            run_command(operation_word, 8'd0);
        end
    endtask

    integer flood_model_x [0:2047];
    integer flood_model_y [0:2047];
    task automatic model_flood(input integer seed_x, input integer seed_y,
                               input integer fill_value);
        integer count;
        integer x;
        integer y;
        integer target;
        begin
            target = expected_pixel(seed_x, seed_y);
            count = 1;
            flood_model_x[0] = seed_x;
            flood_model_y[0] = seed_y;
            while (count != 0) begin
                count = count - 1;
                x = flood_model_x[count];
                y = flood_model_y[count];
                if (x >= model_clip_x0 && x < model_clip_x1 &&
                    y >= model_clip_y0 && y < model_clip_y1 &&
                    expected_pixel(x, y) == target) begin
                    put_expected(x, y, fill_value);
                    if (count + 4 >= 2048)
                        $fatal(1, "software flood model queue overflow");
                    flood_model_x[count] = x - 1;
                    flood_model_y[count] = y;
                    flood_model_x[count + 1] = x + 1;
                    flood_model_y[count + 1] = y;
                    flood_model_x[count + 2] = x;
                    flood_model_y[count + 2] = y - 1;
                    flood_model_x[count + 3] = x;
                    flood_model_y[count + 3] = y + 1;
                    count = count + 4;
                end
            end
        end
    endtask

    task automatic command_flood(input integer seed_x, input integer seed_y,
                                 input integer work_base,
                                 input integer work_entries,
                                 input integer expected_error);
        begin
            write_reg(5'd5, packed_xy(seed_x, seed_y));
            write_reg(5'd17, work_base);
            write_reg(5'd18, work_entries);
            run_command(32'd12, expected_error[7:0]);
        end
    endtask

    task automatic compare_memory;
        integer index;
        integer mismatches;
        begin
            mismatches = 0;
            for (index = 0; index < MEM_BYTES; index = index + 1) begin
                if (memory[index] !== expected[index]) begin
                    if (mismatches < 16)
                        $display("MEM mismatch @%05x expected=%02x actual=%02x",
                                 index, expected[index], memory[index]);
                    mismatches = mismatches + 1;
                end
            end
            if (mismatches != 0)
                $fatal(1, "Astraea draw memory mismatches=%0d", mismatches);
        end
    endtask

    initial begin
        for (memory_index = 0; memory_index < MEM_BYTES;
             memory_index = memory_index + 1) begin
            memory[memory_index] = (memory_index * 37 + 8'h5d) & 8'hff;
            expected[memory_index] = memory[memory_index];
        end

        repeat (6) @(posedge cpu_clk);
        cpu_rst = 1'b0;
        mem_rst = 1'b0;
        repeat (6) @(posedge cpu_clk);

        // Command registers retain untouched bytes through the synchronous
        // BRAM read/modify/write path.
        write_reg(5'd9, 32'h11223344);
        write_reg_be(5'd9, 32'haabbccdd, 4'b0101);
        read_reg(5'd9, register_value);
        if (register_value !== 32'h11bb33dd)
            $fatal(1, "draw partial write expected=11bb33dd actual=%08x",
                   register_value);

        // START snapshots the active command. Writes made while it runs must
        // prepare the next command without changing the in-flight operation.
        configure_surface(16'h7001, 40, 0, 1, 1, 32, 20, 8'h35, 8'h00);
        write_reg(5'd5, packed_xy(2, 2));
        write_reg(5'd6, packed_xy(15, 15));
        model_rect(2, 2, 15, 15, 1);
        write_reg(5'd22, fence_value);
        write_reg(5'd19, 32'd2);
        write_reg(5'd20, 32'h00000001);
        if (!cpu_busy)
            $fatal(1, "draw command did not become busy after START");
        write_reg(5'd8, 32'h000000c7);
        write_reg(5'd5, packed_xy(18, 2));
        write_reg(5'd6, packed_xy(28, 10));
        wait_command(8'd0, fence_value);
        fence_value = fence_value + 1;
        model_fg = 8'hc7;
        model_rect(18, 2, 28, 10, 1);
        run_command(32'd2, 8'd0);

        configure_surface(16'h0101, 29, 0, 2, 2, 22, 18, 8'h5a, 8'hc3);
        command_line(-4, 4, 18, 8);
        command_line(18, 8, -4, 4);
        command_line(4, -5, 8, 17);
        command_line(8, 17, 4, -5);
        command_line(3, 16, 19, 3);
        command_line(19, 3, 3, 16);
        command_line(3, 3, 19, 16);
        command_line(19, 16, 3, 3);
        command_rect(-2, 1, 10, 7, 0);
        command_rect(14, 4, 25, 12, 1);
        command_circle(7, 8, 4, 0);
        command_circle(17, 13, 3, 1);
        command_circle(3, 3, 0, 0);
        command_ellipse(12, 10, 6, 3, 0);
        command_ellipse(6, 14, 4, 2, 1);
        command_ellipse(4, 9, 0, 3, 0);
        command_ellipse(12, 4, 5, 0, 1);
        command_pattern(0, 0, 24, 20, -3, 5,
                        64'h8040201008040201, 0);
        command_pattern(4, 5, 11, 12, 2, -1,
                        64'haa55aa55aa55aa55, 1);

        configure_surface(16'h3003, 47, 1, 1, 1, 19, 14,
                          16'habcd, 16'h1234);
        command_line(-3, 2, 18, 12);
        command_rect(3, 3, 15, 9, 1);
        command_circle(16, 5, 4, 0);
        command_ellipse(9, 8, 7, 4, 1);
        command_pattern(0, 0, 20, 15, 1, 2,
                        64'hf0f00f0faa55cc33, 1);

        // Packed monochrome glyph with a non-byte-aligned source origin.
        set_memory_byte(16'h4000, 8'hd2);
        set_memory_byte(16'h4001, 8'h6d);
        set_memory_byte(16'h4002, 8'ha5);
        set_memory_byte(16'h4003, 8'h3c);
        set_memory_byte(16'h4004, 8'h81);
        set_memory_byte(16'h4005, 8'h7e);
        set_memory_byte(16'h4006, 8'h55);
        set_memory_byte(16'h4007, 8'haa);
        set_memory_byte(16'h4008, 8'he7);
        configure_surface(16'h1003, 31, 0, 2, 2, 24, 17, 8'he1, 8'h19);
        command_glyph_single(8, 16'h4000, 3, 3, 1, -1, 5, 10, 3,
                             0, 0, 1);

        // A4 coverage uses the exact native-channel RGB565 blend formula.
        set_memory_byte(16'h4100, 8'h0f);
        set_memory_byte(16'h4101, 8'h37);
        set_memory_byte(16'h4102, 8'h8c);
        set_memory_byte(16'h4103, 8'hf1);
        set_memory_byte(16'h4104, 8'h5a);
        set_memory_byte(16'h4105, 8'he0);
        set_memory_byte(16'h4106, 8'h24);
        set_memory_byte(16'h4107, 8'h9d);
        configure_surface(16'h1801, 45, 1, 1, 1, 20, 12,
                          16'hf81f, 16'h0000);
        command_glyph_single(9, 16'h4100, 4, 1, 0, 3, 3, 7, 2,
                             0, 0, 0);

        // INDEX4 color glyph and unaligned RGB565 palette.
        for (memory_index = 0; memory_index < 16; memory_index = memory_index + 1) begin
            set_memory_byte(16'h4801 + memory_index * 2,
                            (16'h0421 * memory_index) >> 8);
            set_memory_byte(16'h4802 + memory_index * 2,
                            16'h0421 * memory_index);
        end
        set_memory_byte(16'h4200, 8'h12);
        set_memory_byte(16'h4201, 8'h34);
        set_memory_byte(16'h4202, 8'h56);
        set_memory_byte(16'h4203, 8'h78);
        set_memory_byte(16'h4204, 8'h9a);
        set_memory_byte(16'h4205, 8'hbc);
        set_memory_byte(16'h4206, 8'hde);
        set_memory_byte(16'h4207, 8'hf0);
        command_glyph_single(10, 16'h4200, 4, 1, 0, 5, 6, 7, 2,
                             16'h4801, 3, 0);

        // INDEX8 batched descriptors share one cached 256-entry palette.
        for (memory_index = 0; memory_index < 256; memory_index = memory_index + 1) begin
            set_memory_byte(16'h4a01 + memory_index * 2,
                            (16'h0103 * memory_index) >> 8);
            set_memory_byte(16'h4a02 + memory_index * 2,
                            16'h0103 * memory_index);
        end
        for (memory_index = 0; memory_index < 128; memory_index = memory_index + 1)
            set_memory_byte(16'h4300 + memory_index,
                            (memory_index * 13 + 7) & 8'hff);
        set_memory_long(16'h5201, 32'h00000000);
        set_memory_long(16'h5205, packed_xy(1, 0));
        set_memory_long(16'h5209, packed_xy(3, 2));
        set_memory_long(16'h520d, {16'd3, 16'd6});
        set_memory_long(16'h5211, 32'h00000040);
        set_memory_long(16'h5215, packed_xy(2, 1));
        set_memory_long(16'h5219, packed_xy(11, 5));
        set_memory_long(16'h521d, {16'd2, 16'd5});
        write_reg(5'd13, 16'h4300);
        write_reg(5'd14, 16);
        write_reg(5'd15, 32'd0);
        write_reg(5'd16, 16'h4a01);
        write_reg(5'd17, 16'h5201);
        write_reg(5'd18, 2);
        model_glyph(11, 16'h4300, 16, 1, 0, 3, 2, 6, 3,
                    16'h4a01, 8'h7f, 0);
        model_glyph(11, 16'h4340, 16, 2, 1, 11, 5, 5, 2,
                    16'h4a01, 8'h7f, 0);
        run_command(32'd11 | (32'h7f << 16), 8'd0);

        // Irregular INDEX8 component split by an obstacle wall.
        configure_surface(16'h2003, 24, 0, 1, 1, 15, 11, 8'h77, 8'd0);
        for (setup_y = 1; setup_y < 11; setup_y = setup_y + 1) begin
            for (setup_x = 1; setup_x < 15; setup_x = setup_x + 1) begin
                setup_address = 16'h2003 + setup_y * 24 + setup_x;
                set_memory_byte(setup_address, setup_x == 8 ? 8'h20 : 8'h10);
            end
        end
        set_memory_byte(16'h2003 + 4 * 24 + 4, 8'h20);
        set_memory_byte(16'h2003 + 5 * 24 + 4, 8'h20);
        set_memory_byte(16'h2003 + 5 * 24 + 5, 8'h20);
        model_flood(3, 3, 8'h77);
        command_flood(3, 3, 16'h6001, 128, 0);
        for (memory_index = 0; memory_index < 128 * 4;
             memory_index = memory_index + 1)
            expected[16'h6001 + memory_index] = memory[16'h6001 + memory_index];

        // The same flood machinery must preserve unaligned RGB565 neighbors.
        configure_surface(16'h2801, 34, 1, 1, 1, 9, 8, 16'hf800, 16'd0);
        for (setup_y = 1; setup_y < 8; setup_y = setup_y + 1) begin
            for (setup_x = 1; setup_x < 9; setup_x = setup_x + 1) begin
                setup_address = 16'h2801 + setup_y * 34 + setup_x * 2;
                if (setup_x == 5) begin
                    set_memory_byte(setup_address, 8'h00);
                    set_memory_byte(setup_address + 1, 8'h1f);
                end else begin
                    set_memory_byte(setup_address, 8'h07);
                    set_memory_byte(setup_address + 1, 8'he0);
                end
            end
        end
        model_flood(2, 2, 16'hf800);
        command_flood(2, 2, 16'h6501, 64, 0);
        for (memory_index = 0; memory_index < 64 * 4;
             memory_index = memory_index + 1)
            expected[16'h6501 + memory_index] = memory[16'h6501 + memory_index];

        // Queue exhaustion is reported and cannot touch its guard bytes.
        configure_surface(16'h2401, 20, 0, 1, 1, 10, 8, 8'h66, 8'd0);
        for (setup_y = 1; setup_y < 8; setup_y = setup_y + 1)
            for (setup_x = 1; setup_x < 10; setup_x = setup_x + 1)
                set_memory_byte(16'h2401 + setup_y * 20 + setup_x, 8'h11);
        command_flood(3, 3, 16'h6301, 1, 3);
        for (setup_y = 1; setup_y < 8; setup_y = setup_y + 1)
            for (setup_x = 1; setup_x < 10; setup_x = setup_x + 1) begin
                setup_address = 16'h2401 + setup_y * 20 + setup_x;
                expected[setup_address] = memory[setup_address];
            end
        for (memory_index = 0; memory_index < 4; memory_index = memory_index + 1)
            expected[16'h6301 + memory_index] = memory[16'h6301 + memory_index];

        // Unsupported formats and oversized radii must complete with an error
        // and leave every destination/guard byte untouched.
        write_reg(5'd2, 32'd2);
        write_reg(5'd5, packed_xy(4, 4));
        write_reg(5'd6, packed_xy(8, 8));
        run_command(32'd0, 8'd1);
        write_reg(5'd2, 32'd1);
        write_reg(5'd7, 32'h00008000);
        run_command(32'd3, 8'd1);

        // Configuration values are validated before narrowing to the native
        // 25-bit address and 16-bit pitch buses.
        write_reg(5'd0, 32'h80002000);
        write_reg(5'd1, 32'd20);
        run_command(32'd0, 8'd1);
        write_reg(5'd0, 32'h00002401);
        write_reg(5'd1, 32'h00010014);
        run_command(32'd0, 8'd1);

        write_reg(5'd1, 32'd20);
        write_reg(5'd5, packed_xy(2, 2));
        write_reg(5'd6, packed_xy(0, 0));
        write_reg(5'd13, 32'h00004200);
        write_reg(5'd14, 32'h00010004);
        write_reg(5'd15, {16'd1, 16'd1});
        write_reg(5'd17, 32'd0);
        write_reg(5'd18, 32'd0);
        run_command(32'd8, 8'd1);

        compare_memory();
        if (cpu_irq)
            $fatal(1, "draw IRQ asserted while disabled");
        if (cache_flush || cpu_busy || mem_lock)
            $fatal(1, "draw engine did not return idle");
        $display("ASTRAEA DRAW PASS fences=%0d", fence_value - 1);
        $finish;
    end
endmodule

`default_nettype wire
