#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

namespace recomp {

// A plain HTTP GET to a file, for fetching a title update the player chooses to
// download. Nothing here is required for a game to run: when the host has no
// way to make the request, or the request fails, the caller carries on and the
// player installs a package from their own machine instead.
class HttpDownload {
 public:
  struct Progress {
    // Bytes written so far, and the length the server declared (0 when it
    // declared none). Both are read from another thread while the download
    // runs, so they are atomic.
    std::atomic<uint64_t> received{0};
    std::atomic<uint64_t> total{0};
    std::atomic<bool> cancelled{false};
  };

  // True when this build can make requests at all. False on a host without a
  // client compiled in, which leaves only the manual path.
  static bool IsAvailable();

  // Fetches url into destination, replacing whatever is there. Returns false
  // and fills error on any failure, including a cancelled download. The file is
  // written through a temporary and moved into place, so a failed download
  // never leaves a half-written package behind.
  static bool Fetch(const std::string& url, const std::filesystem::path& destination,
                    Progress& progress, std::string& error);
};

}  // namespace recomp
