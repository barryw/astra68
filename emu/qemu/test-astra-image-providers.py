#!/usr/bin/env python3
"""Image publication contracts, including real ext4 configuration preservation."""

import hashlib
import importlib.util
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

import astra_image

_observer_spec = importlib.util.spec_from_file_location(
    "state_preservation", os.path.join(os.path.dirname(__file__), "verify-state-preservation.py"))
state_preservation = importlib.util.module_from_spec(_observer_spec)
_observer_spec.loader.exec_module(state_preservation)


assert astra_image.HOME_DIRECTORY == "home"
with tempfile.TemporaryDirectory(prefix="astra-test-app-grants-") as bundle:
    manifest = os.path.join(bundle, "manifest")
    with open(manifest, "w", encoding="ascii") as handle:
        handle.write("capability NETWORK\ncapability LIBS:r\n")
    assert astra_image.test_app_grants(bundle) == "NETWORK LIBS:r"
    with open(manifest, "w", encoding="ascii") as handle:
        handle.write("capability NETWORK extra\n")
    try:
        astra_image.test_app_grants(bundle)
    except RuntimeError as error:
        assert "invalid test app capability" in str(error)
    else:
        raise AssertionError("malformed test app grant accepted")
with open(os.path.join(astra_image.REPOSITORY,
                       "sw/userspace/apps/Terminal.app/manifest"),
          encoding="ascii") as manifest:
    terminal_manifest = manifest.read()
    assert "capability HOME:rw\n" in terminal_manifest
    assert "capability RAM_READ\n" in terminal_manifest
    assert "capability RAM_WRITE\n" in terminal_manifest
    assert "capability APP_LAUNCH\n" in terminal_manifest
    assert "capability ENTROPY\n" in terminal_manifest
    assert "capability PCM\n" in terminal_manifest
assert astra_image.HOSTBENCH_SERVICES == \
    astra_image.DISPLAY_SERVICES + ("hostbench",)
assert astra_image.HOSTBENCH_STARTUP_MANIFEST == \
    astra_image.DISPLAY_STARTUP_MANIFEST + \
    "application /services/hostbench grants HOST_DEVICE\n"
assert astra_image.AUDIO_CERTIFY_SERVICES == \
    astra_image.DISPLAY_SERVICES + ("audio-certify",)
assert astra_image.AUDIO_CERTIFY_STARTUP_MANIFEST == \
    astra_image.DISPLAY_STARTUP_MANIFEST + \
    "application /services/audio-certify grants HOST_DEVICE\n"
assert "audio-certify" not in astra_image.DISPLAY_SERVICES
assert astra_image.PCM_CERTIFY_SERVICES == \
    astra_image.DISPLAY_SERVICES + ("pcm-certify",)
assert astra_image.PCM_CERTIFY_STARTUP_MANIFEST == \
    astra_image.DISPLAY_STARTUP_MANIFEST + \
    "application /services/pcm-certify grants PCM LIBS:r\n"
assert "media" in astra_image.DISPLAY_SERVICES
assert "service /services/media grants HOST_DEVICE serves PCM priority=16\n" in \
    astra_image.DISPLAY_STARTUP_MANIFEST
assert "NETWORK NETWORK_LISTEN NTP PCM required priority=20\n" in \
    astra_image.DISPLAY_STARTUP_MANIFEST
assert "APPS:r LIBS:r SYSTEM:r" in astra_image.DISPLAY_STARTUP_MANIFEST
assert "service /services/media grants HOST_DEVICE serves PCM required" not in \
    astra_image.PCM_CERTIFY_STARTUP_MANIFEST
assert "service /services/hostfs" in astra_image.PCM_CERTIFY_STARTUP_MANIFEST
assert "fault-probe" not in astra_image.DISPLAY_SERVICES
assert astra_image.FAULT_PROBE_SERVICES == \
    astra_image.DISPLAY_SERVICES + ("fault-probe",)
assert astra_image.FAULT_CRITICAL_STARTUP_MANIFEST == \
    astra_image.DISPLAY_STARTUP_MANIFEST + \
    "service /services/fault-probe grants critical\n"
assert "application /apps/InterfaceGallery.app" not in \
    astra_image.DISPLAY_STARTUP_MANIFEST
try:
    astra_image.install_test_app("unused.img", "../unsafe/../probe!.app")
except RuntimeError as error:
    assert "invalid test app name" in str(error)
else:
    raise AssertionError("unsafe test app name accepted")
assert not any(line.startswith("application ") and line.endswith(" required")
               for line in astra_image.DISPLAY_STARTUP_MANIFEST.splitlines())
# Trusted bundles get explicit ceilings; ordinary applications cannot read DH0.
ceiling = astra_image._application_ceiling()
assert ceiling["GUI"] == "" and ceiling["APPS"] == "r" and \
    ceiling["STORE"] == "rw" and "SERVICE_MANAGER" not in ceiling and \
    "DH0" not in ceiling
assert [line for line in astra_image.DISPLAY_STARTUP_MANIFEST.splitlines()
        if line.startswith("trusted ")] == [
    "trusted /apps/Terminal.app grants " + astra_image.TERMINAL_CEILING,
    "trusted /apps/Atlas.app grants GUI VOLUMES VOLUME_READ DH0:r APPS:r LIBS:r LOG",
    "trusted /apps/Fonts.app grants GUI FONTS FONTS_ADMIN DH0:r LIBS:r"]
