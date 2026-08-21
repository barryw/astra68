# Astra Arty Z7-20 PS/DDR graphics integration build.
#
# Set ASTRA_ARTY_BD_ONLY=1 to stop after validating the processing-system block
# design and generating its wrapper. The same block design is used by the full
# routed graphics build; the probe mode exists to make interface changes
# measurable before they are coupled to handwritten RTL.

set script_dir [file dirname [file normalize [info script]]]
set repo_root [file normalize [file join $script_dir ../../..]]
if {[info exists ::env(ASTRA_ARTY_OUT)]} {
    set out_dir [file normalize $::env(ASTRA_ARTY_OUT)]
} else {
    set out_dir [file normalize [file join $repo_root build arty-graphics]]
}

set part xc7z020clg400-1
set board digilentinc.com:arty-z7-20:part0:1.1
set project_dir [file join $out_dir project]

set render_frequency_hz 200000000
if {[info exists ::env(ASTRA_ARTY_RENDER_FREQ_HZ)]} {
    set render_frequency_hz $::env(ASTRA_ARTY_RENDER_FREQ_HZ)
}
if {![string is integer -strict $render_frequency_hz] ||
    $render_frequency_hz < 100000000 ||
    $render_frequency_hz > 200000000} {
    puts "ASTRA_ARTY_GRAPHICS ERROR invalid render frequency: $render_frequency_hz"
    exit 3
}
set render_frequency_mhz [format %.6f \
    [expr {$render_frequency_hz / 1000000.0}]]
set render_frequency_requested_hz $render_frequency_hz

set implementation_frequency_hz $render_frequency_requested_hz
if {[info exists ::env(ASTRA_ARTY_IMPLEMENT_FREQ_HZ)]} {
    set implementation_frequency_hz $::env(ASTRA_ARTY_IMPLEMENT_FREQ_HZ)
}
if {![string is integer -strict $implementation_frequency_hz] ||
    $implementation_frequency_hz < $render_frequency_requested_hz ||
    $implementation_frequency_hz > 200000000} {
    puts "ASTRA_ARTY_GRAPHICS ERROR invalid implementation frequency: $implementation_frequency_hz"
    exit 3
}

file mkdir $out_dir
create_project -force astra_arty_graphics $project_dir -part $part
set_property board_part $board [current_project]
set implementation_strategy Performance_Explore
if {[info exists ::env(ASTRA_ARTY_IMPL_STRATEGY)]} {
    set implementation_strategy $::env(ASTRA_ARTY_IMPL_STRATEGY)
}
set_property STRATEGY $implementation_strategy [get_runs impl_1]
set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.TCL.POST \
    [file join $script_dir repair_postroute_routes.tcl] [get_runs impl_1]

set incremental_checkpoint ""
if {[info exists ::env(ASTRA_ARTY_INCREMENTAL_CHECKPOINT)]} {
    set incremental_checkpoint \
        [file normalize $::env(ASTRA_ARTY_INCREMENTAL_CHECKPOINT)]
    if {![file exists $incremental_checkpoint]} {
        puts "ASTRA_ARTY_GRAPHICS ERROR incremental checkpoint not found: $incremental_checkpoint"
        exit 3
    }
    set_property INCREMENTAL_CHECKPOINT $incremental_checkpoint \
        [get_runs impl_1]
    set_property INCREMENTAL_CHECKPOINT.DIRECTIVE TimingClosure \
        [get_runs impl_1]
}

set rqs_file ""
if {[info exists ::env(ASTRA_ARTY_RQS_FILE)]} {
    set rqs_file [file normalize $::env(ASTRA_ARTY_RQS_FILE)]
    if {![file exists $rqs_file]} {
        puts "ASTRA_ARTY_GRAPHICS ERROR QoR suggestions file not found: $rqs_file"
        exit 3
    }
    add_files -fileset utils_1 -norecurse $rqs_file
    set_property RQS_FILES $rqs_file [get_runs {synth_1 impl_1}]
}

create_bd_design astra_ps
create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7 ps7
apply_bd_automation -rule xilinx.com:bd_rule:processing_system7 \
    -config {make_external "FIXED_IO, DDR" apply_board_preset "1" Master "Disable" Slave "Disable"} \
    [get_bd_cells ps7]
set_property -dict [list \
    CONFIG.PCW_EN_CLK0_PORT {1} \
    CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ {100.000000} \
    CONFIG.PCW_EN_CLK1_PORT {1} \
    CONFIG.PCW_FPGA1_PERIPHERAL_FREQMHZ $render_frequency_mhz \
    CONFIG.PCW_EN_RST1_PORT {1} \
    CONFIG.PCW_USE_S_AXI_HP0 {1} \
    CONFIG.PCW_USE_S_AXI_HP1 {1} \
    CONFIG.PCW_USE_S_AXI_HP2 {1} \
    CONFIG.PCW_USE_S_AXI_HP3 {1} \
    CONFIG.PCW_USE_M_AXI_GP0 {1} \
    CONFIG.PCW_USE_FABRIC_INTERRUPT {1} \
    CONFIG.PCW_IRQ_F2P_INTR {1} \
    CONFIG.PCW_IRQ_F2P_MODE {DIRECT} \
    CONFIG.PCW_I2C0_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_I2C0_I2C0_IO {EMIO} \
    CONFIG.PCW_GPIO_EMIO_GPIO_ENABLE {1} \
    CONFIG.PCW_GPIO_EMIO_GPIO_IO {1} \
    CONFIG.PCW_UIPARAM_DDR_ADV_ENABLE {1} \
    CONFIG.PCW_DDR_PORT3_HPR_ENABLE {1} \
    CONFIG.PCW_DDR_HPRLPR_QUEUE_PARTITION {HPR(24)/LPR(8)} \
    CONFIG.PCW_DDR_LPR_TO_CRITICAL_PRIORITY_LEVEL {15} \
    CONFIG.PCW_DDR_HPR_TO_CRITICAL_PRIORITY_LEVEL {2} \
    CONFIG.PCW_DDR_WRITE_TO_CRITICAL_PRIORITY_LEVEL {15} \
] [get_bd_cells ps7]

