set target $::ASTRA_TARGET_QSYS
set component $::ASTRA_LPDDR4B_IP
load_system $target

if {[lsearch -exact [get_instances] astra_lpddr4b] >= 0} {
    error "astra_lpddr4b already exists"
}

# The vendor OCM is debug scratch behind hps2fpga. Astra boots from the HPS SD
# card and uses LPDDR4B for its fabric-visible arena, so retaining this unused
# 256 KiB instance consumes 103 of the device's 358 M20Ks for no runtime path.
remove_instance ocm

add_component astra_lpddr4b $component
add_instance astra_lpddr4b_status altera_avalon_pio
set_instance_parameter_value astra_lpddr4b_status width 3
set_instance_parameter_value astra_lpddr4b_status direction Input

# Keep the HPS path observable after a stalled Linux access.  Intel's PMON is
# an AXI4 pass-through monitor with its own System Console JTAG endpoint.
add_instance astra_hps_pmon pmon 4.0.1
set_instance_parameter_value astra_hps_pmon EXPORT_JTAG true
set_instance_parameter_value astra_hps_pmon MONITOR_0_UNIT_ID 1
set_instance_parameter_value astra_hps_pmon MONITOR_0_COUNTER_WIDTH 48
set_instance_parameter_value astra_hps_pmon MONITOR_0_ADVANCED_LAT true
set_instance_parameter_value astra_hps_pmon MONITOR_0_RD_MAX_OUT_TXNS 16
set_instance_parameter_value astra_hps_pmon MONITOR_0_WR_MAX_OUT_TXNS 16
set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_ARADDR_WIDTH 30
set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_AWADDR_WIDTH 30
set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_ARID_WIDTH 4
set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_AWID_WIDTH 4
set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_RDATA_WIDTH 128
set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_WDATA_WIDTH 128
set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_USE_ARUSER true
set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_ARUSER_WIDTH 14
set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_USE_ARREGION true
set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_USE_AWUSER true
set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_AWUSER_WIDTH 14
set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_USE_AWREGION true
set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_USE_BUSER true
set_instance_parameter_value astra_hps_pmon MONITOR_0_MEM_AXI4_BUSER_WIDTH 1
add_connection clk_100.out_clk astra_hps_pmon.clk
add_connection rst_in.out_reset astra_hps_pmon.reset_n
add_connection clk_100.out_clk astra_hps_pmon.csr_clk
add_connection rst_in.out_reset astra_hps_pmon.csr_reset_n
add_connection subsys_hps.hps2fpga astra_hps_pmon.sink_axi4
set_connection_parameter_value \
    subsys_hps.hps2fpga/astra_hps_pmon.sink_axi4 baseAddress 0x40000000
add_connection astra_hps_pmon.src_axi4 astra_lpddr4b.s0_axi4
set_connection_parameter_value \
    astra_hps_pmon.src_axi4/astra_lpddr4b.s0_axi4 baseAddress 0x0
set_connection_parameter_value \
    astra_hps_pmon.src_axi4/astra_lpddr4b.s0_axi4 qsys_mm.enableOutOfOrderSupport true

add_connection subsys_hps.lwhps2fpga astra_lpddr4b_status.s1
set_connection_parameter_value subsys_hps.lwhps2fpga/astra_lpddr4b_status.s1 baseAddress 0x20000
add_connection clk_100.out_clk astra_lpddr4b.s0_axi4_clock_in
add_connection clk_100.out_clk astra_lpddr4b.s0_axi4lite_clock
add_connection clk_100.out_clk astra_lpddr4b_status.clk
add_connection rst_in.out_reset astra_lpddr4b.s0_axi4lite_reset_n
add_connection rst_in.out_reset astra_lpddr4b_status.reset

add_instance astra_control_bridge altera_axi_bridge
set_instance_parameter_value astra_control_bridge AXI_VERSION AXI4-Lite
set_instance_parameter_value astra_control_bridge ADDR_WIDTH 16
set_instance_parameter_value astra_control_bridge DATA_WIDTH 32
add_connection clk_100.out_clk astra_control_bridge.clk
add_connection rst_in.out_reset astra_control_bridge.clk_reset
add_connection subsys_hps.lwhps2fpga astra_control_bridge.s0
set_connection_parameter_value \
    subsys_hps.lwhps2fpga/astra_control_bridge.s0 baseAddress 0x100000
add_interface astra_control axi4lite start
set_interface_property astra_control EXPORT_OF astra_control_bridge.m0

proc add_memory_bridge {name exported read write id_width} {
    add_instance $name altera_axi_bridge
    set_instance_parameter_value $name AXI_VERSION AXI4
    set_instance_parameter_value $name ADDR_WIDTH 32
    set_instance_parameter_value $name DATA_WIDTH 64
    set_instance_parameter_value $name S0_ID_WIDTH $id_width
    set_instance_parameter_value $name M0_ID_WIDTH $id_width
    set_instance_parameter_value $name ENABLE_AXI4_READ_ONLY_INTERFACE $read
    set_instance_parameter_value $name ENABLE_AXI4_WRITE_ONLY_INTERFACE $write
    add_connection clk_100.out_clk $name.clk
    add_connection rst_in.out_reset $name.clk_reset
    add_connection $name.m0 astra_lpddr4b.s0_axi4
    set_connection_parameter_value \
        $name.m0/astra_lpddr4b.s0_axi4 baseAddress 0x40000000
    # LPDDR4B may return responses across manager IDs out of command order.
    set_connection_parameter_value \
        $name.m0/astra_lpddr4b.s0_axi4 qsys_mm.enableOutOfOrderSupport true
    add_interface $exported axi4 end
    set_interface_property $exported EXPORT_OF $name.s0
}

add_memory_bridge astra_fb_bridge astra_fb 1 0 1
add_memory_bridge astra_scene_bridge astra_scene 1 0 2
add_memory_bridge astra_render_bridge astra_render 1 1 3

foreach {name type role endpoint} {
    astra_lpddr4b_core_init_n reset end astra_lpddr4b.core_init_n
    astra_lpddr4b_ctrl_ready reset start astra_lpddr4b.s0_axi4_ctrl_ready
    astra_lpddr4b_calibration axi4lite end astra_lpddr4b.s0_axi4lite
    astra_lpddr4b_status conduit end astra_lpddr4b_status.external_connection
    astra_lpddr4b_mem conduit end astra_lpddr4b.mem_0
    astra_lpddr4b_mem_ck conduit end astra_lpddr4b.mem_ck_0
    astra_lpddr4b_mem_reset_n conduit end astra_lpddr4b.mem_reset_n
    astra_lpddr4b_oct conduit end astra_lpddr4b.oct_0
    astra_lpddr4b_ref_clk clock end astra_lpddr4b.ref_clk
} {
    add_interface $name $type $role
    set_interface_property $name EXPORT_OF $endpoint
}

save_system $target