for grant, admitted in (("GUI", True), ("APPS:r", True), ("APPS:rw", False),
                        ("GUI:r", False), ("STORE", False), ("DISPLAY", False),
                        ("HOME:x", False), ("STORE:rw", True)):
    assert astra_image._grant_admitted(grant, ceiling) == admitted, grant
with tempfile.TemporaryDirectory() as directory:
    def bundle(name, capabilities):
        path = os.path.join(directory, name)
        os.makedirs(path, exist_ok=True)
        with open(os.path.join(path, "manifest"), "w",
                  encoding="ascii") as handle:
            handle.write("astra-bundle 1\nkind application\n" + "".join(
                "capability %s\n" % item for item in capabilities))
        return path

    ordinary = bundle("Game.app", ["GUI", "PCM", "STORE:rw"])
    greedy = bundle("Greedy.app", ["GUI", "DISPLAY"])
    shell = bundle("Terminal.app", ["GUI", "SERVICE_MANAGER", "SYSTEM:r"])
    atlas = bundle("Atlas.app", ["GUI", "VOLUMES", "VOLUME_READ", "DH0:r", "APPS:r", "LIBS:r", "LOG"])
    fonts = bundle("Fonts.app", ["GUI", "FONTS", "FONTS_ADMIN", "DH0:r", "LIBS:r"])
    disk = bundle("Disk.app", ["GUI", "DH0:r"])
    astra_image.check_application_policy(
        [ordinary, shell, atlas, fonts],
        astra_image.DISPLAY_STARTUP_MANIFEST)
    bundle("Atlas.app", ["GUI", "DH0:rw"])
    for bundles, manifest in (([greedy], astra_image.DISPLAY_STARTUP_MANIFEST),
                              ([disk], astra_image.DISPLAY_STARTUP_MANIFEST),
                              ([atlas], astra_image.DISPLAY_STARTUP_MANIFEST),
                              ([shell], "service /services/hostfs grants\n")):
        try:
            astra_image.check_application_policy(bundles, manifest)
        except RuntimeError as error:
            assert "beyond its launch ceiling" in str(error)
        else:
            raise AssertionError("bundle beyond its ceiling accepted")
    # Inventories: one per directory, no duplicates, only listed bundles.
    first = os.path.join(directory, "first")
    second = os.path.join(directory, "second")
    for place, names in ((first, ["A.app"]), (second, ["B.app"])):
        os.makedirs(place)
        for name in names + ["Probe.app"]:
            os.makedirs(os.path.join(place, name))
            open(os.path.join(place, name, "manifest"), "w").close()
        with open(os.path.join(place, ".apps"), "w") as handle:
            handle.write("".join(name + "\n" for name in names))
    assert [os.path.basename(path) for path in
            astra_image._application_bundles((first, second))] == \
        ["A.app", "B.app"]
    with open(os.path.join(second, ".apps"), "w") as handle:
        handle.write("A.app\n")
    os.makedirs(os.path.join(second, "A.app"), exist_ok=True)
    open(os.path.join(second, "A.app", "manifest"), "w").close()
    try:
        astra_image._application_bundles((first, second))
    except RuntimeError as error:
        assert "shipped twice" in str(error)
    else:
        raise AssertionError("duplicate application accepted")
with open(os.path.join(astra_image.REPOSITORY,
                       "sw/userspace/kits/Runtime.kit/manifest"),
          encoding="ascii") as manifest:
    runtime_manifest = manifest.read()
    assert "version 2.1.0\n" in runtime_manifest
    assert "provides runtime.library 1 1.17.0\n" in runtime_manifest
    assert "provides system.library 4 4.2.0\n" in runtime_manifest
    assert "version 1.6.0\n" not in runtime_manifest
    assert "provides runtime.library 1 1.6.0\n" not in runtime_manifest
with open(os.path.join(astra_image.REPOSITORY,
                       "sw/userspace/apps/Makefile"),
          encoding="ascii") as makefile:
    app_rules = makefile.read()
    assert "$(TERMINAL): FORCE\n" in app_rules
    assert "$(GALLERY): FORCE\n" in app_rules
assert "commands/zsh/zshrc" in astra_image.CONFIGURATION
assert "commands/zsh/motd" in astra_image.CONFIGURATION
assert "remote-desktop" in astra_image.DISPLAY_SERVICES
assert "storage" not in astra_image.DISPLAY_SERVICES
assert "ramfs" not in astra_image.DISPLAY_SERVICES
assert "services/ramfs/service.conf" not in astra_image.CONFIGURATION
assert "services/ramfs/settings.conf" not in astra_image.CONFIGURATION
hostfs_settings = astra_image.CONFIGURATION["services/hostfs/settings.conf"]
for value in ("owner_files 1024\n", "owner_sessions 64\n", "queue_messages 16\n"):
    assert value in hostfs_settings
assert "grants " not in hostfs_settings
assert "entropy" in astra_image.DISPLAY_SERVICES
remote_service = astra_image.CONFIGURATION[
    "services/remote-desktop/service.conf"]
for required in ("runs paired\n", "start boot\n", "restart on-fault\n",
                 "grant HOST_DEVICE\n"):
    assert required in remote_service
assert "start manual\n" not in remote_service
assert astra_image._debugfs_quote('/runtime/menu(foo).vim') == \
    '"/runtime/menu(foo).vim"'
commands_make = open(os.path.join(astra_image.REPOSITORY,
                                  "sw/userspace/commands/Makefile"),
                     encoding="ascii").read()
shipped_commands = commands_make.split("COMMANDS :=", 1)[1].split(
    "TEST_COMMANDS :=", 1)[0]
