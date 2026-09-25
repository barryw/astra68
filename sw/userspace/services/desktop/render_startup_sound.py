#!/usr/bin/env python3
"""Convert the canonical startup WAV to the mixer's raw S16BE stereo format."""

import array
import sys
import wave
from pathlib import Path


def render(source: Path, destination: Path, verify: bool = False) -> None:
    with wave.open(str(source), "rb") as wav:
        if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(),
                wav.getcomptype()) != (2, 2, 48000, "NONE"):
            raise ValueError("startup sound must be 48 kHz stereo 16-bit PCM")
        frames = wav.getnframes()
        pcm = wav.readframes(frames)
        if frames == 0 or len(pcm) != frames * 4:
            raise ValueError("startup sound is empty or truncated")

    samples = array.array("h")
    samples.frombytes(pcm)
    if sys.byteorder == "little":
        samples.byteswap()
    expected = samples.tobytes()
    if verify:
        if destination.read_bytes() != expected:
            raise ValueError("startup sound asset is stale")
    else:
        destination.write_bytes(expected)


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4) or (len(sys.argv) == 4 and
                                     sys.argv[3] != "--verify"):
        sys.exit("usage: render_startup_sound.py INPUT.wav OUTPUT.pcm [--verify]")
    try:
        render(Path(sys.argv[1]), Path(sys.argv[2]), len(sys.argv) == 4)
    except (OSError, ValueError, wave.Error) as error:
        sys.exit(str(error))
