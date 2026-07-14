GHDL ?= ghdl
GHDL_FLAGS := --std=08 -fsynopsys
BUILD_DIR ?= build
TARGET := $(BUILD_DIR)/tb_astra_conformance
CORE_DIR := ../../fpga/cpu/tg68k_c_030_mmu2
SOURCES := \
	$(CORE_DIR)/TG68K_Pack.vhd \
	$(CORE_DIR)/TG68K_ALU.vhd \
	$(CORE_DIR)/TG68K_PMMU_030.vhd \
	$(CORE_DIR)/TG68K_Cache_030.vhd \
	$(CORE_DIR)/TG68K_CacheCtrl_030.vhd \
	$(CORE_DIR)/TG68KdotC_Kernel.vhd \
	tb_astra_conformance.vhd
ANALYZED := $(BUILD_DIR)/.analyzed

.PHONY: all clean

all: $(TARGET)

$(ANALYZED): $(SOURCES)
	mkdir -p $(BUILD_DIR)
	$(GHDL) -a $(GHDL_FLAGS) --workdir=$(BUILD_DIR) $(SOURCES)
	touch $@

$(TARGET): $(ANALYZED)
	$(GHDL) -e $(GHDL_FLAGS) --workdir=$(BUILD_DIR) -o $@ tb_astra_conformance

clean:
	rm -rf $(BUILD_DIR)
