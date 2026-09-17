#pragma once

#include <filesystem>
#include <string>

#include "recomp/app/game_descriptor.h"

namespace recomp {

class TitleUpdateInstaller {
 public:
  static bool IsInstalled(const std::filesystem::path& update_root,
                          const TitleUpdateDescriptor& descriptor);

  bool Install(const std::filesystem::path& package_path, const std::filesystem::path& update_root,
               const TitleUpdateDescriptor& descriptor);

  const std::string& error() const { return error_; }

 private:
  std::string error_;
};

}  // namespace recomp
