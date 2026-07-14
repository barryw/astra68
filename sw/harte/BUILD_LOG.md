# Harte harness build log

Every row is one verified hardware load. The device self-reports its `BUILD_ID`
(SHA-1 prefix of all RTL+firmware source) over UART (`CMD_ID`), so any loaded board maps back
to its exact source + the fixes/features below. Query the live board any time:
`python3 sw/harte/host/whatsloaded.py`.

| BUILD_ID | git | astra.bit sha | when (UTC) | fixes / features |
|----------|-----|---------------|------------|------------------|
| `0x908ea94e` | dirty | bit:15b724148b57 | 2026-07-11 | TG030+PMMU, SDRAM path present, 12.5 MHz CPU, 115200 UART; hardware 5/5 directed ALU and 8,065/8,065 NOP vectors passed [CPU_CORE=tg68k030 CPU_CLK_DIV_BIT=0 SDRAM_ENABLE=1 HDMI_ENABLE=0 TARGET_FREQ_MHZ=12.5] |
| `0x589cf9ea` | `12b784e5`+dirty | sha256:5b2bcf5c9d8843609a287bb52afcf4d5855b8b59151a2c3b5f949213655c53aa | 2026-07-14 | Exact `tg68k030_mmu2`, 12.5 MHz CPU, SDRAM+HDMI, 460800 UART and 128-byte RX FIFO; final mapped netlist 0 SCCs, routed CPU Fmax 12.74 MHz; maintained `SingleStepTests/m68000@64b2531` hardware gate 96,103/96,103 pass, corpus sha256:29d2b6343bd7, runner sha256:72e64b91ddf0, report sha256:e5ce5d87dd3e [CPU_CORE=tg68k030_mmu2 CPU_CLK_DIV_BIT=0 SDRAM_ENABLE=1 HDMI_ENABLE=1 UART_BAUD=460800 TARGET_FREQ_MHZ=12.5 PNR_SEED=4] |
