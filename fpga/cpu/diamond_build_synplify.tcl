# Astra 68 — WF68K30L Fmax check via Synplify Pro (vs LSE), target LFE5U-85F.
# Run on beast from ~/astra_cpu/prj_syn:
#   export bindir=~/diamond/3.14/bin/lin64; source $bindir/diamond_env
#   $bindir/diamondc diamond_build_synplify.tcl
set core ../wf68k30L
prj_project new -name wf68ksyn -impl impl1 -dev LFE5U-85F-6BG381C -synthesis synplify
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
prj_strgy set_value -strategy Strategy1 \
    PROP_SYN_EdfFrequency=50 \
    PROP_SYN_EdfRunRetiming=True \
    PROP_MAP_TimingDriven=True \
    PROP_MAP_RegRetiming=True \
    PROP_MAP_TimingDrivenPack=True \
    PROP_PAR_parPathBased=On \
    PROP_PAR_EffortParDes=5
prj_project save
prj_run Synthesis -impl impl1 -forceOne
prj_run Map -impl impl1 -forceOne
prj_run PAR -impl impl1 -forceOne
prj_project close
