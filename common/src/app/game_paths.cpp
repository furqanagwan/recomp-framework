#include "recomp/app/game_paths.h"

#include <cstdint>
#include <cstdio>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <appmodel.h>
#endif

#include <rex/cvar.h>
#include <rex/filesystem.h>

#include "recomp/installer/disc_image_installer.h"

namespace recomp {

namespace {

constexpr const char* kPortableMarkerFile = "portable.txt";
constexpr const char* kPortableUserFolder = "user";
constexpr const char* kWriteProbeFile = ".recomp_write_probe";

// A build carries RECOMP_DEVELOPMENT_GAME_ROOT so a developer's own extracted
// copy starts the game without installing one. A packaged build is the same
// binary, so on the machine that built it that path still exists and the game
// silently runs from outside its package - no install prompt, and a layout that
// is not what a player would get. Packaged identity is the line between the
// two: it is what a player's copy has and a developer's loose build does not.
bool RunningPackaged() {
#ifdef _WIN32
  uint32_t length = 0;
  // Unpackaged answers APPMODEL_ERROR_NO_PACKAGE; packaged asks for a buffer.
  return ::GetCurrentPackageFullName(&length, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
#else
  return false;
#endif
}

}  // namespace

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
  if (descriptor.development_game_root && !RunningPackaged() &&
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

}  // namespace recomp
