#!/usr/bin/env python3
"""Cut a disposable Linux VM, including its filesystem/page cache, beneath Astra.

The inner MC68040 QEMU, provider, native probe and recovery checks are unchanged
production mechanisms. Both QEMUs run without root privilege. No host block
device is accepted; only generated regular-file disks enter the Linux VM.
"""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import select
import shutil
import signal
import socket
import stat
import struct
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PAYLOAD = "Astra-native-durable-68040-v84"
BASELINE = "previously-acknowledged-baseline"
LABELS = {"fat32": "CUT_FAT32", "exfat": "CUT_EXFAT"}
OUTER_LABEL = 'ASTRA_POWERCUT'
MARKER = "ASTRA-POWER-"


def module(filename):
    spec = importlib.util.spec_from_file_location(filename.replace('-', '_'), HERE / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            value.update(block)
    return value.hexdigest()


def verdict(phase, value, baseline):
    if phase not in ('ready', 'written', 'synced'):
        raise RuntimeError('invalid power-cut phase')
    if baseline != "EXACT":
        raise RuntimeError("previous acknowledged baseline was lost")
    if phase == "synced" and value != "EXACT":
        raise RuntimeError("acknowledged data was lost: " + value)
    if phase != "synced" and value != "UNACKNOWLEDGED":
        raise RuntimeError("invalid unacknowledged recovery result")


def checked(command, **kwargs):
    return subprocess.run(list(map(str, command)), check=True, **kwargs)


def filesystem_check(command, repair):
    result = subprocess.run(list(map(str, command)), stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    print(result.stdout, end='', flush=True)
    if result.returncode not in ((0, 1) if repair else (0,)):
        raise RuntimeError("filesystem check failed: %r status %u" % (command, result.returncode))


def worker():
    # This guard precedes every mount, sysctl or block-device operation.
    cmdline = dict(word.split('=', 1) for word in Path('/proc/cmdline').read_text().split() if '=' in word)
    if os.geteuid() != 0 or cmdline.get('astra_power_vm') != '1':
        raise RuntimeError("worker runs only in its disposable Linux VM")
    mode, target, phase = (cmdline['astra_' + name] for name in ('mode', 'target', 'phase'))
    if mode not in ('cut', 'recover', 'baseline') or target not in ('system', 'fat32', 'exfat') or phase not in ('ready', 'written', 'synced'):
        raise RuntimeError("invalid VM test selection")
    outer = subprocess.check_output(['blkid', '-s', 'LABEL', '-o', 'value', '/dev/vda'], text=True).strip()
    assert outer == OUTER_LABEL, outer
    checked(['mount', '/dev/vda', '/data'])
    for name, permissions in (('/data/tmp', 0o1777), ('/data/session', 0o700), ('/data/system.img', 0o600)):
        os.chown(name, 0, 0)
        os.chmod(name, permissions)
    checked(['mount', '--bind', '/data/tmp', '/var/tmp'])
    Path('/proc/sys/vm/dirty_writeback_centisecs').write_text('0\n')
    Path('/proc/sys/vm/dirty_expire_centisecs').write_text('360000\n')
    terminal = module('test-terminal.py')
    semantics = module('test-fs-semantics.py')
    from qemu_runtime import storage_fixture, require_provider_abi
    from storage_image import image_window
    image = Path('/data/system.img')
    devices = {}
    end = time.monotonic() + 20
    while time.monotonic() < end and set(devices) != set(LABELS):
        for device in Path('/sys/class/block').glob('sd*'):
            path = '/dev/' + device.name
            info = subprocess.run(['blkid', '-o', 'export', path], capture_output=True, text=True)
            values = dict(line.split('=', 1) for line in info.stdout.splitlines() if '=' in line)
            for kind, label in LABELS.items():
                if values.get('LABEL') == label:
                    devices[kind] = path
        if set(devices) != set(LABELS):
            time.sleep(.1)
    assert set(devices) == set(LABELS), devices
    for device in devices.values():
        properties = subprocess.check_output(['udevadm', 'info', '--query=property', '--name', device], text=True)
        print(properties, flush=True)
        assert 'ID_BUS=usb\n' in properties, 'udev must identify the actual USB transport'
    if mode == 'recover':
        offset, size, _ = image_window(image)
        loop = subprocess.check_output(['losetup', '--find', '--show', '--offset', str(offset),
            '--sizelimit', str(size), str(image)], text=True).strip()
        try:
            filesystem_check(['e2fsck', '-p', loop], True)
        finally:
            checked(['losetup', '-d', loop])
        filesystem_check(['fsck.fat', '-a', devices['fat32']], True)
        filesystem_check(['fsck.exfat', '-p', devices['exfat']], True)
    # Commit only the initial fixture. No global sync occurs after writes arm.
    os.sync()
    control = Path('/tmp/control')
    control.mkdir()
    os.chown(control, 1000, 1000)
    os.environ['ASTRA_TEST_QEMU_UID'] = '1000'
    os.environ['ASTRA_TEST_QEMU_GID'] = '1000'
    require_provider_abi('/opt/astra/qemu', '/usr/local/libexec/astra-storage')
    launcher, owner = storage_fixture(image, control, exports='/data/session/exports',
        credentials=(1000, 1000), discovery='udev')
    machine = terminal.Machine('/opt/astra/qemu', '/opt/astra/rom', str(image), str(control),
        launcher=launcher, socket_owner=owner, extra_args=('-pidfile', '/tmp/native.pid'))
    finished = False
    step = 0

    def wait(text, before, timeout=90):
        lines, highest = machine.wait_for_text(text, timeout, before)
        if lines is None:
            raise RuntimeError('native marker absent: %r\n%s' % (text, '\n'.join(machine.said(before)[0][-35:])))
        return lines, highest

    def command(text):
        nonlocal step
        step += 1
        lines, status = semantics.command(machine, text, 90, step)
        assert status == 0, (text, status, lines[-30:])
        return lines

    def check(path, payload):
        lines = command('posix --durability-check ' + path + ' ' + payload)
        outcomes = [line.rsplit(' ', 1)[-1] for line in lines if line in (
            'ASTRA DURABILITY EXACT', 'ASTRA DURABILITY ABSENT')]
        assert len(outcomes) == 1, lines
        return outcomes[0]

    try:
        assert terminal.open_terminal(machine, 120, 90), 'native Terminal unavailable'
        pid = int(Path('/tmp/native.pid').read_text())
        status = dict(line.split(':', 1) for line in Path('/proc/%u/status' % pid).read_text().splitlines() if ':' in line)
        assert list(map(int, status['Uid'].split())) == [1000] * 4
        assert list(map(int, status['Gid'].split())) == [1000] * 4
        assert not status['Groups'].strip() and status['NoNewPrivs'].strip() == '1'
        assert all(int(status[key], 16) == 0 for key in ('CapInh', 'CapPrm', 'CapEff', 'CapBnd', 'CapAmb'))
        print(MARKER + 'NATIVE-UID1000-CAPS0', flush=True)
        if mode == 'baseline':
            step += 1
            lines, result = semantics.command(machine, 'ntp offline.invalid', 30, step)
            assert result != 0 and any('ntp: could not resolve the time server' in line for line in lines), lines
            print(MARKER + 'OFFLINE-NTP-CONTROL-PASS', flush=True)
        command('service disable media; service stop media; service disable remote-desktop; service stop remote-desktop')
        rows = [line.split() for line in command('volumes --all')]
        paths = {'system': '/home'}
        for kind, label in LABELS.items():
            found = [row for row in rows if len(row) >= 11 and row[1:3] == ['mount', 'mounted'] and row[-1] == label]
            assert len(found) == 1 and found[0][5] == 'rw', (kind, rows)
            paths[kind] = found[0][3]
        path = paths[target] + '/POWER-CUT.TXT'
        baseline = paths[target] + '/POWER-BASELINE.TXT'
        if mode == 'cut':
            assert check(path, PAYLOAD) == 'ABSENT'
            command('posix --durability ' + baseline + ' ' + BASELINE)
            before = machine.sequence()
            machine.qmp.type_line('posix --durability-gated ' + path + ' ' + PAYLOAD)
            _, before = wait(('ASTRA DURABILITY GATE 2', 'ASTRA DURABILITY READY'), before)
            if phase in ('written', 'synced'):
                machine.qmp.key('ret')
                _, before = wait('ASTRA DURABILITY WRITTEN', before)
            if phase == 'synced':
                machine.qmp.key('ret')
                wait('ASTRA DURABILITY SYNCED', before)
            machine.qmp.execute('stop')
            print(MARKER + 'CUT ' + json.dumps({'target': target, 'phase': phase, 'path': path}), flush=True)
            # Host kills the complete Linux VM. Do not close/sync on this path.
            while True:
                time.sleep(1)
        elif mode == 'baseline':
            for kind, directory in paths.items():
                name = directory + '/POWER-CUT.TXT'
                before = machine.sequence()
                machine.qmp.type_line('posix --durability-gated ' + name + ' ' + PAYLOAD + '; print POWER-BASELINE-DONE-$?')
                wait('ASTRA DURABILITY GATE 2', before)
                for stage in ('READY', 'WRITTEN', 'SYNCED'):
                    _, before = wait('ASTRA DURABILITY ' + stage, before)
                    machine.qmp.key('ret')
                wait('POWER-BASELINE-DONE-0', before)
                assert check(name, PAYLOAD) == 'EXACT'
        else:
            previous = check(baseline, BASELINE)
            value = check(path, PAYLOAD) if phase in ('ready', 'synced') else 'UNACKNOWLEDGED'
            if phase == 'ready':
                assert value == 'ABSENT', value
            print(MARKER + 'RECOVER ' + json.dumps({'baseline': previous, 'value': value}), flush=True)
        assert machine.settle()
        machine.qmp.type_line('shutdown')
        assert machine.process.wait(timeout=90) == 88
        machine.close()
        finished = True
        offset, size, _ = image_window(image)
        loop = subprocess.check_output(['losetup', '--find', '--show', '--read-only',
            '--offset', str(offset), '--sizelimit', str(size), str(image)], text=True).strip()
        try:
            filesystem_check(['e2fsck', '-fn', loop], False)
        finally:
            checked(['losetup', '-d', loop])
        filesystem_check(['fsck.fat', '-n', devices['fat32']], False)
        filesystem_check(['fsck.exfat', '-n', devices['exfat']], False)
        print(MARKER + 'FSCK-PASS', flush=True)
    finally:
        if not finished:
            machine.close()


def copy_binary(source, root, destination=None, seen=None):
    """Copy one ELF plus the libraries reported by the installed loader."""
    source = Path(source)
    destination = Path(destination or str(source).lstrip('/'))
    target = root / destination
    if seen is None:
        seen = set()
    key = (str(source), str(destination))
    if key in seen:
        return
    seen.add(key)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target, follow_symlinks=True)
    result = subprocess.run(['ldd', str(source)], capture_output=True, text=True)
    assert 'not found' not in result.stdout, result.stdout
    for library in re.findall(r'(/[^\s()]+)', result.stdout):
        if Path(library).is_file():
            copy_binary(library, root, seen=seen)


def prepare(args):
    work = args.output
    work.mkdir(parents=True)
    root = work / 'initramfs'
    root.mkdir()
    for name in ('dev', 'proc', 'sys', 'run', 'tmp', 'var/tmp', 'data', 'etc', 'usr/bin'):
        (root / name).mkdir(parents=True, exist_ok=True)
    (root / 'tmp').chmod(0o1777)
    (root / 'var/tmp').chmod(0o1777)
    seen = set()
    binaries = ('/usr/bin/busybox', '/usr/bin/python3', '/usr/bin/setpriv', '/usr/bin/mount',
        '/usr/bin/umount', '/usr/sbin/blkid', '/usr/sbin/losetup', '/usr/sbin/e2fsck',
        '/usr/sbin/fsck.fat', '/usr/sbin/fsck.exfat', '/usr/sbin/modprobe',
        '/usr/bin/udevadm', '/usr/lib/systemd/systemd-udevd')
    for binary in binaries:
        copy_binary(binary, root, seen=seen)
    copy_binary('/usr/bin/busybox', root, 'bin/busybox', seen)
    (root / 'bin/sh').symlink_to('busybox')
    for applet in ('mkdir', 'sleep', 'mount', 'sync', 'poweroff'):
        (root / 'bin' / applet).symlink_to('busybox')
    version = '%u.%u' % sys.version_info[:2]
    stdlib = Path('/usr/lib/python' + version)
    shutil.copytree(stdlib, root / stdlib.relative_to('/'),
        ignore=shutil.ignore_patterns('__pycache__', 'test', 'tests', 'idlelib', 'tkinter', 'config-*'))
    for library in (stdlib / 'lib-dynload').glob('*.so'):
        if '_tkinter' not in library.name:
            copy_binary(library, root, seen=seen)
    for helper in ('ata_id', 'scsi_id'):
        copy_binary('/usr/lib/udev/' + helper, root, seen=seen)
    rules = root / 'usr/lib/udev/rules.d'
    rules.mkdir(parents=True)
    shutil.copy2('/usr/lib/udev/rules.d/60-persistent-storage.rules', rules)
    modules = Path('/lib/modules') / args.kernel_release
    target = root / modules.relative_to('/')
    target.mkdir(parents=True)
    for path in modules.glob('modules.*'):
        if path.is_file():
            shutil.copy2(path, target)
    drivers = ('loop', 'virtio_pci', 'virtio_blk', 'ext4', 'vfat', 'exfat', 'usb_storage', 'xhci_pci', 'nls_cp437', 'nls_iso8859_1')
    for driver in drivers:
        output = subprocess.check_output(['/usr/sbin/modprobe', '-S', args.kernel_release, '--show-depends', driver], text=True)
        for path in re.findall(r'^insmod (\S+)', output, re.M):
            destination = root / path.lstrip('/')
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
    gate = root / 'opt/astra'
    gate.mkdir(parents=True)
    for directory in ('emu/qemu', 'tools'):
        (gate / directory).mkdir(parents=True)
        for path in (ROOT / directory).glob('*.py'):
            shutil.copy2(path, gate / directory)
    for relative in ('sw/kernel/trace.h',):
        destination = gate / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / relative, destination)
    shutil.copytree(ROOT / 'sw/include/astra', gate / 'sw/include/astra')
    for relative in ('supervisor/build/m68k/astra_supervisor.elf',
                     'services/terminal/build/m68k/terminal.elf',
                     'services/desktop/build/m68k/desktop.elf'):
        source = ROOT / 'sw/userspace' / relative
        destination = gate / 'sw/userspace' / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    copy_binary(args.qemu, root, 'opt/astra/qemu', seen)
    copy_binary(args.provider, root, 'usr/local/libexec/astra-storage', seen)
    copy_binary('/usr/local/libexec/astra-codec-worker', root, seen=seen)
    shutil.copy2(args.rom, gate / 'rom')
    (root / 'usr/local/sbin').mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / 'tools/astra-qemu-root', root / 'usr/local/sbin/astra-qemu-root')
    (root / 'sbin').symlink_to('usr/sbin')
    (root / 'etc/mtab').symlink_to('/proc/self/mounts')
    (root / 'etc/passwd').write_text('root:x:0:0:root:/root:/bin/sh\nastra:x:1000:1000:astra:/tmp:/bin/sh\n')
    (root / 'etc/group').write_text('root:x:0:\ndisk:x:6:\nastra:x:1000:\n')
    (root / 'etc/nsswitch.conf').write_text('passwd: files\ngroup: files\nhosts: files\n')
    (root / 'init').write_text('#!/bin/sh\n/bin/mount -t devtmpfs devtmpfs /dev\nexec </dev/console >/dev/console 2>&1\nset -eu\nexport PATH=/usr/sbin:/usr/bin:/sbin:/bin\nmount -t proc proc /proc\nmount -t sysfs sysfs /sys\nmount -t tmpfs tmpfs /run\n' +
        '\n'.join('/usr/sbin/modprobe ' + driver for driver in drivers) + '\n' +
        '/usr/lib/systemd/systemd-udevd --daemon\nudevadm trigger --action=add --subsystem-match=block\nudevadm settle --timeout=20\n' +
        'if python3 -u /opt/astra/emu/qemu/test-storage-power-cuts.py --worker; then\n sync\n poweroff -f\nelse\n echo ASTRA-POWER-FAIL\n poweroff -n -f\nfi\n')
    (root / 'init').chmod(0o755)
    with (work / 'initramfs.cpio.gz').open('wb') as output:
        result = subprocess.run(['bash', '-o', 'pipefail', '-c',
            'find . -print0 | cpio --null -o --format=newc --owner=0:0 | gzip -1'], cwd=root, stdout=output)
        assert result.returncode == 0
    seed = work / 'seed'
    seed.mkdir()
    (seed / 'tmp').mkdir(mode=0o1777)
    (seed / 'session').mkdir()
    shutil.copyfile(args.image, seed / 'system.img')
    # Install only the changed probe and offline-boot fix, not a new OS image.
    image_tools = module('astra_image.py')
    image_tools.install_test_command(str(seed / 'system.img'), 'posix')
    image_tools.install_service(str(seed / 'system.img'), 'ntpd')
    for kind, size in (('system', 1024), ('fat32', 64), ('exfat', 64)):
        image = work / (kind + '-base.img')
        with image.open('wb') as output:
            output.truncate(size << 20)
        if kind == 'system':
            checked(['/usr/sbin/mkfs.ext4', '-q', '-F', '-O', '^orphan_file', '-L', OUTER_LABEL, '-d', seed, image])
        elif kind == 'fat32':
            checked(['/usr/sbin/mkfs.fat', '-F', '32', '-n', LABELS[kind], image])
        else:
            checked(['/usr/sbin/mkfs.exfat', '-L', LABELS[kind], image])
    manifest = {name: {'path': str(path), 'sha256': digest(path)} for name, path in (
        ('qemu', args.qemu), ('provider', args.provider), ('rom', args.rom), ('image', args.image),
        ('linux_kernel', args.linux_kernel), ('linux_qemu', args.linux_qemu),
        ('codec_worker', Path('/usr/local/libexec/astra-codec-worker')),
        ('initramfs', work / 'initramfs.cpio.gz'), ('prepared_image', seed / 'system.img'),
        ('posix_probe', ROOT / 'sw/userspace/commands/build/m68k/posix'),
        ('ntpd', ROOT / 'sw/userspace/services/ntpd/build/m68k/ntpd'))}
    (work / 'inputs.json').write_text(json.dumps(manifest, indent=2) + '\n')


