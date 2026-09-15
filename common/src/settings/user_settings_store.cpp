#include "recomp/settings/user_settings_store.h"

#include <array>
#include <fstream>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(recomp_shared_controllers, true, "Recomp",
                    "Route every connected controller to player 1");

namespace recomp {

namespace {

constexpr std::array<UserSetting, 9> kUserSettings = {{
    {"fullscreen", false, false},
    {"resolution_scale", false, true},
    {"swap_post_effect", true, false},
    {"vsync", false, false},
    {"render_target_path_d3d12", true, true},
    {"render_target_path_vulkan", true, true},
    {"input_backend", true, true},
    {"recomp_shared_controllers", false, true},
    {"guide_button", false, false},
}};

}

std::span<const UserSetting> UserSettingsStore::Settings() {
  return kUserSettings;
}

void UserSettingsStore::Load() const {
  std::error_code error;
  if (!std::filesystem::exists(file_, error)) {
    return;
  }
  rex::cvar::LoadConfig(file_);
  REXLOG_INFO("Loaded user settings from {}", file_.string());
}

bool UserSettingsStore::Save() const {
  std::error_code error;
  std::filesystem::create_directories(file_.parent_path(), error);
  std::ofstream out(file_, std::ios::binary | std::ios::trunc);
  if (!out) {
    REXLOG_ERROR("Could not write user settings to {}", file_.string());
    return false;
  }
  for (const auto& setting : kUserSettings) {
    const std::string value = rex::cvar::GetFlagByName(setting.cvar);
    out << setting.cvar << " = " << (setting.quoted ? "\"" + value + "\"" : value) << "\n";
  }
  REXLOG_INFO("Saved user settings to {}", file_.string());
  return static_cast<bool>(out);
}

}
