"""A Kit recipe change must invalidate its packaged artifact."""

import re
import unittest
from pathlib import Path


MAKEFILE = Path(__file__).resolve().parent / "Makefile"


def package_recipe_tracked(source: str) -> bool:
    match = re.search(r"^KIT_STAMPS := (.*?)(?=^[A-Za-z_]+ :=|^all:)",
                      source, re.MULTILINE | re.DOTALL)
    return (match is not None and "$(SDL_STAMP)" in match.group(1)
            and "$(KIT_STAMPS): Makefile" in source)


class StampDependencyTest(unittest.TestCase):
    def test_recipe_change_invalidates_sdl_kit(self):
        source = MAKEFILE.read_text()
        self.assertTrue(package_recipe_tracked(source))
        self.assertFalse(package_recipe_tracked(
            source.replace("$(KIT_STAMPS): Makefile", "$(KIT_STAMPS):")))
        self.assertFalse(package_recipe_tracked(
            source.replace("$(PCM_STAMP) $(SDL_STAMP)", "$(PCM_STAMP)")))


if __name__ == "__main__":
    unittest.main()
