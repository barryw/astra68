import pathlib
import subprocess
import tempfile
import textwrap
import unittest


PROGRAM_MK = pathlib.Path(__file__).resolve().parents[2] / "sw/userspace/program.mk"


class ProgramMakeTest(unittest.TestCase):
    def test_owner_contracts_are_explicit_and_ordered(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            for name in ("alpha", "beta", "unrelated"):
                owner = root / name
                owner.mkdir()
                (owner / "Makefile").write_text(textwrap.dedent(f"""\
                    contract:
                    \t@printf '{name}:contract\\n' >> ../owners.log

                    product:
                    \t@printf '{name}:product\\n' >> ../owners.log

                    all:
                    \t@printf '{name}:all\\n' >> ../owners.log
                    """))

            (root / "Makefile").write_text(textwrap.dedent(f"""\
                ASTRA_TOOLCHAIN_STAMP := build/.toolchain/test
                TARGET := build/program
                ASTRA_PROGRAM_OWNER_TARGETS := $(if \
                    $(filter build/program,$(MAKECMDGOALS)),\
                    alpha:contract beta:product,unrelated:all)
                ASTRA_PROGRAM_TARGETS := $(TARGET)
                include {PROGRAM_MK}

                $(ASTRA_TOOLCHAIN_STAMP):
                \t@mkdir -p $(@D)
                \t@touch $@

                $(TARGET):
                \t@mkdir -p $(@D)
                \t@touch $@
                """))

            subprocess.run(
                ["make", "build/program"], cwd=root, check=True,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

            self.assertEqual(
                (root / "owners.log").read_text().splitlines(),
                ["alpha:contract", "beta:product"])

    def test_malformed_owner_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            (root / "Makefile").write_text(textwrap.dedent(f"""\
                ASTRA_TOOLCHAIN_STAMP := build/.toolchain/test
                TARGET := build/program
                ASTRA_PROGRAM_OWNER_TARGETS := missing-target
                ASTRA_PROGRAM_TARGETS := $(TARGET)
                include {PROGRAM_MK}

                $(ASTRA_TOOLCHAIN_STAMP):
                \t@mkdir -p $(@D)
                \t@touch $@

                $(TARGET):
                \t@mkdir -p $(@D)
                \t@touch $@
                """))

            result = subprocess.run(
                ["make", "build/program"], cwd=root, check=False,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("invalid Astra program owner target", result.stderr)

    def test_nested_owner_uses_its_own_relative_contracts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            child = root / "child"
            leaf = root / "leaf"
            child.mkdir()
            leaf.mkdir()

            (leaf / "Makefile").write_text(textwrap.dedent("""\
                contract:
                \t@printf 'leaf:contract\\n' >> ../owners.log
                """))
            (child / "Makefile").write_text(textwrap.dedent(f"""\
                ASTRA_TOOLCHAIN_STAMP := build/.toolchain/test
                TARGET := build/child
                ASTRA_PROGRAM_OWNER_TARGETS := ../leaf:contract
                ASTRA_PROGRAM_TARGETS := $(TARGET)
                include {PROGRAM_MK}

                $(ASTRA_TOOLCHAIN_STAMP):
                \t@mkdir -p $(@D)
                \t@touch $@

                $(TARGET):
                \t@mkdir -p $(@D)
                \t@touch $@
                """))
            (root / "Makefile").write_text(textwrap.dedent(f"""\
                ASTRA_TOOLCHAIN_STAMP := build/.toolchain/test
                TARGET := build/parent
                ASTRA_PROGRAM_OWNER_TARGETS := child:all
                ASTRA_PROGRAM_TARGETS := $(TARGET)
                include {PROGRAM_MK}

                $(ASTRA_TOOLCHAIN_STAMP):
                \t@mkdir -p $(@D)
                \t@touch $@

                $(TARGET):
                \t@mkdir -p $(@D)
                \t@touch $@
                """))

            result = subprocess.run(
                ["make", "-j2", "all"], cwd=root, check=True,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

            self.assertEqual(
                (root / "owners.log").read_text().splitlines(),
                ["leaf:contract"])
            self.assertNotIn("jobserver unavailable", result.stderr)


if __name__ == "__main__":
    unittest.main()
