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


def test_post_failure_and_kernel_panic_are_fatal() -> None:
    assert failure_reached(b"POST FAILURE: SDRAM\n")
    assert failure_reached(b"*** ASTRA KERNEL PANIC ***\n")
    assert not failure_reached(b"POST PASS\nK0 ENTRY PASS\n")


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