# PS7 fabric clocks use discrete PLL/divider combinations. Propagate the
# generated frequency, not the request, into every AXI endpoint so clock
# metadata and timing constraints cannot diverge after quantization.
set render_frequency_hz [get_property CONFIG.FREQ_HZ \
    [get_bd_pins ps7/FCLK_CLK1]]
if {$implementation_frequency_hz < $render_frequency_hz} {
    puts "ASTRA_ARTY_GRAPHICS ERROR implementation frequency is below generated render frequency: $implementation_frequency_hz < $render_frequency_hz"
    exit 3
}
if {$implementation_frequency_hz != $render_frequency_hz} {
    set_property STEPS.OPT_DESIGN.TCL.PRE \
        [file join $script_dir apply_implementation_clock.tcl] \
        [get_runs impl_1]
}

# UG1145 maps HP0 and HP1 to DDR controller port 3. The 24/8 HPR/LPR
# partition and 2/15/15 critical levels are AMD's measured low-latency read
# configuration for that pair; framebuffer and sprite deadlines share it.

# HP0 is reserved for deterministic framebuffer line reads. A standalone
# protocol converter is sufficient for each 1x1 HP path; all Astra bursts are
# at most 16 beats, so unprotected AXI4-to-AXI3 translation needs no splitter.
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_protocol_converter hp0_conv
set_property -dict [list \
    CONFIG.SI_PROTOCOL {AXI4} \
    CONFIG.MI_PROTOCOL {AXI3} \
    CONFIG.TRANSLATION_MODE {0} \
] [get_bd_cells hp0_conv]
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_register_slice:2.1 \
    hp0_ps_slice
set_property -dict [list \
    CONFIG.PROTOCOL {AXI3} \
    CONFIG.READ_WRITE_MODE {READ_ONLY} \
    CONFIG.DATA_WIDTH {64} \
    CONFIG.ID_WIDTH {6} \
    CONFIG.REG_AR {1} \
    CONFIG.REG_R {9} \
] [get_bd_cells hp0_ps_slice]
connect_bd_intf_net [get_bd_intf_pins hp0_conv/M_AXI] \
    [get_bd_intf_pins hp0_ps_slice/S_AXI]
connect_bd_intf_net [get_bd_intf_pins hp0_ps_slice/M_AXI] \
    [get_bd_intf_pins ps7/S_AXI_HP0]

# Keep the proven full register slice at the framebuffer-facing edge.
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_register_slice:2.1 \
    hp0_render_slice
set_property -dict [list \
    CONFIG.PROTOCOL {AXI4} \
    CONFIG.READ_WRITE_MODE {READ_ONLY} \
    CONFIG.ADDR_WIDTH {32} \
    CONFIG.DATA_WIDTH {64} \
    CONFIG.ID_WIDTH {6} \
    CONFIG.REG_AR {1} \
    CONFIG.REG_R {1} \
] [get_bd_cells hp0_render_slice]
connect_bd_intf_net [get_bd_intf_pins hp0_render_slice/M_AXI] \
    [get_bd_intf_pins hp0_conv/S_AXI]

# Tile and sprite arbitration is implemented by astra_axi_read_3to1 in the
# top-level RTL. IDs preserve independent outstanding traffic, so this block
# design only needs the same direct AXI4-to-AXI3 conversion used by HP0.
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_protocol_converter hp1_conv
set_property -dict [list \
    CONFIG.SI_PROTOCOL {AXI4} \
    CONFIG.MI_PROTOCOL {AXI3} \
    CONFIG.TRANSLATION_MODE {0} \
] [get_bd_cells hp1_conv]
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_register_slice:2.1 \
    hp1_ps_slice
set_property -dict [list \
    CONFIG.PROTOCOL {AXI3} \
    CONFIG.READ_WRITE_MODE {READ_ONLY} \
    CONFIG.DATA_WIDTH {64} \
    CONFIG.ID_WIDTH {6} \
    CONFIG.REG_AR {1} \
    CONFIG.REG_R {1} \
] [get_bd_cells hp1_ps_slice]
connect_bd_intf_net [get_bd_intf_pins hp1_conv/M_AXI] \
    [get_bd_intf_pins hp1_ps_slice/S_AXI]
connect_bd_intf_net [get_bd_intf_pins hp1_ps_slice/M_AXI] \
    [get_bd_intf_pins ps7/S_AXI_HP1]

# HP2 and HP3 are dedicated to the renderer. Keeping reads and writes on
# separate PS ports avoids coupling command/descriptor/source traffic to
# destination/completion backpressure.
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_protocol_converter hp2_conv
set_property -dict [list \
    CONFIG.SI_PROTOCOL {AXI4} \
    CONFIG.MI_PROTOCOL {AXI3} \
    CONFIG.TRANSLATION_MODE {0} \
] [get_bd_cells hp2_conv]
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_register_slice:2.1 \
    hp2_ps_slice
set_property -dict [list \
    CONFIG.PROTOCOL {AXI3} \
    CONFIG.READ_WRITE_MODE {READ_ONLY} \
    CONFIG.DATA_WIDTH {64} \
    CONFIG.ID_WIDTH {6} \
    CONFIG.REG_AR {1} \
    CONFIG.REG_R {9} \
] [get_bd_cells hp2_ps_slice]
connect_bd_intf_net [get_bd_intf_pins hp2_conv/M_AXI] \
    [get_bd_intf_pins hp2_ps_slice/S_AXI]
