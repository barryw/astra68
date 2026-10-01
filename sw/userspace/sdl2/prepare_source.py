#!/usr/bin/env python3
"""Make a disposable Astra SDL2 source tree without editing upstream SDL."""

import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path


SDL_REVISION = "5d249570393f7a37e037abf22cd6012a4cc56a71"
PORT = Path(__file__).resolve().parent
OUTPUT = PORT / "build" / "vendor" / "SDL2"
PATCHES = {
    "include/SDL_config_minimal.h": (
        "#define SDL_THREADS_DISABLED    1\n\n/* Enable the stub timer support",
        "#define SDL_THREAD_PTHREAD 1\n"
        "#define SDL_THREAD_PTHREAD_RECURSIVE_MUTEX 1\n\n"
        "/* Enable the stub timer support",
    ),
    "Makefile.minimal": (
        "\tsrc/thread/generic/*.c \\\n\tsrc/timer/*.c \\\n\tsrc/timer/dummy/*.c \\\n",
        "\tsrc/thread/pthread/SDL_syscond.c \\\n"
        "\tsrc/thread/pthread/SDL_sysmutex.c \\\n"
        "\tsrc/thread/pthread/SDL_systhread.c \\\n"
        "\tsrc/thread/pthread/SDL_systls.c \\\n"
        "\tsrc/thread/generic/SDL_syssem.c \\\n"
        "\tsrc/timer/*.c \\\n\tsrc/timer/unix/*.c \\\n",
    ),
    "src/audio/SDL_audio.c": (
        "/* Available audio drivers */\nstatic const AudioBootStrap *const bootstrap[] = {\n",
        "/* Available audio drivers */\nstatic const AudioBootStrap *const bootstrap[] = {\n"
        "#ifdef SDL_AUDIO_DRIVER_ASTRA\n    &ASTRAAUDIO_bootstrap,\n#endif\n",
    ),
    "src/audio/SDL_sysaudio.h": (
        "extern AudioBootStrap PIPEWIRE_bootstrap;\n",
        "extern AudioBootStrap ASTRAAUDIO_bootstrap;\n"
        "extern AudioBootStrap PIPEWIRE_bootstrap;\n",
    ),
    "src/video/SDL_video.c": (
        "/* Available video drivers */\nstatic VideoBootStrap *bootstrap[] = {\n",
        "/* Available video drivers */\nstatic VideoBootStrap *bootstrap[] = {\n"
        "#ifdef SDL_VIDEO_DRIVER_ASTRA\n    &ASTRA_bootstrap,\n#endif\n",
    ),
    "src/video/SDL_sysvideo.h": (
        "extern VideoBootStrap DUMMY_bootstrap;\n",
        "extern VideoBootStrap ASTRA_bootstrap;\n"
        "extern VideoBootStrap DUMMY_bootstrap;\n",
    ),
    "src/render/SDL_render.c": (
        "static const SDL_RenderDriver *render_drivers[] = {\n",
        "static const SDL_RenderDriver *render_drivers[] = {\n"
        "#if SDL_VIDEO_RENDER_ASTRA\n    &ASTRA_RenderDriver,\n#endif\n",
    ),
    "src/render/SDL_sysrender.h": (
        "extern SDL_RenderDriver D3D_RenderDriver;\n",
        "extern SDL_RenderDriver ASTRA_RenderDriver;\n"
        "extern SDL_RenderDriver D3D_RenderDriver;\n",
    ),
}

