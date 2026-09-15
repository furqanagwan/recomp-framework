#include "recomp/app/game_paths.h"

#include <cstdio>

#include <rex/cvar.h>
#include <rex/filesystem.h>

#include "recomp/installer/disc_image_installer.h"

namespace recomp {

namespace {

constexpr const char* kPortableMarkerFile = "portable.txt";
constexpr const char* kPortableUserFolder = "user";
constexpr const char* kWriteProbeFile = ".recomp_write_probe";

}

void GamePaths::Configure(rex::PathConfig& paths) {
  executable_folder_ = rex::filesystem::GetExecutableFolder();
  std::error_code error;
  portable_ = std::filesystem::exists(executable_folder_ / kPortableMarkerFile, error);
  if (portable_) {
    paths.user_data_root = executable_folder_ / kPortableUserFolder;
    paths.cache_root = paths.user_data_root / "cache";
  }
  user_data_root_ = paths.user_data_root;
  executable_folder_writable_ = IsWritableFolder(executable_folder_);
  if (!executable_folder_writable_) {
    RedirectLogsToUserData(paths.config_path.stem().string());
  }
}

std::filesystem::path GamePaths::ResolveGameRoot(const std::filesystem::path& requested,
                                                 const GameDescriptor& descriptor) const {
  if (!requested.empty()) {
    return requested;
  }
  if (descriptor.development_game_root &&
      DiscImageInstaller::IsGameInstalled(*descriptor.development_game_root)) {
    return *descriptor.development_game_root;
  }
  return executable_folder_writable_ ? executable_folder_ / "game" : user_data_root_ / "game";
}

std::filesystem::path GamePaths::dlc_folder() const {
  return executable_folder_writable_ ? executable_folder_ / "dlc" : user_data_root_ / "dlc";
}

bool GamePaths::IsWritableFolder(const std::filesystem::path& folder) {
  const auto probe = folder / kWriteProbeFile;
  std::FILE* file = rex::filesystem::OpenFile(probe, "wb");
  if (!file) {
    return false;
  }
  std::fclose(file);
  std::error_code error;
  std::filesystem::remove(probe, error);
  return true;
}

void GamePaths::RedirectLogsToUserData(const std::string& app_name) const {
  if (rex::cvar::HasNonDefaultValue("log_file")) {
    return;
  }
  const auto log_folder = user_data_root_ / "logs";
  std::error_code error;
  std::filesystem::create_directories(log_folder, error);
  rex::cvar::SetFlagByName("log_file", (log_folder / (app_name + ".log")).string());
}

}
