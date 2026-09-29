import tempfile
import unittest
from pathlib import Path

from generate_exports import upstream_exports


class ExportListTests(unittest.TestCase):
    def test_accepts_upstream_public_names(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "SDL2.exports"
            source.write_text(
                "# platform-specific entry\n"
                "# ++'_SDL_WinRTGetFSPathUNICODE'.'SDL2.dll'.'SDL_WinRTGetFSPathUNICODE'\n"
                "++'_SDL_Init'.'SDL2.dll'.'SDL_Init'\n"
                "++'_SDL_OpenAudioDevice'.'SDL2.dll'.'SDL_OpenAudioDevice'\n"
            )
            self.assertEqual(upstream_exports(source),
                             {"SDL_Init", "SDL_OpenAudioDevice"})

    def test_rejects_changed_export_syntax(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "SDL2.exports"
            source.write_text("SDL_Init\n")
            with self.assertRaisesRegex(ValueError, "unrecognized SDL export"):
                upstream_exports(source)

    def test_rejects_missing_required_api(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "SDL2.exports"
            source.write_text("++'_SDL_Init'.'SDL2.dll'.'SDL_Init'\n")
            with self.assertRaisesRegex(ValueError, "required SDL exports"):
                upstream_exports(source)


if __name__ == "__main__":
    unittest.main()
