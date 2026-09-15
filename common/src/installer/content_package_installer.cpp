#include "recomp/installer/content_package_installer.h"

#include <algorithm>
#include <vector>

#include <rex/filesystem.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/logging.h>
#include <rex/system/xam/content_device.h>
#include <rex/system/xam/content_manager.h>

namespace recomp {

namespace {

using rex::system::XContentType;

std::vector<std::filesystem::path> CollectPackageFiles(const std::filesystem::path& source) {
  std::vector<std::filesystem::path> files;
  std::error_code error;
  if (std::filesystem::is_regular_file(source, error)) {
    files.push_back(source);
    return files;
  }
  if (!std::filesystem::is_directory(source, error)) {
    return files;
  }
  for (const auto& entry : std::filesystem::recursive_directory_iterator(
           source, std::filesystem::directory_options::skip_permission_denied, error)) {
    if (entry.is_regular_file(error)) {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

}

ContentPackageInstaller::ContentPackageInstaller(rex::system::xam::ContentManager& content_manager,
                                                 uint32_t title_id)
    : content_manager_(content_manager), title_id_(title_id) {}

int ContentPackageInstaller::InstallFrom(const std::filesystem::path& source) {
  int installed = 0;
  for (const auto& file : CollectPackageFiles(source)) {
    if (InstallPackage(file)) {
      ++installed;
    }
  }
  return installed;
}

bool ContentPackageInstaller::InstallPackage(const std::filesystem::path& package_path) {
  const auto header = rex::filesystem::StfsContainerDevice::ReadPackageHeader(package_path);
  if (!header) {
    return false;
  }
  const auto file_name = rex::path_to_utf8(package_path.filename());
  const XContentType content_type = header->metadata.content_type;
  const uint32_t package_title_id = header->metadata.execution_info.title_id;

  if (package_title_id != title_id_) {
    REXLOG_WARN("DLC: skipping {}: it belongs to title {:08X}, not {:08X}", file_name,
                package_title_id, title_id_);
    return false;
  }
  if (content_type == XContentType::kInstaller) {
    REXLOG_WARN("DLC: skipping {}: title updates change game code and need a recompile from the "
                "updated default.xex",
                file_name);
    return false;
  }
  if (content_type != XContentType::kMarketplaceContent) {
    REXLOG_WARN("DLC: skipping {}: content type {:08X} is not downloadable content", file_name,
                uint32_t(content_type));
    return false;
  }

  rex::system::xam::XCONTENT_AGGREGATE_DATA content_data;
  content_data.device_id = static_cast<uint32_t>(rex::system::xam::DummyDeviceId::HDD);
  content_data.content_type = XContentType::kMarketplaceContent;
  content_data.title_id = title_id_;
  content_data.xuid = 0;
  content_data.set_file_name(file_name);
  rex::system::xam::XCONTENT_AGGREGATE_DATA installed_header;
  if (XSUCCEEDED(content_manager_.ReadContentHeaderFile(content_data.file_name(), 0, title_id_,
                                                        content_type, installed_header))) {
    REXLOG_DEBUG("DLC: {} is already installed", file_name);
    return false;
  }

  const auto display_name =
      rex::string::to_utf8(header->metadata.display_name(rex::system::XLanguage::kEnglish));
  REXLOG_INFO("DLC: installing {} ({})", display_name.empty() ? file_name : display_name, file_name);
  const auto result = content_manager_.InstallContent(package_path);
  if (XFAILED(result)) {
    REXLOG_ERROR("DLC: installing {} failed: {:08X}", file_name, result);
    content_manager_.DeleteContent(0, content_data);
    return false;
  }
  return true;
}

}
