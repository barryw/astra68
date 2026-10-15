#!/usr/bin/env python3
"""Exercise actual unprivileged QEMU through the confined Linux provider.

This is the provider bridge gate, not the native namespace/release gate.
The existing protocol behavioral oracle is reused without a second FS engine.
"""

import argparse
import importlib.util
import os
from pathlib import Path
import runpy
import struct
import subprocess
import tempfile
import time


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu", type=Path)
    parser.add_argument("provider", type=Path)
    parser.add_argument("--uid", type=int, required=True)
    parser.add_argument("--gid", type=int, required=True)
    parser.add_argument("--scenario", choices=("full", "serial", "budgets", "memory"), default="full")
    parser.add_argument("--budget-out", type=Path)
    args = parser.parse_args(argv)
    if args.scenario == "memory" and args.budget_out is None:
        parser.error("memory scenario requires --budget-out")
    here = Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location("hostfs", here / "test-hostfs.py")
    gate = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gate)
    QmpClient = runpy.run_path(str(here / "astra-input-hotplug.py"))["QmpClient"]
    uid, gid = args.uid, args.gid
    assert os.geteuid() == 0 and uid != 0 and gid != 0
    uuid = "e1f97188-8405-4bd3-9f3b-3b8bb123dd1f"
    before = set(Path("/var/tmp").glob("astra-storage.*"))
    with tempfile.TemporaryDirectory(prefix="astra-provider-qemu-") as scratch:
        work = Path(scratch)
        work.chmod(0o755)
        control = work / "control"
        control.mkdir()
        os.chown(control, uid, gid)
        root = work / "root"
        for name in ("work", "sound"):
            (root / name).mkdir(parents=True)
        if args.scenario == "memory":
            cost = root / "work" / "cost"
            cost.mkdir()
            (cost / "file").touch()
            for index in range(2000):
                (cost / ("entry-%04d-%s" % (index, "x" * 48))).touch()
        outside = work / "outside"
        outside.mkdir()
        (outside / "sentinel").write_bytes(b"outside")
        seed = work / "seed"
        (seed / "startup").mkdir(parents=True)
        (seed / "startup" / "system").write_bytes(b"bootstrap\n")
        (seed / "home").mkdir()
        (seed / "home" / "private").write_bytes(b"private-data")
        (seed / "home" / "private").chmod(0o600)
        (seed / "home").chmod(0o700)
        iso = work / "disc.iso"
        if args.scenario == "full":
            optical_seed = work / "disc-tree"
            (optical_seed / "Folder").mkdir(parents=True)
            (optical_seed / "Folder" / "café.txt").write_text("Astra ISO test\n", encoding="utf-8")
            subprocess.run(["genisoimage", "-quiet", "-input-charset", "utf-8", "-R", "-J",
                "-V", "ASTRA_DISC", "-o", str(iso), str(optical_seed)], check=True)
        subprocess.run(["sudo", "-n", "chown", "-R", "0:0", str(seed)], check=True)
        image = work / "system.img"
        with image.open("wb") as file:
            file.truncate(32 * 1024 * 1024)
        subprocess.run(["sudo", "-n", "mkfs.ext4", "-q", "-F", "-U", uuid,
                        "-d", str(seed), str(image)], check=True)
        subprocess.run(["sudo", "-n", "chown", "-R", f"{uid}:{gid}", str(seed)], check=True)
        rom = work / "test.rom"
        rom.write_bytes(struct.pack(">II", 0x02001000, 0xFFE00008))
        qtest_path, qmp_path = str(control / "qtest.sock"), str(control / "qmp.sock")
        command = [str(args.provider.resolve()), "--uid", str(uid), "--gid", str(gid),
                   "--system", str(image), "0", str(image.stat().st_size), uuid,
                   "--work", str(root / "work"), "--sound", str(root / "sound"),
                   "--limits", "1048576", "1048576", "2", "5000", "2",
                   "2097152", "64", "1048576", "32", "1048576", "32", "--discovery", "none", "--seed-sound", "none", "--",
                   str(args.qemu.resolve()), "-machine", "astra68,accel=qtest", "-m", "32M",
                   "-bios", str(rom), "-S", "-display", "none", "-nodefaults",
                   "-qtest", f"unix:{qtest_path},server=on,wait=off",
                   "-qtest-log", "/dev/null",
                   "-qmp", f"unix:{qmp_path},server=on,wait=off", "-pidfile", str(control / "qemu.pid")]
        if args.scenario == "serial":
            command += ["-object", "main-loop,id=main-loop,thread-pool-max=1"]
        if args.scenario == "memory":
            command += ["-mem-prealloc"]
        environment = os.environ.copy()
        for name in ("ASTRA_HOSTFS_ROOT", "ASTRA_STORAGE_CONTROL_FD", "ASTRA_STORAGE_PATH_FD"):
            environment.pop(name, None)
        if args.scenario == "budgets":
            environment.update(ASTRA_HOSTFS_MAX_DESCRIPTORS="12", ASTRA_HOSTFS_MAX_DIRECTORIES="2")
        if args.scenario == "memory":
            environment.update(ASTRA_HOSTFS_MAX_DESCRIPTORS="4096", ASTRA_HOSTFS_MAX_DIRECTORIES="512")
        with tempfile.TemporaryFile(mode="w+") as errors:
            process = subprocess.Popen(command, env=environment, stdout=subprocess.DEVNULL,
                                       stderr=errors, text=True)
            qtest = None
            try:
                qtest = gate.QTest(qtest_path)
                pid = int((control / "qemu.pid").read_text())
                for task in Path(f"/proc/{pid}/task").iterdir():
                    status = dict(line.split(":", 1) for line in (task / "status").read_text().splitlines() if ":" in line)
                    assert list(map(int, status["Uid"].split())) == [uid] * 4
                    assert list(map(int, status["Gid"].split())) == [gid] * 4
                    assert not status["Groups"].strip() and status["NoNewPrivs"].strip() == "1"
                    assert all(int(status[name].strip(), 16) == 0 for name in
                               ("CapInh", "CapPrm", "CapEff", "CapBnd", "CapAmb"))
                qtest.detect_endian()
                if args.scenario == "serial":
                    gate.scope_revocation(qtest, str(root))
                    gate.orphan_open(qtest, str(root))
                elif args.scenario == "budgets":
                    gate.budgets(qtest, str(root), qmp_path)
                elif args.scenario == "memory":
                    gate.measure_objects(qtest, str(root), qmp_path, str(args.budget_out))
                else:
                    gate.run(qtest, str(root / "work"), str(outside))
                    qtest.write32(gate.HACC_RESET, 1)
                    gate.system_volume(qtest)
                    # Metadata is visible without becoming file or admin authority.
                    generation = qtest.read32(gate.HACC_GENERATION)

                    def catalog(epoch=0, sequence=0, after=0):
                        query = struct.pack(">IIQQQ", 1, 32, epoch, sequence, after)
                        return gate.execute(qtest, [gate.Command(generation, 1,
                            service=6, data=query, capacity=8104)], owner=0x2002)[0]

                    visible = catalog()
                    assert gate.status(visible) == gate.STATUS_OK
                    page = gate.output(qtest, visible, gate.get32(visible.raw, 52))
                    version, size, epoch, sequence, next_id, count, reserved = struct.unpack_from(">IIQQQII", page)
                    assert version == 1 and size == 8104 and epoch != 0 and sequence != 0 and reserved == 0
                    assert next_id == 0 and count == 8
                    names = []
                    for at in range(count):
                        row = page[40 + at * 504:40 + (at + 1) * 504]
                        object_id, object_generation = struct.unpack_from(">QQ", row)
                        assert object_id != 0 and object_generation != 0
                        assert struct.unpack_from(">I", row, 84)[0] == 0
                        names.append(row[88:120].split(b"\0", 1)[0])
                    assert set(names) == {b"", b"/dh0", b"/work", b"/sound", b"/ram0", b"/tmp"}
                    assert gate.status(catalog(epoch ^ 1)) == gate.STATUS_LOST
                    assert gate.status(catalog(epoch, sequence + 1)) == gate.STATUS_BUSY
                    refused = gate.execute(qtest, [gate.Command(generation, gate.FS_SESSION_OPEN,
                        path="/", data=gate.volume_reference(qtest, generation, "/work"))], owner=0x2002)[0]
                    assert gate.status(refused) == gate.STATUS_ACCESS
                    qtest.write32(gate.HACC_RESET, 1)
                    generation = qtest.read32(gate.HACC_GENERATION)
                    assert gate.claim(qtest, generation) == gate.STATUS_OK
                    qmp = QmpClient(qmp_path)
                    try:
                        def gauge(name):
                            return qmp.execute("qom-get", {"path": "/machine",
                                "property": "astra-host-" + name})
                        before_fds = gauge("fds")
                        logical = [gate.open_session(qtest, generation) for _ in range(40)]
                        assert gauge("fds") == before_fds and gauge("fs-descriptors") == 0
                        parent = logical[0]
                        derived = gate.open_session(qtest, generation, parent=parent)
                        assert gauge("fds") == before_fds and gauge("fs-descriptors") == 0
                        opened = gate.execute(qtest, [gate.Command(generation, gate.FS_OPEN,
                            path="/", session=derived, flags=gate.OPEN_READ | gate.OPEN_DIRECTORY)])[0]
                        assert gate.status(opened) == gate.STATUS_OK
                        assert gauge("fds") == before_fds + 1 and gauge("fs-descriptors") == 1
                        for session in logical + [derived]:
                            answer = gate.execute(qtest, [gate.Command(generation,
                                gate.FS_SESSION_CLOSE, session=session)])[0]
                            assert gate.status(answer) == gate.STATUS_OK
                        assert gauge("fds") == before_fds and gauge("fs-descriptors") == 0
                        # Revocation retains the disconnected native identity until
                        # its holder closes it, but retains no Linux descriptor.
                        assert gauge("fs-objects") == 1
                        closed = gate.execute(qtest, [gate.Command(generation,
                            gate.FS_CLOSE, session=derived, handle=gate.get32(opened.raw, 16))])[0]
                        assert gate.status(closed) == gate.STATUS_OK
                        assert gauge("fs-objects") == 0
                    finally:
                        qmp.close()
                    print("PASS logical-root authority does not pin storage; retained directories close exactly")
                    qtest.write32(gate.HACC_RESET, 1)
                    gate.claims_and_sessions(qtest, str(root / "work"))
                    gate.volumes(qtest, str(root))
                    gate.scopes(qtest, str(root))
                    gate.claim_race(qtest)
                    gate.descriptions(qtest, str(root))
                    gate.paths(qtest, str(root))
                    gate.provider_loss(qtest, str(root), qmp_path)
                    qtest.write32(gate.HACC_RESET, 1)
                    generation = qtest.read32(gate.HACC_GENERATION)
                    assert gate.claim(qtest, generation) == gate.STATUS_OK
                    session = gate.open_session(qtest, generation, volume="/dh0")

                    def call(operation, **kwargs):
                        answer = gate.execute(qtest, [gate.Command(generation, operation, session=session, **kwargs)])[0]
                        assert gate.status(answer) == gate.STATUS_OK, (operation, gate.status(answer))
                        return answer

                    directory = call(gate.FS_READDIR, path="/home", capacity=8192, value=8)
                    assert b"private" in gate.output(qtest, directory, gate.get32(directory.raw, 52))
                    opened = call(gate.FS_OPEN, path="/home/private", flags=gate.OPEN_READ | gate.OPEN_WRITE)
                    handle = gate.get32(opened.raw, 16)
                    assert struct.unpack_from(">H", opened.raw, gate.NODE_MODE)[0] & 0o7777 == 0o600
                    read = call(gate.FS_READ, handle=handle, capacity=32)
                    assert gate.output(qtest, read, gate.get32(read.raw, 52)) == b"private-data"
                    call(gate.FS_WRITE, handle=handle, offset=12, data=b"Astra", value=128)
                    call(gate.FS_SYNC, handle=handle)
                    call(gate.FS_CHMOD, handle=handle, value=0o600)
                    call(gate.FS_CLOSE, handle=handle)
                    call(gate.FS_SESSION_CLOSE)
                    # Administrator transport is available only on the claimed
                    # kernel-stamped provider channel. Data/metadata caps do not
                    # become lifecycle authority.
                    ram_reference = gate.volume_reference(qtest, generation, "/ram0")
                    source_reference = None
                    ram_id = struct.unpack_from(">Q", ram_reference, 8)[0]
                    first = gate.volume_page(qtest, generation)
                    first_page = gate.output(qtest, first, gate.get32(first.raw, 52))
                    epoch = struct.unpack_from(">Q", first_page, 8)[0]
                    for at in range(struct.unpack_from(">I", first_page, 32)[0]):
                        row = first_page[40 + at * 504:40 + (at + 1) * 504]
                        if struct.unpack_from(">Q", row)[0] == ram_id:
                            source_reference = struct.pack(">QQQ", epoch, *struct.unpack_from(">QQ", row, 16))
                    assert source_reference is not None

                    def provider_exchange(operation, payload, capacity, provider=True, **operands):
                        # Filesystem opcode 4 defaults to append-write in the raw
                        # helper; volume commands always have an explicit zero value.
                        operands.setdefault("value", 0)
                        gate.claim_generations[0] += 1
                        channel_generation = gate.claim_generations[0]
                        slot = 30
                        assert gate.configure_channel(qtest, generation, gate.CONFIG_OPEN,
                            slot=slot, owner=gate.PROVIDER, channel_generation=channel_generation,
                            buffer=gate.CLAIM_BUFFER,
                            byte_size=16384,
                            scope_flags=gate.CHANNEL_PROVIDER if provider else 0) == 0
                        offsets = gate.channel_write(qtest, gate.CLAIM_BUFFER, 1,
                            [gate.Command(generation, operation, service=6, data=payload, capacity=capacity,
                                **operands)])
                        qtest.write32(gate.CLAIM_BUFFER + 36, 1)
                        qtest.write32(gate.CHANNEL_APERTURE + slot * 4096 + 0x20, 1)
                        qtest.wait32(gate.CLAIM_BUFFER + 48, 1)
                        answer = qtest.read(gate.CLAIM_BUFFER + gate.CHANNEL_HEADER_SIZE, gate.COMMAND_SIZE)
                        length = gate.get32(answer, 52)
                        result = qtest.read(gate.CLAIM_BUFFER + offsets[0], length) if length else b""
                        assert gate.configure_channel(qtest, generation, gate.CONFIG_CLOSE,
                            slot=slot, owner=gate.PROVIDER, channel_generation=channel_generation) == 0
                        return gate.status(answer), result

                    def administrator(operation, reference, flags=0, bytes_limit=0, inodes_limit=0,
                                      requester=gate.PROVIDER, provider=True, token=0):
                        payload = reference + struct.pack(">IIQQQII", operation, flags,
                            bytes_limit, inodes_limit, token, requester, 0)
                        return provider_exchange(2, payload, 64, provider)

                    def usage(reference):
                        sequence = catalog_sequence = after = after_kind = after_process = 0
                        rows = []
                        while True:
                            payload = struct.pack(">II", 2, 64) + reference + struct.pack(">QQQII",
                                sequence, catalog_sequence, after, after_kind, after_process)
                            status, page = provider_exchange(3, payload, 8392)
                            assert status == gate.STATUS_OK, status
                            assert len(page) == 8392 and struct.unpack_from(">II", page) == (2, 8392)
                            sequence, catalog_sequence, after, after_kind, count, after_process, reserved = \
                                struct.unpack_from(">QQQIIII", page, 32)
                            assert count <= 2 and reserved == 0
                            rows.extend(page[72 + at * 4160:72 + (at + 1) * 4160] for at in range(count))
                            if not after: return rows

                    assert administrator(2, ram_reference, provider=False)[0] == gate.STATUS_ACCESS
                    assert administrator(2, gate.volume_reference(qtest, generation, "/tmp"))[0] == gate.STATUS_ACCESS
                    assert administrator(2, ram_reference, requester=0)[0] == gate.STATUS_INVALID
                    assert administrator(2, struct.pack(">QQQ", epoch, ram_id, 99))[0] == gate.STATUS_LOST
                    query = struct.pack(">II", 2, 64) + ram_reference + bytes(32)
                    status, before_page = provider_exchange(3, query, 8392)
                    assert status == gate.STATUS_OK
                    before_sequence, catalog_sequence = struct.unpack_from(">QQ", before_page, 32)
                    assert provider_exchange(4, b"", 0, provider=False)[0] == gate.STATUS_ACCESS
                    for payload, operands in ((b"x", {}), (b"", {"flags": 1}),
                            (b"", {"path": "/"}), (b"", {"handle": 1}), (b"", {"capacity": 1})):
                        capacity = operands.pop("capacity", 0)
                        assert provider_exchange(4, payload, capacity, **operands)[0] == gate.STATUS_INVALID
                    assert provider_exchange(4, b"", 0)[0] == gate.STATUS_OK
                    status, after_page = provider_exchange(3, query, 8392)
                    assert status == gate.STATUS_OK and struct.unpack_from(">Q", after_page, 32)[0] > before_sequence
                    stale_query = struct.pack(">II", 2, 64) + ram_reference + struct.pack(">QQQII",
                        before_sequence, catalog_sequence, 0, 0, 0)
                    assert provider_exchange(3, stale_query, 8392)[0] == gate.STATUS_BUSY
                    idle = gate.open_session(qtest, generation, volume="/ram0")
                    created = gate.execute(qtest, [gate.Command(generation, gate.FS_MKDIR,
                        path="/held", value=0o755, session=idle)])[0]
                    assert gate.status(created) == gate.STATUS_OK
                    derived = gate.open_session(qtest, generation, volume="/ram0", root="/held")
                    assert administrator(2, ram_reference)[0] == gate.STATUS_BUSY
                    assert gate.status(gate.execute(qtest, [gate.Command(generation, gate.FS_SESSION_CLOSE,
                        session=derived)])[0]) == gate.STATUS_OK
                    opened = gate.execute(qtest, [gate.Command(generation, gate.FS_OPEN,
                        path="/held/file", session=idle, flags=gate.OPEN_READ | gate.OPEN_WRITE |
                        gate.OPEN_CREATE, value=0o600)])[0]
                    assert gate.status(opened) == gate.STATUS_OK
                    handle = gate.get32(opened.raw, 16)
                    assert administrator(2, ram_reference)[0] == gate.STATUS_BUSY
                    assert gate.status(gate.execute(qtest, [gate.Command(generation, gate.FS_CLOSE,
                        session=idle, handle=handle)])[0]) == gate.STATUS_OK
                    status, retired = administrator(2, ram_reference)
                    assert status == gate.STATUS_OK and retired == ram_reference
                    deadline = time.monotonic() + 10
                    while True:
                        visible = gate.volume_page(qtest, generation)
                        page = gate.output(qtest, visible, gate.get32(visible.raw, 52))
                        rows = [page[40 + at * 504:40 + (at + 1) * 504]
                            for at in range(struct.unpack_from(">I", page, 32)[0])]
                        state = next((struct.unpack_from(">I", row, 64)[0] for row in rows
                            if struct.unpack_from(">Q", row)[0] == ram_id), None)
                        if state is None or state == 3: break
                        assert time.monotonic() < deadline, state
                        time.sleep(.01)
                    # Catalog reconciliation retires idle sessions without
                    # reopening an old incarnation or waiting for its owner.
                    stale = gate.execute(qtest, [gate.Command(generation, gate.FS_STAT,
                        path="/", session=idle)])[0]
                    assert gate.status(stale) == gate.STATUS_BAD_HANDLE, gate.status(stale)
                    status, admitted = administrator(1, source_reference,
                        bytes_limit=1048576, inodes_limit=32)
                    assert status == gate.STATUS_OK and admitted != ram_reference, (status, admitted.hex(), source_reference.hex())
                    print("PASS native lifecycle: authority separation, stale refusal, retained-directory/file busy, idle-root safe removal and remount")

                    def records():
                        rows, after, sequence = [], 0, 0
                        while True:
                            answer = gate.volume_page(qtest, generation, epoch, sequence, after)
                            assert gate.status(answer) == gate.STATUS_OK
                            page = gate.output(qtest, answer, gate.get32(answer.raw, 52))
                            sequence, after, count = struct.unpack_from(">QQI", page, 16)
                            rows.extend(page[40 + at * 504:40 + (at + 1) * 504] for at in range(count))
                            if not after: return rows

                    def wait_record(reference, state):
                        _, wanted_id, wanted_generation = struct.unpack(">QQQ", reference)
                        deadline = time.monotonic() + 15
                        while True:
                            selected = next((row for row in records()
                                if struct.unpack_from(">Q", row)[0] == wanted_id), None)
                            if state is None and selected is None: return None
                            if selected is not None and struct.unpack_from(">Q", selected, 8)[0] == wanted_generation:
                                current_state = struct.unpack_from(">I", selected, 64)[0]
                                if current_state == state: return selected
                                assert current_state != 8, ("failed lifecycle", selected.hex())
                            assert time.monotonic() < deadline, (reference.hex(), state, selected)
                            time.sleep(.01)

                    wait_record(admitted, 5)
                    for invalid in (source_reference,
                            struct.pack(">QQQ", epoch, struct.unpack_from(">Q", admitted, 8)[0], 99),
                            struct.pack(">QQQ", epoch ^ 1, *struct.unpack_from(">QQ", admitted, 8))):
                        query = struct.pack(">II", 2, 64) + invalid + bytes(32)
                        assert provider_exchange(3, query, 8392)[0] == gate.STATUS_LOST
                    ram = gate.open_session(qtest, generation, volume="/ram0")

                    def ram_call(operation, **kwargs):
                        return gate.execute(qtest, [gate.Command(generation, operation, session=ram, **kwargs)])[0]

                    opened = ram_call(gate.FS_OPEN, path="/disc.iso", flags=gate.OPEN_READ | gate.OPEN_WRITE |
                        gate.OPEN_CREATE, value=0o600)
                    assert gate.status(opened) == gate.STATUS_OK
                    image_handle = gate.get32(opened.raw, 16)
                    image_bytes = iso.read_bytes()
                    for offset in range(0, len(image_bytes), 4096):
                        chunk = image_bytes[offset:offset + 4096]
                        written = ram_call(gate.FS_WRITE, handle=image_handle, offset=offset, data=chunk)
                        assert gate.status(written) == gate.STATUS_OK and gate.get32(written.raw, 52) == len(chunk), (
                            offset, gate.status(written), gate.get32(written.raw, 52))
                    writable = ram_call(gate.FS_SHARE, handle=image_handle)
                    writable_token = gate.get64(writable.raw, 32)
                    assert gate.status(writable) == gate.STATUS_OK
                    assert administrator(3, bytes(24), token=writable_token)[0] == gate.STATUS_ACCESS
                    assert gate.status(ram_call(gate.FS_ADOPT, offset=writable_token,
                        flags=gate.ADOPT_DISCARD)) == gate.STATUS_OK
                    assert gate.status(ram_call(gate.FS_CLOSE, handle=image_handle)) == gate.STATUS_OK

                    # The kernel's resource owner is independent of native PID.
                    # Exercise both the real typed bridge and delayed retirement
                    # while another incarnation reuses the same visible PID.
                    actor_pid, old_owner, new_owner = 39, 0x1001ffee, 0x1002ffed
                    actor_slot, actor_generation, produced = 29, 61, [0]
                    actor_scope = [ram]

                    def actor_open(owner, channel_generation, scope=ram, process=actor_pid):
                        produced[0] = 0
                        actor_scope[0] = scope
                        assert gate.configure_scope(qtest, generation, gate.CONFIG_OPEN,
                            actor_slot, channel_generation, scope=scope,
                            scope_flags=gate.SCOPE_BOUND | gate.SCOPE_READ_ONLY,
                            scope_owner=gate.PROVIDER, owner=owner, process=process,
                            buffer=gate.RACE_BUFFER, byte_size=16384, capacity=1) == 0

                    def actor_call(operation, **kwargs):
                        produced[0] += 1
                        at = gate.scoped_post(qtest, actor_slot, produced[0],
                            gate.Command(generation, operation, session=actor_scope[0], **kwargs),
                            buffer=gate.RACE_BUFFER, capacity=1)
                        return gate.scoped_wait(qtest, produced[0], at, buffer=gate.RACE_BUFFER)

                    actor_open(old_owner, actor_generation)
                    opened = actor_call(gate.FS_OPEN, path="/disc.iso", flags=gate.OPEN_READ)
                    assert gate.status(opened) == gate.STATUS_OK
                    image_handle = gate.get32(opened, 16)
                    shared = actor_call(gate.FS_SHARE, handle=image_handle)
                    assert gate.status(shared) == gate.STATUS_OK
                    old_token = gate.get64(shared, 32)
                    assert administrator(3, bytes(24), requester=old_owner, token=old_token)[0] == gate.STATUS_ACCESS
                    assert administrator(3, bytes(24), requester=actor_pid + 1, token=old_token)[0] == gate.STATUS_ACCESS
                    assert gate.configure_scope(qtest, generation, gate.CONFIG_CLOSE,
                        actor_slot, actor_generation, owner=old_owner) == 0
                    actor_open(new_owner, actor_generation + 1)
                    assert administrator(3, bytes(24), requester=actor_pid, token=old_token)[0] == gate.STATUS_ACCESS
                    qtest.write32(gate.HACC_RELEASE_OWNER, old_owner)
                    assert administrator(3, bytes(24), requester=actor_pid, token=old_token)[0] == gate.STATUS_ACCESS
                    opened = actor_call(gate.FS_OPEN, path="/disc.iso", flags=gate.OPEN_READ)
                    assert gate.status(opened) == gate.STATUS_OK
                    image_handle = gate.get32(opened, 16)
                    shared = actor_call(gate.FS_SHARE, handle=image_handle)
                    assert gate.status(shared) == gate.STATUS_OK
                    token = gate.get64(shared, 32)
                    retained = usage(admitted)
                    assert any(struct.unpack_from(">Q", row, 24)[0] == image_handle and
                        struct.unpack_from(">II", row, 40) == (actor_pid, 1) for row in retained)
                    assert any(struct.unpack_from(">Q", row, 24)[0] == token and
                        struct.unpack_from(">II", row, 40) == (actor_pid, 5) for row in retained)
                    status, image_source = administrator(3, bytes(24), requester=actor_pid, token=token)
                    assert status == gate.STATUS_OK and len(image_source) == 24
                    assert gate.status(actor_call(gate.FS_ADOPT, offset=token,
                        flags=gate.ADOPT_DISCARD)) == gate.STATUS_OK
                    assert gate.status(actor_call(gate.FS_CLOSE, handle=image_handle)) == gate.STATUS_OK
                    assert gate.configure_scope(qtest, generation, gate.CONFIG_CLOSE,
                        actor_slot, actor_generation + 1, owner=new_owner) == 0
                    print("PASS native identity: independent resource owners, exact PID usage, forged requester and delayed PID-reuse token refusal")
                    wait_record(image_source, 3)
                    status, image_mount = administrator(1, image_source, flags=1)
                    assert status == gate.STATUS_OK
                    image_row = wait_record(image_mount, 5)
                    assert image_row[248:504].split(b"\0", 1)[0] == b"ASTRA_DISC"
                    assert image_row[88:120].split(b"\0", 1)[0] == b"/cd0"
                    assert administrator(2, admitted)[0] == gate.STATUS_BUSY
                    assert administrator(5, image_source)[0] == gate.STATUS_BUSY
                    backing_holders = usage(admitted)
                    source_id = struct.unpack_from(">Q", image_source, 8)[0]
                    image_holders = [row for row in backing_holders
                        if struct.unpack_from(">I", row, 44)[0] == 6 and
                        struct.unpack_from(">Q", row, 24)[0] == source_id]
                    assert len(image_holders) == 1
                    held = image_holders[0]
                    assert struct.unpack_from(">Q", held, 32)[0] != 0
                    assert struct.unpack_from(">I", held, 52)[0] & 2 == 0
                    assert held[64:64 + struct.unpack_from(">I", held, 56)[0]] == b"/ram0/disc.iso"
                    disc = gate.open_session(qtest, generation, volume="/cd0", read_only=True)
                    actor_open(gate.PROVIDER, actor_generation + 2, scope=disc, process=gate.PROVIDER)

                    def disc_call(operation, **kwargs):
                        # Usage verifies an explicitly stamped native PID;
                        # a raw HOST_EXECUTE owner is not a native identity.
                        answer = actor_call(operation, **kwargs)
                        return gate.Answer(answer, gate.RACE_BUFFER - gate.BUFFER + gate.get32(answer, 40))

                    opened = disc_call(gate.FS_OPEN, path="/", flags=gate.OPEN_READ | gate.OPEN_DIRECTORY)
                    assert gate.status(opened) == gate.STATUS_OK
                    directory = gate.get32(opened.raw, 16)
                    listing = disc_call(gate.FS_READDIR, handle=directory, capacity=4096, value=32)
                    assert gate.status(listing) == gate.STATUS_OK
                    assert b"Folder" in gate.directory_names(gate.output(qtest, listing, gate.get32(listing.raw, 52)))
                    assert administrator(2, image_mount)[0] == gate.STATUS_BUSY
                    directory_holders = usage(image_mount)
                    assert any(struct.unpack_from(">II", held, 40) == (gate.PROVIDER, 2)
                        and struct.unpack_from(">Q", held, 24)[0] == directory
                        for held in directory_holders)
                    assert gate.status(disc_call(gate.FS_CLOSE, handle=directory)) == gate.STATUS_OK
                    opened = disc_call(gate.FS_OPEN, path="/Folder/café.txt", flags=gate.OPEN_READ)
                    assert gate.status(opened) == gate.STATUS_OK
                    handle = gate.get32(opened.raw, 16)
                    read = disc_call(gate.FS_READ, handle=handle, capacity=64, offset=0)
                    assert gate.status(read) == gate.STATUS_OK
                    assert gate.output(qtest, read, gate.get32(read.raw, 52)) == b"Astra ISO test\n"
                    assert gate.status(disc_call(gate.FS_OPEN, path="/new", flags=gate.OPEN_WRITE |
                        gate.OPEN_CREATE, value=0o600)) == gate.STATUS_READ_ONLY
                    assert gate.status(disc_call(gate.FS_CLOSE, handle=handle)) == gate.STATUS_OK
                    assert gate.configure_scope(qtest, generation, gate.CONFIG_CLOSE,
                        actor_slot, actor_generation + 2, owner=gate.PROVIDER) == 0
                    assert administrator(2, image_mount)[0] == gate.STATUS_OK
                    wait_record(image_mount, None)
                    assert administrator(5, image_source)[0] == gate.STATUS_OK
                    wait_record(image_source, None)
                    assert administrator(2, admitted)[0] == gate.STATUS_OK
                    wait_record(admitted, None)
                    print("PASS native ISO: authenticated retained readonly file, label/CD0, UTF-8 tree, busy backing/source/directory, readonly writes, actual detach and lease release")
                # Proper QEMU quit lets the provider perform real cleanup.
                qmp = QmpClient(qmp_path)
                try:
                    qmp.execute("quit")
                finally:
                    qmp.close()
                assert process.wait(timeout=15) == 0
            except BaseException:
                errors.seek(0)
                print(errors.read())
                raise
            finally:
                if qtest is not None:
                    qtest.close()
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
        subprocess.run(["e2fsck", "-fn", str(image)], check=True)
        data = subprocess.run(["debugfs", "-R", "cat /home/private", str(image)],
                              check=True, capture_output=True, text=True).stdout
        expected = "private-dataAstra" if args.scenario == "full" else "private-data"
        assert data == expected, data
    assert set(Path("/var/tmp").glob("astra-storage.*")) == before
    print("QEMU provider bridge: native grants/descriptions/path oracle, preserved root-owned data, unprivileged QEMU and real cleanup PASS")


if __name__ == "__main__":
    main()
