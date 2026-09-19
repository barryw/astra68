import pathlib
import subprocess
import tempfile
import textwrap
import time
import unittest


LIBRARIES_MK = pathlib.Path(__file__).resolve().parents[2] / "sw/userspace/libraries.mk"
SHARED_LIBRARY_MK = pathlib.Path(__file__).resolve().parents[2] / "mk/astra-shared-library.mk"


class LibraryDependenciesTest(unittest.TestCase):
    def evaluate(self, expression):
        with tempfile.TemporaryDirectory() as temporary:
            makefile = pathlib.Path(temporary) / "Makefile"
            makefile.write_text(textwrap.dedent(f"""\
                include {LIBRARIES_MK}
                all:
                \t@printf '%s\\n' '$({expression})'
                """))
            return subprocess.run(
                ["make", "--no-print-directory"], cwd=temporary, check=True,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
            ).stdout.strip()

    def test_zsh_dependency_set_reduces_to_terminfo(self):
        self.assertEqual(
            self.evaluate(
                "call astra_library_root_keys,terminfo libc runtime compiler"),
            "terminfo")

    def test_independent_roots_are_preserved(self):
        self.assertEqual(
            set(self.evaluate(
                "call astra_library_root_keys,interface config"
            ).split()),
            {"interface", "config"})

    def test_removed_alias_library_has_no_separate_owner(self):
        owners = self.evaluate(
            "call astra_library_owner_targets,interface input").split()
        self.assertEqual(len(owners), 1)
        self.assertTrue(owners[0].endswith("interface:interface-library"))

    def test_application_builds_do_not_run_certification_targets(self):
        owners = self.evaluate(
            "foreach key,$(ASTRA_LIBRARY_KEYS),$(ASTRA_LIBRARY_OWNER_$(key))")
        self.assertNotIn("contract", owners)

    def test_release_contracts_cover_every_library(self):
        contracts = self.evaluate(
            "call astra_library_contract_owner_targets,$(ASTRA_LIBRARY_KEYS)")
        registered = set(self.evaluate(
            "foreach key,$(ASTRA_LIBRARY_KEYS),"
            "$(ASTRA_LIBRARY_CONTRACT_OWNER_$(key))").split())
        self.assertEqual(set(contracts.split()), registered)
        self.assertTrue(all("contract" in item for item in contracts.split()))

    def test_unchanged_content_outputs_are_not_regenerated(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            (root / "input").write_text("input\n")
            makefile = root / "Makefile"
            makefile.write_text(textwrap.dedent(f"""\
                include {SHARED_LIBRARY_MK}
                all: output-a output-b
                stamp: input
                \t$(call ASTRA_UPDATE_CONTENT_STAMP,input)
                output-a output-b &: stamp
                \t@printf x >output-a
                \t@printf x >output-b
                \t@printf x >>runs
                \t$(call ASTRA_MARK_CONTENT_OUTPUTS,stamp,output-a output-b)
                """))
            subprocess.run(["make", "--no-print-directory"], cwd=root,
                           check=True, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, text=True)
            subprocess.run(["make", "--no-print-directory"], cwd=root,
                           check=True, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, text=True)
            self.assertEqual((root / "runs").read_text(), "x")
            time.sleep(1.01)
            (root / "input").write_text("changed\n")
            subprocess.run(["make", "--no-print-directory"], cwd=root,
                           check=True, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, text=True)
            self.assertEqual((root / "runs").read_text(), "xx")


if __name__ == "__main__":
    unittest.main()
