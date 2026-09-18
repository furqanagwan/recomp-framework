// Reading XboxUnity's listing and picking the update a build's disc takes.
//
// Build and run it directly:
//   clang++ -std=c++23 -I common/include common/tests/xboxunity_catalog_test.cpp \
//           common/src/installer/xboxunity_catalog.cpp -o xboxunity_catalog_test

#include "recomp/installer/xboxunity_catalog.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const std::string& what) {
  if (!condition) {
    std::printf("FAILED %s\n", what.c_str());
    ++failures;
  }
}

// Skate's listing, as the archive served it: one update for each of the two
// discs, told apart by media ID and the executable version they patch.
const char* const kSkate = R"({
  "Type": 1,
  "MediaIDS": [
    {
      "MediaID": "1F6E4912",
      "Updates": [
        {
          "TitleUpdateID": "21309",
          "Version": "2",
          "hash": "BCD3E1B4B7E103CC1BE2E4D22B344D1A3A794E19",
          "Size": "816",
          "UploadDate": "2011-06-08 00:00:00",
          "Name": "skate.",
          "BaseVersion": "00000004"
        }
      ],
      "Count": 1
    },
    {
      "MediaID": "21D6D331",
      "Updates": [
        {
          "TitleUpdateID": "21310",
          "Version": "2",
          "hash": "64008AAF72FE8B27F30931DCD9C61EFD6300B70C",
          "Size": "816",
          "UploadDate": "2011-06-08 00:00:00",
          "Name": "skate.",
          "BaseVersion": "00000003"
        }
      ],
      "Count": 1
    }
  ]
})";

void ReadsBothDiscsUpdates() {
  std::vector<recomp::XboxUnityUpdate> updates;
  std::string error;
  Check(recomp::XboxUnityCatalog::Parse(kSkate, updates, error), "parses the listing");
  Check(updates.size() == 2, "finds both updates");
  if (updates.size() != 2) {
    return;
  }
  Check(updates[0].id == "21309", "first update's id");
  Check(updates[0].media_id == "1F6E4912", "first update's media id");
  Check(updates[0].version == 2, "first update's version");
  Check(updates[0].base_version == 4, "first update's base version");
  Check(updates[0].size == 816 * 1024, "size comes back in bytes");
  Check(updates[0].sha1 == "BCD3E1B4B7E103CC1BE2E4D22B344D1A3A794E19", "first update's hash");
  Check(updates[0].name == "skate.", "the title's name");
  Check(updates[1].id == "21310", "second update's id");
  Check(updates[1].media_id == "21D6D331", "second update's media id");
  Check(updates[1].base_version == 3, "second update's base version");
}

void ChoosesTheOneThisDiscTakes() {
  std::vector<recomp::XboxUnityUpdate> updates;
  std::string error;
  recomp::XboxUnityCatalog::Parse(kSkate, updates, error);

  const auto european = recomp::XboxUnityCatalog::Choose(updates, 0x1F6E4912, 4);
  Check(european.has_value() && european->id == "21309", "picks the update for this disc");

  const auto other = recomp::XboxUnityCatalog::Choose(updates, 0x21D6D331, 3);
  Check(other.has_value() && other->id == "21310", "picks the other disc's update for that disc");

  // The same media ID at a version the archive does not patch, and a disc the
  // archive has never heard of, both come back empty rather than guessing.
  Check(!recomp::XboxUnityCatalog::Choose(updates, 0x1F6E4912, 3).has_value(),
        "refuses an update for another base version");
  Check(!recomp::XboxUnityCatalog::Choose(updates, 0x5C087C2C, 3).has_value(),
        "refuses an update for another disc");
}

void PrefersTheHighestVersion() {
  const char* const listing = R"({"MediaIDS":[{"MediaID":"6ADB5821","Updates":[
    {"TitleUpdateID":"1","Version":"1","Size":"10","BaseVersion":"00000002"},
    {"TitleUpdateID":"4","Version":"4","Size":"10","BaseVersion":"00000002"},
    {"TitleUpdateID":"2","Version":"2","Size":"10","BaseVersion":"00000002"}]}]})";
  std::vector<recomp::XboxUnityUpdate> updates;
  std::string error;
  Check(recomp::XboxUnityCatalog::Parse(listing, updates, error), "parses a multi-update listing");
  Check(updates.size() == 3, "finds every update for the disc");
  const auto chosen = recomp::XboxUnityCatalog::Choose(updates, 0x6ADB5821, 2);
  Check(chosen.has_value() && chosen->id == "4", "takes the highest version");
}

void RejectsWhatIsNotAListing() {
  std::vector<recomp::XboxUnityUpdate> updates;
  std::string error;
  Check(!recomp::XboxUnityCatalog::Parse("<html><body>503</body></html>", updates, error),
        "a page that is not a listing fails");
  Check(!error.empty(), "and says why");
  Check(!recomp::XboxUnityCatalog::Parse(R"({"Type":1,"MediaIDS":[]})", updates, error),
        "an empty listing fails");
}

void BuildsTheArchivesUrls() {
  Check(recomp::XboxUnityCatalog::ListUrl(0x45410813) ==
            "http://xboxunity.net/Resources/Lib/TitleUpdateInfo.php?titleid=45410813",
        "the listing url pads the title id to eight digits");
  Check(recomp::XboxUnityCatalog::DownloadUrl("21309") ==
            "http://xboxunity.net/Resources/Lib/TitleUpdate.php?tuid=21309",
        "the download url takes the update's id");
}

}  // namespace

int main() {
  ReadsBothDiscsUpdates();
  ChoosesTheOneThisDiscTakes();
  PrefersTheHighestVersion();
  RejectsWhatIsNotAListing();
  BuildsTheArchivesUrls();
  if (failures == 0) {
    std::printf("all checks passed\n");
  }
  return failures == 0 ? 0 : 1;
}