connect_bd_intf_net [get_bd_intf_pins hp2_ps_slice/M_AXI] \
    [get_bd_intf_pins ps7/S_AXI_HP2]

# Keep the proven full register slice at the renderer-facing edge.
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_register_slice:2.1 \
    hp2_render_slice
set_property -dict [list \
    CONFIG.PROTOCOL {AXI4} \
    CONFIG.READ_WRITE_MODE {READ_ONLY} \
    CONFIG.ADDR_WIDTH {32} \
    CONFIG.DATA_WIDTH {64} \
    CONFIG.ID_WIDTH {6} \
    CONFIG.REG_AR {1} \
    CONFIG.REG_R {1} \
] [get_bd_cells hp2_render_slice]
connect_bd_intf_net [get_bd_intf_pins hp2_render_slice/M_AXI] \
    [get_bd_intf_pins hp2_conv/S_AXI]

create_bd_cell -type ip -vlnv xilinx.com:ip:axi_protocol_converter hp3_conv
set_property -dict [list \
    CONFIG.SI_PROTOCOL {AXI4} \
    CONFIG.MI_PROTOCOL {AXI3} \
    CONFIG.TRANSLATION_MODE {0} \
] [get_bd_cells hp3_conv]
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_register_slice:2.1 \
    hp3_ps_slice
set_property -dict [list \
    CONFIG.PROTOCOL {AXI3} \
    CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
    CONFIG.DATA_WIDTH {64} \
    CONFIG.ID_WIDTH {6} \
    CONFIG.REG_AW {9} \
    CONFIG.REG_W {9} \
    CONFIG.REG_B {9} \
] [get_bd_cells hp3_ps_slice]
connect_bd_intf_net [get_bd_intf_pins hp3_conv/M_AXI] \
    [get_bd_intf_pins hp3_ps_slice/S_AXI]
connect_bd_intf_net [get_bd_intf_pins hp3_ps_slice/M_AXI] \
    [get_bd_intf_pins ps7/S_AXI_HP3]

# Match HP2 with a proven full renderer-facing write slice.
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_register_slice:2.1 \
    hp3_render_slice
set_property -dict [list \
    CONFIG.PROTOCOL {AXI4} \
    CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
    CONFIG.ADDR_WIDTH {32} \
    CONFIG.DATA_WIDTH {64} \
    CONFIG.ID_WIDTH {6} \
    CONFIG.REG_AW {1} \
    CONFIG.REG_W {1} \
    CONFIG.REG_B {1} \
] [get_bd_cells hp3_render_slice]
connect_bd_intf_net [get_bd_intf_pins hp3_render_slice/M_AXI] \
    [get_bd_intf_pins hp3_conv/S_AXI]

# The PS 200 MHz FCLK owns every graphics/control AXI clock. The HP blocks have
# their own asynchronous bridge into the DDR controller, as specified by UG585.
foreach pin [list \
    ps7/S_AXI_HP0_ACLK \
    ps7/S_AXI_HP1_ACLK \
    ps7/S_AXI_HP2_ACLK \
    ps7/S_AXI_HP3_ACLK \
    ps7/M_AXI_GP0_ACLK \
    hp0_conv/aclk \
    hp1_conv/aclk \
    hp2_conv/aclk \
    hp3_conv/aclk \
    hp0_ps_slice/aclk \
    hp0_render_slice/aclk \
    hp1_ps_slice/aclk \
    hp2_ps_slice/aclk \
    hp2_render_slice/aclk \
    hp3_ps_slice/aclk \
    hp3_render_slice/aclk \
] {
    connect_bd_net [get_bd_pins ps7/FCLK_CLK1] [get_bd_pins $pin]
}
create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset graphics_reset
connect_bd_net [get_bd_pins ps7/FCLK_CLK1] \
    [get_bd_pins graphics_reset/slowest_sync_clk]
connect_bd_net [get_bd_pins ps7/FCLK_RESET1_N] \
    [get_bd_pins graphics_reset/ext_reset_in]
connect_bd_net [get_bd_pins graphics_reset/interconnect_aresetn] \
    [get_bd_pins hp0_conv/aresetn]
connect_bd_net [get_bd_pins graphics_reset/interconnect_aresetn] \
    [get_bd_pins hp1_conv/aresetn]
connect_bd_net [get_bd_pins graphics_reset/interconnect_aresetn] \
    [get_bd_pins hp2_conv/aresetn]
connect_bd_net [get_bd_pins graphics_reset/interconnect_aresetn] \
    [get_bd_pins hp3_conv/aresetn]
connect_bd_net [get_bd_pins graphics_reset/interconnect_aresetn] \
    [get_bd_pins hp0_ps_slice/aresetn]
connect_bd_net [get_bd_pins graphics_reset/interconnect_aresetn] \
    [get_bd_pins hp0_render_slice/aresetn]
connect_bd_net [get_bd_pins graphics_reset/interconnect_aresetn] \
    [get_bd_pins hp1_ps_slice/aresetn]
connect_bd_net [get_bd_pins graphics_reset/interconnect_aresetn] \
    [get_bd_pins hp2_ps_slice/aresetn]
connect_bd_net [get_bd_pins graphics_reset/interconnect_aresetn] \
    [get_bd_pins hp2_render_slice/aresetn]
connect_bd_net [get_bd_pins graphics_reset/interconnect_aresetn] \
    [get_bd_pins hp3_ps_slice/aresetn]
connect_bd_net [get_bd_pins graphics_reset/interconnect_aresetn] \
    [get_bd_pins hp3_render_slice/aresetn]

