import pathlib
import sys
import tempfile

TOOLS = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))

import check_bundle_dependencies as dependencies


def identity(name, abi, item_version):
    return ({"name": "%s.%d" % (name, abi), "abi_major": abi,
             "version": item_version}, pathlib.Path(name))


def write(path, text):
    path.write_text(text, encoding="utf-8")
    return path


def test_matching_provider_and_compatible_requirement_pass(root):
    manifest = write(root / "matching",
                     "provides streams.library 1 1.1.0\n"
                     "requires streams.library 1 1.0.0\n")
    assert dependencies.validate(
        [identity("streams.library", 1, "1.1.0")], [manifest]) == []


def test_stale_provider_is_rejected(root):
    manifest = write(root / "stale-provider",
                     "provides filesystem.library 2 2.4.0\n")
    errors = dependencies.validate(
        [identity("filesystem.library", 3, "3.0.0")], [manifest])
    assert any("has 0 exact providers" in error for error in errors)
    assert any("has no matching library" in error for error in errors)


def test_stale_requirement_is_rejected(root):
    provider = write(root / "provider",
                     "provides filesystem.library 4 4.0.0\n")
    consumer = write(root / "consumer",
                     "requires filesystem.library 2 2.4.0\n")
    errors = dependencies.validate(
        [identity("filesystem.library", 3, "3.0.0")],
        [provider, consumer])
    assert any("requires unavailable filesystem.library ABI 2" in error
               for error in errors)


def main():
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        test_matching_provider_and_compatible_requirement_pass(root)
        test_stale_provider_is_rejected(root)
        test_stale_requirement_is_rejected(root)
    print("bundle dependency coverage tests passed")


if __name__ == "__main__":
    main()
