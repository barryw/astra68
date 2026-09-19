import unittest

from tools.check_dynamic_executable import needed, violations


class DynamicExecutableTest(unittest.TestCase):
    def test_needed_preserves_loader_order(self):
        dynamic = """
 0x00000001 (NEEDED) Shared library: [libc.library.1]
 0x00000001 (NEEDED) Shared library: [runtime.library.1]
"""
        self.assertEqual(needed(dynamic),
                         ["libc.library.1", "runtime.library.1"])

    def test_valid_contract(self):
        self.assertEqual(violations(
            "Type: EXEC (Executable file)",
            "INTERP GNU_RELRO", "  [     0]  loader.library.1",
            "(NEEDED) [libc.library.1]\nFLAGS BIND_NOW",
            "loader.library.1", ["libc.library.1"]), [])

    def test_reports_every_independent_failure(self):
        problems = violations(
            "Type: DYN", "INTERP", "  [     0]  wrong.library",
            "(NEEDED) [wrong.library]\nTEXTREL", "loader.library.1",
            ["libc.library.1"])
        self.assertEqual(len(problems), 6)
        self.assertIn("ELF type", problems[0])
        self.assertTrue(any("interpreter" in item for item in problems))
        self.assertTrue(any("dependencies" in item for item in problems))
        self.assertTrue(any("BIND_NOW" in item for item in problems))
        self.assertTrue(any("text relocations" in item for item in problems))
        self.assertTrue(any("GNU_RELRO" in item for item in problems))


if __name__ == "__main__":
    unittest.main()
