#include "recomp/installer/title_update_installer.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <span>
#include <string_view>

#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/hash.h>
#include <rex/logging.h>
#include <rex/system/xam/content_device.h>

namespace recomp {

namespace {

constexpr std::string_view kMarkerName = ".recomp-title-update";

std::string Marker(const TitleUpdateDescriptor& descriptor) {
  return descriptor.label + "\n" + std::to_string(descriptor.title_id) + "\n" +
         std::to_string(descriptor.media_id) + "\n" + std::to_string(descriptor.version) + "\n";
}

bool SafeName(std::string_view name) {
  return !name.empty() && name != "." && name != ".." && name.find('/') == std::string_view::npos &&
         name.find('\\') == std::string_view::npos;
}

bool ExtractEntry(rex::filesystem::Entry& entry, const std::filesystem::path& destination,
                  std::string& error) {
  if (!SafeName(entry.name())) {
    error = "The title update contains an unsafe path component.";
    return false;
  }
  const auto target = destination / rex::to_path(entry.name());
  if (entry.attributes() & rex::filesystem::kFileAttributeDirectory) {
    std::error_code ec;
    std::filesystem::create_directories(target, ec);
    if (ec) {
      error = "Unable to create " + target.string() + ".";
      return false;
    }
    for (const auto& child : entry.children()) {
      if (!ExtractEntry(*child, target, error)) {
        return false;
      }
    }
    return true;
  }

  std::error_code ec;
  std::filesystem::create_directories(target.parent_path(), ec);
  rex::filesystem::File* source = nullptr;
  if (XFAILED(entry.Open(rex::filesystem::FileAccess::kGenericRead, &source)) || !source) {
    error = "Unable to read " + entry.path() + " from the title update.";
    return false;
  }
  std::ofstream output(target, std::ios::binary | std::ios::trunc);
  if (!output) {
    source->Destroy();
    error = "Unable to create " + target.string() + ".";
    return false;
  }

  std::array<uint8_t, 256 * 1024> buffer{};
  size_t offset = 0;
  while (offset < entry.size()) {
    size_t read = 0;
    const size_t wanted = std::min(buffer.size(), entry.size() - offset);
    if (XFAILED(source->ReadSync(std::span<uint8_t>(buffer.data(), wanted), offset, &read)) ||
        read == 0) {
      source->Destroy();
      error = "Failed while reading " + entry.path() + ".";
      return false;
    }
    output.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(read));
    if (!output) {
      source->Destroy();
      error = "Failed while writing " + target.string() + ".";
      return false;
    }
    offset += read;
  }
  source->Destroy();
  return true;
}

bool VerifyCodePatches(const std::filesystem::path& root, const TitleUpdateDescriptor& descriptor,
                       std::string& error) {
  if (descriptor.code_patches.empty()) {
    error = "The build does not declare any title-update code patches.";
    return false;
  }
  for (const auto& patch : descriptor.code_patches) {
    if (patch.path.empty() || patch.path.is_absolute() ||
        std::find(patch.path.begin(), patch.path.end(), "..") != patch.path.end()) {
      error = "The build declares an unsafe title-update code path.";
      return false;
    }
    const auto path = root / patch.path;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) ||
        std::filesystem::file_size(path, ec) != patch.size || ec) {
      error = "The package does not contain the required " + patch.path.string() + ".";
      return false;
    }
    const auto hash = rex::hash_file(path);
    if (hash.empty() || hash != patch.content_hash) {
      error = patch.path.string() + " belongs to a different title-update version.";
      return false;
    }
  }
  return true;
}

}  // namespace

bool TitleUpdateInstaller::IsInstalled(const std::filesystem::path& update_root,
                                       const TitleUpdateDescriptor& descriptor) {
  std::ifstream marker(update_root / kMarkerName, std::ios::binary);
  if (!marker) {
    return false;
  }
  const std::string value((std::istreambuf_iterator<char>(marker)),
                          std::istreambuf_iterator<char>());
  std::string error;
  return value == Marker(descriptor) && VerifyCodePatches(update_root, descriptor, error);
}

bool TitleUpdateInstaller::Install(const std::filesystem::path& package_path,
                                   const std::filesystem::path& update_root,
                                   const TitleUpdateDescriptor& descriptor) {
  error_.clear();
  const auto header = rex::filesystem::StfsContainerDevice::ReadPackageHeader(package_path);
  if (!header) {
    error_ = "The selected file is not an Xbox 360 content package.";
    return false;
  }
  if (header->metadata.content_type != rex::system::XContentType::kInstaller) {
    error_ = "The selected package is not a title update.";
    return false;
  }
  const auto& execution = header->metadata.execution_info;
  if (uint32_t(execution.title_id) != descriptor.title_id) {
    error_ = "The selected title update belongs to another game.";
    return false;
  }
  if ((descriptor.media_id && uint32_t(execution.media_id) != descriptor.media_id) ||
      (descriptor.version && uint32_t(execution.version_value) != descriptor.version)) {
    error_ = "The selected package is a different title-update version than this build requires.";
    return false;
  }

  rex::filesystem::StfsContainerDevice package("\\TitleUpdate", package_path);
  if (!package.Initialize()) {
    error_ = "The title update package is damaged or unsupported.";
    return false;
  }
  auto* root = package.ResolvePath("");
  if (!root) {
    error_ = "The title update package has no file tree.";
    return false;
  }

  auto staging = update_root;
  staging += ".installing";
  std::error_code ec;
  std::filesystem::remove_all(staging, ec);
  std::filesystem::create_directories(staging, ec);
  if (ec) {
    error_ = "Unable to create the title-update staging folder.";
    return false;
  }
  for (const auto& child : root->children()) {
    if (!ExtractEntry(*child, staging, error_)) {
      std::filesystem::remove_all(staging, ec);
      return false;
    }
  }
  if (!VerifyCodePatches(staging, descriptor, error_)) {
    std::filesystem::remove_all(staging, ec);
    return false;
  }
  {
    std::ofstream marker(staging / kMarkerName, std::ios::binary | std::ios::trunc);
    marker << Marker(descriptor);
    if (!marker) {
      error_ = "Unable to record the installed title-update version.";
      std::filesystem::remove_all(staging, ec);
      return false;
    }
  }

  std::filesystem::remove_all(update_root, ec);
  ec.clear();
  std::filesystem::rename(staging, update_root, ec);
  if (ec) {
    error_ = "Unable to activate the installed title update: " + ec.message();
    return false;
  }
  REXLOG_INFO("Installed title update {} at {}", descriptor.label, update_root.string());
  return true;
}

}  // namespace recomp
