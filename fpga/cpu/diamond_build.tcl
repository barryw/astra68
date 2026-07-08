# Astra 68 — WF68K30L cost check via Lattice Diamond (LSE), target LFE5U-85F.
# Diamond's commercial-grade synthesis handles the core's mixed clocked/comb
# VHDL that the open ghdl flow crashes on (see fpga/cpu/synth.sh).
#
# Run on the Diamond host (beast), from ~/astra_cpu/prj:
#   export bindir=~/diamond/3.14/bin/lin64; source $bindir/diamond_env
#   $bindir/diamondc diamond_build.tcl
# Requires a Diamond license.dat in ~/diamond/3.14/license/.
#
# Reports: Map (LUT/FF/EBR/DSP utilisation) + PAR (Fmax) in impl1/.

set core ../wf68k30L
if {[file exists wf68k.ldf]} {
    prj_project open wf68k.ldf
} else {
    prj_project new -name wf68k -impl impl1 -dev LFE5U-85F-6BG381C -synthesis lse
    prj_src add $core/wf68k30L_pkg.vhd \
                $core/wf68k30L_address_registers.vhd \
                $core/wf68k30L_data_registers.vhd \
                $core/wf68k30L_alu.vhd \
                $core/wf68k30L_opcode_decoder.vhd \
                $core/wf68k30L_bus_interface.vhd \
                $core/wf68k30L_exception_handler.vhd \
                $core/wf68k30L_control.vhd \
                $core/wf68k30L_top.vhd
    prj_impl option top WF68K30L_TOP
    prj_project save
}
prj_run Synthesis -impl impl1
prj_run Map -impl impl1
prj_run PAR -impl impl1
prj_project close
