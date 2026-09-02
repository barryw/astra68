#!/usr/bin/env python3

from pathlib import Path


script = (Path(__file__).parent / "rootfs-overlay/etc/init.d/astra-firstboot") \
    .read_text(encoding="ascii")

data_wait = script[script.index("while ! mountpoint -q /data"):]
data_wait = data_wait[:data_wait.index("done")]
assert "-lt" not in data_wait and "sleep" in data_wait

network = script[script.index("bring_up_network()") :]
network = network[:network.index("\n}")]
steps = ("ifdown -f eth0", "ip link set dev eth0 up",
         "/sys/class/net/eth0/carrier", "ifup eth0")
positions = [network.index(step) for step in steps]
assert positions == sorted(positions)

print("Astra firstboot dependency tests passed")
