import pytest

from sw.boot import check_hardware
from sw.boot.check_hardware import acceptance_reached, failure_reached


def test_post_is_enough_by_default() -> None:
    assert acceptance_reached(b"POST PASS\n", None, False)


def test_expected_build_must_match() -> None:
    expected = b"BUILD: 0x25B55C0A"

    assert not acceptance_reached(b"POST PASS\n", expected, False)
    assert acceptance_reached(b"BUILD: 0x25B55C0A\nPOST PASS\n", expected, False)
    assert acceptance_reached(b"BUILD:  0x25B55C0A\r\nPOST PASS\r\n", expected, False)
    assert not acceptance_reached(b"BUILD:  0xDEADBEEF\nPOST PASS\n", expected, False)


def test_kernel_entry_can_be_required() -> None:
    output = bytearray(b"POST PASS\n")

    assert not acceptance_reached(output, None, True)
    output.extend(b"K0 ENTRY PASS\n")
    assert acceptance_reached(output, None, True)


def test_k1_entry_is_required_exactly() -> None:
    output = b"POST PASS\nK0 ENTRY PASS\n"

    assert not acceptance_reached(output, None, False, expect_k1_entry=True)
    output += b"K1 PROTECTED ENTRY PASS\n"
    assert acceptance_reached(output, None, False, expect_k1_entry=True)


def test_k2_entry_requires_new_and_retained_markers() -> None:
    output = b"POST PASS\nK1 PROTECTED ENTRY PASS\n"

    assert not acceptance_reached(output, None, False, expect_k2_entry=True)
    output += b"K2 THREAD SUBSTRATE PASS\n"
    assert acceptance_reached(output, None, False, expect_k2_entry=True)


def test_k2_blocking_requires_all_retained_markers() -> None:
    output = b"POST PASS\nK1 PROTECTED ENTRY PASS\n"

    assert not acceptance_reached(output, None, False, expect_k2_blocking=True)
    output += b"K2 THREAD SUBSTRATE PASS\n"
    assert not acceptance_reached(output, None, False, expect_k2_blocking=True)
    output += b"K2 BLOCKING SUBSTRATE PASS\n"
    assert not acceptance_reached(output, None, False, expect_k2_blocking=True)
    output += (
        b"K2 PERF irq syscall=1200/50000 timer=1300/50000 "
        b"fault=9000/125000\n"
        b"K2 PERF sched pick=400/10000 same=500/15000 "
        b"cross=1800/50000\n"
        b"K2 PERF wait block=600/15000 wake=700/15000 overruns=0\n"
        b"K2 PERFORMANCE PASS\n"
    )
    assert acceptance_reached(output, None, False, expect_k2_blocking=True)


def test_k3_scheduler_requires_new_retained_and_bounded_markers() -> None:
    output = (
        b"POST PASS\n"
        b"K2 PERF irq syscall=1200/50000 timer=1300/50000 "
        b"fault=9000/125000\n"
        b"K2 PERF sched pick=400/10000 same=500/15000 "
        b"cross=1800/50000\n"
        b"K2 PERF wait block=600/15000 wake=700/15000 overruns=0\n"
        b"K2 PERFORMANCE PASS\n"
        b"K2 BLOCKING SUBSTRATE PASS\n"
        b"K2 THREAD SUBSTRATE PASS\n"
        b"K1 PROTECTED ENTRY PASS\n"
    )

    assert not acceptance_reached(
        output, None, False, expect_k3_scheduler=True
    )
    output += (
        b"K3 PERF deadline expire=800/20000 overruns=0\n"
        b"K3 ONE-SHOT SCHEDULER PASS\n"
        b"K3 DEADLINE QUEUE PASS\n"
    )
    assert acceptance_reached(output, None, False, expect_k3_scheduler=True)
    assert not acceptance_reached(
        output.replace(b"expire=800", b"expire=0"),
        None,
        False,
        expect_k3_scheduler=True,
    )
    assert not acceptance_reached(
        output.replace(b"800/20000", b"800/20001"),
        None,
        False,
        expect_k3_scheduler=True,
    )
    assert not acceptance_reached(
        output.replace(b"K3 DEADLINE QUEUE PASS\n", b""),
        None,
        False,
        expect_k3_scheduler=True,
    )


