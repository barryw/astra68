import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import prepare_addon_source


class PrepareAddonSourceTest(unittest.TestCase):
    def test_clean_pinned_source_copies_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source"
            output = Path(directory) / "copy"
            source.mkdir()
            (source / "SDL_net.h").write_text("upstream")
            with patch.object(prepare_addon_source.subprocess, "check_output",
                              side_effect=[prepare_addon_source.REVISIONS["SDL2_mixer"], ""]):
                prepare_addon_source.prepare("SDL2_mixer", source, output)
            self.assertEqual((output / "SDL_net.h").read_text(), "upstream")
            self.assertTrue((output / ".astra-source-stamp").exists())

    def test_wrong_revision_and_dirty_source_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source"
            output = Path(directory) / "copy"
            source.mkdir()
            with patch.object(prepare_addon_source.subprocess, "check_output",
                              return_value="wrong"):
                with self.assertRaisesRegex(ValueError, "revision"):
                    prepare_addon_source.prepare("SDL2_image", source, output)
            with patch.object(prepare_addon_source.subprocess, "check_output",
                              side_effect=[prepare_addon_source.REVISIONS["SDL2_image"],
                                           " M src/IMG.c"]):
                with self.assertRaisesRegex(ValueError, "modified"):
                    prepare_addon_source.prepare("SDL2_image", source, output)
            self.assertFalse(output.exists())

    def test_ttf_requires_pinned_freetype_submodule(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source"
            output = Path(directory) / "copy"
            freetype = source / "external" / "freetype"
            freetype.mkdir(parents=True)
            revision = prepare_addon_source.REVISIONS["SDL2_ttf"]
            with patch.object(prepare_addon_source.subprocess, "check_output",
                              side_effect=[revision, ""]):
                with self.assertRaisesRegex(ValueError, "missing"):
                    prepare_addon_source.prepare("SDL2_ttf", source, output)
            (freetype / "CMakeLists.txt").write_text("project(freetype C)\n")
            with patch.object(prepare_addon_source.subprocess, "check_output",
                              side_effect=[revision, "", "wrong"]):
                with self.assertRaisesRegex(ValueError, "not pinned"):
                    prepare_addon_source.prepare("SDL2_ttf", source, output)
            with patch.object(prepare_addon_source.subprocess, "check_output",
                              side_effect=[revision, "",
                                           "12c5e620858bd503731091e9371d06c0a3e7c967"]):
                prepare_addon_source.prepare("SDL2_ttf", source, output)
            self.assertTrue((output / "external" / "freetype" /
                             "CMakeLists.txt").is_file())
            cache = output / "build" / "freetype" / "CMakeCache.txt"
            cache.parent.mkdir(parents=True)
            cache.write_text("stale source path")
            with patch.object(prepare_addon_source.subprocess, "check_output",
                              side_effect=[revision, "",
                                           "12c5e620858bd503731091e9371d06c0a3e7c967"]):
                prepare_addon_source.prepare("SDL2_ttf", source, output)
            self.assertTrue(cache.is_file())
            (output / ".astra-source-stamp").write_text("obsolete")
            with patch.object(prepare_addon_source.subprocess, "check_output",
                              side_effect=[revision, "",
                                           "12c5e620858bd503731091e9371d06c0a3e7c967"]):
                prepare_addon_source.prepare("SDL2_ttf", source, output)
            self.assertFalse(cache.exists())


if __name__ == "__main__":
    unittest.main()
