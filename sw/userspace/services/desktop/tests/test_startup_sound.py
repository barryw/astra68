#!/usr/bin/env python3
"""Positive and negative checks for the shipped startup sound conversion."""

import array
import subprocess
import sys
import tempfile
import wave
from pathlib import Path


def run(renderer: Path, source: Path, output: Path, success: bool) -> None:
    result = subprocess.run(
        [sys.executable, str(renderer), str(source), str(output)],
        capture_output=True, text=True, check=False,
    )
    assert (result.returncode == 0) == success, result.stderr
    assert output.exists() == success


def fixture(path: Path, channels: int, rate: int, frames: bytes) -> None:
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(frames)


def main(source: Path, built: Path) -> None:
    renderer = Path(__file__).resolve().parents[1] / "render_startup_sound.py"
    with wave.open(str(source), "rb") as wav:
        samples = array.array("h")
        samples.frombytes(wav.readframes(wav.getnframes()))
        if sys.byteorder == "little":
            samples.byteswap()
        assert built.read_bytes() == samples.tobytes()

    with tempfile.TemporaryDirectory() as directory:
        temporary = Path(directory)
        good = temporary / "good.wav"
        output = temporary / "test.pcm"
        fixture(good, 2, 48000, b"\x01\x02\x03\x04")
        run(renderer, good, output, True)
        assert output.read_bytes() == b"\x02\x01\x04\x03"
        assert subprocess.run(
            [sys.executable, str(renderer), str(good), str(output), "--verify"],
            check=False).returncode == 0
        output.write_bytes(b"\x02\x01\x04\x02")
        assert subprocess.run(
            [sys.executable, str(renderer), str(good), str(output), "--verify"],
            capture_output=True, check=False).returncode != 0
        for name, channels, rate, data in (
            ("mono", 1, 48000, b"\x01\x02"),
            ("rate", 2, 44100, b"\x01\x02\x03\x04"),
            ("empty", 2, 48000, b""),
        ):
            path = temporary / f"{name}.wav"
            fixture(path, channels, rate, data)
            output.unlink(missing_ok=True)
            run(renderer, path, output, False)
        bad = temporary / "bad.wav"
        bad.write_bytes(b"not a wave file")
        run(renderer, bad, output, False)
    print("startup sound PASS")


if __name__ == "__main__":
    main(Path(sys.argv[1]), Path(sys.argv[2]))
