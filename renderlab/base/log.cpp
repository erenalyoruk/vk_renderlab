#include "renderlab/base/log.hpp"

#include <spdlog/cfg/env.h>
#include <spdlog/spdlog.h>

namespace rl {
void init_logging() {
  spdlog::cfg::load_env_levels();
  spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
  spdlog::set_level(spdlog::level::info);
}
}  // namespace rl
