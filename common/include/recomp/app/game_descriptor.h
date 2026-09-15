#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace recomp {

struct GameDescriptor {
  std::string app_name;
  std::string display_name;
  std::optional<std::filesystem::path> development_game_root;
};

}