# GP0 is AXI3 in the Zynq PS. Present one AXI4-Lite master to the Astra control
# register block and let the protocol converter own the legal adaptation. A
# fully registered AXI4-Lite boundary isolates protocol conversion from the
# control block's address decode and response mux at 200 MHz.
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_protocol_converter gp0_conv
set_property -dict [list \
    CONFIG.SI_PROTOCOL {AXI3} \
    CONFIG.MI_PROTOCOL {AXI4LITE} \
] [get_bd_cells gp0_conv]
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_register_slice:2.1 \
    gp0_ctrl_slice
set_property -dict [list \
    CONFIG.PROTOCOL {AXI4LITE} \
    CONFIG.READ_WRITE_MODE {READ_WRITE} \
    CONFIG.ADDR_WIDTH {32} \
    CONFIG.DATA_WIDTH {32} \
    CONFIG.REG_AW {1} \
    CONFIG.REG_W {1} \
    CONFIG.REG_B {1} \
    CONFIG.REG_AR {1} \
    CONFIG.REG_R {1} \
] [get_bd_cells gp0_ctrl_slice]
connect_bd_intf_net [get_bd_intf_pins ps7/M_AXI_GP0] \
    [get_bd_intf_pins gp0_conv/S_AXI]
connect_bd_intf_net [get_bd_intf_pins gp0_conv/M_AXI] \
    [get_bd_intf_pins gp0_ctrl_slice/S_AXI]
connect_bd_net [get_bd_pins ps7/FCLK_CLK1] [get_bd_pins gp0_conv/aclk]
connect_bd_net [get_bd_pins ps7/FCLK_CLK1] \
    [get_bd_pins gp0_ctrl_slice/aclk]
connect_bd_net [get_bd_pins graphics_reset/peripheral_aresetn] \
    [get_bd_pins gp0_conv/aresetn]
connect_bd_net [get_bd_pins graphics_reset/peripheral_aresetn] \
    [get_bd_pins gp0_ctrl_slice/aresetn]

make_bd_intf_pins_external -name S_AXI_FB \
    [get_bd_intf_pins hp0_render_slice/S_AXI]
make_bd_intf_pins_external -name S_AXI_SCENE \
    [get_bd_intf_pins hp1_conv/S_AXI]
make_bd_intf_pins_external -name S_AXI_RENDER_READ \
    [get_bd_intf_pins hp2_render_slice/S_AXI]
make_bd_intf_pins_external -name S_AXI_RENDER_WRITE \
    [get_bd_intf_pins hp3_render_slice/S_AXI]
make_bd_intf_pins_external -name M_AXI_CTRL \
    [get_bd_intf_pins gp0_ctrl_slice/M_AXI]

create_bd_port -dir O -type clk fclk_clk0
connect_bd_net [get_bd_ports fclk_clk0] [get_bd_pins ps7/FCLK_CLK0]
create_bd_port -dir O -type clk fclk_clk1
connect_bd_net [get_bd_ports fclk_clk1] [get_bd_pins ps7/FCLK_CLK1]
create_bd_port -dir O graphics_resetn
connect_bd_net [get_bd_ports graphics_resetn] \
    [get_bd_pins graphics_reset/peripheral_aresetn]
create_bd_port -dir I -from 0 -to 0 render_interrupt
connect_bd_net [get_bd_ports render_interrupt] [get_bd_pins ps7/IRQ_F2P]

# UG585 routes the hardened PS I2C controller and one PS GPIO through EMIO.
# Preserve their native interfaces so Vivado creates the required I/O buffers.
make_bd_intf_pins_external -name IIC_0 [get_bd_intf_pins ps7/IIC_0]
make_bd_intf_pins_external -name GPIO_0 [get_bd_intf_pins ps7/GPIO_0]

set hp_busifs [list S_AXI_FB S_AXI_SCENE]
foreach busif $hp_busifs {
    set_property CONFIG.FREQ_HZ $render_frequency_hz [get_bd_intf_ports $busif]
    set_property CONFIG.DATA_WIDTH {64} [get_bd_intf_ports $busif]
    set_property CONFIG.ID_WIDTH {6} [get_bd_intf_ports $busif]
    set_property CONFIG.READ_WRITE_MODE {READ_ONLY} \
        [get_bd_intf_ports $busif]
    # Every scanout engine hard-wires ARSIZE=3 and only emits 64-bit beats.
    # Accurate endpoint metadata lets the converters omit narrow-burst logic.
    set_property CONFIG.SUPPORTS_NARROW_BURST {0} \
        [get_bd_intf_ports $busif]
}
set render_read_busif [get_bd_intf_ports S_AXI_RENDER_READ]
set_property CONFIG.FREQ_HZ $render_frequency_hz $render_read_busif
set_property CONFIG.DATA_WIDTH {64} $render_read_busif
set_property CONFIG.ID_WIDTH {6} $render_read_busif
set_property CONFIG.READ_WRITE_MODE {READ_ONLY} $render_read_busif
set_property CONFIG.SUPPORTS_NARROW_BURST {0} $render_read_busif
set render_write_busif [get_bd_intf_ports S_AXI_RENDER_WRITE]
set_property CONFIG.FREQ_HZ $render_frequency_hz $render_write_busif
set_property CONFIG.DATA_WIDTH {64} $render_write_busif
set_property CONFIG.ID_WIDTH {6} $render_write_busif
set_property CONFIG.READ_WRITE_MODE {WRITE_ONLY} $render_write_busif
set_property CONFIG.SUPPORTS_NARROW_BURST {0} $render_write_busif
set_property CONFIG.FREQ_HZ $render_frequency_hz [get_bd_intf_ports M_AXI_CTRL]
set_property CONFIG.FREQ_HZ {100000000} [get_bd_ports fclk_clk0]
set_property CONFIG.FREQ_HZ $render_frequency_hz [get_bd_ports fclk_clk1]
set_property CONFIG.ASSOCIATED_BUSIF \
    {S_AXI_FB:S_AXI_SCENE:S_AXI_RENDER_READ:S_AXI_RENDER_WRITE:M_AXI_CTRL} \
    [get_bd_ports fclk_clk1]
