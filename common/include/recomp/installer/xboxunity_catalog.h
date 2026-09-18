#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace recomp {

// XboxUnity's archive of Xbox 360 title updates.
//
// A console downloaded its updates from Xbox Live, which is gone. XboxUnity
// keeps the same packages, listed by title ID and grouped by the media ID of
// the disc each one patches, so a build can offer the player the update its
// disc actually takes. Nothing here is required: when the site cannot be
// reached the player installs a package from their own machine instead.
//
//   list      GET /Resources/Lib/TitleUpdateInfo.php?titleid=<8 hex digits>
//   download  GET /Resources/Lib/TitleUpdate.php?tuid=<TitleUpdateID>
struct XboxUnityUpdate {
  std::string id;           // TitleUpdateID, which the download takes
  std::string name;         // the title as the archive spells it
  std::string media_id;     // 8 hex digits, the disc this one patches
  uint32_t version = 0;     // the update's number: 2 for Title Update 2
  uint32_t base_version = 0;// the executable version it patches
  uint64_t size = 0;        // bytes, converted from the kilobytes listed
  std::string sha1;         // of the package, as the archive holds it
  std::string upload_date;
};

class XboxUnityCatalog {
 public:
  static std::string ListUrl(uint32_t title_id);
  static std::string DownloadUrl(const std::string& update_id);

  // Reads a TitleUpdateInfo response. Returns false when the body is not a
  // listing at all, which is what a site that is down tends to answer with.
  static bool Parse(const std::string& body, std::vector<XboxUnityUpdate>& updates,
                    std::string& error);

  // The update a build should offer: one for this disc's media ID that patches
  // the executable version the build was recompiled from. Where the archive
  // holds several, the highest version wins.
  static std::optional<XboxUnityUpdate> Choose(const std::vector<XboxUnityUpdate>& updates,
                                               uint32_t media_id, uint32_t base_version);
};

}  // namespace recomp
