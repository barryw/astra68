`timescale 1ns/1ps
`default_nettype none
`include "astra_render_protocol.vh"

module tb_astra_render_geometry;
    localparam integer WIDTH = 48;
    localparam integer HEIGHT = 36;
    localparam integer BASE = 32'h1000;
    reg clk = 0;
    always #2.5 clk = ~clk;
    reg reset = 1;
    reg start = 0;
    reg abort = 0;
    reg [15:0] opcode;
    reg [15:0] flags;
    reg signed [15:0] clip_left, clip_top, clip_right, clip_bottom;
    reg signed [15:0] p0_x, p0_y, p1_x, p1_y;
    reg [15:0] radius_x, radius_y;
    reg signed [15:0] origin_x, origin_y;
    reg [63:0] pattern;
    reg [31:0] foreground, background;
    wire busy, done;
    wire [15:0] status;
    wire [31:0] completed_pixels;
    wire writer_start, writer_abort, writer_flush;
    reg writer_done = 0;
    wire pixel_valid;
    reg pixel_ready = 0;
    wire [31:0] pixel_address;
    wire [7:0] pixel_format;
    wire [31:0] pixel_value;
    reg [15:0] actual [0:WIDTH*HEIGHT-1];
    reg [15:0] expected [0:WIDTH*HEIGHT-1];
    reg [15:0] lfsr = 16'h1;
    integer writes;
    integer i;

    astra_render_geometry dut (
        .clk(clk), .reset(reset), .start(start), .abort(abort),
        .opcode(opcode), .command_flags(flags),
        .clip_left(clip_left), .clip_top(clip_top),
        .clip_right(clip_right), .clip_bottom(clip_bottom),
        .p0_x(p0_x), .p0_y(p0_y), .p1_x(p1_x), .p1_y(p1_y),
        .radius_x(radius_x), .radius_y(radius_y),
        .pattern_origin_x(origin_x), .pattern_origin_y(origin_y),
        .pattern(pattern), .foreground(foreground), .background(background),
        .arena_base(BASE), .destination_data_offset(0),
        .destination_pitch(WIDTH*2),
        .destination_format(8'd1),
        .destination_bytes_per_pixel(3'd2),
        .busy(busy), .done(done), .status(status), .fault_detail(),
        .completed_pixels(completed_pixels),
        .writer_start(writer_start), .writer_abort(writer_abort),
        .writer_flush(writer_flush), .writer_flush_ready(1'b1),
        .writer_done(writer_done), .writer_aborted(1'b0),
        .writer_error(1'b0), .writer_fault_detail(0),
        .pixel_valid(pixel_valid), .pixel_ready(pixel_ready),
        .pixel_address(pixel_address), .pixel_format(pixel_format),
        .pixel_value(pixel_value)
    );

    always @(posedge clk) begin
        lfsr <= {lfsr[14:0], lfsr[15] ^ lfsr[13] ^ lfsr[12] ^ lfsr[10]};
        pixel_ready <= lfsr[0] | lfsr[3];
        writer_done <= writer_flush;
        if (pixel_valid && pixel_ready) begin
            if (pixel_address < BASE || pixel_address >= BASE + WIDTH*HEIGHT*2)
                $fatal(1, "out-of-range pixel address %08x", pixel_address);
            if (pixel_address[0])
                $fatal(1, "unaligned RGB565 pixel %08x", pixel_address);
            actual[(pixel_address-BASE)>>1] <= pixel_value[15:0];
            writes <= writes + 1;
        end
    end

    task automatic clear_buffers;
        begin
            for (i = 0; i < WIDTH*HEIGHT; i = i + 1) begin
                actual[i] = 16'h1234;
                expected[i] = 16'h1234;
            end
            writes = 0;
        end
    endtask

    task automatic model_pixel(input integer x, input integer y,
                                input [15:0] color);
        begin
            if (x >= clip_left && x < clip_right &&
                y >= clip_top && y < clip_bottom)
                expected[y*WIDTH+x] = color;
        end
    endtask

    task automatic model_line(input integer x0, input integer y0,
                              input integer x1, input integer y1,
                              input [15:0] color);
        integer dx, sx, dy, sy, error, e2;
        begin
            dx = x0 < x1 ? x1-x0 : x0-x1;
            sx = x0 < x1 ? 1 : -1;
            dy = -(y0 < y1 ? y1-y0 : y0-y1);
            sy = y0 < y1 ? 1 : -1;
            error = dx + dy;
            begin : line_loop
                forever begin
                    model_pixel(x0, y0, color);
                    if (x0 == x1 && y0 == y1)
                        disable line_loop;
                    e2 = 2*error;
                    if (e2 >= dy) begin error = error+dy; x0 = x0+sx; end
                    if (e2 <= dx) begin error = error+dx; y0 = y0+sy; end
                end
            end
        end
    endtask

    task automatic model_rect(input integer x0, input integer y0,
                              input integer x1, input integer y1,
                              input bit filled, input [15:0] color);
        integer x, y, t;
        begin
            if (x0 > x1) begin t=x0; x0=x1; x1=t; end
            if (y0 > y1) begin t=y0; y0=y1; y1=t; end
            if (filled)
                for (y=y0; y<=y1; y=y+1)
                    for (x=x0; x<=x1; x=x+1) model_pixel(x,y,color);
            else begin
                model_line(x0,y0,x1,y0,color); model_line(x1,y0,x1,y1,color);
                model_line(x1,y1,x0,y1,color); model_line(x0,y1,x0,y0,color);
            end
        end
    endtask

    task automatic model_pattern(input integer x0, input integer y0,
                                 input integer x1, input integer y1,
                                 input integer ox, input integer oy,
                                 input [63:0] bits, input bit opaque);
        integer x, y, t, index;
        begin
            if (x0 > x1) begin t=x0; x0=x1; x1=t; end
            if (y0 > y1) begin t=y0; y0=y1; y1=t; end
            for (y=y0; y<=y1; y=y+1) for (x=x0; x<=x1; x=x+1) begin
                index = (((y-oy)&7)*8)+((x-ox)&7);
                if (bits[63-index]) model_pixel(x,y,foreground[15:0]);
                else if (opaque) model_pixel(x,y,background[15:0]);
            end
        end
    endtask

    task automatic model_circle(input integer cx, input integer cy,
                                input integer radius, input bit filled,
                                input [15:0] color);
        integer x, y, err, slot, xx0, xx1, yy, px, py;
        begin
            x=radius; y=0; err=1-radius;
            begin : circle_loop
                forever begin
                    if (filled) begin
                        for (slot=0; slot<4; slot=slot+1) begin
                            case(slot)
                              0: begin xx0=cx-x; xx1=cx+x; yy=cy+y; end
                              1: begin xx0=cx-x; xx1=cx+x; yy=cy-y; end
                              2: begin xx0=cx-y; xx1=cx+y; yy=cy+x; end
                              default: begin xx0=cx-y; xx1=cx+y; yy=cy-x; end
                            endcase
                            for (px=xx0; px<=xx1; px=px+1) model_pixel(px,yy,color);
                        end
                    end else for (slot=0; slot<8; slot=slot+1) begin
                        case(slot)
                          0: begin px=cx+x; py=cy+y; end
                          1: begin px=cx+y; py=cy+x; end
                          2: begin px=cx-y; py=cy+x; end
                          3: begin px=cx-x; py=cy+y; end
                          4: begin px=cx-x; py=cy-y; end
                          5: begin px=cx-y; py=cy-x; end
                          6: begin px=cx+y; py=cy-x; end
                          default: begin px=cx+x; py=cy-y; end
                        endcase
                        model_pixel(px,py,color);
                    end
                    if (x < y) disable circle_loop;
                    y=y+1; err=err+1+2*y;
                    if ((((err-x)*2)+1)>0) begin err=err+1-2*x; x=x-1; end
                end
            end
        end
    endtask

    task automatic model_ellipse(input integer cx, input integer cy,
                                 input integer rx, input integer ry,
                                 input bit filled, input [15:0] color);
        integer dy, x, extent;
        reg [63:0] lhs, rhs;
        begin
            rhs = rx*rx*ry*ry;
            for (dy=-ry; dy<=ry; dy=dy+1) begin
                extent=0;
                for (x=0; x<=rx; x=x+1) begin
                    lhs = x*x*ry*ry + dy*dy*rx*rx;
                    if (lhs <= rhs) extent=x;
                end
                if (filled)
                    for (x=-extent; x<=extent; x=x+1)
                        model_pixel(cx+x,cy+dy,color);
                else begin
                    model_pixel(cx-extent,cy+dy,color);
                    model_pixel(cx+extent,cy+dy,color);
                end
            end
        end
    endtask

    task automatic launch;
        begin
            @(posedge clk); start <= 1;
            @(posedge clk); start <= 0;
            wait(done); @(posedge clk);
            if (status != `ASTRA_RENDER_STATUS_OK)
                $fatal(1, "geometry status %0d", status);
        end
    endtask

    task automatic compare(input [255:0] name);
        begin
            for (i=0; i<WIDTH*HEIGHT; i=i+1)
                if (actual[i] !== expected[i])
                    $fatal(1, "%0s mismatch x=%0d y=%0d actual=%04x expected=%04x",
                           name, i%WIDTH, i/WIDTH, actual[i], expected[i]);
        end
    endtask

    initial begin
        clip_left=2; clip_top=2; clip_right=46; clip_bottom=34;
        foreground=16'hf81f; background=16'h07e0; radius_x=0; radius_y=0;
        origin_x=0; origin_y=0; pattern=64'h0; flags=0;
        repeat(5) @(posedge clk); reset=0;

        clear_buffers(); opcode=`ASTRA_RENDER_OP_LINE;
        p0_x=-3; p0_y=4; p1_x=30; p1_y=17; model_line(-3,4,30,17,foreground);
        launch(); compare("line octant 0 clipped");
        clear_buffers(); p0_x=40; p0_y=30; p1_x=8; p1_y=3;
        model_line(40,30,8,3,foreground); launch(); compare("line octant 5");
        clear_buffers(); p0_x=20; p0_y=32; p1_x=23; p1_y=4;
        model_line(20,32,23,4,foreground); launch(); compare("line steep");

        clear_buffers(); opcode=`ASTRA_RENDER_OP_RECT; flags=0;
        p0_x=35; p0_y=27; p1_x=5; p1_y=6;
        model_rect(35,27,5,6,0,foreground); launch(); compare("rectangle outline");
        clear_buffers(); flags=`ASTRA_RENDER_GEOMETRY_FLAG_FILLED;
        p0_x=-2; p0_y=28; p1_x=12; p1_y=36;
        model_rect(-2,28,12,36,1,foreground); launch(); compare("rectangle fill clip");

        clear_buffers(); opcode=`ASTRA_RENDER_OP_PATTERN_FILL;
        flags=`ASTRA_RENDER_GEOMETRY_FLAG_PATTERN_OPAQUE;
        p0_x=3; p0_y=3; p1_x=27; p1_y=16; origin_x=-3; origin_y=5;
        pattern=64'h8040201008040201;
        model_pattern(3,3,27,16,-3,5,pattern,1); launch(); compare("opaque pattern");
        clear_buffers(); flags=0; origin_x=7; origin_y=-4;
        model_pattern(3,3,27,16,7,-4,pattern,0); launch(); compare("transparent pattern");

        clear_buffers(); opcode=`ASTRA_RENDER_OP_CIRCLE; flags=0;
        p0_x=24; p0_y=18; radius_x=9;
        model_circle(24,18,9,0,foreground); launch(); compare("circle outline");
        clear_buffers(); flags=`ASTRA_RENDER_GEOMETRY_FLAG_FILLED; radius_x=6;
        model_circle(24,18,6,1,foreground); launch(); compare("circle fill");

        clear_buffers(); opcode=`ASTRA_RENDER_OP_ELLIPSE; flags=0;
        p0_x=24; p0_y=18; radius_x=13; radius_y=6;
        model_ellipse(24,18,13,6,0,foreground); launch(); compare("ellipse outline");
        clear_buffers(); flags=`ASTRA_RENDER_GEOMETRY_FLAG_FILLED;
        radius_x=8; radius_y=3;
        model_ellipse(24,18,8,3,1,foreground); launch(); compare("ellipse fill");
        clear_buffers(); flags=0; radius_x=0; radius_y=0;
        model_ellipse(24,18,0,0,0,foreground); launch(); compare("ellipse point");

        $display("ASTRA RENDER GEOMETRY PASS writes=%0d", writes);
        $finish;
    end
endmodule
`default_nettype wire
