#!/usr/bin/env python3
"""astra-top -- where the DE25's time goes, guest and host, in one window.

usage: astra-top [SECONDS] [--motion HZ] [--json] [--perf] [--top N]

Over one window (default 5 s) it reports:

  guest   CPU per Astra process, from the kernel's own runtime accounting
          (the sampler service copies PROC:snapshot and PROC:scheduler to
          WORK:.astra/sample once a second); idle is what no process used.
          Switches per second, same- and cross-address-space, and why the
          CPU changed hands: blocking, quantum expiry, or preemption by a
          higher priority, a wake or a deadline.
  host    CPU per thread of QEMU and the Astra helpers (the vCPU thread is
          the 68040), from /proc.
  output  display presents per second (FPGA scene generation) and audio
          underruns and software gaps (the audio host's counters).
  --motion HZ  move the pointer in a circle at HZ events/s through QMP for
          the window, as a hand on a mouse does, and report how often the
          FPGA's pointer plane took a new position and the longest gaps.
  --perf  the host's hottest symbols in the vCPU thread over the same
          window: TCG code, softmmu refills, helpers -- the emulator's own
          share, which the guest cannot see.

Every guest number is a difference of two cumulative kernel counters, so a
late sample loses nothing. --json prints the same window as one object, for
A/B scripts.
"""

import argparse
import bisect
import json
import math
import mmap
import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

STORE = os.environ.get("ASTRA_STORE", "/var/lib/astra")
SAMPLE = os.path.join(STORE, "hostfs/work/.astra/sample")
HOST_COMMS = ("qemu-system-m68", "astra-terminal-", "astra-audio-hos",
              "astra-remote-de")
SAMPLE_MAGIC = 0x41534D31
HEADER = struct.Struct(">6I")
SCHEDULER = struct.Struct(">Q16I")
# AstraSchedulerStats, sw/include/astra/proc.h. The seven *_switches are
# each switch's one cause and sum to context_switches.
SWITCH_CAUSES = ("block", "yield", "quantum", "deadline", "preempt", "exit",
                 "idle")
SCHEDULER_FIELDS = (
    ("context_switches", "same_space_switches", "cross_space_switches") +
    tuple(cause + "_switches" for cause in SWITCH_CAUSES) +
    ("wait_blocks", "quantum_expirations", "syscalls_low", "syscalls_high",
     "live_processes", "live_threads"))
# AstraProcessInfo (80 bytes) then a 32-byte name: sw/include/astra/proc.h.
PROCESS = struct.Struct(">9IH8B2xQQIIHHI32s")
CLOCK_TICKS = os.sysconf("SC_CLK_TCK")


def read_sample(path=SAMPLE):
    with open(path, "rb") as handle:
        data = handle.read()
    magic, header_size, scheduler_size, record_size, count, sequence = \
        HEADER.unpack_from(data)
    if (magic != SAMPLE_MAGIC or header_size != HEADER.size or
            scheduler_size != SCHEDULER.size or record_size != PROCESS.size or
            len(data) != header_size + scheduler_size + count * record_size):
        raise ValueError("%s: not a sampler file this tool understands" % path)
    values = SCHEDULER.unpack_from(data, header_size)
    scheduler = dict(zip(SCHEDULER_FIELDS, values[1:]))
    scheduler["now_ns"] = values[0]
    processes = {}
    for index in range(count):
        fields = PROCESS.unpack_from(
            data, header_size + scheduler_size + index * record_size)
        (_size, pid, generation, _owner, resident, runs, _ticks, syscalls,
         _exit_status, _handles, state, _threads, live, priority, ceiling,
         _reason, _thread_state, suspended, runtime_ns, elapsed_ns,
         _fault_pc, _fault_address, _vector, _status, _peak, name) = fields
        if pid == 0:
            continue
        processes[(pid, generation)] = {
            "pid": pid, "name": name.split(b"\0", 1)[0].decode(
                "utf-8", "replace"),
            "state": state,
            "suspended": suspended, "threads": live, "priority": priority,
            "ceiling": ceiling, "resident_kib": resident * 4,
            "runs": runs, "syscalls": syscalls, "runtime_ns": runtime_ns,
            "elapsed_ns": elapsed_ns,
        }
    return sequence, scheduler, processes


def wait_for_new_sample(after, timeout=5.0):
    deadline = time.monotonic() + timeout
    while True:
        sequence, scheduler, processes = read_sample()
        if sequence != after:
            return sequence, scheduler, processes
        if time.monotonic() > deadline:
            raise RuntimeError("the sampler has stopped writing %s" % SAMPLE)
        time.sleep(0.05)


