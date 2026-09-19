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

// One piece of downloadable content the title shipped, so the guide can list it
// whether or not the player has it yet.
//
// A declared entry is matched against installed content by file_name when one
// is known - that is how the console identified content, and it does not move
// between regions - and otherwise by the name inside the package, which is the
// marketplace title. Matching by title is what a catalogue built from a store
// listing can do, but the name in the package is localised, so a player on a
// different region's copy may see their content listed separately rather than
// ticked off against the entry it belongs to.
struct DlcDescriptor {
  std::string label;
  // Empty when only the marketplace title is known.
  std::string file_name;
};

struct GameDescriptor {
  std::string app_name;
  std::string display_name;
  std::optional<std::filesystem::path> development_game_root;
  // Empty means this executable was generated from the disc XEX. When set,
  // this executable was generated from exactly the declared title update.
  std::optional<TitleUpdateDescriptor> title_update;
  // The content this title can have. Leaving it empty is fine: the guide still
  // lists whatever is installed, it just cannot name what is missing.
  std::vector<DlcDescriptor> dlc;
};

}  // namespace recomp
