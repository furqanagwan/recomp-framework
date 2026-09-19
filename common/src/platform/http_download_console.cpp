#include "recomp/platform/http_download.h"

namespace recomp {

bool HttpDownload::IsAvailable() { return false; }

bool HttpDownload::Fetch(const std::string&, const std::filesystem::path&, Progress&,
                         std::string& error) {
  error = "Direct title-update downloads are not enabled for the Xbox console target.";
  return false;
}

}  // namespace recomp