for test_command in ("posix", "hello", "cxx"):
    assert " " + test_command + " " not in \
        " " + " ".join(shipped_commands.split()) + " "
assert "TEST_COMMANDS := posix hello cxx" in commands_make
assert "--enable-etcdir=/config" in commands_make
assert "--enable-etcdir=CONFIG:" not in commands_make
assert "$(ZSH_BUILD_CONFIG): Makefile $(ZSH_CONFIG_SITE)" in commands_make
with open(os.path.join(astra_image.REPOSITORY,
                       "sw/userspace/services/terminal/console_session.c"),
          encoding="ascii") as session:
    launcher = session.read()
    assert '"/libs/vim/runtime"' in launcher
    assert '"/local/commands:/commands"' in launcher
    assert '"LIBS:vim/runtime"' not in launcher

with tempfile.TemporaryDirectory() as directory:
    try:
        astra_image._vim_runtime(directory)
        raise AssertionError("Vim runtime without defaults.vim was accepted")
    except RuntimeError as error:
        assert "no complete Vim runtime" in str(error)
    with open(os.path.join(directory, "defaults.vim"), "w",
              encoding="ascii") as handle:
        handle.write('set nocompatible\n')
    assert astra_image._vim_runtime(directory) == directory

provider_path_max = (astra_image.PROVIDER_INDEX_MAX -
                     astra_image.PROVIDER_INDEX_HEADER.size)
record = astra_image._provider_index_record(
    "p" * provider_path_max, (1, 2, 3), 1, 0, 7)
assert len(record) == astra_image.PROVIDER_INDEX_MAX
try:
    astra_image._provider_index_record(
        "p" * (provider_path_max + 1), (1, 2, 3), 1, 0, 7)
    raise AssertionError("provider path beyond the VFS ABI was accepted")
except RuntimeError as error:
    assert "provider index path is too long" in str(error)
zshrc = astra_image.CONFIGURATION["commands/zsh/zshrc"]
assert "/home/.motd.zsh" in zshrc
assert "/config/motd.zsh" in zshrc
assert zshrc.index("/home/.motd.zsh") < zshrc.index("elif [[ -r /home/.motd ]]")
assert zshrc.index("/config/motd.zsh") < zshrc.index("elif [[ -r /config/motd ]]")
assert "$(</home/.motd)" in zshrc and "$(</config/motd)" in zshrc
assert "HOME:/.motd" not in zshrc and "CONFIG:motd" not in zshrc
assert "\\e]133;A" in zshrc and "\\e]133;B" in zshrc
startup = astra_image.DISPLAY_STARTUP_MANIFEST.splitlines()
desktop_startup = next(line for line in startup
                       if line.startswith("service /services/desktop "))
assert " grants GUI APP_LAUNCH SERVICE_MANAGER VOLUMES VOLUME_ADMIN VOLUME_USAGE PROC:r APPS:r LIBS:r " in desktop_startup
assert "DH0:r" in desktop_startup.split()
assert "/apps/r" not in desktop_startup
assert startup[0].startswith("service /services/hostfs ")
# The machine's tiers: storage and display halt it, the desktop is restarted,
# and media is the user's. Priority is its own word: system services at 20,
# media above them at 24.
assert startup[0].endswith(" critical priority=20")
display_startup = next(line for line in startup
                       if line.startswith("service /services/display "))
fonts_startup = next(line for line in startup
                     if line.startswith("service /services/fonts "))
assert "serves FONTS FONTS_ADMIN FONTS_RENDER" in fonts_startup
assert "FONTS_RENDER" in display_startup.split()
assert "FONTS_ADMIN" not in display_startup.split()
assert display_startup.endswith(" critical priority=20")
assert desktop_startup.endswith(" required priority=20")
assert [line for line in startup
        if line.startswith("service /services/media ")] == \
    ["service /services/media grants HOST_DEVICE serves PCM priority=16"]
assert startup[1] == \
    "service /services/posixd grants serves POSIX_PROCESS required priority=20"
assert startup[0] == \
    "service /services/hostfs grants HOST_FILESYSTEM " \
    "serves SYSTEM:rw WORK:rw SOUND:rw TMP:rw METRICS:r VOLUMES VOLUME_READ VOLUME_WRITE VOLUME_ADMIN VOLUME_USAGE RAM_READ RAM_WRITE critical priority=20"
assert startup[2] == \
    "service /services/entropy grants HOST_DEVICE serves ENTROPY required priority=20"


class Result:
    def __init__(self, output=b"", returncode=0):
        self.returncode = returncode
        self.stdout = output


original_run = astra_image.subprocess.run
original_cpu_count = astra_image.os.cpu_count
commands = []


with tempfile.TemporaryDirectory() as directory:
    volume = os.path.join(directory, "volume.img")
    image = os.path.join(directory, "system.img")
    with open(volume, "wb") as handle:
        handle.write(b"V" * (1 << 20))
    astra_image._write_partitioned_image(image, volume, 2 << 20)
    assert astra_image.ext4_partition(image) == (1 << 20, 1 << 20)
    with open(image, "rb") as handle:
        handle.seek(1 << 20)
        assert handle.read(16) == b"V" * 16
    try:
        astra_image._write_partitioned_image(
            os.path.join(directory, "bad.img"), volume, (2 << 20) + 1)
        raise AssertionError("unaligned system image size was accepted")
    except RuntimeError as error:
        assert "multiple of 512" in str(error)