set_property CONFIG.ASSOCIATED_RESET {graphics_resetn} \
    [get_bd_ports fclk_clk1]

# Both PL readers can address the complete physical DDR aperture. Their RTL
# independently validates every request against the reserved graphics arena.
set fb_space [get_bd_addr_spaces -quiet *S_AXI_FB*]
set scene_space [get_bd_addr_spaces -quiet *S_AXI_SCENE*]
set render_read_space [get_bd_addr_spaces -quiet *S_AXI_RENDER_READ*]
set render_write_space [get_bd_addr_spaces -quiet *S_AXI_RENDER_WRITE*]
foreach space [list $fb_space $scene_space \
                    $render_read_space $render_write_space] {
    if {[llength $space] != 1} {
        puts "ASTRA_ARTY_GRAPHICS ERROR missing external HP address space: $space"
        exit 1
    }
}
assign_bd_address -force -target_address_space $render_read_space \
    [get_bd_addr_segs ps7/S_AXI_HP2/HP2_DDR_LOWOCM] \
    -range 512M -offset 0x00000000
assign_bd_address -force -target_address_space $render_write_space \
    [get_bd_addr_segs ps7/S_AXI_HP3/HP3_DDR_LOWOCM] \
    -range 512M -offset 0x00000000
assign_bd_address -force -target_address_space $fb_space \
    [get_bd_addr_segs ps7/S_AXI_HP0/HP0_DDR_LOWOCM] \
    -range 512M -offset 0x00000000
assign_bd_address -force -target_address_space $scene_space \
    [get_bd_addr_segs ps7/S_AXI_HP1/HP1_DDR_LOWOCM] \
    -range 512M -offset 0x00000000

assign_bd_address -target_address_space [get_bd_addr_spaces ps7/Data] \
    [get_bd_addr_segs M_AXI_CTRL/Reg] -range 64K -offset 0x43C00000

regenerate_bd_layout
validate_bd_design
save_bd_design

set bd_file [get_files -quiet *astra_ps.bd]
generate_target all $bd_file
make_wrapper -files $bd_file -top
set wrapper [lindex [glob [file join $project_dir astra_arty_graphics.gen sources_1 bd astra_ps hdl astra_ps_wrapper.v]] 0]
add_files -norecurse $wrapper

set wrapper_report [open [file join $out_dir wrapper_ports.txt] w]
set wrapper_file [open $wrapper r]
set wrapper_text [read $wrapper_file]
close $wrapper_file
foreach line [split $wrapper_text "\n"] {
    if {[regexp {^[[:space:]]*(input|output|inout)} $line]} {
        puts $wrapper_report [string trim $line]
    }
}
close $wrapper_report

