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


def test_k1_soak_requires_complete_bounded_checkpoint() -> None:
    prefix = (
        b"POST PASS\n"
        b"K1 soak ............. armed, baseline 7997 pages\n"
        b"K1 PROTECTED ENTRY PASS\n"
    )
    checkpoint = (
        b"K1 SOAK cycles=100 switches=203 ticks=205 "
        b"syscalls=0x0000000000012345 free=7997\n"
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
    prefix = b"POST PASS\n*** ASTRA KERNEL PANIC ***\n"
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
        b"*** ASTRA KERNEL PANIC ***\n"
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
    assert failure_reached(b"*** ASTRA KERNEL PANIC ***\n")
    assert failure_reached(b"GFX FAIL 0A\r\n")
    assert failure_reached(b"GFX F41\n")
    assert not failure_reached(b"POST PASS\nK0 ENTRY PASS\n")
    assert not failure_reached(
        b"*** ASTRA KERNEL PANIC ***\n", expected_kernel_panic=True
    )
    assert failure_reached(
        b"POST FAILURE\n*** ASTRA KERNEL PANIC ***\n",
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
