#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>

int main()
{
    const std::expected<int, int> value = 68;
    const std::filesystem::path path("/store");
    if (std::format("{} {}", path.string(), *value).empty())
        return 1;
    return std::puts("Astra CMake C++23") < 0;
}
