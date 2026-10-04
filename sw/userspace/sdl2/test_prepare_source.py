import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import prepare_source


class PrepareSourceTest(unittest.TestCase):
    def test_driver_registered_only_in_generated_copy(self):
        source = Path(__file__).resolve().parents[3] / "../SDL2"
        before = (source / "src/audio/SDL_audio.c").read_bytes()
        video_before = (source / "src/video/SDL_video.c").read_bytes()
        render_before = (source / "src/render/SDL_render.c").read_bytes()
        output = prepare_source.prepare(source)
        self.assertIn("&ASTRA_RenderDriver",
                      (output / "src/render/SDL_render.c").read_text())
        self.assertIn("extern SDL_RenderDriver ASTRA_RenderDriver;",
                      (output / "src/render/SDL_sysrender.h").read_text())
        self.assertTrue(
            (output / "src/render/astra/SDL_astrarender.c").exists())
        self.assertTrue((output / "src/video/astra/SDL_astravideo.h").exists())
        self.assertIn("&ASTRAAUDIO_bootstrap", (output / "src/audio/SDL_audio.c").read_text())
        self.assertIn("&ASTRA_bootstrap", (output / "src/video/SDL_video.c").read_text())
        self.assertTrue((output / "src/video/astra/SDL_astravideo.c").exists())
        config = (output / "include/SDL_config_minimal.h").read_text()
        self.assertIn("#define SDL_THREAD_PTHREAD 1", config)
        self.assertIn("#define SDL_TIMER_UNIX 1", config)
        self.assertIn("#define HAVE_CLOCK_GETTIME 1", config)
        self.assertIn("#define HAVE_STDIO_H    1", config)
        for header in ("STDLIB", "STRING", "MATH", "CTYPE"):
            self.assertRegex(config, rf"#define HAVE_{header}_H +1\n")
        self.assertIn("#include <string.h>",
                      (output / "src/file/SDL_rwops.c").read_text())
        self.assertNotIn("#define SDL_THREADS_DISABLED", config)
        self.assertNotIn("#define SDL_TIMERS_DISABLED", config)
        makefile = (output / "Makefile.minimal").read_text()
        self.assertIn("src/thread/generic/SDL_syssem.c", makefile)
        self.assertNotIn("src/thread/pthread/SDL_syssem.c", makefile)
        self.assertIn("src/video/astra/*.c", makefile)
        self.assertIn("src/render/astra/*.c", makefile)
        self.assertIn("src/filesystem/astra/*.c", makefile)
        self.assertNotIn("src/filesystem/dummy/*.c", makefile)
        self.assertIn("#define SDL_FILESYSTEM_ASTRA  1", config)
        self.assertNotIn("SDL_FILESYSTEM_DUMMY", config)
        self.assertTrue(
            (output / "src/filesystem/astra/SDL_astrafilesystem.c").exists())
        self.assertEqual(render_before,
                         (source / "src/render/SDL_render.c").read_bytes())
        self.assertEqual(before, (source / "src/audio/SDL_audio.c").read_bytes())
        self.assertEqual(video_before, (source / "src/video/SDL_video.c").read_bytes())

    def test_changed_registration_anchor_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.dict(prepare_source.PATCHES, {
                "src/audio/SDL_audio.c": ("missing anchor", "replacement")
            }):
                with patch.object(prepare_source, "OUTPUT", Path(directory) / "SDL2"):
                    with self.assertRaisesRegex(ValueError, "integration point changed"):
                        prepare_source.prepare(
                            Path(__file__).resolve().parents[3] / "../SDL2"
                        )

    def test_changed_video_registration_anchor_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.dict(prepare_source.PATCHES, {
                "src/video/SDL_video.c": ("missing anchor", "replacement")
            }):
                with patch.object(prepare_source, "OUTPUT", Path(directory) / "SDL2"):
                    with self.assertRaisesRegex(ValueError, "integration point changed"):
                        prepare_source.prepare(
                            Path(__file__).resolve().parents[3] / "../SDL2"
                        )

    def test_build_contract_change_recreates_overlay(self):
        source = Path(__file__).resolve().parents[3] / "../SDL2"
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(prepare_source, "OUTPUT", Path(directory) / "SDL2"):
                output = prepare_source.prepare(source)
                marker = output / "stale-object"
                marker.write_text("old flags")
                self.assertEqual(prepare_source.prepare(source), output)
                self.assertTrue(marker.exists())
                original_read_bytes = Path.read_bytes

                def changed_build_contract(path):
                    contents = original_read_bytes(path)
                    if path == prepare_source.PORT / "Makefile":
                        return contents + b"changed flags"
                    return contents

                with patch.object(Path, "read_bytes", changed_build_contract):
                    prepare_source.prepare(source)
                self.assertFalse(marker.exists())

    def test_toolchain_change_recreates_overlay(self):
        source = Path(__file__).resolve().parents[3] / "../SDL2"
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(prepare_source, "OUTPUT", Path(directory) / "SDL2"):
                with patch.dict("os.environ", ASTRA_TOOLCHAIN_CONTENT_ID="old"):
                    output = prepare_source.prepare(source)
                    marker = output / "stale-object"
                    marker.write_text("old toolchain")
                with patch.dict("os.environ", ASTRA_TOOLCHAIN_CONTENT_ID="new"):
                    prepare_source.prepare(source)
                self.assertFalse(marker.exists())


if __name__ == "__main__":
    unittest.main()