def run_vm(args, directory, mode, target, phase):
    terminal = module('test-terminal.py')
    console = directory / (mode + '.log')
    qmp = directory / 'outer.qmp'
    command = ['sudo', '-n', 'setpriv', '--reuid=' + str(os.getuid()), '--regid=' + str(os.getgid()),
        '--groups=' + str(args.kvm_gid), '--no-new-privs', '--bounding-set=-all', '--inh-caps=-all', '--ambient-caps=-all']
    command += ['env', 'LD_LIBRARY_PATH=' + str(args.linux_qemu.parents[1] / 'lib/x86_64-linux-gnu'), str(args.linux_qemu),
        '-machine', 'q35,accel=kvm', '-cpu', 'host', '-m', '1536', '-smp', '1', '-nodefaults',
        '-display', 'none', '-monitor', 'none', '-serial', 'stdio', '-no-reboot',
        '-qmp', 'unix:' + str(qmp) + ',server=on,wait=off',
        '-L', str(args.firmware), '-bios', str(args.bios), '-kernel', str(args.linux_kernel),
        '-initrd', str(args.output / 'initramfs.cpio.gz'), '-append',
        'console=ttyS0 rdinit=/init panic=-1 astra_power_vm=1 astra_mode=%s astra_target=%s astra_phase=%s' % (mode, target, phase),
        '-device', 'qemu-xhci,id=xhci']
    for kind, interface in (('system', 'virtio'), ('fat32', 'none'), ('exfat', 'none')):
        command += ['-drive', 'file=%s,format=raw,if=%s,id=%s,cache=none' % (directory / (kind + '.img'), interface, kind)]
        if kind != 'system':
            command += ['-device', 'usb-storage,bus=xhci.0,removable=on,drive=' + kind + ',serial=' + kind]
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
    connection = None
    descriptor = None
    result = None
    fsck = False
    end = time.monotonic() + 360
    try:
        connection = terminal.Qmp(str(qmp), deadline=20, process=process)
        pid, uid, gid = struct.unpack('3i', connection.socket.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
        assert uid == os.getuid() != 0 and gid == os.getgid(), (pid, uid, gid)
        descriptor = os.pidfd_open(pid)
        status = dict(line.split(':', 1) for line in Path('/proc/%u/status' % pid).read_text().splitlines() if ':' in line)
        assert status['NoNewPrivs'].strip() == '1'
        assert all(int(status[key], 16) == 0 for key in ('CapInh', 'CapPrm', 'CapEff', 'CapBnd', 'CapAmb'))
        (directory / (mode + '-identity.json')).write_text(json.dumps({'pid': pid, 'uid': uid, 'gid': gid, 'groups': status['Groups'].strip(), 'command': command}, indent=2) + '\n')
        pending = b''
        with console.open('wb') as log:
            while time.monotonic() < end:
                ready, _, _ = select.select([process.stdout], [], [], .25)
                if ready:
                    block = os.read(process.stdout.fileno(), 65536)
                    if not block:
                        break
                    log.write(block)
                    log.flush()
                    pending += block
                    while b'\n' in pending:
                        line, pending = pending.split(b'\n', 1)
                        text = line.decode('utf-8', 'replace').strip()
                        if MARKER in text:
                            text = text[text.index(MARKER):]
                            print(directory.name, mode, text, flush=True)
                        if text.startswith(MARKER + 'CUT '):
                            assert mode == 'cut'
                            result = json.loads(text.split(' ', 1)[1])
                            assert result['target'] == target and result['phase'] == phase
                            signal.pidfd_send_signal(descriptor, signal.SIGKILL)
                        elif text.startswith(MARKER + 'RECOVER '):
                            result = json.loads(text.split(' ', 1)[1])
                        elif text == MARKER + 'FSCK-PASS':
                            fsck = True
                if process.poll() is not None and not ready:
                    break
            else:
                raise RuntimeError('Linux VM deadline expired: ' + str(console))
        code = process.wait(timeout=20)
        if mode == 'cut':
            assert result is not None and code in (-9, 137), (result, code, console)
        else:
            assert code == 0 and fsck, (code, console)
        return result
    finally:
        if process.poll() is None:
            if descriptor is not None:
                signal.pidfd_send_signal(descriptor, signal.SIGKILL)
            else:
                os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=30)
        if connection is not None:
            connection.close()
        if descriptor is not None:
            os.close(descriptor)
        if qmp.exists():
            qmp.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('qemu', type=Path)
    parser.add_argument('rom', type=Path)
    parser.add_argument('image', type=Path)
    parser.add_argument('--provider', type=Path, default=Path('/usr/local/libexec/astra-storage'))
    parser.add_argument('--linux-qemu', type=Path, required=True)
    parser.add_argument('--linux-kernel', type=Path, required=True)
    parser.add_argument('--kernel-release', required=True)
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--bios', type=Path, required=True)
    parser.add_argument('--kvm-gid', type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if os.geteuid() == 0:
        parser.error('run controller as an unprivileged user')
    for name in ('qemu', 'rom', 'image', 'provider', 'linux_qemu', 'linux_kernel', 'firmware', 'bios', 'output'):
        setattr(args, name, getattr(args, name).absolute())
    if args.output.exists():
        parser.error('output directory must be fresh; never overwrite prior evidence')
    for path in (args.qemu, args.rom, args.image, args.provider, args.linux_qemu, args.linux_kernel, args.bios):
        if not stat.S_ISREG(path.lstat().st_mode):
            parser.error('regular file required: ' + str(path))
    if args.image.stat().st_size > 4 << 30:
        parser.error('input image exceeds 4 GiB')
    from qemu_runtime import require_provider_abi
    require_provider_abi(args.qemu, args.provider)
    with args.qemu.open('rb') as stream:
        header = stream.read(20)
    if header[4:6] != b'\x02\x01' or header[18:20] != b'\x3e\x00':
        parser.error('Linux VM fixture requires the x86-64 host-profile emulator')
    if shutil.disk_usage(args.output.parent).free < 16 << 30:
        parser.error('at least 16 GiB free required for disposable disks')
    prepare(args)
    cases = [('baseline', 'system', 'synced')]
    cases += [(kind + '-' + phase, kind, phase) for kind in ('system', 'fat32', 'exfat') for phase in ('ready', 'written', 'synced')]
    cases += [('negative-early-ack', 'system', 'ready')]
    results = []
    for name, kind, phase in cases:
        directory = args.output / name
        directory.mkdir()
        for disk in ('system', 'fat32', 'exfat'):
            checked(['cp', '--reflink=auto', '--sparse=always', '--', args.output / (disk + '-base.img'), directory / (disk + '.img')])
        if name == 'baseline':
            run_vm(args, directory, 'baseline', kind, phase)
            results.append({'case': name, 'clean': True})
        else:
            run_vm(args, directory, 'cut', kind, phase)
            recovered = run_vm(args, directory, 'recover', kind, phase)
            if name == 'negative-early-ack':
                try:
                    verdict('synced', recovered['value'], recovered['baseline'])
                except RuntimeError as error:
                    assert 'acknowledged data was lost' in str(error)
                else:
                    raise AssertionError('early-ack negative control was accepted')
            else:
                verdict('synced' if phase == 'synced' else 'written',
                        recovered['value'] if phase != 'ready' else 'UNACKNOWLEDGED', recovered['baseline'])
            results.append({'case': name, 'negative_control_rejected': name == 'negative-early-ack', **recovered})
        (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    print('ASTRA LINUX POWER-LOSS PASS: 3 writable filesystems, 9 boundary cuts plus early-ack negative control, clean baseline, recovery/fsck', flush=True)


if __name__ == '__main__':
    if sys.argv[1:] == ['--worker']:
        worker()
    else:
        main()
