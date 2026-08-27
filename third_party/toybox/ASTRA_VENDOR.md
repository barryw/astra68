# Toybox networking sources

Astra's SNTP packet/clock math and `ping` command are ports of Toybox 0.8.14
commit `b7ec52ac35e075caffca5d330995d44e8dbfc8c3`:

- `toys/net/sntp.c`
- `toys/net/ping.c`

Copyright 2006, 2019 Rob Landley. Toybox grants permission to use, copy,
modify, and distribute the software for any purpose, with or without fee, and
provides it without warranty (the 0BSD license). The complete upstream license
is at <https://github.com/landley/toybox/blob/b7ec52ac35e075caffca5d330995d44e8dbfc8c3/LICENSE>.

The ports replace Toybox's command framework and Linux-specific helpers with
Astra's POSIX and service APIs. Packet validation, NTP era conversion,
four-timestamp offset calculation, ICMP checksum, sequence, and timing behavior
remain traceable to the cited upstream files.
