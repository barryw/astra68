"""Keep SDL header installation inside the NDK distribution recipe."""

import subprocess
import tempfile
import unittest
from pathlib import Path


MAKEFILE = Path(__file__).resolve().parents[1] / "Makefile"


def install_recipe() -> str:
    lines = MAKEFILE.read_text().splitlines(keepends=True)
    selected = []
    for index, line in enumerate(lines):
        if ("install -m 0644 ../sw/userspace/sdl2/build/vendor/SDL2_" in line
                and ".h" in line):
            selected.extend(lines[index:index + 2])
    if len(selected) != 8:
        raise AssertionError("expected all four SDL add-on header installs")
    return "all:\n" + "".join(selected)


def parses(recipe: str) -> bool:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "Makefile"
        path.write_text(recipe)
        result = subprocess.run(["make", "-s", "-n", "-f", str(path), "all"],
                                capture_output=True, text=True)
        return result.returncode == 0


class DistributionMakefileTest(unittest.TestCase):
    def test_sdl_headers_are_valid_recipe_commands(self):
        self.assertTrue(parses(install_recipe()))

    def test_unindented_header_install_is_rejected(self):
        recipe = install_recipe()
        self.assertFalse(parses(recipe.replace("\tinstall", "install", 1)))


if __name__ == "__main__":
    unittest.main()