astra_image.subprocess.run = lambda command, **_kwargs: \
    commands.append(command) or Result()
astra_image.os.cpu_count = lambda: 32
astra_image._build_current_userspace()
userspace = os.path.join(astra_image.REPOSITORY, "sw/userspace")
assert commands == [["make", "-j", "32", "-C", userspace, "all"]]
astra_image.os.cpu_count = original_cpu_count

astra_image.subprocess.run = lambda *_args, **_kwargs: Result(b"failed", 2)
try:
    astra_image._build_current_userspace()
    raise AssertionError("failed userspace build was accepted")
except RuntimeError as error:
    assert "cannot build current userspace products: failed" == str(error)
astra_image.subprocess.run = original_run
commands.clear()


def existing_run(command, **_kwargs):
    commands.append(command)
    return Result(b"Inode: 12   Type: directory    Mode: 0755\n")


astra_image.subprocess.run = existing_run
astra_image._mkdir("volume", "/existing", "test directory")
assert len(commands) == 1 and commands[0][2] == 'stat "/existing"'

commands.clear()


def missing_run(command, **_kwargs):
    commands.append(command)
    return Result(b"File not found by ext2_lookup while trying to resolve filename\n"
                  if command[2].startswith("stat ") else b"")


astra_image.subprocess.run = missing_run
astra_image._mkdir("volume", "/missing", "test directory")
assert [command[2] if command[2] != "-R" else command[3]
        for command in commands] == ['stat "/missing"', 'mkdir "/missing"']
astra_image.subprocess.run = original_run

for output, status in ((b"unreadable image\n", 0),
                       (b"File not found by ext2_lookup\n", 1)):
    astra_image.subprocess.run = lambda *_args, **_kwargs: Result(output, status)
    try:
        astra_image._inode_type("volume", "/config")
        raise AssertionError("unreadable inode was mistaken for missing configuration")
    except RuntimeError as error:
        assert "cannot inspect image path" in str(error)
astra_image.subprocess.run = original_run

commands.clear()

with tempfile.TemporaryDirectory() as directory:
    with open(os.path.join(directory, "defaults.vim"), "w",
              encoding="ascii") as handle:
        handle.write("set nocompatible\n")
    astra_image.subprocess.run = missing_run
    astra_image._install_tree("volume", directory, "/libs/vim/runtime")
    requests = [command[2] if command[2] != "-R" else command[3]
                for command in commands]
    for parent in ('"/libs"', '"/libs/vim"', '"/libs/vim/runtime"'):
        assert "mkdir %s" % parent in requests
    assert any(request.startswith("write ") and
               request.endswith(' "/libs/vim/runtime/defaults.vim"')
               for request in requests)
    commands.clear()
    try:
        astra_image._install_tree("volume", directory, "libs/vim/runtime")
        raise AssertionError("relative image tree destination was accepted")
    except RuntimeError as error:
        assert "invalid image tree destination" in str(error)
    assert not commands
astra_image.subprocess.run = original_run

commands.clear()


def directory_run(command, **_kwargs):
    commands.append(command)
    if command[2] == 'ls -p "/libs"':
        return Result(b"/2/040755/0/0/.//\n/2/040755/0/0/..//\n"
                      b"/12/100644/0/0/menu chinese(taiwan).vim/4/\n"
                      b"/13/040755/0/0/Old.kit//\n")
    if command[2] == 'ls -p "/libs/Old.kit"':
        return Result(b"/13/040755/0/0/.//\n/2/040755/0/0/..//\n"
                      b"/14/100644/0/0/manifest/4/\n")
    return Result()


astra_image.subprocess.run = directory_run
astra_image._clear_directory("volume", "/libs")
assert [command[2] if command[2] != "-R" else command[3]
        for command in commands] == [
            'ls -p "/libs"', 'rm "/libs/menu chinese(taiwan).vim"',
            'ls -p "/libs/Old.kit"', 'rm "/libs/Old.kit/manifest"',
            'rmdir "/libs/Old.kit"']
astra_image.subprocess.run = lambda *_args, **_kwargs: Result(
    b"/12/100644/0/0/not\x00safe/4/\n")
try:
    astra_image._clear_directory("volume", "/libs")
    raise AssertionError("unsafe stale image name was accepted")
except RuntimeError as error:
    assert "unsafe image directory entry" in str(error)
astra_image.subprocess.run = original_run

commands.clear()
astra_image.subprocess.run = existing_run
astra_image._reset_journal("volume")
assert commands == [
    ["tune2fs", "-f", "-O", "^has_journal", "volume"],
    ["e2fsck", "-fy", "volume"],
    ["tune2fs", "-j", "-J", "size=4", "volume"],
    ["e2fsck", "-fy", "volume"],
]
astra_image.subprocess.run = original_run

calls = []
original_commands = astra_image._commands
original_services = astra_image._services
original_bundles = astra_image._bundles
original_partition = astra_image.ext4_partition


class SelectionComplete(Exception):
    pass


astra_image._commands = lambda directory: \
    calls.append(("commands", directory))
astra_image._services = lambda directory, names: \
    calls.append(("services", directory, names)) or []
astra_image._bundles = lambda directory, names: \
    calls.append(("bundles", directory, names))
astra_image.ext4_partition = lambda _image: (_ for _ in ()).throw(
    SelectionComplete())
try:
    astra_image._install_built(
        "image", catalog=__file__, commands=None, services="services",
        kits=None, apps=None, terminfo=None, vim_runtime=None,
        service_names=("hostbench",))
    raise AssertionError("selection check reached the image")
