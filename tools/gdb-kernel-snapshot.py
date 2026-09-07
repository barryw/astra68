"""GDB command that prints Axiom's live scheduler state."""

import gdb


def number(value):
    return int(value)


def entries(name):
    values = gdb.parse_and_eval(name)
    first, last = values.type.range()
    return ((index, values[index]) for index in range(first, last + 1))


def address(value):
    return number(value.address)


def prefix(value, count):
    return "".join("%02x" % (number(value[index]) & 0xff)
                   for index in range(count))


def stack_usage(thread):
    base = number(thread["kernel_stack_base"])
    top = number(thread["kernel_stack_top"])
    observed = top - number(thread["kernel_stack_low_water"])
    poison = b"\xa5\xa5\xa5\xa5"
    contents = bytes(gdb.selected_inferior().read_memory(
        base + 4, top - base - 4))
    poisoned = 0
    for offset in range(0, len(contents), 4):
        if contents[offset:offset + 4] != poison:
            poisoned = top - (base + 4 + offset)
            break
    return max(observed, poisoned), top - base


class AstraKernelSnapshot(gdb.Command):
    def __init__(self):
        super().__init__("astra-kernel-snapshot", gdb.COMMAND_STATUS)

    def invoke(self, _argument, _from_tty):
        stats = gdb.parse_and_eval("scheduler_stats")
        gdb.write(
            "scheduler current=%08x/%08x live=%u/%u switches=%u "
            "syscalls=%08x%08x\n" % (
                number(stats["current_process_id"]),
                number(stats["current_thread_id"]),
                number(stats["live_processes"]),
                number(stats["live_threads"]),
                number(stats["context_switches"]),
                number(stats["total_syscalls_high"]),
                number(stats["total_syscalls_low"])))
        gdb.write(
            "queues ready=%08x count=%u deadlines=%u waits=%u\n" % (
                number(gdb.parse_and_eval("ready_bitmap")),
                number(gdb.parse_and_eval("ready_count")),
                number(gdb.parse_and_eval("deadline_count")),
                number(gdb.parse_and_eval("wait_registration_count"))))

        for index, process in entries("processes"):
            if number(process["process_state"]) == 0:
                continue
            gdb.write(
                "P%-2u id=%08x state=%u progress=%u live=%u image=%u "
                "entry=%08x\n" % (
                    index, number(process["id"]),
                    number(process["process_state"]),
                    number(process["progress"]),
                    number(process["live_threads"]),
                    number(process["image_size"]),
                    number(process["entry_base"])))
            table = process["handles"].dereference()
            handle_entries = table["entries"]
            first, last = handle_entries.type.range()
            for slot in range(first, last + 1):
                entry = handle_entries[slot]
                if number(entry["occupied"]) == 0:
                    continue
                handle = (number(entry["generation"]) << 8) | slot + 1
                gdb.write(
                    "  H%-3u handle=%08x type=%u rights=%08x object=%08x "
                    "context=%08x\n" % (
                        slot, handle, number(entry["type"]),
                        number(entry["rights"]), number(entry["object"]),
                        number(entry["release_context"])))

        for index, thread in entries("threads"):
            if number(thread["occupied"]) == 0:
                continue
            context = thread["context"]
            used, capacity = stack_usage(thread)
            gdb.write(
                "T%-2u id=%08x process=%08x state=%u pc=%08x sp=%08x "
                "wait=%u/%u syscalls=%u runs=%u kstack=%u/%u\n" % (
                    index, number(thread["id"]),
                    number(thread["process_id"]), number(thread["state"]),
                    number(context["program_counter"]),
                    number(context["usp"]), number(thread["wait_mode"]),
                    number(thread["wait_member_count"]),
                    number(thread["syscall_count"]),
                    number(thread["run_count"]), used, capacity))

            for member in range(number(thread["wait_member_count"])):
                registration = gdb.parse_and_eval(
                    "wait_registrations[%u][%u]" % (index, member))
                gdb.write(
                    "  W%-2u queue=%08x previous=%u next=%u\n" % (
                        member, number(registration["queue"]),
                        number(registration["previous"]),
                        number(registration["next"])))

        for index, port in entries("ports"):
            if number(port["state"]) == 0:
                continue
            gdb.write(
                "O%-3u owner=%08x state=%u refs=%u/%u/%u queued=%u/%u "
                "limit=%u/%u wait=%u/%u seq=%u/%u queue=%08x/%08x\n" % (
                    index, number(port["owner"]), number(port["state"]),
                    number(port["references"]),
                    number(port["send_references"]),
                    number(port["receive_references"]),
                    number(port["queued_messages"]),
                    number(port["queued_bytes"]),
                    number(port["maximum_messages"]),
                    number(port["maximum_bytes"]),
                    number(port["readable"]["count"]),
                    number(port["writable"]["count"]),
                    number(port["readable"]["sequence"]),
                    number(port["writable"]["sequence"]),
                    address(port["readable"]), address(port["writable"])))

        for index, message in entries("messages"):
            if number(message["state"]) == 0:
                continue
            size = number(message["size"])
            gdb.write(
                "M%-3u state=%u port=%u sender=%08x size=%u handles=%u "
                "next=%u data=%s\n" % (
                    index, number(message["state"]),
                    number(message["port_slot"]), number(message["sender"]),
                    size, number(message["handle_count"]),
                    number(message["next"]),
                    prefix(message["data"], min(size, 24))))

        for index, load in entries("executable_loads"):
            if number(load["stage"]) == 0:
                continue
            process = load["process"]
            process_id = 0 if number(process) == 0 else number(
                process.dereference()["id"])
            gdb.write(
                "L%-2u stage=%u process=%08x segment=%u page=%u\n" % (
                    index, number(load["stage"]), process_id,
                    number(load["segment"]), number(load["page"])))


AstraKernelSnapshot()
