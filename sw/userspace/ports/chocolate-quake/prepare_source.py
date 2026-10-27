#!/usr/bin/env python3
"""Copy pinned Chocolate Quake and use Astra's shared SDL_mixer codecs."""

import hashlib
import shutil
import subprocess
import sys
from pathlib import Path


REVISION = "8ee22175e3a5613e94ad2f9776aadc428b948cf3"


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    if text.count(old) != 1:
        raise ValueError(f"upstream source changed at {path}")
    path.write_text(text.replace(old, new))


def prepare(source: Path, output: Path) -> Path:
    source = source.resolve()
    revision = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if revision != REVISION:
        raise ValueError(f"chocolate-quake {revision} != pinned {REVISION}")
    if subprocess.check_output(
            ["git", "-C", str(source), "status", "--porcelain"],
            text=True).strip():
        raise ValueError("chocolate-quake source tree is modified")

    music_source = Path(__file__).with_name("bgmusic_astra.c")
    stamp = hashlib.sha256(
        (revision + Path(__file__).read_text() + music_source.read_text())
        .encode()).hexdigest()
    marker = output / ".astra-source-stamp"
    if marker.exists() and marker.read_text() == stamp:
        return output
    if output.exists():
        shutil.rmtree(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, output, ignore=shutil.ignore_patterns(".git"))

    root = output / "CMakeLists.txt"
    codecs = '''if(UNIX AND NOT APPLE)
\tfind_library(VORBIS_LIBRARY vorbis REQUIRED)
\tfind_library(VORBISFILE_LIBRARY vorbisfile REQUIRED)
\tfind_library(MAD_LIBRARY mad REQUIRED)
\tfind_library(FLAC_LIBRARY FLAC REQUIRED)
else()
\tfind_package(Vorbis CONFIG REQUIRED)
\tfind_package(mad CONFIG REQUIRED)
\tfind_package(FLAC CONFIG REQUIRED)
endif()'''
    replace_once(root, codecs, 'find_package(SDL2_mixer CONFIG REQUIRED)')

    sound = output / "src/sound/CMakeLists.txt"
    for codec in ("codec", "flac", "mp3", "mp3tag", "vorbis", "wave"):
        replace_once(sound, f"    src/snd_{codec}.c\n", "")
    replace_once(sound, "    src/bgmusic.c\n", "    src/bgmusic_astra.c\n")
    sound_codecs = '''if(UNIX AND NOT APPLE)
    list(APPEND LIBS
        ${VORBIS_LIBRARY}
        ${VORBISFILE_LIBRARY}
        ${FLAC_LIBRARY}
        ${MAD_LIBRARY}
    )
else()
    list(APPEND LIBS
        Vorbis::vorbisfile
        mad::mad
        FLAC::FLAC
    )
endif()'''
    replace_once(sound, sound_codecs,
                 'list(APPEND LIBS SDL2_mixer::SDL2_mixer)')
    shutil.copyfile(music_source, output / "src/sound/src/bgmusic_astra.c")

    dma = output / "src/sound/src/snd_dma.c"
    replace_once(dma,
                 'cvar_t snd_mixspeed = {"snd_mixspeed", "44100", true};',
                 'cvar_t snd_mixspeed = {"snd_mixspeed", "11025", true};')
    replace_once(dma, "    S_CodecInit();\n", "")
    replace_once(dma, "    S_CodecShutdown();\n", "")

    filesystem = output / "src/common/src/com_fs.c"
    replace_once(filesystem, "static void COM_AddGameDirectory(char* dir) {",
                 "static void COM_AddGameDirectory(const char* dir) {")
    replace_once(filesystem,
                 '''    // start up with GAMENAME by default (id1)
    //
    COM_AddGameDirectory(va("%s/" GAMENAME, basedir));''',
                 '''    // start up with GAMENAME by default (id1)
    //
    if (!Q_strcmp(basedir, "/store")) {
        COM_AddGameDirectory("/app/resources/id1");
        Sys_mkdir("/store/id1");
    }
    COM_AddGameDirectory(va("%s/" GAMENAME, basedir));''')

    system = output / "src/sys/src/sys.c"
    replace_once(system, "#include <stdarg.h>\n",
                 "#include <stdarg.h>\n#include <sys/stat.h>\n")
    replace_once(system, "void Sys_mkdir(char* path) {\n}\n",
                 '''void Sys_mkdir(const char* path) {
    if (mkdir(path, 0777) != 0 && errno != EEXIST) {
        Sys_Printf("Cannot create %s: %s\\n", path, strerror(errno));
    }
}
''')
    system_header = output / "src/sys/include/sys.h"
    replace_once(system_header, "void Sys_mkdir(char* path);",
                 "void Sys_mkdir(const char* path);")

    target = output / "src/CMakeLists.txt"
    replace_once(target,
                 'target_link_libraries(${PACKAGE_TARNAME} ${LIBS})\n',
                 '''target_link_libraries(${PACKAGE_TARNAME} PRIVATE ${LIBS})
if(CMAKE_SYSTEM_NAME STREQUAL "Astra")
    find_package(Astra CONFIG REQUIRED)
    astra_posix_program(${PACKAGE_TARNAME}
        NAME chocolate-quake VERSION ${PROJECT_VERSION}
        AUTHOR "Chocolate Quake contributors"
        COPYRIGHT "Copyright Chocolate Quake contributors")
endif()
''')
    marker.write_text(stamp)
    return output


if __name__ == "__main__":
    try:
        print(prepare(Path(sys.argv[1]), Path(sys.argv[2])))
    except (IndexError, OSError, subprocess.CalledProcessError,
            ValueError) as error:
        raise SystemExit(f"chocolate-quake preparation failed: {error}")
