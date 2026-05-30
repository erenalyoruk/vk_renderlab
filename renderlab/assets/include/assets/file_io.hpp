#pragma once

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

namespace rl::assets {

[[nodiscard]] std::filesystem::path resolve_runfile(std::string_view workspace_relative_path);
[[nodiscard]] std::vector<std::byte> read_binary_file(const std::filesystem::path& path);

}  // namespace rl::assets
