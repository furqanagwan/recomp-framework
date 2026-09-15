#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

namespace recomp {

struct InstallProgress {
  std::atomic<uint64_t> copied_bytes{0};
  std::atomic<uint64_t> total_bytes{0};
};

class DiscImageInstaller {
 public:
  static bool IsGameInstalled(const std::filesystem::path& game_root);

  bool Install(const std::filesystem::path& disc_image, const std::filesystem::path& destination,
               InstallProgress& progress);

  const std::string& error() const { return error_; }

 private:
  std::string error_;
};

}