def host_threads():
    out = {}
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            comm = open("/proc/%s/comm" % pid).read().strip()
        except OSError:
            continue
        if not comm.startswith(HOST_COMMS):
            continue
        try:
            tids = os.listdir("/proc/%s/task" % pid)
        except OSError:
            continue
        for tid in tids:
            try:
                stat = open("/proc/%s/task/%s/stat" % (pid, tid)).read()
            except OSError:
                continue
            name = stat[stat.index("(") + 1:stat.rindex(")")]
            fields = stat[stat.rindex(")") + 2:].split()
            out[(comm, int(tid), name)] = (int(fields[11]) + int(fields[12]),
                                           int(fields[36]))
    return out


def vcpu_thread():
    """The vCPU thread: named "CPU 0/TCG" when QEMU names its threads,
    otherwise the QEMU thread that has used the most CPU."""
    qemu = [(ticks, name, tid)
            for (comm, tid, name), (ticks, _) in host_threads().items()
            if comm.startswith("qemu-system-m68")]
    for _, name, tid in qemu:
        if "TCG" in name:
            return tid
    return max(qemu)[2] if qemu else None


def audio_counters():
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as audio:
            audio.settimeout(2)
            audio.connect("/run/astra/audio.sock")
            audio.send(struct.pack("<8I", 0x41554431, 6, 4, 0, 0, 0, 0, 0))
            values = struct.unpack("<10I", audio.recv(65536)[:40])
        return {"underruns": values[6], "software_gaps": values[8]}
    except OSError:
        return None


def fpga_window():
    try:
        fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
        try:
            return mmap.mmap(fd, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ,
                             offset=0x20100000)
        finally:
            os.close(fd)
    except OSError:
        return None


FPGA = fpga_window()
SCENE_GENERATION = 0x14
POINTER_GENERATION = 0x178


def fpga(offset):
    return None if FPGA is None else struct.unpack_from("<I", FPGA, offset)[0]


def presents():
    return fpga(SCENE_GENERATION)


def drive_pointer(hz, stop, sent):
    """QMP absolute motion in a circle, the way pointer-smooth measured it."""
    with socket.socket(socket.AF_UNIX) as qmp:
        qmp.settimeout(10)
        qmp.connect("/run/astra/qmp.sock")
        stream = qmp.makefile("rwb", buffering=0)

        def reply():
            while True:
                answer = json.loads(stream.readline())
                if "event" not in answer:
                    return answer

        def execute(command, arguments=None):
            message = {"execute": command}
            if arguments is not None:
                message["arguments"] = arguments
            stream.write(json.dumps(message).encode() + b"\n")
            return reply()

        reply()
        execute("qmp_capabilities")
        step, due = 0, time.monotonic()
        while not stop.is_set():
            angle = step * 0.05
            execute("input-send-event", {"events": [
                {"type": "abs", "data": {"axis": "x", "value":
                                         int(640 + 300 * math.cos(angle))}},
                {"type": "abs", "data": {"axis": "y", "value":
                                         int(360 + 200 * math.sin(angle))}}]})
            step += 1
            sent[0] = step
            due += 1.0 / hz
            time.sleep(max(0.0, due - time.monotonic()))


def watch_pointer(stop, commits):
    last = fpga(POINTER_GENERATION)
    while not stop.is_set():
        now = fpga(POINTER_GENERATION)
        if now != last:
            commits.append(time.monotonic())
            last = now
        time.sleep(0.0005)