def test_k4_synchronization_requires_exact_lifecycle_and_handoffs() -> None:
    output = (
        b"POST PASS\n"
        b"K2 PERF irq syscall=1200/50000 timer=1300/50000 "
        b"fault=9000/125000\n"
        b"K2 PERF sched pick=400/10000 same=500/15000 "
        b"cross=1800/50000\n"
        b"K2 PERF wait block=600/15000 wake=700/15000 overruns=0\n"
        b"K3 PERF deadline expire=800/20000 overruns=0\n"
        b"K2 PERFORMANCE PASS\n"
        b"Wait/wake ........... 6 blocks, 2 wake, 3 priority handoff\n"
        b"Deadlines ........... 1 expired, 1 priority handoff\n"
        b"Sync objects ........ 4 event, 1 sem; cancel/close/death 1/1/1\n"
        b"K4 HANDLE SYNCHRONIZATION PASS\n"
        b"K3 ONE-SHOT SCHEDULER PASS\n"
        b"K3 DEADLINE QUEUE PASS\n"
        b"K2 BLOCKING SUBSTRATE PASS\n"
        b"K2 THREAD SUBSTRATE PASS\n"
        b"K1 PROTECTED ENTRY PASS\n"
    )

    assert acceptance_reached(
        output, None, False, expect_k4_synchronization=True
    )
    for missing in (
        b"K4 HANDLE SYNCHRONIZATION PASS\n",
        b"Wait/wake ........... 6 blocks, 2 wake, 3 priority handoff\n",
        b"Deadlines ........... 1 expired, 1 priority handoff\n",
        b"Sync objects ........ 4 event, 1 sem; cancel/close/death 1/1/1\n",
    ):
        assert not acceptance_reached(
            output.replace(missing, b""),
            None,
            False,
            expect_k4_synchronization=True,
        )
    for old, new in (
        (b"4 event", b"5 event"),
        (b"1 sem", b"2 sem"),
        (b"cancel/close/death 1/1/1", b"cancel/close/death 0/1/1"),
        (b"3 priority handoff", b"2 priority handoff"),
    ):
        assert not acceptance_reached(
            output.replace(old, new, 1),
            None,
            False,
            expect_k4_synchronization=True,
        )


def test_k5_thread_lifecycle_requires_exact_counts_budgets_and_history() -> None:
    output = (
        b"POST PASS\n"
        b"K2 PERF irq syscall=1200/50000 timer=1300/50000 "
        b"fault=9000/125000\n"
        b"K2 PERF sched pick=400/10000 same=500/15000 "
        b"cross=1800/50000\n"
        b"K2 PERF wait block=600/15000 wake=700/15000 overruns=0\n"
        b"K3 PERF deadline expire=800/20000 overruns=0\n"
        b"K5 PERF thread create=102670/150000 exit=7427/50000 "
        b"reap=33657/125000 overruns=0\n"
        b"K2 PERFORMANCE PASS\n"
        b"Wait/wake ........... 6 blocks, 2 wake, 3 priority handoff\n"
        b"Deadlines ........... 1 expired, 1 priority handoff\n"
        b"Sync objects ........ 4 event, 1 sem; cancel/close/death 1/1/1\n"
        b"Thread lifecycle .... 1 exit, 2 waits, 1 reaped\n"
        b"K5 THREAD LIFECYCLE PASS\n"
        b"K4 HANDLE SYNCHRONIZATION PASS\n"
        b"K3 ONE-SHOT SCHEDULER PASS\n"
        b"K3 DEADLINE QUEUE PASS\n"
        b"K2 BLOCKING SUBSTRATE PASS\n"
        b"K2 THREAD SUBSTRATE PASS\n"
        b"K1 PROTECTED ENTRY PASS\n"
    )

    assert acceptance_reached(
        output, None, False, expect_k5_thread_lifecycle=True
    )
    for missing in (
        b"Thread lifecycle .... 1 exit, 2 waits, 1 reaped\n",
        b"K5 PERF thread create=102670/150000 exit=7427/50000 "
        b"reap=33657/125000 overruns=0\n",
        b"K5 THREAD LIFECYCLE PASS\n",
        b"K4 HANDLE SYNCHRONIZATION PASS\n",
    ):
        assert not acceptance_reached(
            output.replace(missing, b""),
            None,
            False,
            expect_k5_thread_lifecycle=True,
        )
    for old, new in (
        (b"1 exit, 2 waits, 1 reaped", b"2 exit, 2 waits, 1 reaped"),
        (b"create=102670", b"create=150001"),
        (b"102670/150000", b"102670/150001"),
        (b"exit=7427", b"exit=0"),
        (b"reap=33657/125000", b"reap=33657/124999"),
        (b"overruns=0", b"overruns=1"),
    ):
        assert not acceptance_reached(
            output.replace(old, new, 1),
            None,
            False,
            expect_k5_thread_lifecycle=True,
        )


