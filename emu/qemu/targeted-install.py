#!/usr/bin/env python3
"""Install built Astra bundles into an offline image without rebuilding it.

The caller must hold the image's exclusive storage lease (stop Astra first).
Applications replace their bundle contents. Kit installs retain earlier library
versions and update the shared provider index through the image builder.
"""

import argparse
import os
import subprocess
import tempfile

import astra_image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", help="offline, writable Astra disk image")
    parser.add_argument("bundles", nargs="+", help="built .kit or .app paths")
    args = parser.parse_args()
    if not os.path.isfile(args.image):
        parser.error("image is not a regular file")

    targets = []
    kits = []
    for source in args.bundles:
        source = os.path.abspath(source)
        name = os.path.basename(source.rstrip("/"))
        if not os.path.isdir(source) or not name or any(
                not (character.isalnum() or character in "._-")
                for character in name):
            parser.error("invalid bundle path: " + source)
        if name.endswith(".kit"):
            target = "/libs/" + name
            kits.append(source)
        elif name.endswith(".app"):
            target = "/apps/" + name
        else:
            parser.error("bundle must end in .kit or .app: " + source)
        if target in (existing for _, existing in targets):
            parser.error("duplicate bundle target: " + target)
        subprocess.run([os.path.join(os.path.dirname(__file__),
            "../../tools/build/astra-bundle"), "check", source], check=True)
        targets.append((source, target))

    offset, length = astra_image.ext4_partition(args.image)
    with tempfile.TemporaryDirectory(prefix="astra-targeted-install-") as tmp:
        volume = os.path.join(tmp, "volume.img")
        astra_image._slice(args.image, offset, length, volume)
        checked = subprocess.run(["e2fsck", "-fy", volume],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if checked.returncode > 2:
            raise RuntimeError("e2fsck refused image: " +
                checked.stdout.decode("utf-8", "replace"))
        for source, target in targets:
            astra_image._mkdir(volume, target, "bundle directory")
            if target.startswith("/apps/"):
                astra_image._clear_directory(volume, target)
            astra_image._install_bundle(volume, source, target)
        if kits:
            astra_image._write_provider_index(volume, kits, tmp)
        astra_image._reset_journal(volume)
        astra_image._splice(args.image, offset, volume)
    print("installed " + ", ".join(target for _, target in targets))


if __name__ == "__main__":
    main()