def pointer_report(commits, sent, wall):
    gaps = sorted((b - a) * 1000.0 for a, b in zip(commits, commits[1:]))
    report = {"input_per_s": sent / wall,
              "commits_per_s": len(commits) / wall}
    if gaps:
        report.update(gap_ms_p50=gaps[len(gaps) // 2],
                      gap_ms_p99=gaps[min(len(gaps) - 1,
                                          int(len(gaps) * 0.99))],
                      gap_ms_max=gaps[-1],
                      gaps_over_50ms=sum(gap > 50.0 for gap in gaps))
    return report


def elf_functions(path):
    """(address, size, name) of every function in an ELF64 little-endian
    file's .symtab, sorted: enough to name a perf sample in a binary perf
    itself will not resolve."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        return []
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum = struct.unpack_from("<HH", data, 0x3A)
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize)
                for i in range(shnum)]
    functions = []
    for (_name, kind, _flags, _addr, offset, size, link, _info, _align,
         entsize) in sections:
        if kind != 2 or entsize == 0:      # SHT_SYMTAB
            continue
        strings = sections[link][4]
        for at in range(offset, offset + size, entsize):
            name, info, _other, _shndx, value, length = \
                struct.unpack_from("<IBBHQQ", data, at)
            if info & 0xF == 2 and value:  # STT_FUNC
                end = data.index(b"\0", strings + name)
                functions.append((value, length, data[strings + name:end]
                                  .decode("utf-8", "replace")))
    functions.sort()
    return functions


def name_address(functions, address):
    index = bisect.bisect_right(functions, (address, float("inf"), "")) - 1
    if index >= 0:
        start, length, name = functions[index]
        if address < start + max(length, 1):
            return name
    return None


def run_perf(tid, seconds, out):
    perf = "/var/lib/astra/tools/perf"
    if not os.access(perf, os.X_OK):
        perf = "perf"
    with tempfile.NamedTemporaryFile(suffix=".data", dir="/data" if
                                     os.path.isdir("/data") else None) as data:
        record = subprocess.run(
            [perf, "record", "-F", "999", "-t", str(tid), "-o", data.name,
             "--", "sleep", str(seconds)], capture_output=True, text=True)
        if record.returncode != 0:
            out["error"] = record.stderr.strip().splitlines()[-1:] or ["?"]
            return
        report = subprocess.run(
            [perf, "report", "-i", data.name, "--no-children",
             "--sort", "dso,symbol", "--stdio"],
            capture_output=True, text=True)
        try:
            functions = elf_functions(os.readlink("/proc/%d/exe" % tid))
        except (OSError, ValueError, struct.error):
            functions = []
        totals = {}
        for line in report.stdout.splitlines():
            parts = line.split()
            if len(parts) < 3 or not parts[0].endswith("%"):
                continue
            percent, dso = float(parts[0][:-1]), parts[1]
            if dso == "[JIT]":
                symbol = "(translated guest code)"
            else:
                symbol = None
                for word in parts[2:]:
                    if word.startswith("0x"):
                        symbol = name_address(functions, int(word, 16))
                        break
                if symbol is None:
                    symbol = " ".join(w for w in parts[2:]
                                      if w not in ("[.]", "[k]", "-"))
            key = (dso, symbol)
            totals[key] = totals.get(key, 0.0) + percent
        out["rows"] = [{"percent": percent, "dso": dso, "symbol": symbol}
                       for (dso, symbol), percent in
                       sorted(totals.items(), key=lambda item: -item[1])]


def delta32(after, before):
    return (after - before) & 0xFFFFFFFF


def guest_window(before, procs_before, after, procs_after):
    """CPU per process and scheduler rates between two samples."""
    guest_ns = after["now_ns"] - before["now_ns"]
    if guest_ns <= 0:
        raise ValueError("the samples are not in time order")
    rows = []
    busy = 0.0
    for key, now in procs_after.items():
        then = procs_before.get(key)
        runtime = now["runtime_ns"] - (then["runtime_ns"] if then else 0)
        runs = delta32(now["runs"], then["runs"] if then else 0)
        calls = delta32(now["syscalls"], then["syscalls"] if then else 0)
        cpu = 100.0 * runtime / guest_ns
        busy += cpu
        rows.append(dict(now, cpu=cpu, runs_per_s=runs * 1e9 / guest_ns,
                         syscalls_per_s=calls * 1e9 / guest_ns,
                         new=then is None))
    rows.sort(key=lambda row: -row["cpu"])
    rate = {name: delta32(after[name], before[name]) * 1e9 / guest_ns
            for name in SCHEDULER_FIELDS
            if name not in ("syscalls_low", "syscalls_high",
                            "live_processes", "live_threads")}
    return {"window_s": guest_ns / 1e9, "processes": rows,
            "idle": max(0.0, 100.0 - busy), "scheduler_per_s": rate}


def measure(seconds, want_perf, motion_hz=None):
    sequence, before, procs_before = read_sample()
    sequence, before, procs_before = wait_for_new_sample(sequence)
    host_before = host_threads()
    audio_before = audio_counters()
    presents_before = presents()
    wall_start = time.monotonic()
    perf = {}
    perf_thread = None
    if want_perf:
        tid = vcpu_thread()
        if tid is not None:
            perf_thread = threading.Thread(target=run_perf,
                                           args=(tid, seconds, perf))
            perf_thread.start()
        else:
            perf["error"] = ["no QEMU vCPU thread"]
    stop, sent, commits, movers = threading.Event(), [0], [], []
    if motion_hz:
        if FPGA is None:
            raise RuntimeError("--motion needs /dev/mem (run as root)")
        movers = [threading.Thread(target=drive_pointer,
                                   args=(motion_hz, stop, sent)),
                  threading.Thread(target=watch_pointer,
                                   args=(stop, commits))]
        for thread in movers:
            thread.start()
    time.sleep(seconds)
    stop.set()
    for thread in movers:
        thread.join()
    sequence, after, procs_after = wait_for_new_sample(sequence)
    wall = time.monotonic() - wall_start
    host_after = host_threads()
    audio_after = audio_counters()
    presents_after = presents()
    if perf_thread is not None:
        perf_thread.join()

    guest = guest_window(before, procs_before, after, procs_after)
    threads = []
    for key, (ticks, cpu_number) in host_after.items():
        then = host_before.get(key)
        used = ticks - (then[0] if then else 0)
        threads.append({"process": key[0], "tid": key[1], "thread": key[2],
                        "cpu": 100.0 * used / CLOCK_TICKS / wall,
                        "on_cpu": cpu_number})
    threads.sort(key=lambda row: -row["cpu"])
    result = {
        "window_s": wall,
        "guest_window_s": guest["window_s"],
        "guest": guest,
        "host_threads": threads,
    }
    if presents_before is not None and presents_after is not None:
        result["presents_per_s"] = delta32(presents_after,
                                           presents_before) / wall
    if audio_before is not None and audio_after is not None:
        result["audio"] = {name: delta32(audio_after[name],
                                         audio_before[name])
                           for name in audio_after}
    if motion_hz:
        result["pointer"] = pointer_report(commits, sent[0], wall)
    if want_perf:
        result["perf"] = perf
    return result


def show(result, top):
    print("window %.1f s (guest %.2f s)" % (result["window_s"],
                                             result["guest_window_s"]))
    if "presents_per_s" in result:
        print("display  %.1f presents/s" % result["presents_per_s"])
    if "audio" in result:
        print("audio    %d underruns, %d software gaps" % (
            result["audio"]["underruns"], result["audio"]["software_gaps"]))
    pointer = result.get("pointer")
    if pointer is not None:
        print("pointer  %.0f events/s in, %.1f commits/s on screen" % (
            pointer["input_per_s"], pointer["commits_per_s"]), end="")
        if "gap_ms_max" in pointer:
            print("; gap p50 %.1f p99 %.1f max %.1f ms, %d over 50 ms" % (
                pointer["gap_ms_p50"], pointer["gap_ms_p99"],
                pointer["gap_ms_max"], pointer["gaps_over_50ms"]))
        else:
            print("; no commits")
    guest = result["guest"]
    rate = guest["scheduler_per_s"]
    print("\nguest    idle %.1f%%   switches %.0f/s (%.0f cross-space, "
          "%.0f same)" % (guest["idle"], rate["context_switches"],
                          rate["cross_space_switches"],
                          rate["same_space_switches"]))
    print("         by cause: " + "  ".join(
        "%s %.0f/s" % (cause, rate[cause + "_switches"])
        for cause in SWITCH_CAUSES))
    print("\n  %5s %4s %6s %8s %9s %6s  %s" % ("PID", "PRI", "CPU%",
                                              "RUNS/s", "SYSCALL/s", "RES-K",
                                              "NAME"))
    for row in guest["processes"][:top]:
        print("  %5d %4d %6.1f %8.0f %9.0f %6d  %s%s" % (
            row["pid"], row["priority"], row["cpu"], row["runs_per_s"],
            row["syscalls_per_s"], row["resident_kib"], row["name"],
            " (stopped)" if row["suspended"] else ""))
    print("\n  %-16s %-20s %8s %6s %4s" % ("HOST", "THREAD", "TID", "CPU%",
                                          "CORE"))
    for row in result["host_threads"][:top]:
        if row["cpu"] < 0.5:
            break
        print("  %-16s %-20s %8d %6.1f %4d" % (
            row["process"], row["thread"], row["tid"], row["cpu"],
            row["on_cpu"]))
    perf = result.get("perf")
    if perf is not None:
        print("\nvCPU thread hot spots (host perf)")
        if "error" in perf:
            print("  perf failed: %s" % " ".join(perf["error"]))
        for row in perf.get("rows", [])[:top]:
            print("  %6.2f%%  %-24s %s" % (row["percent"], row["dso"][:24],
                                           row["symbol"]))


def main():
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("seconds", nargs="?", type=float, default=5.0)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--perf", action="store_true")
    parser.add_argument("--motion", type=float, metavar="HZ")
    parser.add_argument("--top", type=int, default=15)
    args = parser.parse_args()
    if args.seconds < 1.0:
        parser.error("the window must be at least the sampler's 1 s period")
    try:
        result = measure(args.seconds, args.perf, args.motion)
    except (OSError, ValueError, RuntimeError) as error:
        print("astra-top: %s" % error, file=sys.stderr)
        return 1
    if args.json:
        json.dump(result, sys.stdout, indent=1)
        print()
    else:
        show(result, args.top)
    return 0


if __name__ == "__main__":
    sys.exit(main())
