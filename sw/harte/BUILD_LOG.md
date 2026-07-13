# Harte harness — build log

Every row is one verified flash via `build_flash.sh`. The device self-reports its `BUILD_ID`
(SHA-1 prefix of all RTL+firmware source) over UART (`CMD_ID`), so any loaded board maps back
to its exact source + the fixes/features below. Query the live board any time:
`python3 sw/harte/host/whatsloaded.py`.

| BUILD_ID | git | astra.bit sha | when (UTC) | fixes / features |
|----------|-----|---------------|------------|------------------|
| `0x908ea94e` | dirty | bit:15b724148b57 | 2026-07-11 | TG030+PMMU, SDRAM path present, 12.5 MHz CPU, 115200 UART; hardware 5/5 directed ALU and 8,065/8,065 NOP vectors passed [CPU_CORE=tg68k030 CPU_CLK_DIV_BIT=0 SDRAM_ENABLE=1 HDMI_ENABLE=0 TARGET_FREQ_MHZ=12.5] |
