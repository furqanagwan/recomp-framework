#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace recomp {

struct TitleUpdateCodePatch {
  std::filesystem::path path;
  uint64_t size = 0;
  // Lowercase XXH3-128 of the exact payload. This pins codegen and runtime to
  // the same XEXP without distributing the update.
  std::string content_hash;
};

struct TitleUpdateDescriptor {
  std::string label;
  uint32_t title_id = 0;
  uint32_t media_id = 0;
  uint32_t version = 0;
  std::vector<TitleUpdateCodePatch> code_patches;
};

struct GameDescriptor {
  std::string app_name;
  std::string display_name;
  std::optional<std::filesystem::path> development_game_root;
  // Empty means this executable was generated from the disc XEX. When set,
  // this executable was generated from exactly the declared title update.
  std::optional<TitleUpdateDescriptor> title_update;
};

}  // namespace recomp
