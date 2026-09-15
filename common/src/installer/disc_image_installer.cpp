#include "recomp/installer/disc_image_installer.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <span>
#include <vector>

#include <rex/filesystem.h>
#include <rex/filesystem/devices/disc_image_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/system/xtypes.h>

namespace recomp {

namespace {

constexpr size_t kCopyChunkBytes = 4 * 1024 * 1024;
constexpr const char* kEntrypointFile = "default.xex";

using GuestFile = std::unique_ptr<rex::filesystem::File, void (*)(rex::filesystem::File*)>;

bool IsSafePathComponent(const std::string& name) {
  return !name.empty() && name != "." && name != ".." &&
         name.find_first_of("/\\:") == std::string::npos;
}

class EntryCopier {
 public:
  EntryCopier(InstallProgress& progress, std::string& error)
      : progress_(progress), error_(error), buffer_(kCopyChunkBytes) {}

  bool CopyChildren(rex::filesystem::Entry& folder, const std::filesystem::path& host_folder) {
    for (const auto& child : folder.children()) {
      if (!IsSafePathComponent(child->name())) {
        error_ = "Disc contains an unsafe file name: " + child->name();
        return false;
      }
      const auto host_path = host_folder / std::filesystem::u8path(child->name());
      const bool copied = (child->attributes() & rex::filesystem::kFileAttributeDirectory)
                              ? CopyFolder(*child, host_path)
                              : CopyFile(*child, host_path);
      if (!copied) {
        return false;
      }
    }
    return true;
  }

 private:
  bool CopyFolder(rex::filesystem::Entry& folder, const std::filesystem::path& host_path) {
    std::error_code error;
    std::filesystem::create_directories(host_path, error);
    if (error) {
      error_ = "Could not create " + host_path.string() + ": " + error.message();
      return false;
    }
    return CopyChildren(folder, host_path);
  }

  bool CopyFile(rex::filesystem::Entry& entry, const std::filesystem::path& host_path) {
    rex::filesystem::File* raw_file = nullptr;
    if (!XSUCCEEDED(entry.Open(rex::filesystem::FileAccess::kFileReadData, &raw_file)) ||
        !raw_file) {
      error_ = "Could not read " + entry.path() + " from the disc image.";
      return false;
    }
    GuestFile guest_file(raw_file, [](rex::filesystem::File* file) { file->Destroy(); });

    std::FILE* host_file = rex::filesystem::OpenFile(host_path, "wb");
    if (!host_file) {
      error_ = "Could not create " + host_path.string() + ".";
      return false;
    }
    const bool copied = CopyContents(entry, *guest_file, host_file, host_path);
    std::fclose(host_file);
    return copied;
  }

  bool CopyContents(rex::filesystem::Entry& entry, rex::filesystem::File& guest_file,
                    std::FILE* host_file, const std::filesystem::path& host_path) {
    for (size_t offset = 0; offset < entry.size();) {
      const size_t wanted = std::min(buffer_.size(), entry.size() - offset);
      size_t read = 0;
      if (!XSUCCEEDED(guest_file.ReadSync(std::span<uint8_t>(buffer_.data(), wanted), offset, &read)) ||
          read == 0) {
        error_ = "Read error in " + entry.path() + "; the disc image may be damaged.";
        return false;
      }
      if (std::fwrite(buffer_.data(), 1, read, host_file) != read) {
        error_ = "Write error for " + host_path.string() + "; the disk may be full.";
        return false;
      }
      offset += read;
      progress_.copied_bytes += read;
    }
    return true;
  }

  InstallProgress& progress_;
  std::string& error_;
  std::vector<uint8_t> buffer_;
};

}

bool DiscImageInstaller::IsGameInstalled(const std::filesystem::path& game_root) {
  std::error_code error;
  return std::filesystem::is_regular_file(game_root / kEntrypointFile, error);
}

bool DiscImageInstaller::Install(const std::filesystem::path& disc_image,
                                 const std::filesystem::path& destination,
                                 InstallProgress& progress) {
  error_.clear();
  rex::filesystem::DiscImageDevice device("\\Device\\RecompInstallImage", disc_image);
  if (!device.Initialize()) {
    error_ = "Not a readable Xbox 360 disc image: " + disc_image.string();
    return false;
  }
  progress.total_bytes = device.total_file_size();

  std::error_code folder_error;
  std::filesystem::create_directories(destination, folder_error);
  if (folder_error) {
    error_ = "Could not create " + destination.string() + ": " + folder_error.message();
    return false;
  }

  auto* root = device.ResolvePath("");
  if (!root) {
    error_ = "The disc image has no root folder.";
    return false;
  }
  EntryCopier copier(progress, error_);
  if (!copier.CopyChildren(*root, destination)) {
    return false;
  }
  if (!IsGameInstalled(destination)) {
    error_ = "The disc image was extracted but contains no default.xex.";
    return false;
  }
  return true;
}

}
