# Harte harness — build log

Every row is one verified flash via `build_flash.sh`. The device self-reports its `BUILD_ID`
(SHA-1 prefix of all RTL+firmware source) over UART (`CMD_ID`), so any loaded board maps back
to its exact source + the fixes/features below. Query the live board any time:
`python3 sw/harte/host/whatsloaded.py`.

| BUILD_ID | git | astra.bit sha | when (UTC) | fixes / features |
|----------|-----|---------------|------------|------------------|