def test_k6_wait_multiple_requires_exact_counts_budgets_and_history() -> None:
    output = (
        b"POST PASS\n"
        b"K2 PERF irq syscall=23349/50000 timer=13742/50000 "
        b"fault=13283/125000\n"
        b"K2 PERF sched pick=776/10000 same=1268/15000 "
        b"cross=1555/50000\n"
        b"K2 PERF wait block=2837/15000 wake=4355/15000 overruns=0\n"
        b"K3 PERF deadline expire=6041/20000 overruns=0\n"
        b"K5 PERF thread create=71837/150000 exit=12424/50000 "
        b"reap=34719/125000 overruns=0\n"
        b"K6 PERF wait-set block=3690/50000 wake=5545/50000 overruns=0\n"
        b"K2 PERFORMANCE PASS\n"
        b"Wait/wake ........... 11 blocks, 5 wake, 5 priority handoff\n"
        b"Deadlines ........... 2 expired, 1 priority handoff\n"
        b"Sync objects ........ 4 event, 1 sem; cancel/close/death 1/1/1\n"
        b"Thread lifecycle .... 2 exit, 3 waits, 2 reaped\n"
        b"Wait multiple ....... 7 calls, 4 block, 4 wake; max 2 members\n"
        b"Wait registrations .. 1 live, 3 max\n"
        b"Waitable timers ..... 1 created, 1 armed, 1 expired\n"
        b"Process death ....... 1 waits, 0 blocked wakes\n"
        b"K6 BOUNDED WAIT-MULTIPLE PASS\n"
        b"K5 THREAD LIFECYCLE PASS\n"
        b"K4 HANDLE SYNCHRONIZATION PASS\n"
        b"K3 ONE-SHOT SCHEDULER PASS\n"
        b"K3 DEADLINE QUEUE PASS\n"
        b"K2 BLOCKING SUBSTRATE PASS\n"
        b"K2 THREAD SUBSTRATE PASS\n"
        b"K1 PROTECTED ENTRY PASS\n"
    )

    assert acceptance_reached(
        output, None, False, expect_k6_wait_multiple=True
    )
    for missing in (
        b"Wait multiple ....... 7 calls, 4 block, 4 wake; max 2 members\n",
        b"Wait registrations .. 1 live, 3 max\n",
        b"Waitable timers ..... 1 created, 1 armed, 1 expired\n",
        b"Process death ....... 1 waits, 0 blocked wakes\n",
        b"K6 PERF wait-set block=3690/50000 wake=5545/50000 overruns=0\n",
        b"K6 BOUNDED WAIT-MULTIPLE PASS\n",
        b"K5 THREAD LIFECYCLE PASS\n",
    ):
        assert not acceptance_reached(
            output.replace(missing, b""),
            None,
            False,
            expect_k6_wait_multiple=True,
        )
    for old, new in (
        (b"11 blocks, 5 wake, 5 priority handoff", b"10 blocks, 5 wake, 5 priority handoff"),
        (b"2 expired, 1 priority handoff", b"1 expired, 1 priority handoff"),
        (b"2 exit, 3 waits, 2 reaped", b"2 exit, 2 waits, 2 reaped"),
        (b"7 calls, 4 block, 4 wake", b"7 calls, 3 block, 4 wake"),
        (b"max 2 members", b"max 3 members"),
        (b"1 live, 3 max", b"0 live, 3 max"),
        (b"1 created, 1 armed, 1 expired", b"1 created, 1 armed, 0 expired"),
        (b"1 waits, 0 blocked wakes", b"1 waits, 1 blocked wakes"),
        (b"block=3690", b"block=50001"),
        (b"3690/50000", b"3690/50001"),
        (b"wake=5545", b"wake=0"),
    ):
        assert not acceptance_reached(
            output.replace(old, new, 1),
            None,
            False,
            expect_k6_wait_multiple=True,
        )