set design_report [open [file join $out_dir block_design.rpt] w]
puts $design_report "FCLK0_MHZ=[get_property CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ [get_bd_cells ps7]]"
puts $design_report "FCLK1_MHZ=[get_property CONFIG.PCW_FPGA1_PERIPHERAL_FREQMHZ [get_bd_cells ps7]]"
puts $design_report "RENDER_FREQUENCY_REQUEST_HZ=$render_frequency_requested_hz"
puts $design_report "RENDER_FREQUENCY_ACTUAL_HZ=$render_frequency_hz"
puts $design_report "IMPLEMENTATION_FREQUENCY_HZ=$implementation_frequency_hz"
puts $design_report "DDR_PORT3_HPR=[get_property CONFIG.PCW_DDR_PORT3_HPR_ENABLE [get_bd_cells ps7]]"
puts $design_report "DDR_HPRLPR_QUEUE=[get_property CONFIG.PCW_DDR_HPRLPR_QUEUE_PARTITION [get_bd_cells ps7]]"
puts $design_report "DDR_CRITICAL_LEVELS LPR=[get_property CONFIG.PCW_DDR_LPR_TO_CRITICAL_PRIORITY_LEVEL [get_bd_cells ps7]] HPR=[get_property CONFIG.PCW_DDR_HPR_TO_CRITICAL_PRIORITY_LEVEL [get_bd_cells ps7]] WRITE=[get_property CONFIG.PCW_DDR_WRITE_TO_CRITICAL_PRIORITY_LEVEL [get_bd_cells ps7]]"
puts $design_report "FABRIC_IRQ ENABLE=[get_property CONFIG.PCW_USE_FABRIC_INTERRUPT [get_bd_cells ps7]] F2P=[get_property CONFIG.PCW_IRQ_F2P_INTR [get_bd_cells ps7]] MODE=[get_property CONFIG.PCW_IRQ_F2P_MODE [get_bd_cells ps7]] INPUTS=[get_property CONFIG.PCW_NUM_F2P_INTR_INPUTS [get_bd_cells ps7]]"
puts $design_report "I2C0 ENABLE=[get_property CONFIG.PCW_I2C0_PERIPHERAL_ENABLE [get_bd_cells ps7]] IO=[get_property CONFIG.PCW_I2C0_I2C0_IO [get_bd_cells ps7]]"
puts $design_report "GPIO_EMIO ENABLE=[get_property CONFIG.PCW_GPIO_EMIO_GPIO_ENABLE [get_bd_cells ps7]] WIDTH=[get_property CONFIG.PCW_GPIO_EMIO_GPIO_IO [get_bd_cells ps7]]"
puts $design_report "IMPLEMENTATION_STRATEGY=[get_property STRATEGY [get_runs impl_1]]"
puts $design_report "INCREMENTAL_CHECKPOINT=$incremental_checkpoint"
puts $design_report "RQS_FILE=$rqs_file"
foreach conv [list hp0_conv hp1_conv hp2_conv hp3_conv] {
    puts $design_report "PROTOCOL_CONVERTER $conv SI_PROTOCOL=[get_property CONFIG.SI_PROTOCOL [get_bd_cells $conv]] MI_PROTOCOL=[get_property CONFIG.MI_PROTOCOL [get_bd_cells $conv]] TRANSLATION_MODE=[get_property CONFIG.TRANSLATION_MODE [get_bd_cells $conv]]"
}
foreach slice [list hp0_ps_slice hp1_ps_slice] {
    puts $design_report "REGISTER_SLICE $slice PROTOCOL=[get_property CONFIG.PROTOCOL [get_bd_cells $slice]] READ_WRITE_MODE=[get_property CONFIG.READ_WRITE_MODE [get_bd_cells $slice]] DATA_WIDTH=[get_property CONFIG.DATA_WIDTH [get_bd_cells $slice]] ID_WIDTH=[get_property CONFIG.ID_WIDTH [get_bd_cells $slice]] REG_AR=[get_property CONFIG.REG_AR [get_bd_cells $slice]] REG_R=[get_property CONFIG.REG_R [get_bd_cells $slice]]"
}
puts $design_report "REGISTER_SLICE hp0_render_slice PROTOCOL=[get_property CONFIG.PROTOCOL [get_bd_cells hp0_render_slice]] READ_WRITE_MODE=[get_property CONFIG.READ_WRITE_MODE [get_bd_cells hp0_render_slice]] DATA_WIDTH=[get_property CONFIG.DATA_WIDTH [get_bd_cells hp0_render_slice]] ID_WIDTH=[get_property CONFIG.ID_WIDTH [get_bd_cells hp0_render_slice]] REG_AR=[get_property CONFIG.REG_AR [get_bd_cells hp0_render_slice]] REG_R=[get_property CONFIG.REG_R [get_bd_cells hp0_render_slice]]"
puts $design_report "REGISTER_SLICE hp2_ps_slice PROTOCOL=[get_property CONFIG.PROTOCOL [get_bd_cells hp2_ps_slice]] READ_WRITE_MODE=[get_property CONFIG.READ_WRITE_MODE [get_bd_cells hp2_ps_slice]] DATA_WIDTH=[get_property CONFIG.DATA_WIDTH [get_bd_cells hp2_ps_slice]] ID_WIDTH=[get_property CONFIG.ID_WIDTH [get_bd_cells hp2_ps_slice]] REG_AR=[get_property CONFIG.REG_AR [get_bd_cells hp2_ps_slice]] REG_R=[get_property CONFIG.REG_R [get_bd_cells hp2_ps_slice]]"
puts $design_report "REGISTER_SLICE hp2_render_slice PROTOCOL=[get_property CONFIG.PROTOCOL [get_bd_cells hp2_render_slice]] READ_WRITE_MODE=[get_property CONFIG.READ_WRITE_MODE [get_bd_cells hp2_render_slice]] DATA_WIDTH=[get_property CONFIG.DATA_WIDTH [get_bd_cells hp2_render_slice]] ID_WIDTH=[get_property CONFIG.ID_WIDTH [get_bd_cells hp2_render_slice]] REG_AR=[get_property CONFIG.REG_AR [get_bd_cells hp2_render_slice]] REG_R=[get_property CONFIG.REG_R [get_bd_cells hp2_render_slice]]"
puts $design_report "REGISTER_SLICE hp3_ps_slice PROTOCOL=[get_property CONFIG.PROTOCOL [get_bd_cells hp3_ps_slice]] READ_WRITE_MODE=[get_property CONFIG.READ_WRITE_MODE [get_bd_cells hp3_ps_slice]] DATA_WIDTH=[get_property CONFIG.DATA_WIDTH [get_bd_cells hp3_ps_slice]] ID_WIDTH=[get_property CONFIG.ID_WIDTH [get_bd_cells hp3_ps_slice]] REG_AW=[get_property CONFIG.REG_AW [get_bd_cells hp3_ps_slice]] REG_W=[get_property CONFIG.REG_W [get_bd_cells hp3_ps_slice]] REG_B=[get_property CONFIG.REG_B [get_bd_cells hp3_ps_slice]]"
puts $design_report "REGISTER_SLICE hp3_render_slice PROTOCOL=[get_property CONFIG.PROTOCOL [get_bd_cells hp3_render_slice]] READ_WRITE_MODE=[get_property CONFIG.READ_WRITE_MODE [get_bd_cells hp3_render_slice]] DATA_WIDTH=[get_property CONFIG.DATA_WIDTH [get_bd_cells hp3_render_slice]] ID_WIDTH=[get_property CONFIG.ID_WIDTH [get_bd_cells hp3_render_slice]] REG_AW=[get_property CONFIG.REG_AW [get_bd_cells hp3_render_slice]] REG_W=[get_property CONFIG.REG_W [get_bd_cells hp3_render_slice]] REG_B=[get_property CONFIG.REG_B [get_bd_cells hp3_render_slice]]"
puts $design_report "REGISTER_SLICE gp0_ctrl_slice PROTOCOL=[get_property CONFIG.PROTOCOL [get_bd_cells gp0_ctrl_slice]] READ_WRITE_MODE=[get_property CONFIG.READ_WRITE_MODE [get_bd_cells gp0_ctrl_slice]] ADDR_WIDTH=[get_property CONFIG.ADDR_WIDTH [get_bd_cells gp0_ctrl_slice]] DATA_WIDTH=[get_property CONFIG.DATA_WIDTH [get_bd_cells gp0_ctrl_slice]] REG_AW=[get_property CONFIG.REG_AW [get_bd_cells gp0_ctrl_slice]] REG_W=[get_property CONFIG.REG_W [get_bd_cells gp0_ctrl_slice]] REG_B=[get_property CONFIG.REG_B [get_bd_cells gp0_ctrl_slice]] REG_AR=[get_property CONFIG.REG_AR [get_bd_cells gp0_ctrl_slice]] REG_R=[get_property CONFIG.REG_R [get_bd_cells gp0_ctrl_slice]]"
foreach port [lsort [get_bd_ports]] {
    puts $design_report "PORT $port DIR=[get_property DIR $port] TYPE=[get_property TYPE $port]"
}
foreach intf [lsort [get_bd_intf_ports]] {
    puts $design_report "INTERFACE $intf MODE=[get_property MODE $intf] FREQ_HZ=[get_property CONFIG.FREQ_HZ $intf] SUPPORTS_NARROW_BURST=[get_property CONFIG.SUPPORTS_NARROW_BURST $intf]"
}
foreach space [list $fb_space $scene_space \
                    $render_read_space $render_write_space] {
    foreach seg [get_bd_addr_segs -of_objects $space] {
        puts $design_report "ADDRESS $space SEG=$seg OFFSET=[get_property OFFSET $seg] RANGE=[get_property RANGE $seg]"
    }
}
close $design_report
write_bd_tcl -force [file join $out_dir astra_ps_generated.tcl]
puts "ASTRA_ARTY_GRAPHICS BD PASS wrapper=$wrapper"

