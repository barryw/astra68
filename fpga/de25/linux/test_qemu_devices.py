#!/usr/bin/env python3
"""Run on the DE25 as the QEMU user, with supplementary groups cleared."""

import errno
import mmap
import os
import struct

assert os.getuid() != 0 and os.getgid() != 0
assert not os.getgroups()
for path in ('/dev/mem', '/dev/astra-graphics-arena', '/dev/astra-display-capture'):
    try:
        fd = os.open(path, os.O_RDWR)
    except PermissionError:
        pass
    else:
        os.close(fd)
        raise AssertionError(f'QEMU user can open privileged device {path}')
for path, limit in (('/dev/astra-front-panel', 4096),
                    ('/dev/astra-host-arena', 8 * 1024 * 1024)):
    fd = os.open(path, os.O_RDWR)
    with mmap.mmap(fd, limit, flags=mmap.MAP_SHARED,
                   prot=mmap.PROT_READ | mmap.PROT_WRITE) as region:
        if path.endswith('front-panel'):
            assert struct.unpack_from('<I', region)[0] == 0x504e4c30
    for length, offset in ((limit + 4096, 0), (4096, limit)):
        try:
            region = mmap.mmap(fd, length, flags=mmap.MAP_SHARED,
                prot=mmap.PROT_READ | mmap.PROT_WRITE, offset=offset)
        except OSError as error:
            # dma_direct_mmap uses ENXIO for bounds; the MMIO driver uses EINVAL.
            expected = errno.ENXIO if path.endswith('host-arena') else errno.EINVAL
            assert error.errno == expected, (path, error)
        else:
            region.close()
            raise AssertionError(f'out-of-range mapping accepted: {path}')
    os.close(fd)
print(f'ASTRA-QEMU-DEVICE-BOUNDS PASS uid={os.getuid()} gid={os.getgid()}')