def test_k7_message_ports_requires_exact_counts_budgets_and_history() -> None:
    output = (
        b"POST PASS\n"
        b"K2 PERF irq syscall=23349/50000 timer=13742/50000 "
        b"fault=13283/125000\n"
        b"K2 PERF sched pick=776/10000 same=1268/15000 "
        b"cross=1555/50000\n"
        b"K2 PERF wait block=2837/15000 wake=4355/15000 overruns=0\n"
        b"K3 PERF deadline expire=6041/20000 overruns=0\n"
        b"K5 PERF thread create=71837/150000 exit=12424/50000 "
        b"reap=34719/125000 overruns=0\n"
        b"K6 PERF wait-set block=3690/50000 wake=5545/50000 overruns=0\n"
        b"K7 PERF port send=7119/25000 receive=9869/30000 overruns=0\n"
        b"K2 PERFORMANCE PASS\n"
        b"Wait/wake ........... 11 blocks, 5 wake, 5 priority handoff\n"
        b"Deadlines ........... 2 expired, 1 priority handoff\n"
        b"Sync objects ........ 6 event, 1 sem; cancel/close/death 1/1/1\n"
        b"Thread lifecycle .... 2 exit, 3 waits, 2 reaped\n"
        b"Wait multiple ....... 7 calls, 4 block, 4 wake; max 2 members\n"
        b"Wait registrations .. 1 live, 3 max\n"
        b"Waitable timers ..... 1 created, 1 armed, 1 expired\n"
        b"Process death ....... 1 waits, 0 blocked wakes\n"
        b"Message ports ....... 3 send, 3 receive, 1 backpressure\n"
        b"Handle transfer ..... 2 committed, max detached 1\n"
        b"K7 MESSAGE PORTS PASS\n"
        b"K6 BOUNDED WAIT-MULTIPLE PASS\n"
        b"K5 THREAD LIFECYCLE PASS\n"
        b"K4 HANDLE SYNCHRONIZATION PASS\n"
        b"K3 ONE-SHOT SCHEDULER PASS\n"
        b"K3 DEADLINE QUEUE PASS\n"
        b"K2 BLOCKING SUBSTRATE PASS\n"
        b"K2 THREAD SUBSTRATE PASS\n"
        b"K1 PROTECTED ENTRY PASS\n"
    )

    assert acceptance_reached(
        output, None, False, expect_k7_message_ports=True
    )
    for missing in (
        b"Message ports ....... 3 send, 3 receive, 1 backpressure\n",
        b"Handle transfer ..... 2 committed, max detached 1\n",
        b"K7 PERF port send=7119/25000 receive=9869/30000 overruns=0\n",
        b"K7 MESSAGE PORTS PASS\n",
    ):
        assert not acceptance_reached(
            output.replace(missing, b""),
            None,
            False,
            expect_k7_message_ports=True,
        )
    for old, new in (
        (b"3 send, 3 receive, 1 backpressure", b"2 send, 3 receive, 1 backpressure"),
        (b"2 committed, max detached 1", b"1 committed, max detached 1"),
        (b"2 committed, max detached 1", b"2 committed, max detached 2"),
        (b"send=7119", b"send=0"),
        (b"send=7119", b"send=25001"),
        (b"7119/25000", b"7119/25001"),
        (b"receive=9869", b"receive=30001"),
        (b"9869/30000", b"9869/30001"),
        (
            b"K7 PERF port send=7119/25000 receive=9869/30000 overruns=0",
            b"K7 PERF port send=7119/25000 receive=9869/30000 overruns=1",
        ),
        (b"Sync objects ........ 6 event", b"Sync objects ........ 4 event"),
    ):
        assert not acceptance_reached(
            output.replace(old, new, 1),
            None,
            False,
            expect_k7_message_ports=True,
        )