if {[info exists ::env(ASTRA_ARTY_BD_ONLY)] &&
    $::env(ASTRA_ARTY_BD_ONLY) eq "1"} {
    exit 0
}

set graphics_dir [file join $repo_root fpga arty graphics]
set audio_dir [file join $repo_root fpga arty audio]
set hdmi_dir [file join $repo_root third_party hdl-util-hdmi]
add_files -norecurse [list \
    [file join $audio_dir astra_hdmi_audio.sv] \
    [file join $repo_root fpga soc astra_front_panel.sv] \
    [file join $repo_root fpga arty rtl astra_front_panel_axi.sv] \
    [file join $graphics_dir astra_framebuffer_config_validator.sv] \
    [file join $graphics_dir astra_tile_config_validator.sv] \
    [file join $graphics_dir astra_sprite_scene_store.sv] \
    [file join $graphics_dir astra_framebuffer_line_store.sv] \
    [file join $graphics_dir astra_framebuffer_line_builder.sv] \
    [file join $graphics_dir astra_tile_span_walker.sv] \
    [file join $graphics_dir astra_tile_line_store.sv] \
    [file join $graphics_dir astra_tile_line_builder.sv] \
    [file join $graphics_dir astra_sprite_line_store.sv] \
    [file join $graphics_dir astra_premult_blend.sv] \
    [file join $graphics_dir astra_sprite_line_builder.sv] \
    [file join $graphics_dir astra_line_scheduler.sv] \
    [file join $graphics_dir astra_palette_store.sv] \
    [file join $graphics_dir astra_pixel_compositor.sv] \
    [file join $graphics_dir astra_boot_text_overlay.sv] \
    [file join $graphics_dir astra_axi_lite_1to2.sv] \
    [file join $graphics_dir astra_axi_read_3to1.sv] \
    [file join $graphics_dir astra_copper.sv] \
    [file join $graphics_dir astra_copper_control.sv] \
    [file join $graphics_dir astra_copper_beam_scheduler.sv] \
    [file join $graphics_dir astra_copper_registers.sv] \
    [file join $graphics_dir astra_copper_structural_state.sv] \
    [file join $repo_root fpga soc astra_async_fifo.sv] \
    [file join $graphics_dir astra_copper_pixel_events.sv] \
    [file join $graphics_dir astra_graphics_control.sv] \
    [file join $graphics_dir astra_render_protocol.vh] \
    [file join $graphics_dir astra_render_surface_validator.sv] \
    [file join $graphics_dir astra_render_pixel_writer.sv] \
    [file join $graphics_dir astra_render_blitter.sv] \
    [file join $graphics_dir astra_render_geometry.sv] \
    [file join $graphics_dir astra_render_flood.sv] \
    [file join $graphics_dir astra_render_glyph.sv] \
    [file join $graphics_dir astra_render_command_processor.sv] \
    [file join $graphics_dir astra_graphics_pipeline.sv] \
    [file join $repo_root fpga arty rtl astra_arty_graphics_top.sv] \
    [file join $hdmi_dir hdmi.sv] \
    [file join $hdmi_dir hdmi_mode_control.sv] \
    [file join $hdmi_dir tmds_channel.sv] \
    [file join $hdmi_dir serializer.sv] \
    [file join $hdmi_dir packet_assembler.sv] \
    [file join $hdmi_dir packet_picker.sv] \
    [file join $hdmi_dir audio_clock_regeneration_packet.sv] \
    [file join $hdmi_dir audio_info_frame.sv] \
    [file join $hdmi_dir audio_sample_packet.sv] \
    [file join $hdmi_dir auxiliary_video_information_info_frame.sv] \
    [file join $hdmi_dir source_product_description_info_frame.sv] \
]
set boot_font [file join $repo_root fpga soc post_fonts.hex]
add_files -norecurse $boot_font
set_property FILE_TYPE {Memory Initialization Files} [get_files $boot_font]
add_files -fileset constrs_1 -norecurse [list \
    [file join $repo_root fpga arty constraints astra_arty_720p.xdc] \
    [file join $repo_root fpga arty constraints astra_arty_graphics_cdc.xdc] \
]

