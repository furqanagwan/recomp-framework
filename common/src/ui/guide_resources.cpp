#include "recomp/ui/guide_resources.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <system_error>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/immediate_drawer.h>

REXCVAR_DEFINE_STRING(recomp_guide_resources, "", "Recomp",
                      "Artwork for the compatibility guide: an Xbox 360 XUI package (a "
                      "shrdres.xzp the player already owns) or a folder of PNGs. Empty looks "
                      "for resources/guide next to the game, and falls back to drawn shapes.")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace recomp {

namespace {

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

// The name the guide looks a file up by: no folders, no case.
std::string LookupKey(const std::string& name) {
  const size_t slash = name.find_last_of("/\\");
  return ToLower(slash == std::string::npos ? name : name.substr(slash + 1));
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return {};
  }
  const std::streamsize length = file.tellg();
  if (length <= 0) {
    return {};
  }
  file.seekg(0);
  std::vector<uint8_t> bytes(static_cast<size_t>(length));
  if (!file.read(reinterpret_cast<char*>(bytes.data()), length)) {
    return {};
  }
  return bytes;
}

uint32_t ReadBE32(const std::vector<uint8_t>& data, size_t offset) {
  return (static_cast<uint32_t>(data[offset]) << 24) |
         (static_cast<uint32_t>(data[offset + 1]) << 16) |
         (static_cast<uint32_t>(data[offset + 2]) << 8) | static_cast<uint32_t>(data[offset + 3]);
}

uint16_t ReadBE16(const std::vector<uint8_t>& data, size_t offset) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1]);
}

}  // namespace

GuideResources::GuideResources(rex::ui::ImmediateDrawer* drawer) : drawer_(drawer) {}

GuideResources::~GuideResources() = default;

void GuideResources::LoadIfNeeded() {
  if (loaded_) {
    return;
  }
  loaded_ = true;

  std::error_code ec;
  std::filesystem::path path(REXCVAR_GET(recomp_guide_resources));
  if (path.empty()) {
    path = std::filesystem::path("resources") / "guide";
    if (!std::filesystem::exists(path, ec)) {
      return;
    }
  }
  if (!std::filesystem::exists(path, ec)) {
    REXLOG_WARN("Guide: no artwork at {}; drawing its own shapes", path.string());
    return;
  }

  if (std::filesystem::is_directory(path, ec)) {
    LoadFolder(path);
  } else {
    LoadPackage(path);
  }
  source_ = path.string();
  REXLOG_INFO("Guide: loaded {} files of artwork from {}", blobs_.size(), source_);
}

void GuideResources::LoadFolder(const std::filesystem::path& path) {
  std::error_code ec;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(path, ec)) {
    if (!entry.is_regular_file(ec)) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (ToLower(entry.path().extension().string()) == ".xzp") {
      LoadPackage(entry.path());
      continue;
    }
    AddFile(name, ReadFile(entry.path()));
  }
}

// An XUI package (.xzp): 'XUIZ', then a header, a name table, and the files.
//
//   u32 version, u32 package size, u32 unused, u32 name table size,
//   u16 entry count, u16 unused, u32 first data offset, u16 unused,
//   then one record per entry from 0x1E:
//     version 1 (Blades):     u16 name length (little-endian), UTF-16LE name,
//                             u8 unused, u16 data size, u32 data offset
//     version 3 (NXE, Metro): u8 name length, name, u32 data size, u32 offset
//
// Offsets are relative to the start of the data, which follows the name table.
void GuideResources::LoadPackage(const std::filesystem::path& path) {
  const std::vector<uint8_t> data = ReadFile(path);
  if (data.size() < 0x1E || std::memcmp(data.data(), "XUIZ", 4) != 0) {
    REXLOG_WARN("Guide: {} is not an XUI package", path.string());
    return;
  }
  const uint32_t version = ReadBE32(data, 4);
  if (version != 1 && version != 3) {
    REXLOG_WARN("Guide: {} is an XUI package of version {}, which this does not read",
                   path.string(), version);
    return;
  }
  const uint16_t count = ReadBE16(data, 0x14);
  const size_t base = ReadBE32(data, 0x10) + 0x16;

  size_t offset = 0x1E;
  for (uint16_t index = 0; index < count; ++index) {
    std::string name;
    size_t size = 0;
    size_t data_offset = 0;
    if (version == 1) {
      if (offset + 2 > data.size()) {
        break;
      }
      // The only little-endian field in the package.
      const size_t length = static_cast<size_t>(data[offset]) | (static_cast<size_t>(data[offset + 1]) << 8);
      if (offset + 2 + length * 2 + 7 > data.size()) {
        break;
      }
      name.reserve(length);
      for (size_t i = 0; i < length; ++i) {
        // The names are ASCII in practice; keep the low byte of each unit.
        name.push_back(static_cast<char>(data[offset + 2 + i * 2]));
      }
      offset += 2 + length * 2;
      size = ReadBE16(data, offset + 1);
      data_offset = ReadBE32(data, offset + 3);
      offset += 7;
    } else {
      if (offset + 1 > data.size()) {
        break;
      }
      const size_t length = data[offset];
      if (offset + 1 + length + 8 > data.size()) {
        break;
      }
      name.assign(reinterpret_cast<const char*>(data.data() + offset + 1), length);
      offset += 1 + length;
      size = ReadBE32(data, offset);
      data_offset = ReadBE32(data, offset + 4);
      offset += 8;
    }
    if (base + data_offset + size > data.size()) {
      // The last record is a terminator rather than a file.
      break;
    }
    AddFile(name, std::vector<uint8_t>(data.begin() + static_cast<long long>(base + data_offset),
                                       data.begin() +
                                           static_cast<long long>(base + data_offset + size)));
  }
}

void GuideResources::AddFile(const std::string& name, std::vector<uint8_t> bytes) {
  if (name.empty() || bytes.empty()) {
    return;
  }
  blobs_[LookupKey(name)] = std::move(bytes);
}

rex::ui::ImmediateTexture* GuideResources::Get(const std::string& name) {
  LoadIfNeeded();
  const std::string key = LookupKey(name);
  auto cached = textures_.find(key);
  if (cached != textures_.end()) {
    return cached->second.get();
  }
  auto blob = blobs_.find(key);
  if (blob == blobs_.end() || !drawer_) {
    return nullptr;
  }

  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgba = rex::ui::DecodeImageRGBA(blob->second.data(), blob->second.size(),
                                                       width, height);
  std::unique_ptr<rex::ui::ImmediateTexture> texture;
  if (!rgba.empty() && width > 0 && height > 0) {
    texture = drawer_->CreateTexture(static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                                     rex::ui::ImmediateTextureFilter::kLinear, false, rgba.data());
  } else {
    REXLOG_WARN("Guide: {} is not an image this can decode", key);
  }
  rex::ui::ImmediateTexture* raw = texture.get();
  textures_.emplace(key, std::move(texture));
  return raw;
}

}  // namespace recomp
