#!/usr/bin/env python3
"""Launch the real provider against an exact disposable partition window."""

import argparse
import os
from pathlib import Path
import select
import signal
import subprocess
import tempfile
import time

UUID = "e1f97188-8405-4bd3-9f3b-3b8bb123dd1f"
OFFSET = 1024 * 1024
LENGTH = 32 * 1024 * 1024


def read_line(stream, deadline):
    # Do not mix select with TextIOWrapper read-ahead: the new guardian and
    # peer announcements can arrive together before another kernel-readable byte.
    line = bytearray()
    while True:
        readable, _, _ = select.select([stream], [], [], max(0, deadline - time.monotonic()))
        assert readable, "no startup result"
        byte = os.read(stream.fileno(), 1)
        if not byte or byte == b"\n":
            return line.decode("utf-8") + ("\n" if byte else "")
        line.extend(byte)


def privileged(*arguments):
    return list(arguments) if os.geteuid() == 0 else ["sudo", "-n", *arguments]


def command(provider, image, work, uid, gid, uuid=UUID, consumer=()):
    return privileged(str(provider), "--uid", str(uid), "--gid", str(gid),
            "--system", str(image), str(OFFSET), str(LENGTH), uuid,
            "--work", str(work), "--sound", str(work), "--limits",
            "1048576", "1048576", "2", "5000", "2", "2097152", "64",
            "1048576", "32", "1048576", "32", "--discovery", "none", "--seed-sound", "none", "--", *map(str, consumer))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("provider", type=Path)
    parser.add_argument("client", type=Path)
    parser.add_argument("--uid", type=int)
    parser.add_argument("--gid", type=int)
    args = parser.parse_args()
    provider, client = args.provider.resolve(), args.client.resolve()
    uid = os.getuid() if args.uid is None else args.uid
    gid = os.getgid() if args.gid is None else args.gid
    if min(uid, gid) <= 0:
        parser.error("explicit non-root peer --uid/--gid required for a root observer")
    before = set(Path("/var/tmp").glob("astra-storage.*"))
    with tempfile.TemporaryDirectory(prefix="astra-storage-launcher-") as scratch:
        work = Path(scratch)
        # The root observer must let its configured non-root peer traverse the
        # disposable fixture parent, including the absent-executable oracle.
        # Private guest-file metadata remains unchanged inside the image.
        work.chmod(0o755)
        assert work.stat().st_mode & 0o777 == 0o755
        seed = work / "seed"
        (seed / "home").mkdir(parents=True)
        (seed / "home" / "private").write_text("private-data")
        (seed / "home" / "private").chmod(0o600)
        # Root ownership is data, not permission policy rewritten for the client.
        subprocess.run(privileged("chown", "-R", "0:0", str(seed)), check=True)
        filesystem = work / "filesystem.ext4"
        with filesystem.open("wb") as file:
            file.truncate(LENGTH)
        subprocess.run(privileged("mkfs.ext4", "-q", "-F", "-U", UUID,
                        "-d", str(seed), str(filesystem)), check=True)
        subprocess.run(privileged("chown", "-R", f"{os.getuid()}:{os.getgid()}", str(seed)), check=True)
        image = work / "selected.img"
        prefix, suffix = b"P" * OFFSET, b"S" * 8192
        initial_filesystem = filesystem.read_bytes()

        def reset():
            image.write_bytes(prefix + initial_filesystem + suffix)

        def verify_acknowledged_data():
            contents = image.read_bytes()
            assert contents[:OFFSET] == prefix and contents[OFFSET + LENGTH:] == suffix
            filesystem.write_bytes(contents[OFFSET:OFFSET + LENGTH])
            subprocess.run(["e2fsck", "-fn", str(filesystem)], check=True)
            metadata = subprocess.run(["debugfs", "-R", "stat /home/private", str(filesystem)],
                                      check=True, capture_output=True, text=True).stdout
            assert "0600" in metadata and "User:     0" in metadata and "Group:     0" in metadata, metadata
            data = subprocess.run(["debugfs", "-R", "cat /home/private", str(filesystem)],
                                  check=True, capture_output=True, text=True).stdout
            assert data == "private-dataAstra", data

        reset()
        cmd = command(provider, image, work, uid, gid, consumer=(client, uid, gid))
        with image.open("r+b"):
            refused = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            assert refused.returncode == 1, refused
            assert "Resource temporarily unavailable" in refused.stderr, refused.stderr
        wrong = subprocess.run(command(provider, image, work, uid, gid,
                                       uuid="wrong", consumer=(client, uid, gid)),
                               capture_output=True, text=True, timeout=30)
        assert wrong.returncode == 1 and "No such device" in wrong.stderr, wrong
        absent = subprocess.run(command(provider, image, work, uid, gid,
                                        consumer=(work / "absent",)),
                                capture_output=True, text=True, timeout=30)
        assert absent.returncode == 127, absent
        subprocess.run(cmd, check=True, timeout=30)
        verify_acknowledged_data()
        # External opens break the exclusive lease. Provider must stop its peer,
        # reap it, unmount and detach before the blocked open can complete.
        reset()
        held = subprocess.Popen(command(provider, image, work, uid, gid,
                                        consumer=(client, uid, gid, "hold")),
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 15
            while True:
                line = read_line(held.stdout, deadline)
                assert line, held.stderr.read()
                if "PASS" in line:
                    break
                assert line.startswith(("ASTRA-STORAGE-PEER ", "ASTRA-STORAGE-PROVIDER ")), line
            opener = subprocess.Popen(["cat", str(image)], stdout=subprocess.DEVNULL)
            try:
                assert held.wait(timeout=15) == 1
                assert opener.wait(timeout=15) == 0
                assert "No such device" in held.stderr.read()
            finally:
                if opener.poll() is None:
                    opener.kill()
                    opener.wait()
        finally:
            if held.poll() is None:
                held.terminate()
                held.wait(timeout=30)

        def start_held():
            reset()
            owned = subprocess.Popen(command(provider, image, work, uid, gid,
                consumer=(client, uid, gid, "hold")), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            service = peer = None
            domain = None
            deadline = time.monotonic() + 15
            while True:
                line = read_line(owned.stdout, deadline)
                assert line, owned.stderr.read()
                if line.startswith("ASTRA-STORAGE-PROVIDER "):
                    _, service, path = line.split()
                    service, domain = int(service), Path(path)
                elif line.startswith("ASTRA-STORAGE-PEER "):
                    peer = int(line.split()[1])
                elif "PASS" in line:
                    break
                else:
                    raise AssertionError(line)
            assert service and peer and domain and domain.is_dir()
            return owned, service, peer, domain

        def service_signal(service, number):
            # Signal the exact announced live child through a pidfd, rather
            # than guessing by process names or sweeping numeric PIDs.
            subprocess.run(privileged("python3", "-c",
                "import os,signal,sys; f=os.pidfd_open(int(sys.argv[1])); "
                "signal.pidfd_send_signal(f,int(sys.argv[2])); os.close(f)",
                str(service), str(int(number))), check=True)

        for crash in (signal.SIGKILL, signal.SIGABRT):
            owned, service, peer, domain = start_held()
            try:
                service_signal(service, crash)
                assert owned.wait(timeout=20) == 128 + crash
                assert not domain.exists(), (crash, domain)
                assert not Path(f"/proc/{peer}").exists(), (crash, peer)
                assert not subprocess.run(privileged("losetup", "-j", str(image)),
                    check=True, capture_output=True, text=True).stdout.strip()
                verify_acknowledged_data()
            finally:
                if owned.poll() is None:
                    owned.terminate(); owned.wait(timeout=30)

        # A real outside loop opener must keep the original provider cleanup
        # owner alive. The guardian does no loop release or premature directory
        # removal; existing provider cleanup finishes after the opener closes.
        owned, service, peer, domain = start_held()
        external = None
        try:
            loop = subprocess.run(privileged("losetup", "--list", "--associated", str(image),
                "--output", "NAME", "--noheadings"), check=True, capture_output=True, text=True).stdout.split()
            assert len(loop) == 1, loop
            external = subprocess.Popen(privileged("python3", "-c",
                "import os,sys; f=os.open(sys.argv[1],os.O_RDONLY|os.O_CLOEXEC); "
                "print('HELD',flush=True); sys.stdin.buffer.read(1); os.close(f)", loop[0]),
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            assert external.stdout.readline() == "HELD\n"
            os.kill(peer, signal.SIGTERM)
            time.sleep(.5)
            assert owned.poll() is None and domain.is_dir(), "retained loop bypassed real provider cleanup"
            external.stdin.write("R"); external.stdin.close()
            assert external.wait(timeout=10) == 0
            assert owned.wait(timeout=20) == 128 + signal.SIGTERM
            assert not domain.exists()
            assert not subprocess.run(privileged("losetup", "-j", str(image)),
                check=True, capture_output=True, text=True).stdout.strip()
            verify_acknowledged_data()
        finally:
            if external is not None and external.poll() is None:
                external.stdin.close(); external.wait(timeout=10)
            if owned.poll() is None:
                owned.terminate(); owned.wait(timeout=30)
        subprocess.run(privileged("chown", "-R", f"{os.getuid()}:{os.getgid()}", str(seed)), check=True)
    assert set(Path("/var/tmp").glob("astra-storage.*")) == before
    print("storage launcher: exact selection, metadata/durability, normal/fatal peer descendants, bounded crash directories, retained external-loop cleanup PASS")


if __name__ == "__main__":
    main()
