#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rex::system::xam {
class ContentManager;
}

namespace recomp {

// Installs Xbox 360 content packages (DLC in CON/LIVE/PIRS form) for the
// running title so the game finds them through XamContentCreateEnumerator.
class ContentPackageInstaller {
 public:
  ContentPackageInstaller(rex::system::xam::ContentManager& content_manager, uint32_t title_id);

  // One piece of content the running title already has on its hard drive.
  struct InstalledContent {
    // The name inside the package, which is what the console listed.
    std::string display_name;
    // The package's own file name, which is how content is identified.
    std::string file_name;
  };

  // Installs every package in a folder, or a single package file. Packages
  // that are already installed, belong to another title or are not DLC are
  // skipped with a log message. Returns the number of packages installed.
  int InstallFrom(const std::filesystem::path& source);

  // Installs one package and says why if it could not, for a caller that has
  // to put the reason on screen rather than only in the log.
  bool InstallOne(const std::filesystem::path& package_path, std::string& error);

  // The title's downloadable content, as the guide lists it.
  std::vector<InstalledContent> ListInstalled() const;

 private:
  bool InstallPackage(const std::filesystem::path& package_path, std::string* error = nullptr);

  rex::system::xam::ContentManager& content_manager_;
  uint32_t title_id_;
};

}  // namespace recomp