def test_k8_shared_bulk_requires_exact_cleanup_wake_budgets_and_history() -> None:
    output = (
        b"POST PASS\n"
        b"K2 PERF irq syscall=28837/50000 timer=13850/50000 "
        b"fault=19688/125000\n"
        b"K2 PERF sched pick=768/10000 same=1379/15000 "
        b"cross=1666/50000\n"
        b"K2 PERF wait block=2825/15000 wake=4303/15000 overruns=0\n"
        b"K3 PERF deadline expire=5963/20000 overruns=0\n"
        b"K5 PERF thread create=70824/150000 exit=12908/50000 "
        b"reap=19522/125000 overruns=0\n"
        b"K6 PERF wait-set block=3674/50000 wake=5485/50000 overruns=0\n"
        b"K7 PERF port send=7174/25000 receive=9914/30000 overruns=0\n"
        b"K8 PERF area create=25288/250000 map=37472/125000 "
        b"unmap=43238/100000 notify=15409/30000 overruns=0\n"
        b"K2 PERFORMANCE PASS\n"
        b"Wait/wake ........... 12 blocks, 5 wake, 6 priority handoff\n"
        b"Deadlines ........... 2 expired, 1 priority handoff\n"
        b"Sync objects ........ 6 event, 1 sem; cancel/close/death 1/1/1\n"
        b"Thread lifecycle .... 3 exit, 4 waits, 3 reaped\n"
        b"Wait multiple ....... 7 calls, 4 block, 4 wake; max 2 members\n"
        b"Wait registrations .. 1 live, 3 max\n"
        b"Waitable timers ..... 1 created, 1 armed, 1 expired\n"
        b"Process death ....... 1 waits, 0 blocked wakes\n"
        b"Message ports ....... 3 send, 3 receive, 1 backpressure\n"
        b"Handle transfer ..... 2 committed, max detached 1\n"
        b"Shared areas ........ 1 create, 1/1 map/unmap, 0 active\n"
        b"Bulk rings .......... 1 create, 1/1 notify, 1 blocked wake, 0 active\n"
        b"K8 SHARED BULK IPC PASS\n"
        b"K7 MESSAGE PORTS PASS\n"
        b"K6 BOUNDED WAIT-MULTIPLE PASS\n"
        b"K5 THREAD LIFECYCLE PASS\n"
        b"K4 HANDLE SYNCHRONIZATION PASS\n"
        b"K3 ONE-SHOT SCHEDULER PASS\n"
        b"K3 DEADLINE QUEUE PASS\n"
        b"K2 BLOCKING SUBSTRATE PASS\n"
        b"K2 THREAD SUBSTRATE PASS\n"
        b"K1 PROTECTED ENTRY PASS\n"
    )

    assert acceptance_reached(
        output, None, False, expect_k8_shared_bulk=True
    )
    for missing in (
        b"Shared areas ........ 1 create, 1/1 map/unmap, 0 active\n",
        b"Bulk rings .......... 1 create, 1/1 notify, 1 blocked wake, 0 active\n",
        b"K8 PERF area create=25288/250000 map=37472/125000 "
        b"unmap=43238/100000 notify=15409/30000 overruns=0\n",
        b"K8 SHARED BULK IPC PASS\n",
        b"K7 MESSAGE PORTS PASS\n",
    ):
        assert not acceptance_reached(
            output.replace(missing, b""),
            None,
            False,
            expect_k8_shared_bulk=True,
        )
    for old, new in (
        (b"1/1 map/unmap, 0 active", b"1/1 map/unmap, 1 active"),
        (b"1 blocked wake, 0 active", b"0 blocked wake, 0 active"),
        (b"create=25288", b"create=250001"),
        (b"25288/250000", b"25288/250001"),
        (b"map=37472", b"map=0"),
        (b"unmap=43238", b"unmap=100001"),
        (b"notify=15409", b"notify=30001"),
        (b"overruns=0", b"overruns=1"),
        (b"3 exit, 4 waits, 3 reaped", b"2 exit, 4 waits, 3 reaped"),
        (b"12 blocks, 5 wake, 6 priority handoff", b"11 blocks, 5 wake, 6 priority handoff"),
    ):
        assert not acceptance_reached(
            output.replace(old, new, 1),
            None,
            False,
            expect_k8_shared_bulk=True,
        )


