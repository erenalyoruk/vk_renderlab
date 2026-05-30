#include "assets/file_io.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace rl::assets {

namespace {

constexpr std::string_view workspace_name = "vk_renderlab";

[[nodiscard]] bool exists_regular_file(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::is_regular_file(path, error);
}

[[nodiscard]] std::filesystem::path checked_candidate(const std::filesystem::path& path) {
  if (exists_regular_file(path)) {
    return path;
  }
  return {};
}

[[nodiscard]] std::filesystem::path env_candidate(std::string_view env_value,
                                                  std::string_view workspace_relative_path) {
  if (env_value.empty()) {
    return {};
  }

  const std::filesystem::path root{env_value};
  const std::string workspace{workspace_name};
  const std::string relative_path{workspace_relative_path};

  if (auto path = checked_candidate(root / workspace / relative_path); !path.empty()) {
    return path;
  }
  if (auto path = checked_candidate(root / "_main" / relative_path); !path.empty()) {
    return path;
  }
  if (auto path = checked_candidate(root / relative_path); !path.empty()) {
    return path;
  }

  return {};
}

}  // namespace

std::filesystem::path resolve_runfile(std::string_view workspace_relative_path) {
  const std::filesystem::path relative_path{std::string(workspace_relative_path)};
  if (auto path = checked_candidate(relative_path); !path.empty()) {
    return path;
  }

  if (auto path = env_candidate("RUNFILES_DIR", workspace_relative_path); !path.empty()) {
    return path;
  }

  if (auto path = env_candidate("TEST_SRCDIR", workspace_relative_path); !path.empty()) {
    return path;
  }

  throw std::runtime_error{"unable to resolve Bazel runfile: " + std::string{workspace_relative_path}};
}

std::vector<std::byte> read_binary_file(const std::filesystem::path& path) {
  std::ifstream file{path, std::ios::binary | std::ios::ate};
  if (!file.is_open()) {
    throw std::runtime_error{"failed to open binary file: " + path.string()};
  }

  const std::streampos size = file.tellg();
  if (size == std::streampos(-1)) {
    throw std::runtime_error{"failed to query binary file size: " + path.string()};
  }

  std::vector<char> raw_bytes(static_cast<std::size_t>(size));
  file.seekg(0, std::ios::beg);

  if (!raw_bytes.empty()) {
    file.read(raw_bytes.data(), static_cast<std::streamsize>(raw_bytes.size()));
    if (!file.good()) {
      throw std::runtime_error{"failed to read binary file: " + path.string()};
    }
  }

  std::vector<std::byte> bytes(raw_bytes.size());
  std::ranges::transform(raw_bytes, bytes.begin(),
                         [](char value) { return std::byte{static_cast<unsigned char>(value)}; });

  return bytes;
}
}  // namespace rl::assets
