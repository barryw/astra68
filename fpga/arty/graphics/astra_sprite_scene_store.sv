// Copyright (c) 2026 Astra68 contributors
//
// Validated sprite metadata and palettes. Two software-visible metadata banks
// implement immutable PENDING and writable EDITABLE generations; dedicated
// active descriptor registers and four active palette replicas implement the
// scanout-facing ACTIVE generation. Promotion is therefore atomic even while
// software starts editing the next generation.
`timescale 1ns/1ps
`default_nettype none

module astra_sprite_scene_store #(
    parameter [31:0] ARENA_BASE = 32'h18000000,
    parameter [31:0] ARENA_LIMIT = 32'h20000000
) (
    input  wire        clk,
    input  wire        reset,

    input  wire        descriptor_write_enable,
    input  wire [5:0]  descriptor_write_index,
    input  wire [2:0]  descriptor_write_word,
    input  wire [31:0] descriptor_write_data,
    input  wire        palette_write_enable,
    input  wire [3:0]  palette_write_bank,
    input  wire [7:0]  palette_write_index,
    input  wire [31:0] palette_write_argb,
    output wire        write_ready,

    input  wire        validate_start,
    output reg         validate_busy,
    output reg         validate_done,
    output reg         validate_valid,
    input  wire        accept_pending,
    output wire        pending_ready,
    output wire        pending_valid,

    input  wire        activate_start,
    output reg         activate_busy,
    output reg         activate_done,

    input  wire        baseline_restore_start,
    output wire        baseline_restore_busy,
    output reg         baseline_restore_done,
    input  wire        copper_palette_write_enable,
    input  wire [3:0]  copper_palette_write_bank,
    input  wire [7:0]  copper_palette_write_index,
    input  wire [31:0] copper_palette_write_argb,
    output wire        copper_palette_write_ready,

    input  wire        order_read_enable,
    input  wire [5:0]  order_read_position,
    output reg  [5:0]  order_read_index,

    input  wire        descriptor_read_enable,
    input  wire [5:0]  descriptor_read_index,
    output reg  [31:0] descriptor_word0,
    output reg  [31:0] descriptor_word1,
    output reg  [31:0] descriptor_word2,
    output reg  [31:0] descriptor_word3,
    output reg  [31:0] descriptor_word4,
    output reg  [31:0] descriptor_word5,
    output reg  [31:0] descriptor_word6,
    output reg  [31:0] descriptor_scale_step_x,
    output reg  [63:0] descriptor_collision_compatible,

    input  wire [3:0]  palette0_read_bank,
    input  wire [7:0]  palette0_read_index,
    output wire [31:0] palette0_read_argb,
    input  wire [3:0]  palette1_read_bank,
    input  wire [7:0]  palette1_read_index,
    output wire [31:0] palette1_read_argb,
    input  wire [3:0]  palette2_read_bank,
    input  wire [7:0]  palette2_read_index,
    output wire [31:0] palette2_read_argb,
    input  wire [3:0]  palette3_read_bank,
    input  wire [7:0]  palette3_read_index,
    output wire [31:0] palette3_read_argb
);
    localparam [4:0] V_IDLE = 5'd0;
    localparam [4:0] V_READ = 5'd1;
    localparam [4:0] V_CAPTURE = 5'd2;
    localparam [4:0] V_CHECK = 5'd3;
    localparam [4:0] V_DIVIDE = 5'd4;
    localparam [4:0] V_SORT = 5'd5;
    localparam [4:0] V_NEXT = 5'd6;
    localparam [4:0] V_ADDRESS = 5'd7;
    localparam [4:0] V_GEOMETRY = 5'd8;
    localparam [4:0] V_END_ADDRESS = 5'd9;
    localparam [4:0] V_SORT_INDEX = 5'd10;
    localparam [4:0] V_SORT_PRIORITY = 5'd11;
    localparam [4:0] V_DIVIDE_WRITE = 5'd12;
    localparam [4:0] V_DIVIDE_SHIFT = 5'd13;
    localparam [4:0] V_CHECK_RESULT = 5'd14;
    localparam [4:0] V_MULTIPLY = 5'd15;
    localparam [4:0] V_DIVIDE_ROUND = 5'd16;
    localparam [4:0] V_READ_PIPE = 5'd17;

    localparam [1:0] METADATA_VALIDATION = 2'd0;
    localparam [1:0] METADATA_CLONE = 2'd1;
    localparam [1:0] METADATA_ACTIVATION = 2'd2;

    localparam [2:0] A_IDLE = 3'd0;
    localparam [2:0] A_DESCRIPTORS = 3'd1;
    localparam [2:0] A_PALETTE = 3'd2;
    localparam [2:0] A_COMPATIBILITY = 3'd3;
    localparam [2:0] A_PALETTE_DRAIN = 3'd4;
    localparam [2:0] A_DESCRIPTOR_DRAIN = 3'd5;
    localparam [2:0] A_COMPATIBILITY_I = 3'd6;
    localparam [2:0] A_COMPATIBILITY_DRAIN = 3'd7;

    localparam [2:0] C_IDLE = 3'd0;
    localparam [2:0] C_DESCRIPTORS = 3'd1;
    localparam [2:0] C_DESCRIPTOR_DRAIN = 3'd2;
    localparam [2:0] C_PALETTE = 3'd3;
    localparam [2:0] C_PALETTE_DRAIN = 3'd4;

    localparam [1:0] R_IDLE = 2'd0;
    localparam [1:0] R_READ = 2'd1;
    localparam [1:0] R_WRITE = 2'd2;

    (* ram_style = "distributed" *) reg [31:0] scale_step0 [0:63];
    (* ram_style = "distributed" *) reg [31:0] scale_step1 [0:63];
    (* ram_style = "distributed" *) reg [7:0] priority0 [0:63];
    (* ram_style = "distributed" *) reg [7:0] priority1 [0:63];
    (* ram_style = "distributed" *) reg [5:0] order0 [0:63];
    (* ram_style = "distributed" *) reg [5:0] order1 [0:63];

    (* ram_style = "distributed" *) reg [31:0] active_word0 [0:63];
    (* ram_style = "distributed" *) reg [31:0] active_word1 [0:63];
    (* ram_style = "distributed" *) reg [31:0] active_word2 [0:63];
    (* ram_style = "distributed" *) reg [31:0] active_word3 [0:63];
    (* ram_style = "distributed" *) reg [31:0] active_word4 [0:63];
    (* ram_style = "distributed" *) reg [31:0] active_word5 [0:63];
    (* ram_style = "distributed" *) reg [31:0] active_word6 [0:63];
    (* ram_style = "distributed" *) reg [31:0] active_scale_step [0:63];
    (* ram_style = "distributed" *) reg [5:0] active_order [0:63];
    (* ram_style = "distributed" *) reg [63:0] active_compatible [0:63];

    integer initialize_index;
    initial begin
        for (initialize_index = 0; initialize_index < 64;
             initialize_index = initialize_index + 1) begin
            scale_step0[initialize_index] = 32'd0;
            scale_step1[initialize_index] = 32'd0;
            priority0[initialize_index] = 8'd0;
            priority1[initialize_index] = 8'd0;
            order0[initialize_index] = initialize_index[5:0];
            order1[initialize_index] = initialize_index[5:0];
            active_word0[initialize_index] = 32'd0;
            active_word1[initialize_index] = 32'd0;
            active_word2[initialize_index] = 32'd0;
            active_word3[initialize_index] = 32'd0;
            active_word4[initialize_index] = 32'd0;
            active_word5[initialize_index] = 32'd0;
            active_word6[initialize_index] = 32'd0;
            active_scale_step[initialize_index] = 32'd0;
            active_order[initialize_index] = initialize_index[5:0];
            active_compatible[initialize_index] = 64'd0;
        end
    end

    reg editable_bank_q;
    reg pending_bank_q;
    reg pending_valid_q;
    reg [2:0] clone_state;
    reg [5:0] clone_descriptor_index;
    reg clone_descriptor_read_valid_q;
    reg [5:0] clone_descriptor_write_index_q;
    reg clone_descriptor_transfer_valid_q;
    reg [5:0] clone_descriptor_transfer_index_q;
    reg [31:0] clone_descriptor_transfer_bank0_data_q [0:7];
    reg [31:0] clone_descriptor_transfer_bank1_data_q [0:7];
    reg [11:0] clone_palette_index;
    reg clone_palette_read_valid_q;
    reg [11:0] clone_palette_write_index_q;
    reg clone_palette_transfer_valid_q;
    reg [11:0] clone_palette_transfer_index_q;
    reg [31:0] clone_palette_transfer_data_q;

    reg descriptor_host_write_valid_q;
    reg descriptor_host_write_bank_q;
    reg [5:0] descriptor_host_write_index_q;
    reg [2:0] descriptor_host_write_word_q;
    reg [31:0] descriptor_host_write_data_q;
    reg palette_host_write_valid_q;
    reg palette_host_write_bank_q;
    reg [11:0] palette_host_write_address_q;
    reg [31:0] palette_host_write_data_q;

    reg [2:0] activation_state;
    reg [5:0] activation_descriptor_index;
    reg activation_descriptor_read_valid_q;
    reg [5:0] activation_descriptor_write_index_q;
    reg activation_descriptor_transfer_valid_q;
    reg [5:0] activation_descriptor_transfer_index_q;
    reg [31:0] activation_descriptor_transfer_bank0_data_q [0:7];
    reg [31:0] activation_descriptor_transfer_bank1_data_q [0:7];
    reg [31:0] activation_descriptor_transfer_scale_step_q;
    reg [5:0] activation_descriptor_transfer_order_q;
    reg [11:0] activation_palette_index;
    reg activation_palette_read_valid_q;
    reg [11:0] activation_palette_write_index_q;
    reg activation_palette_transfer_valid_q;
    reg [11:0] activation_palette_transfer_index_q;
    reg [31:0] activation_palette_transfer_data_q;
    reg [1:0] palette_restore_state;
    reg palette_restore_pending_q;
    reg [11:0] palette_restore_index;
    wire [31:0] palette_baseline_read_data;

    reg [5:0] validation_descriptor_index;
    reg [1:0] metadata_read_owner_q;

    assign write_ready = clone_state == C_IDLE &&
                         !clone_descriptor_transfer_valid_q &&
                         !clone_palette_transfer_valid_q &&
                         !validate_busy && !activate_busy;
    assign pending_ready = pending_valid_q && clone_state == C_IDLE &&
                           !clone_descriptor_transfer_valid_q &&
                           !clone_palette_transfer_valid_q &&
                           !validate_busy && !activate_busy;
    assign pending_valid = pending_valid_q;

    wire [11:0] palette_write_address = {
        palette_write_bank, palette_write_index
    };

    wire clone_descriptor_write_enable = clone_descriptor_transfer_valid_q;
    wire activation_descriptor_write_enable =
        activation_descriptor_transfer_valid_q;
    wire [5:0] metadata_descriptor_read_address =
        metadata_read_owner_q == METADATA_CLONE ? clone_descriptor_index :
        metadata_read_owner_q == METADATA_ACTIVATION ?
            activation_descriptor_index : validation_descriptor_index;

    wire [31:0] descriptor_bank0_read_data [0:7];
    wire [31:0] descriptor_bank1_read_data [0:7];
    reg  [31:0] validation_bank_read_data_q [0:7];
    wire [31:0] clone_descriptor_transfer_data [0:7];
    wire [31:0] activation_descriptor_transfer_data [0:7];

    genvar descriptor_ram_word;
    generate
        for (descriptor_ram_word = 0; descriptor_ram_word < 8;
             descriptor_ram_word = descriptor_ram_word + 1) begin :
                descriptor_metadata_ram
            wire bank0_host_write_enable = descriptor_host_write_valid_q &&
                !descriptor_host_write_bank_q &&
                descriptor_host_write_word_q == descriptor_ram_word;
            wire bank1_host_write_enable = descriptor_host_write_valid_q &&
                descriptor_host_write_bank_q &&
                descriptor_host_write_word_q == descriptor_ram_word;
            wire bank0_clone_write_enable =
                clone_descriptor_write_enable && pending_bank_q;
            wire bank1_clone_write_enable =
                clone_descriptor_write_enable && !pending_bank_q;

            assign clone_descriptor_transfer_data[descriptor_ram_word] =
                pending_bank_q ?
                    clone_descriptor_transfer_bank1_data_q[
                        descriptor_ram_word] :
                    clone_descriptor_transfer_bank0_data_q[
                        descriptor_ram_word];
            assign activation_descriptor_transfer_data[
                descriptor_ram_word] = pending_bank_q ?
                    activation_descriptor_transfer_bank1_data_q[
                        descriptor_ram_word] :
                    activation_descriptor_transfer_bank0_data_q[
                        descriptor_ram_word];

            astra_sprite_descriptor_ram descriptor_bank0_i (
                .clk(clk),
                .write_enable(bank0_host_write_enable ||
                              bank0_clone_write_enable),
                .write_address(clone_descriptor_write_enable ?
                    clone_descriptor_transfer_index_q :
                    descriptor_host_write_index_q),
                .write_data(clone_descriptor_write_enable ?
                    clone_descriptor_transfer_data[descriptor_ram_word] :
                    descriptor_host_write_data_q),
                .read_address(metadata_descriptor_read_address),
                .read_data(descriptor_bank0_read_data[descriptor_ram_word])
            );

            astra_sprite_descriptor_ram descriptor_bank1_i (
                .clk(clk),
                .write_enable(bank1_host_write_enable ||
                              bank1_clone_write_enable),
                .write_address(clone_descriptor_write_enable ?
                    clone_descriptor_transfer_index_q :
                    descriptor_host_write_index_q),
                .write_data(clone_descriptor_write_enable ?
                    clone_descriptor_transfer_data[descriptor_ram_word] :
                    descriptor_host_write_data_q),
                .read_address(metadata_descriptor_read_address),
                .read_data(descriptor_bank1_read_data[descriptor_ram_word])
            );
        end
    endgenerate

    wire clone_palette_write_enable = clone_palette_transfer_valid_q;
    wire activation_palette_write_enable =
        activation_palette_transfer_valid_q;
    wire palette_restore_write_enable =
        palette_restore_state == R_WRITE;
    wire [11:0] copper_palette_write_address = {
        copper_palette_write_bank, copper_palette_write_index
    };
    assign baseline_restore_busy = palette_restore_pending_q ||
        palette_restore_state != R_IDLE;
    assign copper_palette_write_ready = !baseline_restore_busy &&
        !activate_busy && !activate_start;
    wire active_palette_write_enable = activation_palette_write_enable ||
        palette_restore_write_enable ||
        (copper_palette_write_enable && copper_palette_write_ready);
    wire [11:0] active_palette_write_address =
        activation_palette_write_enable ?
            activation_palette_transfer_index_q :
        palette_restore_write_enable ? palette_restore_index :
            copper_palette_write_address;
    wire [31:0] active_palette_write_data =
        activation_palette_write_enable ?
            activation_palette_transfer_data_q :
        palette_restore_write_enable ? palette_baseline_read_data :
            copper_palette_write_argb;
    wire [11:0] metadata_palette_read_address =
        metadata_read_owner_q == METADATA_CLONE ? clone_palette_index :
                                                  activation_palette_index;

    wire [31:0] palette_bank0_read_data;
    wire [31:0] palette_bank1_read_data;
    wire [31:0] pending_palette_read_data = pending_bank_q ?
        palette_bank1_read_data : palette_bank0_read_data;

    wire palette_bank0_host_write_enable = palette_host_write_valid_q &&
        !palette_host_write_bank_q;
    wire palette_bank1_host_write_enable = palette_host_write_valid_q &&
        palette_host_write_bank_q;
    wire palette_bank0_clone_write_enable = clone_palette_write_enable &&
        pending_bank_q;
    wire palette_bank1_clone_write_enable = clone_palette_write_enable &&
        !pending_bank_q;

    wire palette_bank0_write_enable = palette_bank0_host_write_enable ||
                                      palette_bank0_clone_write_enable;
    wire palette_bank1_write_enable = palette_bank1_host_write_enable ||
                                      palette_bank1_clone_write_enable;
    wire [11:0] metadata_palette_write_address =
        clone_palette_write_enable ? clone_palette_transfer_index_q :
                                     palette_host_write_address_q;
    wire [31:0] metadata_palette_write_data =
        clone_palette_write_enable ? clone_palette_transfer_data_q :
                                     palette_host_write_data_q;

    astra_sprite_palette_ram metadata_palette_bank0 (
        .clk(clk),
        .write_enable(palette_bank0_write_enable),
        .write_address(metadata_palette_write_address),
        .write_data(metadata_palette_write_data),
        .read_address(metadata_palette_read_address),
        .read_data(palette_bank0_read_data)
    );

    astra_sprite_palette_ram metadata_palette_bank1 (
        .clk(clk),
        .write_enable(palette_bank1_write_enable),
        .write_address(metadata_palette_write_address),
        .write_data(metadata_palette_write_data),
        .read_address(metadata_palette_read_address),
        .read_data(palette_bank1_read_data)
    );

    astra_sprite_palette_ram active_palette_bank0 (
        .clk(clk),
        .write_enable(active_palette_write_enable),
        .write_address(active_palette_write_address),
        .write_data(active_palette_write_data),
        .read_address({palette0_read_bank, palette0_read_index}),
        .read_data(palette0_read_argb)
    );

    astra_sprite_palette_ram active_palette_bank1 (
        .clk(clk),
        .write_enable(active_palette_write_enable),
        .write_address(active_palette_write_address),
        .write_data(active_palette_write_data),
        .read_address({palette1_read_bank, palette1_read_index}),
        .read_data(palette1_read_argb)
    );

    astra_sprite_palette_ram active_palette_bank2 (
        .clk(clk),
        .write_enable(active_palette_write_enable),
        .write_address(active_palette_write_address),
        .write_data(active_palette_write_data),
        .read_address({palette2_read_bank, palette2_read_index}),
        .read_data(palette2_read_argb)
    );

    astra_sprite_palette_ram active_palette_bank3 (
        .clk(clk),
        .write_enable(active_palette_write_enable),
        .write_address(active_palette_write_address),
        .write_data(active_palette_write_data),
        .read_address({palette3_read_bank, palette3_read_index}),
        .read_data(palette3_read_argb)
    );

    astra_sprite_palette_ram active_palette_baseline (
        .clk(clk),
        .write_enable(activation_palette_write_enable),
        .write_address(activation_palette_transfer_index_q),
        .write_data(activation_palette_transfer_data_q),
        .read_address(palette_restore_index),
        .read_data(palette_baseline_read_data)
    );

    always @(posedge clk) begin
        if (reset) begin
            descriptor_host_write_valid_q <= 1'b0;
            palette_host_write_valid_q <= 1'b0;
            clone_descriptor_transfer_valid_q <= 1'b0;
            activation_descriptor_transfer_valid_q <= 1'b0;
            clone_palette_transfer_valid_q <= 1'b0;
            activation_palette_transfer_valid_q <= 1'b0;
        end else begin
            descriptor_host_write_valid_q <=
                descriptor_write_enable && write_ready;
            palette_host_write_valid_q <= palette_write_enable && write_ready;
            clone_descriptor_transfer_valid_q <=
                clone_descriptor_read_valid_q;
            activation_descriptor_transfer_valid_q <=
                activation_descriptor_read_valid_q;
            clone_palette_transfer_valid_q <= clone_palette_read_valid_q;
            activation_palette_transfer_valid_q <=
                activation_palette_read_valid_q;
        end
    end

    always @(posedge clk) begin
        baseline_restore_done <= 1'b0;
        if (reset) begin
            palette_restore_state <= R_IDLE;
            palette_restore_pending_q <= 1'b0;
            palette_restore_index <= 12'd0;
            baseline_restore_done <= 1'b0;
        end else begin
            if (baseline_restore_start)
                palette_restore_pending_q <= 1'b1;
            case (palette_restore_state)
                R_IDLE: if (palette_restore_pending_q && !activate_busy) begin
                    palette_restore_pending_q <= 1'b0;
                    palette_restore_index <= 12'd0;
                    palette_restore_state <= R_READ;
                end
                R_READ: palette_restore_state <= R_WRITE;
                R_WRITE: begin
                    if (palette_restore_index == 12'd4095) begin
                        baseline_restore_done <= 1'b1;
                        palette_restore_state <= R_IDLE;
                    end else begin
                        palette_restore_index <= palette_restore_index + 12'd1;
                        palette_restore_state <= R_READ;
                    end
                end
                default: palette_restore_state <= R_IDLE;
            endcase
        end
    end

    // Transfer payload is meaningful only when its corresponding valid bit is
    // set. Keeping payload registers off the global reset net preserves BRAM
    // inference and lets placement keep each transfer next to its memory.
    integer transfer_word;
    always @(posedge clk) begin
        if (descriptor_write_enable && write_ready) begin
            descriptor_host_write_bank_q <= editable_bank_q;
            descriptor_host_write_index_q <= descriptor_write_index;
            descriptor_host_write_word_q <= descriptor_write_word;
            descriptor_host_write_data_q <= descriptor_write_data;
        end
        if (palette_write_enable && write_ready) begin
            palette_host_write_bank_q <= editable_bank_q;
            palette_host_write_address_q <= palette_write_address;
            palette_host_write_data_q <= palette_write_argb;
        end

        // The corresponding valid pipeline is the sole qualification for
        // these payloads. Continuous capture removes wide clock-enable cones
        // while preserving the same valid-cycle data at each BRAM boundary.
        clone_descriptor_transfer_index_q <=
            clone_descriptor_write_index_q;
        activation_descriptor_transfer_index_q <=
            activation_descriptor_write_index_q;
        activation_descriptor_transfer_scale_step_q <= pending_bank_q ?
            scale_step1[activation_descriptor_write_index_q] :
            scale_step0[activation_descriptor_write_index_q];
        activation_descriptor_transfer_order_q <= pending_bank_q ?
            order1[activation_descriptor_write_index_q] :
            order0[activation_descriptor_write_index_q];
        for (transfer_word = 0; transfer_word < 8;
             transfer_word = transfer_word + 1) begin
            clone_descriptor_transfer_bank0_data_q[transfer_word] <=
                descriptor_bank0_read_data[transfer_word];
            clone_descriptor_transfer_bank1_data_q[transfer_word] <=
                descriptor_bank1_read_data[transfer_word];
            activation_descriptor_transfer_bank0_data_q[transfer_word] <=
                descriptor_bank0_read_data[transfer_word];
            activation_descriptor_transfer_bank1_data_q[transfer_word] <=
                descriptor_bank1_read_data[transfer_word];
        end
        clone_palette_transfer_index_q <= clone_palette_write_index_q;
        clone_palette_transfer_data_q <= pending_palette_read_data;
        activation_palette_transfer_index_q <=
            activation_palette_write_index_q;
        activation_palette_transfer_data_q <= pending_palette_read_data;
    end

    always @(posedge clk) begin
        if (reset) begin
            metadata_read_owner_q <= METADATA_VALIDATION;
        end else begin
            if (accept_pending && validate_valid && !pending_valid_q &&
                write_ready)
                metadata_read_owner_q <= METADATA_CLONE;
            else if (clone_state == C_PALETTE_DRAIN)
                metadata_read_owner_q <= METADATA_VALIDATION;
            else if (activation_state == A_IDLE && activate_start &&
                     pending_ready)
                metadata_read_owner_q <= METADATA_ACTIVATION;
            else if (activation_state == A_PALETTE_DRAIN)
                metadata_read_owner_q <= METADATA_VALIDATION;
        end
    end

    always @(posedge clk) begin
        if (reset) begin
            editable_bank_q <= 1'b0;
            pending_bank_q <= 1'b0;
            pending_valid_q <= 1'b0;
            clone_state <= C_IDLE;
            clone_descriptor_index <= 6'd0;
            clone_descriptor_read_valid_q <= 1'b0;
            clone_descriptor_write_index_q <= 6'd0;
            clone_palette_index <= 12'd0;
            clone_palette_read_valid_q <= 1'b0;
            clone_palette_write_index_q <= 12'd0;
        end else begin
            if (accept_pending && validate_valid && !pending_valid_q &&
                write_ready) begin
                pending_bank_q <= editable_bank_q;
                pending_valid_q <= 1'b1;
                editable_bank_q <= ~editable_bank_q;
                clone_descriptor_index <= 6'd0;
                clone_descriptor_read_valid_q <= 1'b0;
                clone_state <= C_DESCRIPTORS;
            end

            case (clone_state)
                C_DESCRIPTORS: begin
                    clone_descriptor_read_valid_q <= 1'b1;
                    clone_descriptor_write_index_q <=
                        clone_descriptor_index;
                    if (clone_descriptor_index == 6'd63) begin
                        clone_state <= C_DESCRIPTOR_DRAIN;
                    end else begin
                        clone_descriptor_index <= clone_descriptor_index + 6'd1;
                    end
                end
                C_DESCRIPTOR_DRAIN: begin
                    clone_descriptor_read_valid_q <= 1'b0;
                    clone_palette_index <= 12'd0;
                    clone_palette_read_valid_q <= 1'b0;
                    clone_state <= C_PALETTE;
                end
                C_PALETTE: begin
                    clone_palette_read_valid_q <= 1'b1;
                    clone_palette_write_index_q <= clone_palette_index;
                    if (clone_palette_index == 12'd4095)
                        clone_state <= C_PALETTE_DRAIN;
                    else begin
                        clone_palette_index <= clone_palette_index + 12'd1;
                    end
                end
                C_PALETTE_DRAIN: begin
                    clone_palette_read_valid_q <= 1'b0;
                    clone_state <= C_IDLE;
                end
                default: begin
                    clone_descriptor_read_valid_q <= 1'b0;
                    clone_palette_read_valid_q <= 1'b0;
                end
            endcase

            if (activate_done)
                pending_valid_q <= 1'b0;
        end
    end

    reg [4:0] validation_state;
    reg [2:0] validation_word_index;
    reg [31:0] validation_read_data_q;
    reg [31:0] validation_word0_q;
    reg [31:0] validation_word1_q;
    reg [31:0] validation_word2_q;
    reg [31:0] validation_word3_q;
    reg [31:0] validation_word4_q;
    reg [31:0] validation_word5_q;
    reg [31:0] validation_word6_q;
    reg [6:0] validation_sort_count;
    reg [6:0] validation_sort_position;
    reg [7:0] validation_priority_q;
    reg [5:0] sort_previous_index_q;
    reg [7:0] sort_previous_priority_q;
    reg [20:0] validation_last_row_offset_q;
    reg [8:0] validation_fetch_width_q;
    reg [32:0] validation_end_exclusive_q;
    reg validation_prefix_valid_q;
    reg validation_enabled_q;
    reg validation_descriptor_valid_q;
    reg [7:0] geometry_multiplier_q;
    reg [20:0] geometry_multiplicand_q;
    reg [2:0] geometry_bit_q;

    reg [31:0] divider_numerator_q;
    reg [10:0] divider_denominator_q;
    reg [11:0] divider_remainder_q;
    reg [31:0] divider_quotient_q;
    reg [31:0] divider_result_q;
    reg [4:0] divider_bit_q;
    reg [11:0] divider_shifted_q;

    reg [31:0] validation_read_data;
    always @* begin
        validation_read_data =
            validation_bank_read_data_q[validation_word_index];
    end

    integer validation_bank_word;
    always @(posedge clk) begin
        // Split the BRAM clock-to-output path from the eight-way word mux.
        // V_ADDRESS supplies the pipeline cycle before V_READ consumes it.
        for (validation_bank_word = 0; validation_bank_word < 8;
             validation_bank_word = validation_bank_word + 1) begin
            validation_bank_read_data_q[validation_bank_word] <=
                editable_bank_q ?
                    descriptor_bank1_read_data[validation_bank_word] :
                    descriptor_bank0_read_data[validation_bank_word];
        end
    end

    wire [7:0] validation_source_width = validation_word2_q[7:0];
    wire [7:0] validation_source_height = validation_word2_q[15:8];
    wire [10:0] validation_destination_width = validation_word3_q[10:0];
    wire [10:0] validation_destination_height = validation_word3_q[26:16];
    wire [12:0] validation_pitch = validation_word5_q[12:0];
    wire validation_reserved_clean =
        validation_word0_q[31:28] == 4'd0 &&
        validation_word0_q[7:6] == 2'd0 &&
        validation_word2_q[31:24] == 8'd0 &&
        validation_word3_q[31:27] == 5'd0 &&
        validation_word3_q[15:11] == 5'd0 &&
        validation_word5_q[31:13] == 19'd0 &&
        validation_read_data_q == 32'd0;
    wire validation_enabled_fields =
        validation_source_width >= 8'd1 &&
        validation_source_width <= 8'd128 &&
        validation_source_height >= 8'd1 &&
        validation_source_height <= 8'd128 &&
        validation_destination_width >= 11'd1 &&
        validation_destination_width <= 11'd1024 &&
        validation_destination_height >= 11'd1 &&
        validation_destination_height <= 11'd1024 &&
        validation_word4_q[5:0] == 6'd0 &&
        validation_word4_q >= ARENA_BASE &&
        validation_pitch[5:0] == 6'd0 &&
        validation_pitch >= {5'd0, validation_source_width};
    wire validation_descriptor_valid = validation_prefix_valid_q &&
        (!validation_enabled_q ||
         validation_end_exclusive_q <= {1'b0, ARENA_LIMIT});

    wire divider_subtract = divider_shifted_q >=
                             {1'b0, divider_denominator_q};
    wire [11:0] divider_remainder_next = divider_subtract ?
        divider_shifted_q - {1'b0, divider_denominator_q} :
        divider_shifted_q;
    wire [31:0] divider_quotient_next = divider_quotient_q |
        (divider_subtract ? (32'd1 << divider_bit_q) : 32'd0);

    wire [5:0] sort_order_address = validation_sort_position == 7'd0 ?
        6'd0 : validation_sort_position[5:0] - 6'd1;
    wire [5:0] sort_order_read = editable_bank_q ?
        order1[sort_order_address] : order0[sort_order_address];
    wire [7:0] sort_priority_read = editable_bank_q ?
        priority1[sort_previous_index_q] : priority0[sort_previous_index_q];

    always @(posedge clk) begin
        if (reset) begin
            validation_state <= V_IDLE;
            validate_busy <= 1'b0;
            validate_done <= 1'b0;
            validate_valid <= 1'b0;
            validation_descriptor_index <= 6'd0;
            validation_word_index <= 3'd0;
            validation_read_data_q <= 32'd0;
            validation_word0_q <= 32'd0;
            validation_word1_q <= 32'd0;
            validation_word2_q <= 32'd0;
            validation_word3_q <= 32'd0;
            validation_word4_q <= 32'd0;
            validation_word5_q <= 32'd0;
            validation_word6_q <= 32'd0;
            validation_sort_count <= 7'd0;
            validation_sort_position <= 7'd0;
            validation_priority_q <= 8'd0;
            sort_previous_index_q <= 6'd0;
            sort_previous_priority_q <= 8'd0;
            validation_last_row_offset_q <= 21'd0;
            validation_fetch_width_q <= 9'd0;
            validation_end_exclusive_q <= 33'd0;
            validation_prefix_valid_q <= 1'b0;
            validation_enabled_q <= 1'b0;
            validation_descriptor_valid_q <= 1'b0;
            geometry_multiplier_q <= 8'd0;
            geometry_multiplicand_q <= 21'd0;
            geometry_bit_q <= 3'd0;
            divider_numerator_q <= 32'd0;
            divider_denominator_q <= 11'd1;
            divider_remainder_q <= 12'd0;
            divider_quotient_q <= 32'd0;
            divider_result_q <= 32'd0;
            divider_bit_q <= 5'd0;
            divider_shifted_q <= 12'd0;
        end else begin
            validate_done <= 1'b0;
            if (validation_state == V_IDLE) begin
                if (validate_start && write_ready && !pending_valid_q) begin
                    validate_busy <= 1'b1;
                    validate_valid <= 1'b0;
                    validation_descriptor_index <= 6'd0;
                    validation_word_index <= 3'd0;
                    validation_sort_count <= 7'd0;
                    validation_state <= V_ADDRESS;
                end
            end else begin
                case (validation_state)
                    V_ADDRESS: validation_state <= V_READ_PIPE;
                    V_READ_PIPE: validation_state <= V_READ;
                    V_READ: begin
                        validation_read_data_q <= validation_read_data;
                        validation_state <= V_CAPTURE;
                    end
                    V_CAPTURE: begin
                        case (validation_word_index)
                            3'd0: validation_word0_q <= validation_read_data_q;
                            3'd1: validation_word1_q <= validation_read_data_q;
                            3'd2: validation_word2_q <= validation_read_data_q;
                            3'd3: validation_word3_q <= validation_read_data_q;
                            3'd4: validation_word4_q <= validation_read_data_q;
                            3'd5: validation_word5_q <= validation_read_data_q;
                            3'd6: validation_word6_q <= validation_read_data_q;
                            default: begin end
                        endcase
                        if (validation_word_index == 3'd7)
                            validation_state <= V_GEOMETRY;
                        else begin
                            validation_word_index <= validation_word_index + 3'd1;
                            validation_state <= V_READ;
                        end
                    end
                    V_GEOMETRY: begin
                        validation_last_row_offset_q <= 21'd0;
                        geometry_multiplier_q <=
                            validation_source_height - 8'd1;
                        geometry_multiplicand_q <=
                            {8'd0, validation_pitch};
                        geometry_bit_q <= 3'd0;
                        validation_fetch_width_q <=
                            ({1'b0, validation_source_width} + 9'd7) &
                            9'h1f8;
                        validation_prefix_valid_q <=
                            validation_reserved_clean &&
                            (!validation_word0_q[0] ||
                             validation_enabled_fields);
                        validation_enabled_q <= validation_word0_q[0];
                        validation_state <= V_MULTIPLY;
                    end
                    V_MULTIPLY: begin
                        if (geometry_multiplier_q[0])
                            validation_last_row_offset_q <=
                                validation_last_row_offset_q +
                                geometry_multiplicand_q;
                        geometry_multiplier_q <=
                            {1'b0, geometry_multiplier_q[7:1]};
                        geometry_multiplicand_q <=
                            {geometry_multiplicand_q[19:0], 1'b0};
                        if (geometry_bit_q == 3'd7)
                            validation_state <= V_END_ADDRESS;
                        else
                            geometry_bit_q <= geometry_bit_q + 3'd1;
                    end
                    V_END_ADDRESS: begin
                        validation_end_exclusive_q <=
                            {1'b0, validation_word4_q} +
                            {12'd0, validation_last_row_offset_q} +
                            {24'd0, validation_fetch_width_q};
                        validation_state <= V_CHECK;
                    end
                    V_CHECK: begin
                        validation_descriptor_valid_q <=
                            validation_descriptor_valid;
                        validation_state <= V_CHECK_RESULT;
                    end
                    V_CHECK_RESULT: begin
                        if (!validation_descriptor_valid_q) begin
                            validate_busy <= 1'b0;
                            validate_valid <= 1'b0;
                            validate_done <= 1'b1;
                            validation_state <= V_IDLE;
                        end else begin
                            validation_priority_q <= validation_word0_q[15:8];
                            validation_sort_position <= validation_sort_count;
                            if (!editable_bank_q)
                                priority0[validation_descriptor_index] <= validation_word0_q[15:8];
                            else
                                priority1[validation_descriptor_index] <= validation_word0_q[15:8];
                            if (validation_word0_q[0]) begin
                                divider_numerator_q <= {
                                    validation_source_width, 24'd0
                                };
                                divider_denominator_q <= validation_destination_width;
                                divider_remainder_q <= 12'd0;
                                divider_quotient_q <= 32'd0;
                                divider_bit_q <= 5'd31;
                                validation_state <= V_DIVIDE_SHIFT;
                            end else begin
                                if (!editable_bank_q) begin
                                    scale_step0[validation_descriptor_index] <= 32'd0;
                                end else begin
                                    scale_step1[validation_descriptor_index] <= 32'd0;
                                end
                                validation_state <= V_SORT_INDEX;
                            end
                        end
                    end
                    V_DIVIDE_SHIFT: begin
                        divider_shifted_q <= {
                            divider_remainder_q[10:0],
                            divider_numerator_q[divider_bit_q]
                        };
                        validation_state <= V_DIVIDE;
                    end
                    V_DIVIDE: begin
                        divider_remainder_q <= divider_remainder_next;
                        divider_quotient_q <= divider_quotient_next;
                        if (divider_bit_q == 5'd0) begin
                            validation_state <= V_DIVIDE_ROUND;
                        end else begin
                            divider_bit_q <= divider_bit_q - 5'd1;
                            validation_state <= V_DIVIDE_SHIFT;
                        end
                    end
                    V_DIVIDE_ROUND: begin
                        divider_result_q <= divider_quotient_q +
                            (divider_remainder_q != 12'd0);
                        validation_state <= V_DIVIDE_WRITE;
                    end
                    V_DIVIDE_WRITE: begin
                        // With D <= 1024 and destination offset p < D,
                        // ceil(S*2^24/D) preserves floor(p*S/D) exactly.
                        if (!editable_bank_q) begin
                            scale_step0[validation_descriptor_index] <=
                                divider_result_q;
                        end else begin
                            scale_step1[validation_descriptor_index] <=
                                divider_result_q;
                        end
                        validation_state <= V_SORT_INDEX;
                    end
                    V_SORT_INDEX: begin
                        sort_previous_index_q <= validation_sort_position == 7'd0 ?
                            6'd0 : sort_order_read;
                        validation_state <= V_SORT_PRIORITY;
                    end
                    V_SORT_PRIORITY: begin
                        sort_previous_priority_q <=
                            validation_sort_position == 7'd0 ?
                            8'd0 : sort_priority_read;
                        validation_state <= V_SORT;
                    end
                    V_SORT: begin
                        if (validation_sort_position != 7'd0 &&
                            sort_previous_priority_q > validation_priority_q) begin
                            if (!editable_bank_q)
                                order0[validation_sort_position] <= sort_previous_index_q;
                            else
                                order1[validation_sort_position] <= sort_previous_index_q;
                            validation_sort_position <= validation_sort_position - 7'd1;
                            validation_state <= V_SORT_INDEX;
                        end else begin
                            if (!editable_bank_q)
                                order0[validation_sort_position] <= validation_descriptor_index;
                            else
                                order1[validation_sort_position] <= validation_descriptor_index;
                            validation_state <= V_NEXT;
                        end
                    end
                    V_NEXT: begin
                        if (validation_descriptor_index == 6'd63) begin
                            validate_busy <= 1'b0;
                            validate_valid <= 1'b1;
                            validate_done <= 1'b1;
                            validation_state <= V_IDLE;
                        end else begin
                            validation_descriptor_index <= validation_descriptor_index + 6'd1;
                            validation_word_index <= 3'd0;
                            validation_sort_count <= validation_sort_count + 7'd1;
                            validation_state <= V_ADDRESS;
                        end
                    end
                    default: validation_state <= V_IDLE;
                endcase
            end
        end
    end

    reg [5:0] compatibility_i;
    reg [5:0] compatibility_j;
    reg [63:0] compatibility_row_q;
    reg compatibility_i_enabled_q;
    reg compatibility_j_enabled_q;
    reg [31:0] compatibility_i_word6_q;
    reg [31:0] compatibility_j_word6_q;
    reg [5:0] compatibility_j_index_q;
    reg compatibility_j_valid_q;

    wire compatibility_bit =
        compatibility_i != compatibility_j_index_q &&
        compatibility_i_enabled_q && compatibility_j_enabled_q &&
        ((compatibility_i_word6_q[31:16] &
          compatibility_j_word6_q[15:0]) != 16'd0) &&
        ((compatibility_j_word6_q[31:16] &
          compatibility_i_word6_q[15:0]) != 16'd0);

    always @(posedge clk) begin
        if (reset) begin
            activation_state <= A_IDLE;
            activate_busy <= 1'b0;
            activate_done <= 1'b0;
            activation_descriptor_index <= 6'd0;
            activation_descriptor_read_valid_q <= 1'b0;
            activation_descriptor_write_index_q <= 6'd0;
            activation_palette_index <= 12'd0;
            activation_palette_read_valid_q <= 1'b0;
            activation_palette_write_index_q <= 12'd0;
            compatibility_i <= 6'd0;
            compatibility_j <= 6'd0;
            compatibility_row_q <= 64'd0;
            compatibility_i_enabled_q <= 1'b0;
            compatibility_j_enabled_q <= 1'b0;
            compatibility_i_word6_q <= 32'd0;
            compatibility_j_word6_q <= 32'd0;
            compatibility_j_index_q <= 6'd0;
            compatibility_j_valid_q <= 1'b0;
        end else begin
            activate_done <= 1'b0;

            if (activation_descriptor_write_enable) begin
                active_word0[activation_descriptor_transfer_index_q] <=
                    activation_descriptor_transfer_data[0];
                active_word1[activation_descriptor_transfer_index_q] <=
                    activation_descriptor_transfer_data[1];
                active_word2[activation_descriptor_transfer_index_q] <=
                    activation_descriptor_transfer_data[2];
                active_word3[activation_descriptor_transfer_index_q] <=
                    activation_descriptor_transfer_data[3];
                active_word4[activation_descriptor_transfer_index_q] <=
                    activation_descriptor_transfer_data[4];
                active_word5[activation_descriptor_transfer_index_q] <=
                    activation_descriptor_transfer_data[5];
                active_word6[activation_descriptor_transfer_index_q] <=
                    activation_descriptor_transfer_data[6];
                active_scale_step[
                    activation_descriptor_transfer_index_q] <=
                    activation_descriptor_transfer_scale_step_q;
                active_order[activation_descriptor_transfer_index_q] <=
                    activation_descriptor_transfer_order_q;
            end

            case (activation_state)
                A_IDLE: begin
                    activation_descriptor_read_valid_q <= 1'b0;
                    activation_palette_read_valid_q <= 1'b0;
                    if (activate_start && pending_ready) begin
                        activate_busy <= 1'b1;
                        activation_descriptor_index <= 6'd0;
                        activation_descriptor_read_valid_q <= 1'b0;
                        activation_state <= A_DESCRIPTORS;
                    end
                end
                A_DESCRIPTORS: begin
                    activation_descriptor_read_valid_q <= 1'b1;
                    activation_descriptor_write_index_q <=
                        activation_descriptor_index;
                    if (activation_descriptor_index == 6'd63) begin
                        activation_state <= A_DESCRIPTOR_DRAIN;
                    end else begin
                        activation_descriptor_index <=
                            activation_descriptor_index + 6'd1;
                    end
                end
                A_DESCRIPTOR_DRAIN: begin
                    activation_descriptor_read_valid_q <= 1'b0;
                    activation_palette_index <= 12'd0;
                    activation_palette_read_valid_q <= 1'b0;
                    activation_state <= A_PALETTE;
                end
                A_PALETTE: begin
                    activation_palette_read_valid_q <= 1'b1;
                    activation_palette_write_index_q <=
                        activation_palette_index;
                    if (activation_palette_index == 12'd4095) begin
                        activation_state <= A_PALETTE_DRAIN;
                    end else begin
                        activation_palette_index <=
                            activation_palette_index + 12'd1;
                    end
                end
                A_PALETTE_DRAIN: begin
                    activation_palette_read_valid_q <= 1'b0;
                    compatibility_i <= 6'd0;
                    compatibility_j <= 6'd63;
                    compatibility_row_q <= 64'd0;
                    compatibility_j_valid_q <= 1'b0;
                    activation_state <= A_COMPATIBILITY_I;
                end
                A_COMPATIBILITY_I: begin
                    compatibility_i_enabled_q <=
                        active_word0[compatibility_i][0] &&
                        active_word0[compatibility_i][1] &&
                        active_word0[compatibility_i][5];
                    compatibility_i_word6_q <= active_word6[compatibility_i];
                    compatibility_j <= 6'd63;
                    compatibility_row_q <= 64'd0;
                    compatibility_j_valid_q <= 1'b0;
                    activation_state <= A_COMPATIBILITY;
                end
                A_COMPATIBILITY: begin
                    compatibility_j_enabled_q <=
                        active_word0[compatibility_j][0] &&
                        active_word0[compatibility_j][1] &&
                        active_word0[compatibility_j][5];
                    compatibility_j_word6_q <= active_word6[compatibility_j];
                    compatibility_j_index_q <= compatibility_j;
                    compatibility_j_valid_q <= 1'b1;
                    if (compatibility_j_valid_q)
                        compatibility_row_q <= {
                            compatibility_row_q[62:0], compatibility_bit
                        };
                    if (compatibility_j == 6'd0)
                        activation_state <= A_COMPATIBILITY_DRAIN;
                    else
                        compatibility_j <= compatibility_j - 6'd1;
                end
                A_COMPATIBILITY_DRAIN: begin
                    active_compatible[compatibility_i] <= {
                        compatibility_row_q[62:0], compatibility_bit
                    };
                    compatibility_j_valid_q <= 1'b0;
                    if (compatibility_i == 6'd63) begin
                        activate_busy <= 1'b0;
                        activate_done <= 1'b1;
                        activation_state <= A_IDLE;
                    end else begin
                        compatibility_i <= compatibility_i + 6'd1;
                        activation_state <= A_COMPATIBILITY_I;
                    end
                end
                default: begin
                    activation_descriptor_read_valid_q <= 1'b0;
                    activation_palette_read_valid_q <= 1'b0;
                    activation_state <= A_IDLE;
                end
            endcase
        end
    end

    always @(posedge clk) begin
        if (order_read_enable)
            order_read_index <= active_order[order_read_position];
        if (descriptor_read_enable) begin
            descriptor_word0 <= active_word0[descriptor_read_index];
            descriptor_word1 <= active_word1[descriptor_read_index];
            descriptor_word2 <= active_word2[descriptor_read_index];
            descriptor_word3 <= active_word3[descriptor_read_index];
            descriptor_word4 <= active_word4[descriptor_read_index];
            descriptor_word5 <= active_word5[descriptor_read_index];
            descriptor_word6 <= active_word6[descriptor_read_index];
            descriptor_scale_step_x <= active_scale_step[descriptor_read_index];
            descriptor_collision_compatible <= active_compatible[descriptor_read_index];
        end
    end

endmodule

module astra_sprite_descriptor_ram (
    input  wire        clk,
    input  wire        write_enable,
    input  wire [5:0]  write_address,
    input  wire [31:0] write_data,
    input  wire [5:0]  read_address,
    output reg  [31:0] read_data
);
    (* ram_style = "block" *) reg [31:0] memory [0:63];
    integer initialize_word;

    initial begin
        for (initialize_word = 0; initialize_word < 64;
             initialize_word = initialize_word + 1)
            memory[initialize_word] = 32'd0;
    end

    always @(posedge clk) begin
        if (write_enable)
            memory[write_address] <= write_data;
        read_data <= memory[read_address];
    end
endmodule

module astra_sprite_palette_ram (
    input  wire        clk,
    input  wire        write_enable,
    input  wire [11:0] write_address,
    input  wire [31:0] write_data,
    input  wire [11:0] read_address,
    output reg  [31:0] read_data
);
    (* ram_style = "block" *) reg [31:0] memory [0:4095];

    always @(posedge clk) begin
        if (write_enable)
            memory[write_address] <= write_data;
        read_data <= memory[read_address];
    end
endmodule

`default_nettype wire