def test_k2_performance_requires_exact_budgets_and_bounded_measurements() -> None:
    report = (
        b"K2 PERF irq syscall=1200/50000 timer=1300/50000 "
        b"fault=9000/125000\n"
        b"K2 PERF sched pick=400/10000 same=500/15000 "
        b"cross=1800/50000\n"
        b"K2 PERF wait block=600/15000 wake=700/15000 overruns=0\n"
        b"K2 PERFORMANCE PASS\n"
    )

    assert check_hardware.k2_performance_reached(report)
    assert not check_hardware.k2_performance_reached(
        report.replace(b"syscall=1200", b"syscall=0")
    )
    assert not check_hardware.k2_performance_reached(
        report.replace(b"syscall=1200", b"syscall=50001")
    )
    assert not check_hardware.k2_performance_reached(
        report.replace(b"syscall=1200/50000", b"syscall=1200/50001")
    )
    assert not check_hardware.k2_performance_reached(
        report.replace(b"overruns=0", b"overruns=1")
    )
    assert not check_hardware.k2_performance_reached(
        report.replace(b"K2 PERFORMANCE PASS\n", b"")
    )


def test_k1_soak_requires_complete_bounded_checkpoint() -> None:
    prefix = (
        b"POST PASS\n"
        b"K1 soak ............. armed, baseline 7997 pages\n"
        b"K1 PROTECTED ENTRY PASS\n"
    )
    checkpoint = (
        b"K1 SOAK cycles=100 switches=203 ticks=205 "
        b"syscalls=0x0000000000012345 free=7997 "
        b"elapsed_cycles=0x00000000DF847580\n"
    )
    latency = b"K1 LATENCY user_fault_irqoff_max=1249 cycles\n"

    assert not acceptance_reached(
        prefix, None, False, expect_k1_soak_cycles=100
    )
    assert not acceptance_reached(
        prefix + checkpoint, None, False, expect_k1_soak_cycles=101
    )
    assert acceptance_reached(
        prefix + checkpoint, None, False, expect_k1_soak_cycles=100
    )
    assert acceptance_reached(
        prefix + latency + checkpoint,
        None,
        False,
        expect_k1_soak_cycles=100,
        expect_k1_fault_max_cycles=1250,
    )
    assert not acceptance_reached(
        prefix + latency + checkpoint,
        None,
        False,
        expect_k1_soak_cycles=100,
        expect_k1_fault_max_cycles=1248,
    )
    assert not acceptance_reached(
        prefix + checkpoint,
        None,
        False,
        expect_k1_soak_cycles=100,
        expect_k1_fault_max_cycles=1250,
    )
    earlier_checkpoint = checkpoint.replace(b"cycles=100", b"cycles=10")
    later_latency = b"K1 LATENCY user_fault_irqoff_max=1251 cycles\n"
    assert not acceptance_reached(
        prefix + latency + earlier_checkpoint + later_latency + checkpoint,
        None,
        False,
        expect_k1_soak_cycles=100,
        expect_k1_fault_max_cycles=1250,
    )
    assert not acceptance_reached(
        checkpoint, None, False, expect_k1_soak_cycles=100
    )
    assert not acceptance_reached(
        checkpoint + prefix, None, False, expect_k1_soak_cycles=100
    )
    assert not acceptance_reached(
        prefix + checkpoint.replace(b"switches=203", b"switches=0"),
        None,
        False,
        expect_k1_soak_cycles=100,
    )
    assert not acceptance_reached(
        prefix + checkpoint.rstrip(b"\n"),
        None,
        False,
        expect_k1_soak_cycles=100,
    )
    assert not acceptance_reached(
        prefix + checkpoint.replace(b"free=7997", b"free=7996"),
        None,
        False,
        expect_k1_soak_cycles=100,
    )
    assert acceptance_reached(
        prefix + checkpoint,
        None,
        False,
        expect_k1_soak_cycles=100,
        expect_k1_min_elapsed_cycles=3_750_000_000,
    )
    assert not acceptance_reached(
        prefix + checkpoint,
        None,
        False,
        expect_k1_soak_cycles=100,
        expect_k1_min_elapsed_cycles=3_750_000_001,
    )
    assert not acceptance_reached(
        prefix + checkpoint.replace(
            b" elapsed_cycles=0x00000000DF847580", b""
        ),
        None,
        False,
        expect_k1_soak_cycles=100,
        expect_k1_min_elapsed_cycles=3_750_000_000,
    )
    assert acceptance_reached(
        prefix + checkpoint.replace(
            b" elapsed_cycles=0x00000000DF847580", b""
        ),
        None,
        False,
        expect_k1_soak_cycles=100,
    )
    release_checkpoint = checkpoint.replace(
        b"00000000DF847580", b"000000053D1AC100"
    )
    assert acceptance_reached(
        prefix + release_checkpoint,
        None,
        False,
        expect_k1_soak_cycles=100,
        expect_k1_min_elapsed_cycles=22_500_000_000,
    )
    assert not acceptance_reached(
        prefix + release_checkpoint,
        None,
        False,
        expect_k1_soak_cycles=100,
        expect_k1_min_elapsed_cycles=22_500_000_001,
    )
    assert not acceptance_reached(
        prefix.replace(
            b"K1 soak ............. armed, baseline 7997 pages\n", b""
        )
        + checkpoint,
        None,
        False,
        expect_k1_soak_cycles=100,
    )


