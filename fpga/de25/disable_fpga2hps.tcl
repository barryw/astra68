# Astra has no FPGA-to-HPS manager: add_lpddr4b.tcl removes the vendor JTAG
# master and ACE5-Lite translator that were its only user.  An enabled but
# undriven FPGA2HPS port is illegal on the MPFE primitive (Error 129015), so
# disable it in the HPS IP (data width 0 is the IP's "disabled" setting) and
# drop the subsystem exports that carried it.
load_system hps_subsys/hps_subsys.qsys
load_component agilex_hps
if {[get_component_parameter_value f2s_data_width] != 256} {
    error "agilex_hps f2s_data_width is not the vendor 256"
}
set_component_parameter_value f2s_data_width 0
save_component

# Reloading the HPS component drops its FPGA2HPS interfaces, which removes
# the subsystem exports that referenced them; save and confirm none remain.
save_system
load_system hps_subsys/hps_subsys.qsys
foreach export {fpga2hps fpga2hps_clk fpga2hps_rst} {
    if {[lsearch -exact [get_interfaces] $export] >= 0} {
        error "hps_subsys still exports $export"
    }
}