set_property top astra_arty_graphics_top [current_fileset]
set_property -name {STEPS.SYNTH_DESIGN.ARGS.MORE OPTIONS} \
    -value {-verilog_define SYNTHESIS=1} \
    -objects [get_runs synth_1]
set_property INCREMENTAL_CHECKPOINT "" [get_runs synth_1]
update_compile_order -fileset sources_1

if {[info exists ::env(ASTRA_ARTY_SYNTH_ONLY)] &&
    $::env(ASTRA_ARTY_SYNTH_ONLY) eq "1"} {
    launch_runs synth_1 -jobs 8
    wait_on_run synth_1
    if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {
        puts "ASTRA_ARTY_GRAPHICS ERROR synthesis did not complete"
        puts "STATUS: [get_property STATUS [get_runs synth_1]]"
        exit 1
    }
    open_run synth_1
    report_utilization -file [file join $out_dir synthesis_utilization.rpt]
    report_utilization -hierarchical -hierarchical_depth 8 \
        -file [file join $out_dir synthesis_hierarchical_utilization.rpt]
    report_control_sets -verbose \
        -file [file join $out_dir synthesis_control_sets.rpt]
    write_checkpoint -force \
        [file join $out_dir astra_arty_graphics_synthesized.dcp]
    puts "ASTRA_ARTY_GRAPHICS SYNTHESIS PASS"
    exit 0
}

# Finish every enabled implementation optimization before creating a
# bitstream. Strategies with post-route physical optimization expose that as
# a separate run step; simpler strategies stop at route_design.
set implementation_gate_step route_design
if {[get_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.IS_ENABLED \
        [get_runs impl_1]]} {
    set implementation_gate_step {phys_opt_design (Post-Route)}
}
launch_runs impl_1 -to_step $implementation_gate_step -jobs 8
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {
    puts "ASTRA_ARTY_GRAPHICS ERROR implementation did not complete"
    puts "STATUS: [get_property STATUS [get_runs impl_1]]"
    exit 1
}

open_run impl_1
if {$implementation_frequency_hz != $render_frequency_hz} {
    set render_clock [get_clocks clk_fpga_1]
    set render_clock_source [get_pins [get_property SOURCE_PINS $render_clock]]
    set render_period_ns [expr {1000000000.0 / $render_frequency_hz}]
    create_clock -name clk_fpga_1 -period $render_period_ns \
        $render_clock_source
    puts "ASTRA_ARTY_GRAPHICS restored render clock period_ns=[get_property PERIOD [get_clocks clk_fpga_1]]"
}
report_timing_summary -delay_type min_max -max_paths 50 \
    -file [file join $out_dir timing_summary.rpt]
report_utilization -file [file join $out_dir utilization.rpt]
report_methodology -file [file join $out_dir methodology.rpt]
report_route_status -file [file join $out_dir route_status.rpt]
report_clock_utilization -file [file join $out_dir clock_utilization.rpt]
report_clock_interaction -delay_type min_max \
    -file [file join $out_dir clock_interaction.rpt]
report_cdc -details -file [file join $out_dir cdc.rpt]
write_checkpoint -force [file join $out_dir astra_arty_graphics_routed.dcp]

if {$incremental_checkpoint ne ""} {
    if {[catch {
        report_incremental_reuse -file \
            [file join $out_dir incremental_reuse.rpt]
    } incremental_error]} {
        puts "ASTRA_ARTY_GRAPHICS ERROR incremental reuse report failed: $incremental_error"
        exit 3
    }
}

set route_report [report_route_status -return_string]
if {![regexp {# of routable nets[.]+[[:space:]]*:[[:space:]]*([0-9]+)} \
        $route_report -> routable_nets] ||
    ![regexp {# of fully routed nets[.]+[[:space:]]*:[[:space:]]*([0-9]+)} \
        $route_report -> routed_nets] ||
    ![regexp {# of nets with routing errors[.]+[[:space:]]*:[[:space:]]*([0-9]+)} \
        $route_report -> route_errors]} {
    puts "ASTRA_ARTY_GRAPHICS ERROR unable to parse route status"
    exit 3
}

set setup_path [get_timing_paths -quiet -delay_type max -max_paths 1]
set hold_path [get_timing_paths -quiet -delay_type min -max_paths 1]
if {[llength $setup_path] != 1 || [llength $hold_path] != 1} {
    puts "ASTRA_ARTY_GRAPHICS ERROR missing constrained setup/hold paths"
    exit 3
}
set setup_slack [get_property SLACK $setup_path]
set hold_slack [get_property SLACK $hold_path]
puts "ASTRA_ARTY_GRAPHICS GATE routable=$routable_nets routed=$routed_nets errors=$route_errors setup_slack_ns=$setup_slack hold_slack_ns=$hold_slack"
if {$routable_nets != $routed_nets || $route_errors != 0 ||
    $setup_slack < 0.0 || $hold_slack < 0.0} {
    puts "ASTRA_ARTY_GRAPHICS REJECTED: no bitstream written"
    exit 2
}

close_design
launch_runs impl_1 -to_step write_bitstream -jobs 8
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {
    puts "ASTRA_ARTY_GRAPHICS ERROR bitstream generation did not complete"
    puts "STATUS: [get_property STATUS [get_runs impl_1]]"
    exit 1
}

set bit_file [lindex [glob [file join $project_dir astra_arty_graphics.runs impl_1 *.bit]] 0]
file copy -force $bit_file [file join $out_dir astra_arty_graphics.bit]
write_hw_platform -fixed -include_bit -force \
    [file join $out_dir astra_arty_graphics.xsa]

puts "ASTRA_ARTY_GRAPHICS setup_slack_ns=$setup_slack hold_slack_ns=$hold_slack"
puts "ASTRA_ARTY_GRAPHICS PASS bitstream=[file join $out_dir astra_arty_graphics.bit]"
exit 0
