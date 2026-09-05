echo "Astra 68: configuring FPGA fabric";
if test ${target} = "mmc0"; then
        if mmc rescan; then
                if fatload mmc 0:1 ${loadaddr} astra68.core.rbf; then
                        if fpga load 0 ${loadaddr} ${filesize}; then
                                if bridge enable; then
                                        if fatload mmc 0:1 ${kernel_addr_r} Image; then
                                                if fatload mmc 0:1 ${fdt_addr_r} socfpga_agilex5_de25_nano.dtb; then
                                                        setenv bootargs "console=ttyS0,115200 root=${mmcroot} rw rootwait astra.fabric=ready";
                                                        booti ${kernel_addr_r} - ${fdt_addr_r};
                                                fi;
                                        fi;
                                fi;
                        fi;
                fi;
        fi;
        echo "ASTRA BOOT HALTED: required fabric or Linux artifact failed";
        while true; do sleep 1; done;
fi;