except SelectionComplete:
    pass
assert calls == [("services", "services", ("hostbench",))]
with tempfile.TemporaryDirectory() as directory:
    original_sound = astra_image.DEFAULT_STARTUP_SOUND
    astra_image.DEFAULT_STARTUP_SOUND = os.path.join(directory, "startup.pcm")
    try:
        try:
            astra_image._install_built(
                "image", catalog=__file__, commands=None,
                services="services", kits=None, apps=None, terminfo=None,
                vim_runtime=None, service_names=("desktop",))
            raise AssertionError("missing startup sound was accepted")
        except RuntimeError as error:
            assert "startup sound asset is missing or stale" in str(error)
        subprocess.run(
            [sys.executable, astra_image.DEFAULT_STARTUP_SOUND_RENDERER,
             astra_image.DEFAULT_STARTUP_SOUND_SOURCE,
             astra_image.DEFAULT_STARTUP_SOUND], check=True)
        with open(astra_image.DEFAULT_STARTUP_SOUND, "r+b") as output:
            first = output.read(1)
            output.seek(0)
            output.write(bytes([first[0] ^ 1]))
        try:
            astra_image._install_built(
                "image", catalog=__file__, commands=None,
                services="services", kits=None, apps=None, terminfo=None,
                vim_runtime=None, service_names=("desktop",))
            raise AssertionError("same-size stale startup sound was accepted")
        except RuntimeError as error:
            assert "startup sound asset is missing or stale" in str(error)
        subprocess.run(
            [sys.executable, astra_image.DEFAULT_STARTUP_SOUND_RENDERER,
             astra_image.DEFAULT_STARTUP_SOUND_SOURCE,
             astra_image.DEFAULT_STARTUP_SOUND], check=True)
        try:
            astra_image._install_built(
                "image", catalog=__file__, commands=None,
                services="services", kits=None, apps=None, terminfo=None,
                vim_runtime=None, service_names=("desktop",))
            raise AssertionError("valid startup sound was rejected")
        except SelectionComplete:
            pass
    finally:
        astra_image.DEFAULT_STARTUP_SOUND = original_sound
astra_image._commands = original_commands
astra_image._services = original_services
astra_image._bundles = original_bundles
astra_image.ext4_partition = original_partition

with tempfile.TemporaryDirectory() as directory:
    service = os.path.join(directory, "storage")
    image = os.path.join(service, "build", "m68k", "storage")
    os.makedirs(os.path.dirname(image))
    with open(image, "wb") as handle:
        handle.write(b"storage")

    checks = []

    def current_service(command, **_kwargs):
        checks.append(command)
        return Result()

    astra_image.subprocess.run = current_service
    assert astra_image._services(directory, ("storage",)) == [
        ("storage", image)]
    assert checks == [["make", "-q", "-C", service,
                       "ASTRA_PROGRAM_OWNERS_READY=1",
                       "build/m68k/storage"]]

    astra_image.subprocess.run = lambda *_args, **_kwargs: Result(
        b"storage needs rebuilding", 1)
    try:
        astra_image._services(directory, ("storage",))
        raise AssertionError("stale service was accepted")
    except RuntimeError as error:
        assert "stale service image" in str(error)

astra_image.subprocess.run = original_run

calls = []
original_services = astra_image._services
original_replace_volume_file = astra_image._replace_volume_file
astra_image._services = lambda directory, names: \
    calls.append((directory, names)) or [("display", "display-image")]
astra_image._replace_volume_file = lambda image, source, target: \
    calls.append((image, source, target))
astra_image.install_service("system.img", "display", services="services")
assert calls == [
    ("services", ("display",)),
    ("system.img", "display-image", "/services/display"),
]
astra_image._services = original_services
astra_image._replace_volume_file = original_replace_volume_file

with tempfile.TemporaryDirectory() as directory:
    command_root = os.path.join(directory, "commands")
    command_build = os.path.join(command_root, "build", "m68k")
    os.makedirs(command_build)
    command = os.path.join(command_build, "posix")
    with open(command, "wb") as handle:
        handle.write(b"test")
    os.chmod(command, 0o755)
    calls = []
    checks = []
    astra_image.subprocess.run = lambda command, **_kwargs: \
        checks.append(command) or Result(b"", 0)
    astra_image._replace_volume_file = lambda image, source, target: \
        calls.append((image, source, target))
    astra_image.install_test_command("system.img", "posix", command_build)
    assert calls == [("system.img", command, "/commands/posix")]
    assert checks == [["make", "-q", "-C", command_root,
                       "ASTRA_PROGRAM_OWNERS_READY=1", "build/m68k/posix"]]
    astra_image.subprocess.run = lambda *_args, **_kwargs: Result(b"stale", 1)
    try:
        astra_image.install_test_command("system.img", "posix", command_build)
        raise AssertionError("stale test command was accepted")
    except RuntimeError as error:
        assert "stale test command" in str(error)
    try:
        astra_image.install_test_command("system.img", "../posix",
                                         command_build)
        raise AssertionError("unsafe test command name was accepted")
    except RuntimeError as error:
        assert "invalid test command name" in str(error)
astra_image.subprocess.run = original_run
astra_image._replace_volume_file = original_replace_volume_file

