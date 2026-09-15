#pragma once

#include <cstdint>
#include <filesystem>

namespace rex::system::xam {
class ContentManager;
}

namespace recomp {

// Installs Xbox 360 content packages (DLC in CON/LIVE/PIRS form) for the
// running title so the game finds them through XamContentCreateEnumerator.
class ContentPackageInstaller {
 public:
  ContentPackageInstaller(rex::system::xam::ContentManager& content_manager, uint32_t title_id);

  // Installs every package in a folder, or a single package file. Packages
  // that are already installed, belong to another title or are not DLC are
  // skipped with a log message. Returns the number of packages installed.
  int InstallFrom(const std::filesystem::path& source);

 private:
  bool InstallPackage(const std::filesystem::path& package_path);

  rex::system::xam::ContentManager& content_manager_;
  uint32_t title_id_;
};

}
