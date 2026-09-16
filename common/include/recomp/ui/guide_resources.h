#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rex::ui {
class ImmediateDrawer;
class ImmediateTexture;
}  // namespace rex::ui

namespace recomp {

// The artwork the console's own guide draws with: button glyphs, the Xbox Live
// logo, the achievement placeholders.
//
// Those are Microsoft's files and none of them ship here. The player points the
// runtime at a copy they already own - a shrdres.xzp out of a dashboard or a
// system update, or a folder of extracted resources - through recomp_guide_resources, and the
// guide picks the pieces it knows out of it. With nothing supplied the guide
// draws its own shapes instead, so it always has a complete face.
class GuideResources {
 public:
  explicit GuideResources(rex::ui::ImmediateDrawer* drawer);
  ~GuideResources();

  // Loads from recomp_guide_resources, or from resources/guide when
  // the cvar is empty. Safe to call more than once; only the first load runs.
  void LoadIfNeeded();

  // A texture by file name ("A-Button.png"), matched without regard to case or
  // the folders above it. Null when the package had no such file, or when
  // nothing was supplied.
  rex::ui::ImmediateTexture* Get(const std::string& name);

  bool loaded() const { return loaded_; }
  // Where the artwork came from, for the log and the settings screen.
  const std::string& source() const { return source_; }
  size_t file_count() const { return blobs_.size(); }

 private:
  void LoadPackage(const std::filesystem::path& path);
  void LoadFolder(const std::filesystem::path& path);
  void AddFile(const std::string& name, std::vector<uint8_t> bytes);

  rex::ui::ImmediateDrawer* drawer_ = nullptr;
  bool loaded_ = false;
  std::string source_;
  // Keyed by lower-case file name, without the folders above it.
  std::unordered_map<std::string, std::vector<uint8_t>> blobs_;
  std::unordered_map<std::string, std::unique_ptr<rex::ui::ImmediateTexture>> textures_;
};

}  // namespace recomp
