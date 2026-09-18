#include "recomp/platform/http_download.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <rex/filesystem.h>
#include <rex/logging.h>

namespace recomp {

namespace {

// Downloading is a convenience, so rather than take a library dependency for it
// this asks curl, which these systems carry. When there is no curl the player
// installs a package from their own machine instead, which every host supports.
constexpr const char* kCurl = "curl";

bool HaveCurl() {
  static const bool available = std::system("curl --version > /dev/null 2>&1") == 0;
  return available;
}

// Everything that goes on the command line is quoted, so a URL or a path can
// hold whatever characters it likes without reaching the shell as syntax.
std::string Quoted(const std::string& text) {
  std::string quoted = "'";
  for (char character : text) {
    if (character == '\'') {
      quoted += "'\\''";
    } else {
      quoted += character;
    }
  }
  quoted += "'";
  return quoted;
}

}  // namespace

bool HttpDownload::IsAvailable() { return HaveCurl(); }

bool HttpDownload::Fetch(const std::string& url, const std::filesystem::path& destination,
                         Progress& progress, std::string& error) {
  if (!HaveCurl()) {
    error = "This system has no curl to download with.";
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(destination.parent_path(), ec);
  std::filesystem::path partial = destination;
  partial += ".part";

  const std::string command = std::string(kCurl) + " --fail --location --silent --show-error" +
                              " --output " + Quoted(rex::path_to_utf8(partial)) + " " +
                              Quoted(url) + " 2>&1";
  std::string output;
  if (FILE* pipe = popen(command.c_str(), "r")) {
    std::array<char, 256> chunk{};
    while (fgets(chunk.data(), static_cast<int>(chunk.size()), pipe)) {
      output += chunk.data();
    }
    const int status = pclose(pipe);
    if (status != 0) {
      std::filesystem::remove(partial, ec);
      error = output.empty() ? "The download failed." : output;
      return false;
    }
  } else {
    error = "The download could not be started.";
    return false;
  }

  // curl writes the whole file before returning, so the count is read once.
  const uintmax_t written = std::filesystem::file_size(partial, ec);
  if (ec) {
    error = "The download produced no file.";
    return false;
  }
  progress.received = written;
  progress.total = written;

  std::filesystem::remove(destination, ec);
  std::filesystem::rename(partial, destination, ec);
  if (ec) {
    std::filesystem::remove(partial, ec);
    error = "Could not save the download to " + rex::path_to_utf8(destination);
    return false;
  }
  REXLOG_INFO("Downloaded {} bytes to {}", written, rex::path_to_utf8(destination));
  return true;
}

}  // namespace recomp