def test_loader_command_defaults_to_volatile_sram() -> None:
    assert check_hardware.loader_command("loader", "astra.bit", False) == [
        "loader",
        "--board",
        "ulx3s",
        "astra.bit",
    ]


def test_loader_command_can_program_persistent_flash() -> None:
    assert check_hardware.loader_command("loader", "astra.bit", True) == [
        "loader",
        "--board",
        "ulx3s",
        "-f",
        "-r",
        "astra.bit",
    ]


def test_program_flash_requires_a_bitstream(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(
        check_hardware.sys,
        "argv",
        ["check_hardware.py", "--program-flash"],
    )

    with pytest.raises(SystemExit) as error:
        check_hardware.main()

    assert error.value.code == 2


def test_k0_and_k1_expectations_are_mutually_exclusive(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(
        check_hardware.sys,
        "argv",
        ["check_hardware.py", "--expect-kernel-entry", "--expect-k1-entry"],
    )

    with pytest.raises(SystemExit) as error:
        check_hardware.main()

    assert error.value.code == 2


def test_k1_elapsed_cycles_requires_soak_mode(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(
        check_hardware.sys,
        "argv",
        ["check_hardware.py", "--expect-k1-min-elapsed-cycles", "1"],
    )

    with pytest.raises(SystemExit) as error:
        check_hardware.main()

    assert error.value.code == 2


def test_expected_rom_crc_must_match() -> None:
    output = b"ROM CRC32 ........ 0xB645D379\r\nPOST PASS\r\n"

    assert acceptance_reached(output, None, False, False, b"B645D379")
    assert not acceptance_reached(output, None, False, False, b"DEADBEEF")


def test_route_probe_requires_complete_nonzero_cycle_line() -> None:
    prefix = (
        b"ASTRA ROUTE PROBE id=56535441 sys=00000014 mem=00000000 "
        b"err=00000000 host=00000000 cycles="
    )

    assert not acceptance_reached(prefix + b"0000455E", None, False, True)
    assert not acceptance_reached(prefix + b"00000000\n", None, False, True)
    assert acceptance_reached(prefix + b"0000455E\n", None, False, True)


def test_video_probe_requires_identity_caps_and_text_readback() -> None:
    line = (
        b"ASTRA VIDEO PROBE id=56454741 caps=00000077 ctrl=00000000 "
        b"before=00000020 first=00000041 last=00000045\r\n"
    )

    assert acceptance_reached(line, None, False, False, None, True)
    assert acceptance_reached(
        line.replace(b"before=00000020", b"before=00000041"),
        None,
        False,
        False,
        None,
        True,
    )
    assert not acceptance_reached(
        line.replace(b"first=00000041", b"first=00000020"),
        None,
        False,
        False,
        None,
        True,
    )


def test_graphics_diagnostic_requires_complete_pass_line() -> None:
    assert not acceptance_reached(
        b"GFX PAS", None, False, expect_graphics=True
    )
    assert acceptance_reached(
        b"GFX PASS\r\n", None, False, expect_graphics=True
    )
    assert not acceptance_reached(
        b"GFX FAIL 0A\r\n", None, False, expect_graphics=True
    )


def test_expected_kernel_panic_requires_halt_and_optional_fault() -> None:
    prefix = b"POST PASS\n*** AXIOM KERNEL PANIC ***\n"
    complete = prefix + b"Fault:  0x02028000\nSYSTEM HALTED\n"

    assert not acceptance_reached(
        prefix, None, False, expect_kernel_panic=True
    )
    assert acceptance_reached(
        complete, None, False, expect_kernel_panic=True
    )
    assert acceptance_reached(
        complete,
        None,
        False,
        expect_kernel_panic=True,
        expected_panic_fault=b"02028000",
    )
    assert not acceptance_reached(
        complete,
        None,
        False,
        expect_kernel_panic=True,
        expected_panic_fault=b"DEADBEEF",
    )
    assert not acceptance_reached(
        complete + b"POST PASS\n",
        None,
        False,
        expect_kernel_panic=True,
        expected_panic_fault=b"DEADBEEF",
    )


def test_expected_kernel_panic_must_follow_post() -> None:
    stale_panic = (
        b"*** AXIOM KERNEL PANIC ***\n"
        b"Fault:  0x02028000\n"
        b"SYSTEM HALTED\n"
        b"POST PASS\n"
    )

    assert not acceptance_reached(
        stale_panic,
        None,
        False,
        expect_kernel_panic=True,
        expected_panic_fault=b"02028000",
    )


def test_panic_fault_requires_expected_panic_mode(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(
        check_hardware.sys,
        "argv",
        ["check_hardware.py", "--expect-panic-fault", "02028000"],
    )

    with pytest.raises(SystemExit) as error:
        check_hardware.main()

    assert error.value.code == 2


def test_post_failure_and_kernel_panic_are_fatal() -> None:
    assert failure_reached(b"POST FAILURE: SDRAM\n")
    assert failure_reached(b"*** AXIOM KERNEL PANIC ***\n")
    assert failure_reached(b"GFX FAIL 0A\r\n")
    assert failure_reached(b"GFX F41\n")
    assert not failure_reached(b"POST PASS\nK0 ENTRY PASS\n")
    assert not failure_reached(
        b"*** AXIOM KERNEL PANIC ***\n", expected_kernel_panic=True
    )
    assert failure_reached(
        b"POST FAILURE\n*** AXIOM KERNEL PANIC ***\n",
        expected_kernel_panic=True,
    )


def test_serial_capture_flushes_then_reconnects() -> None:
    events: list[str] = []
    ready = check_hardware.threading.Event()
    stop = check_hardware.threading.Event()
    received: check_hardware.queue.Queue[bytes] = check_hardware.queue.Queue()

    class FakeSerial:
        def __init__(self, name: str) -> None:
            self.name = name
            self.reads = 0

        def reset_input_buffer(self) -> None:
            events.append(f"flush:{self.name}")

        def read(self, _size: int) -> bytes:
            self.reads += 1
            if self.name == "first":
                raise OSError("FTDI disconnected")
            if self.reads == 1:
                return b"Z"
            stop.set()
            return b""

        def close(self) -> None:
            events.append(f"close:{self.name}")

    ports = iter((FakeSerial("first"), FakeSerial("second")))

    check_hardware.capture_serial(
        "/dev/ttyUSB0",
        115200,
        ready,
        stop,
        received,
        open_port=lambda _port, _baud, _deadline: next(ports),
    )

    assert ready.is_set()
    assert received.get_nowait() == b"Z"
    assert events == ["flush:first", "close:first", "close:second"]