with tempfile.TemporaryDirectory() as directory:
    for name in ("status", "stale-orphan"):
        with open(os.path.join(directory, name), "wb") as image:
            image.write(name.encode("ascii"))
    os.chmod(os.path.join(directory, "status"), 0o755)
    with open(os.path.join(directory, ".commands"), "w",
              encoding="ascii") as manifest:
        manifest.write("status\n")
    assert astra_image._commands(directory) == [
        ("status", os.path.join(directory, "status"))]
    os.chmod(os.path.join(directory, "status"), 0o644)
    try:
        astra_image._commands(directory)
        raise AssertionError("non-executable command was accepted")
    except RuntimeError as error:
        assert "not executable" in str(error)


with tempfile.TemporaryDirectory() as directory:
    bundles = []
    for kit, version in (("Old.kit", "1.2.0"), ("New.kit", "1.10.0")):
        bundle = os.path.join(directory, kit)
        os.mkdir(bundle)
        with open(os.path.join(bundle, "manifest"), "w", encoding="ascii") as manifest:
            manifest.write("kind kit\nprovides filesystem.library 1 %s\n" % version)
        library = os.path.join(bundle, "libraries", "filesystem.library",
                               "abi-1", version, "m68k-68040")
        os.makedirs(library)
        with open(os.path.join(library, "filesystem.library"), "wb") as image:
            identity = struct.pack(
                ">IHHHHHHHHIII24s32s40s", 0x414c4942, 2, 128,
                *(int(part) for part in version.split(".")), 1, 0,
                0, 0x4d303430, 0x12345678, 0,
                b"filesystem.library.1",
                b"test", b"test")
            image.write(bytes(0x200))
            image.write(identity)
        bundles.append(bundle)

    providers = astra_image._providers(bundles)
    assert providers[("filesystem.library", 1)] == (
        (1, 10, 0), 0, 0x12345678,
        "New.kit/libraries/filesystem.library/abi-1/1.10.0/"
        "m68k-68040/filesystem.library")

with tempfile.TemporaryDirectory() as directory:
    bundle = os.path.join(directory, "Broken.kit")
    os.mkdir(bundle)
    with open(os.path.join(bundle, "manifest"), "w", encoding="ascii") as manifest:
        manifest.write("kind kit\nprovides broken.library 1 1.0.0\n")
    library = os.path.join(bundle, "libraries", "broken.library",
                           "abi-1", "1.0.0", "m68k-68040")
    os.makedirs(library)
    with open(os.path.join(library, "broken.library"), "wb") as image:
        image.write(bytes(0x200 + astra_image.LIBRARY_RECORD_SIZE))
    try:
        astra_image._providers([bundle])
        raise AssertionError("invalid installed provider was accepted")
    except RuntimeError as error:
        assert "library identity disagrees" in str(error)

with tempfile.TemporaryDirectory() as directory:
    for name in ("Zeta.kit", "Alpha.kit"):
        os.mkdir(os.path.join(directory, name))
        with open(os.path.join(directory, name, "manifest"), "w",
                  encoding="ascii") as manifest:
            manifest.write("kind kit\n")
    os.mkdir(os.path.join(directory, "ignored.app"))
    with open(os.path.join(directory, ".kits"), "w",
              encoding="ascii") as inventory:
        inventory.write("Alpha.kit\nZeta.kit\n")
    assert [os.path.basename(path) for path in astra_image._bundles(directory)] == \
        ["Alpha.kit", "Zeta.kit"]

