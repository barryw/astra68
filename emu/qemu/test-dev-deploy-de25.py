#!/usr/bin/env python3
"""Reject stale Kit packages before a DE25 guest install starts."""

import importlib.util
from pathlib import Path
import tempfile


spec = importlib.util.spec_from_file_location(
    "astra_dev_deploy", Path(__file__).with_name("dev-deploy-de25.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory).resolve()
    module.REPOSITORY = root
    source = root / ("sw/userspace/kits/build/Runtime.kit/libraries/"
                     "system.library/abi-4/4.5.0/m68k-68040/system.library")
    built = root / "sw/userspace/system/build/m68k/libraries/system.library.4"
    manifest = root / "sw/userspace/kits/build/Runtime.kit/manifest"
    source.parent.mkdir(parents=True)
    built.parent.mkdir(parents=True)
    source.write_bytes(b"old")
    built.write_bytes(b"new")
    manifest.write_bytes(b"manifest")
    target = "/libs/" + source.relative_to(root / "sw/userspace/kits/build").as_posix()

    try:
        module.kit_companions(source, target)
    except ValueError as error:
        assert "stale Kit library" in str(error)
    else:
        raise AssertionError("stale Kit package was accepted")

    source.write_bytes(b"new")
    provider = source.relative_to(root / "sw/userspace/kits/build").as_posix()
    module.astra_image._providers = lambda _: {
        ("system.library", 4): ("4.5.0", 0, "test", provider)}
    module.astra_image._provider_index_record = lambda *args: b"provider"
    companions = module.kit_companions(source, target)
    assert len(companions) == 2
    assert companions[0] == (manifest, "/libs/Runtime.kit/manifest")
    assert companions[1][0] == b"provider"

print("dev-deploy Kit freshness PASS")
