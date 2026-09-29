"""Probe bundles must grant the namespace their fixture paths use."""

import unittest
from pathlib import Path


TESTS = Path(__file__).resolve().parent / "tests"


def has_fixture_grant(source: str, manifest: str) -> bool:
    return "/apps/" not in source or "capability APPS:r" in manifest.splitlines()


class ProbeManifestTest(unittest.TestCase):
    def test_mixer_fixture_grant(self):
        for name, bundle in (("mixer_probe.c", "SDLMixerProbe.app"),
                             ("ttf_probe.c", "SDLTTFProbe.app")):
            with self.subTest(name=name):
                source = (TESTS / name).read_text()
                manifest = (TESTS / bundle / "manifest").read_text()
                self.assertTrue(has_fixture_grant(source, manifest))
                self.assertFalse(has_fixture_grant(
                    source, manifest.replace("capability APPS:r\n", "")))


if __name__ == "__main__":
    unittest.main()
