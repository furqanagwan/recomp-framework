#pragma once

#include <filesystem>
#include <span>
#include <string_view>

namespace recomp {

struct UserSetting {
  std::string_view cvar;
  bool quoted;
  bool requires_restart;
};

class UserSettingsStore {
 public:
  explicit UserSettingsStore(std::filesystem::path file) : file_(std::move(file)) {}

  static std::span<const UserSetting> Settings();

  void Load() const;
  bool Save() const;

 private:
  std::filesystem::path file_;
};

}
