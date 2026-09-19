from tools.check_elf_exports import differences


def test_reports_missing_and_unexpected_exports_in_order() -> None:
    missing, unexpected = differences(
        {"alpha", "beta", "gamma"}, {"alpha", "delta", "gamma"}
    )
    assert missing == ["beta"]
    assert unexpected == ["delta"]
