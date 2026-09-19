import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE_KIT = ROOT / "sw/userspace/posix/kit/astra-posix.mk"
COMMAND_MAKEFILE = ROOT / "sw/userspace/commands/Makefile"
POSIX_MAKEFILE = ROOT / "sw/userspace/posix/Makefile"
NATIVE_KIT = ROOT / "ndk/make/astra-native.mk"
POSIX_KIT = ROOT / "ndk/make/astra-posix.mk"
DISTRIBUTION_SMOKE = ROOT / "ndk/tests/distribution_smoke.mk"
TOOLCHAIN_DRIVER = ROOT / "toolchain/test-gcc-driver.sh"


class PosixStartupKitTest(unittest.TestCase):
    def test_source_tree_adapter_uses_ndk_policy(self):
        source = SOURCE_KIT.read_text()
        self.assertIn("include $(ASTRA_POSIX_NDK)/make/astra-posix.mk", source)
        for duplicated_policy in (
                "-nostdlib", "-fPIE", "-Wl,-Bdynamic", "-Wl,-z,now",
                "-Wl,-z,relro", "-Wl,--gc-sections", "PICOLIBC ?="):
            self.assertNotIn(duplicated_policy, source)

    def test_commands_consume_the_same_link_policy(self):
        source = COMMAND_MAKEFILE.read_text()
        self.assertIn("include ../posix/kit/astra-posix.mk", source)
        self.assertIn("$(ASTRA_POSIX_LDFLAGS)", source)
        for duplicated_policy in (
                "DYNAMIC_LINK_FLAGS :=", "-nostdlib", "-Wl,-Bdynamic",
                "-Wl,-z,now", "-Wl,-z,relro", "-Wl,--gc-sections",
                "PICOLIBC ?="):
            self.assertNotIn(duplicated_policy, source)

    def test_dynamic_programs_compile_pic_and_never_pie_objects(self):
        policy = NATIVE_KIT.read_text()
        self.assertIn("-fdata-sections -fPIC", policy)
        self.assertIn("ASTRA_LDFLAGS ?= -nostdlib -pie", policy)
        producers = list((ROOT / "sw/userspace").rglob("Makefile"))
        producers += list((ROOT / "sw/userspace").rglob("*.mk"))
        producers += list((ROOT / "ndk/make").glob("*.mk"))
        for producer in producers:
            self.assertNotIn("-fPIE", producer.read_text(), str(producer))
        driver = TOOLCHAIN_DRIVER.read_text()
        self.assertIn('"$CC" -I"$SOURCE_ROOT/sw/include" -fPIC '
                      '-ftls-model=initial-exec', driver)
        self.assertNotIn("-fPIE", driver)

    def test_dynamic_program_tls_uses_the_loader_supported_model(self):
        policy = NATIVE_KIT.read_text()
        c_flags = policy.split("ASTRA_CFLAGS ?=", 1)[1].split(
            "ASTRA_CXXFLAGS ?=", 1)[0]
        cxx_flags = policy.split("ASTRA_CXXFLAGS ?=", 1)[1].split(
            "ASTRA_LIB_DIR ?=", 1)[0]
        self.assertIn("-ftls-model=initial-exec", c_flags)
        self.assertIn("-ftls-model=initial-exec", cxx_flags)
        self.assertNotIn("-ftls-model=global-dynamic", policy)

    def test_commands_rebuild_when_included_posix_policy_changes(self):
        policy = POSIX_KIT.read_text()
        self.assertIn("ASTRA_POSIX_POLICY_INPUTS ?=", policy)

        commands = COMMAND_MAKEFILE.read_text()
        for target in (
                "build/m68k/lua-obj/interpreter.o:",
                "$(CXX_PROGRAM):", "$(VIM_PROGRAM):", "$(ZSH_PROGRAM):",
                "$(VIM_BUILD_CONFIG):", "$(ZSH_BUILD_CONFIG):",
                "define ASTRA_SOURCE_COMMAND"):
            declaration = commands.split(target, 1)[1][:500]
            self.assertIn("$(ASTRA_POSIX_POLICY_INPUTS)", declaration,
                          target)

    def test_zsh_configure_has_its_link_probe_identity(self):
        commands = COMMAND_MAKEFILE.read_text()
        self.assertIn("ZSH_LIBS := $(abspath $(ZSH_PROGRAM))", commands)
        prerequisites = commands.split("$(ZSH_BUILD_CONFIG):", 1)[1].split(
            "\n\trm -rf", 1)[0]
        self.assertIn("$(ZSH_PROGRAM)", prerequisites)

    def test_posix_dynamic_contract_uses_the_pic_compile_policy(self):
        source = POSIX_MAKEFILE.read_text()
        recipe = source.split("$(LINK_CONTRACT):", 1)[1].split("\ntest:", 1)[0]
        self.assertIn("$(ASTRA_POSIX_CFLAGS)", recipe)
        self.assertNotIn("$(TARGET_FLAGS)", recipe)

    def test_cxx_dynamic_startup_is_pic_and_static_startup_stays_static(self):
        policy = POSIX_KIT.read_text()
        self.assertIn("ASTRA_CXX_CRTBEGIN ?= $(shell $(ASTRA_CXX) "
                      "-print-file-name=crtbeginS.o)", policy)
        self.assertIn("ASTRA_CXX_CRTEND ?= $(shell $(ASTRA_CXX) "
                      "-print-file-name=crtendS.o)", policy)
        self.assertNotIn("ASTRA_CXX_CRTBEGIN ?= $(shell $(ASTRA_CXX) "
                         "-print-file-name=crtbegin.o)", policy)

        smoke = DISTRIBUTION_SMOKE.read_text()
        dynamic = smoke.split("$(BUILD_DIR)/posix-cxx.elf:", 1)[1].split(
            "$(BUILD_DIR)/posix-cxx-static.elf:", 1)[0]
        static = smoke.split("$(BUILD_DIR)/posix-cxx-static.elf:", 1)[1]
        self.assertIn("$(ASTRA_CXX_CRTBEGIN)", dynamic)
        self.assertNotIn("$(ASTRA_STATIC_CXX_CRTBEGIN)", dynamic)
        self.assertIn("$(ASTRA_STATIC_CXX_CRTBEGIN)", static)
        self.assertNotIn("$(ASTRA_CXX_CRTBEGIN)", static)


if __name__ == "__main__":
    unittest.main()