with tempfile.TemporaryDirectory(prefix="astra-config-preservation-") as directory:
    volume = os.path.join(directory, "volume.ext4")
    image = os.path.join(directory, "system.img")
    catalog = os.path.join(directory, "catalog")
    with open(catalog, "wb") as output:
        output.write(b"installer preservation fixture\n")

    def format_volume(path, seed=None):
        with open(path, "wb") as output:
            output.truncate(32 << 20)
        subprocess.run(["mkfs.ext4", "-q", "-F", "-O", "^orphan_file",
                        *(["-d", seed] if seed is not None else []), path],
                       check=True)

    def write_file(path, contents):
        source = os.path.join(directory, "seed")
        with open(source, "wb") as output:
            output.write(contents)
        astra_image._debugfs(volume, "write %s %s" % (
            astra_image._debugfs_quote(source), astra_image._debugfs_quote(path)),
            "preserved fixture")

    def inode_metadata(path):
        output = subprocess.check_output([
            "debugfs", "-R", "stat %s" % astra_image._debugfs_quote(path), volume],
            stderr=subprocess.DEVNULL, text=True, errors="surrogateescape")
        fields = (r"Inode:\s+(\d+)", r"Type:\s+(\S+)", r"Mode:\s+(\S+)",
                  r"Flags:\s+(\S+)", r"Generation:\s+(\S+)",
                  r"User:\s+(\d+)", r"Group:\s+(\d+)")
        values = tuple(re.search(pattern, output).group(1) for pattern in fields)
        times = tuple(re.findall(r"^\s*(?:ctime|atime|mtime|crtime):.*$", output,
                                 re.MULTILINE))
        assert len(times) == 4, output
        link = tuple(re.findall(r"^Fast link dest:.*$", output, re.MULTILINE))
        attributes = subprocess.check_output([
            "debugfs", "-R", "ea_list %s" % astra_image._debugfs_quote(path), volume],
            stderr=subprocess.DEVNULL, text=True)
        return values, times, link, attributes

    def contents(path):
        target = os.path.join(directory, "observed")
        subprocess.run(["debugfs", "-R", "dump %s %s" % (
            astra_image._debugfs_quote(path), astra_image._debugfs_quote(target)), volume],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        with open(target, "rb") as source:
            return source.read()

    def install_fixture():
        # The real image-install seam needs no compiled guest products for
        # this test: publish a catalog and empty product inventories while
        # exercising the actual ext4/configuration/journal transaction.
        astra_image._install_built(
            image, catalog=catalog, commands=None, services=directory,
            kits=None, apps=None, terminfo=None, vim_runtime=None,
            service_names=())

    tree = os.path.join(directory, "seed-tree")
    os.makedirs(os.path.join(tree, "home"))
    binary_target = b"absent\nGeneration: 99\n\xff\\target"
    os.symlink(binary_target, os.fsencode(os.path.join(tree, "home", "binary-link")))
    format_volume(volume, tree)
    directories = (
        "/config", "/config/services", "/config/services/desktop",
        "/config/services/hostfs", "/config/services/remote-desktop",
        "/config/user", "/config/user/nested", "/home", "/local",
        "/local/commands")
    for path in directories:
        astra_image._mkdir(volume, path, "preserved directory")
    files = {
        "/config/services/desktop/policy.conf":
            b"astra-config 1\nschema 1\nenabled false\nrestart never\n",
        "/config/services/hostfs/settings.conf":
            b"astra-config 1\nschema 1\nowner_files 512\nowner_sessions 32\nqueue_messages 8\n",
        "/config/services/remote-desktop/service.conf":
            astra_image.CONFIGURATION["services/remote-desktop/service.conf"].replace(
                "enabled true\n", "enabled false\n").encode("ascii"),
        "/config/user/nested/custom.bin": b"custom\x00configuration\xff\n",
        "/home/kept.txt": b"home retained\n",
        "/local/commands/kept.txt": b"local retained\n",
    }
    for path, data in files.items():
        write_file(path, data)
    links = {"/config/user/nested/link": b"custom.bin",
             "/config/user/nested/broken-link": b"absent.bin",
             "/config/user/nested/long-link": b"absent/" + b"x" * 200,
             "/home/binary-link": binary_target}
    for path, target in links.items():
        if path == "/home/binary-link":
            continue
        astra_image._debugfs(volume, "symlink %s %s" % (
            astra_image._debugfs_quote(path), astra_image._debugfs_quote(target.decode("ascii"))),
            "preserved symlink")
        assert astra_image._inode_type(volume, path) == "symlink"
    hardlink = "/local/commands/shared.txt"
    astra_image._debugfs(volume, 'ln "/home/kept.txt" "%s"' % hardlink, "preserved hardlink")
    astra_image._debugfs(volume, 'set_inode_field "/home/kept.txt" links_count 2', "hardlink accounting")
    preserved = directories + tuple(files) + tuple(links) + (hardlink,)
    for path in preserved:
        kind = astra_image._inode_type(volume, path)
        mode = {"directory": "040751", "regular": "0100640",
                "symlink": "0120777"}[kind]
        for field, value in (("uid", "1234"), ("gid", "2345"), ("mode", mode),
                             ("atime", "20210102030405"),
                             ("mtime", "20210203040506"), ("ctime", "20210304050607"),
                             ("crtime", "20200102030405")):
            astra_image._debugfs(volume, "set_inode_field %s %s %s" % (
                astra_image._debugfs_quote(path), field, value), "fixture metadata")
    astra_image._debugfs(volume,
        'ea_set "/config/user/nested/custom.bin" user.astra retained', "fixture attribute")
    binary_attribute = bytes(range(256)) * 2
    attribute_seed = os.path.join(directory, "attribute-seed")
    for name, value in (("user.binary", binary_attribute), ("user.empty", b"")):
        with open(attribute_seed, "wb") as target:
            target.write(value)
        astra_image._debugfs(volume, 'ea_set -f %s "/config/user/nested/custom.bin" %s' % (
            astra_image._debugfs_quote(attribute_seed), name), "complete fixture attribute")
    before = {path: inode_metadata(path) for path in preserved}
    observed = state_preservation.observe(volume, directory)
    for path, target in links.items():
        assert observed[path]["target"] == target, (path, observed[path])
    assert observed[hardlink]["inode"] == observed["/home/kept.txt"]["inode"]
    assert observed["/config/user/nested/custom.bin"]["xattrs"]["user.binary"] == \
        (len(binary_attribute), hashlib.sha256(binary_attribute).hexdigest())
    assert observed["/config/user/nested/custom.bin"]["xattrs"]["user.empty"] == \
        (0, hashlib.sha256(b"").hexdigest())
    astra_image._write_partitioned_image(image, volume, 33 << 20)
    reference = os.path.join(directory, "reference.img")
    shutil.copyfile(image, reference)
    offset, length = astra_image.ext4_partition(image)
    for iteration in range(2):
        install_fixture()
        astra_image._slice(image, offset, length, volume)
        for path in preserved:
            assert inode_metadata(path) == before[path], (iteration, path)
        for path, data in files.items():
            assert contents(path) == data, path
        for path, data in astra_image.CONFIGURATION.items():
            target = "/config/" + path
            assert contents(target) == files.get(target, data.encode("ascii")), target
        subprocess.run(["e2fsck", "-fn", volume], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        retained, added = state_preservation.verify(reference, image)
        assert retained == len(preserved) and added > 0
    command = subprocess.run([sys.executable, _observer_spec.origin, reference, image],
                             check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert b"input hashes unchanged PASS" in command.stdout, command.stdout
    reference_link = os.path.join(directory, "reference-link.img")
    os.symlink(reference, reference_link)
    try:
        state_preservation.verify(reference_link, image)
    except OSError:
        pass
    else:
        raise AssertionError("observer followed an input image symlink")
    print("astra image ext4 config: disabled policies, settings, nested data/symlinks, "
          "inode identity/permissions/ownership/timestamps/xattrs, missing defaults and clean fsck PASS")

    # Every canary is an actual ext4 mutation on an independent copy. The
    # verifier must name the damaged contract and leave both inputs unchanged.
    def reject_change(request, expected, ea_listing_unchanged=False):
        candidate = os.path.join(directory, "canary.img")
        shutil.copyfile(image, candidate)
        astra_image._slice(candidate, offset, length, volume)
        prior_attributes = state_preservation.debugfs(volume, 'ea_list "/config/user/nested/custom.bin"')
        astra_image._debugfs(volume, request, "preservation rejection canary")
        if ea_listing_unchanged:
            assert state_preservation.debugfs(volume, 'ea_list "/config/user/nested/custom.bin"') == prior_attributes
        subprocess.run(["e2fsck", "-fn", volume], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        astra_image._splice(candidate, offset, volume)
        hashes = tuple(state_preservation.digest(path) for path in (reference, candidate))
        try:
            state_preservation.verify(reference, candidate)
        except RuntimeError as error:
            assert expected in str(error), (request, str(error))
        else:
            raise AssertionError("state corruption accepted: " + request)
        assert tuple(state_preservation.digest(path) for path in (reference, candidate)) == hashes

    with open(attribute_seed, "wb") as target:
        target.write(binary_attribute[:-1] + b"\x00")
    reject_change('ea_set -f %s "/config/user/nested/custom.bin" user.binary' %
                  astra_image._debugfs_quote(attribute_seed), "xattrs", True)
    reject_change('ea_rm "/config/user/nested/custom.bin" user.empty', "xattrs")
    reject_change('set_inode_field "/home/kept.txt" uid 4321', "uid")
    reject_change('set_inode_field "/home/kept.txt" mode 0100600', "mode")
    reject_change('set_inode_field "/home/kept.txt" atime 20240102030405', "atime")
    reject_change('punch "/home/kept.txt" 0 0', "content")
    reject_change('set_inode_field "/config/user/nested/link" block[0] 0x74737558', "target")
    reject_change('rm "%s"' % hardlink, "disappeared")
    reject_change('write %s "/config/user/nested/unexpected.txt"' %
                  astra_image._debugfs_quote(attribute_seed), "unexpected state addition")
    reject_change('punch "/config/commands/zsh/motd" 0 0', "incorrect installer default contents")
    print("state observer actual ext4: full binary/empty xattrs, fast/long/broken/byte symlinks, hardlinks, metadata/content/default/addition rejection and immutable inputs PASS")

    for index, name in enumerate((b"name\nline", b"\xffname")):
        seed = os.path.join(directory, "unsafe-seed-%d" % index)
        os.makedirs(os.path.join(seed, "home"))
        with open(os.path.join(os.fsencode(seed), b"home", name), "wb") as target:
            target.write(b"must not be silently skipped")
        unsafe = os.path.join(directory, "unsafe-%d.img" % index)
        unsafe_copy = os.path.join(directory, "unsafe-%d-copy.img" % index)
        format_volume(volume, seed)
        astra_image._write_partitioned_image(unsafe, volume, 33 << 20)
        shutil.copyfile(unsafe, unsafe_copy)
        hashes = tuple(state_preservation.digest(path) for path in (unsafe, unsafe_copy))
        try:
            state_preservation.verify(unsafe, unsafe_copy)
        except RuntimeError as error:
            assert "unrepresentable directory" in str(error), str(error)
        else:
            raise AssertionError("unrepresentable image name was skipped")
        assert tuple(state_preservation.digest(path) for path in (unsafe, unsafe_copy)) == hashes
    print("state observer rejects newline/non-UTF-8 entry names without changing images PASS")

    # Conflicts must fail before splicing anything into the source image. A
    # default symlink (even to a valid file) is never accepted as regular data.
    for conflict in ("default-directory", "default-symlink", "parent-file", "parent-symlink"):
        image = os.path.join(directory, conflict + ".img")
        format_volume(volume)
        astra_image._mkdir(volume, "/config", "conflict fixture")
        if conflict.startswith("parent-"):
            target = "/config/services"
        else:
            astra_image._mkdir(volume, "/config/services", "conflict fixture")
            astra_image._mkdir(volume, "/config/services/hostfs", "conflict fixture")
            target = "/config/services/hostfs/settings.conf"
        if conflict.endswith("directory"):
            astra_image._mkdir(volume, target, "default conflict")
        elif conflict.endswith("file"):
            write_file(target, b"retained conflict\n")
        else:
            write_file("/retained-target", b"never replace through a symlink\n")
            astra_image._debugfs(volume, 'symlink %s "/retained-target"' %
                astra_image._debugfs_quote(target), "symlink conflict")
        astra_image._write_partitioned_image(image, volume, 33 << 20)
        with open(image, "rb") as source:
            original = hashlib.sha256(source.read()).hexdigest()
        try:
            install_fixture()
            raise AssertionError("configuration path conflict accepted: " + conflict)
        except RuntimeError as error:
            assert "conflicts with" in str(error), (conflict, str(error))
        with open(image, "rb") as source:
            assert hashlib.sha256(source.read()).hexdigest() == original, conflict
    print("astra image ext4 config conflict refusal leaves source image unchanged PASS")

print("astra image provider index: PASS")
