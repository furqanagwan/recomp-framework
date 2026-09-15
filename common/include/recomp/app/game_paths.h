#pragma once

#include <filesystem>

#include <rex/rex_app.h>

#include "recomp/app/game_descriptor.h"

namespace recomp {

class GamePaths {
 public:
  void Configure(rex::PathConfig& paths);
  std::filesystem::path ResolveGameRoot(const std::filesystem::path& requested,
                                        const GameDescriptor& descriptor) const;

  bool portable() const { return portable_; }
  const std::filesystem::path& user_data_root() const { return user_data_root_; }
  std::filesystem::path settings_file() const { return user_data_root_ / "settings.toml"; }
  std::filesystem::path dlc_folder() const;

 private:
  static bool IsWritableFolder(const std::filesystem::path& folder);
  void RedirectLogsToUserData(const std::string& app_name) const;

  bool portable_ = false;
  bool executable_folder_writable_ = true;
  std::filesystem::path executable_folder_;
  std::filesystem::path user_data_root_;
};

}