MORE_PATCHES = [
    ("include/SDL_config_minimal.h", (
        "#define HAVE_STDDEF_H   1",
        "#define HAVE_STDDEF_H   1\n#define HAVE_STDIO_H    1",
    )),
    # No software renderer in the driver list: an MC68040 cannot render in
    # software at any useful rate, so a renderer request the Astra driver
    # cannot meet fails instead of silently landing on the CPU.
    # SDL_CreateSoftwareRenderer, for rendering into a surface, remains.
    ("src/render/SDL_render.c", (
        "#if SDL_VIDEO_RENDER_SW\n    &SW_RenderDriver\n#endif\n};",
        "#if SDL_VIDEO_RENDER_SW && !defined(__astra__)\n"
        "    &SW_RenderDriver\n#endif\n};",
    )),
    # SIGINT and SIGTERM become SDL_QUIT, as on every Unix port: Ctrl-C in
    # the Terminal ends an SDL program through its own quit path, which
    # closes its audio and windows, instead of killing it.
    ("include/SDL_config_minimal.h", (
        "#define HAVE_STDIO_H    1",
        "#define HAVE_STDIO_H    1\n#define HAVE_SIGNAL_H   1\n"
        "#define HAVE_SIGACTION  1",
    )),
    ("include/SDL_config_minimal.h", (
        "#define SDL_TIMERS_DISABLED 1",
        # clock_gettime: SDL's counters then read the monotonic clock, not
        # gettimeofday's wall clock, which can step.
        "#define SDL_TIMER_UNIX 1\n#define HAVE_CLOCK_GETTIME 1",
    )),
    ("Makefile.minimal", (
        "\tsrc/audio/dummy/*.c \\\n",
        "\tsrc/audio/dummy/*.c \\\n\tsrc/audio/astra/*.c \\\n",
    )),
    ("Makefile.minimal", (
        "\tsrc/video/dummy/*.c \\\n",
        "\tsrc/video/dummy/*.c \\\n\tsrc/video/astra/*.c \\\n",
    )),
    ("Makefile.minimal", (
        "\tsrc/render/software/*.c \\\n",
        "\tsrc/render/software/*.c \\\n\tsrc/render/astra/*.c \\\n",
    )),
    ("src/file/SDL_rwops.c", (
        "#include <sys/stat.h>\n#endif",
        "#include <sys/stat.h>\n#include <string.h>\n#endif",
    )),
    ("include/SDL_config_minimal.h", (
        "#define SDL_FILESYSTEM_DUMMY  1",
        "#define SDL_FILESYSTEM_ASTRA  1",
    )),
    ("Makefile.minimal", (
        "\tsrc/filesystem/dummy/*.c \\\n",
        "\tsrc/filesystem/astra/*.c \\\n",
    )),
    # SDL_Log goes to the system log, as Android's goes to logcat: an app
    # opened from the desktop has no stderr anyone reads.
    ("src/SDL_log.c", (
        "#if defined(__ANDROID__)\n#include <android/log.h>\n#endif",
        "#if defined(__ANDROID__)\n#include <android/log.h>\n#endif\n"
        "#if defined(__astra__)\n#include <astra/runtime.h>\n#endif",
    )),
    ("src/SDL_log.c", (
        "#elif defined(__ANDROID__)\n    {\n        char tag[32];",
        "#elif defined(__astra__)\n"
        "    {\n"
        "        char line[256];\n\n"
        "        SDL_snprintf(line, sizeof(line), \"%s: %s\",\n"
        "                     SDL_priority_prefixes[priority], message);\n"
        "        (void)astra_log(line);\n"
        # Already logged: SDL's generic stderr copy below would reach the
        # log a second time (stdout/stderr without a stream go there too).
        "        return;\n"
        "    }\n"
        "#elif defined(__ANDROID__)\n    {\n        char tag[32];",
    )),
]


def prepare(source: Path) -> Path:
    source = source.resolve()
    revision = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != SDL_REVISION:
        raise ValueError(f"SDL2 revision {revision} != pinned {SDL_REVISION}")
    if subprocess.check_output(
        ["git", "-C", str(source), "status", "--porcelain"], text=True
    ).strip():
        raise ValueError("SDL2 source tree is modified")
    stamp = hashlib.sha256(
        (revision + os.environ.get("ASTRA_TOOLCHAIN_CONTENT_ID", "")
         + Path(__file__).read_text()).encode()
        + (PORT / "audio" / "SDL_astraaudio.c").read_bytes()
        + (PORT / "video" / "SDL_astravideo.c").read_bytes()
        + (PORT / "video" / "SDL_astravideo.h").read_bytes()
        + (PORT / "render" / "SDL_astrarender.c").read_bytes()
        + (PORT / "filesystem" / "SDL_astrafilesystem.c").read_bytes()
        + (PORT / "Makefile").read_bytes()
    ).hexdigest()
    if (OUTPUT / ".astra-source-stamp").exists() and (
        OUTPUT / ".astra-source-stamp"
    ).read_text() == stamp:
        return OUTPUT
    if OUTPUT.exists():
        shutil.rmtree(OUTPUT)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, OUTPUT, ignore=shutil.ignore_patterns(".git", "build"))
    for name, (before, after) in (*PATCHES.items(), *MORE_PATCHES):
        path = OUTPUT / name
        original = path.read_text()
        if original.count(before) != 1:
            raise ValueError(f"SDL2 integration point changed: {name}")
        path.write_text(original.replace(before, after))
    audio = OUTPUT / "src" / "audio" / "astra"
    audio.mkdir()
    shutil.copy2(PORT / "audio" / "SDL_astraaudio.c", audio)
    video = OUTPUT / "src" / "video" / "astra"
    video.mkdir()
    shutil.copy2(PORT / "video" / "SDL_astravideo.c", video)
    shutil.copy2(PORT / "video" / "SDL_astravideo.h", video)
    render = OUTPUT / "src" / "render" / "astra"
    render.mkdir()
    shutil.copy2(PORT / "render" / "SDL_astrarender.c", render)
    filesystem = OUTPUT / "src" / "filesystem" / "astra"
    filesystem.mkdir()
    shutil.copy2(PORT / "filesystem" / "SDL_astrafilesystem.c", filesystem)
    (OUTPUT / ".astra-source-stamp").write_text(stamp)
    return OUTPUT


if __name__ == "__main__":
    try:
        print(prepare(Path(sys.argv[1])))
    except (IndexError, OSError, subprocess.CalledProcessError, ValueError) as error:
        raise SystemExit(f"SDL2 preparation failed: {error}")
