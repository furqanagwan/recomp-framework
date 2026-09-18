#include "recomp/installer/xboxunity_catalog.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <string>

namespace recomp {

namespace {

// XboxUnity is served over plain HTTP; it has no certificate to offer, so
// asking for HTTPS fails rather than falling back. What comes back is checked
// against the build's own digests before it is installed, so the transport is
// not what makes the package trustworthy.
constexpr const char* kHost = "http://xboxunity.net";

// The response is a small, flat object of strings and numbers, so it is read
// with a scanner for that shape rather than by taking on a JSON library. Values
// arrive as either "3" or 3, and every key that matters is unique inside the
// object it belongs to.
class Scanner {
 public:
  explicit Scanner(const std::string& text) : text_(text) {}

  // Moves to just past the next occurrence of a key, e.g. "MediaID". Returns
  // false at the end of the text.
  bool SeekKey(const std::string& key) {
    const std::string quoted = "\"" + key + "\"";
    const size_t at = text_.find(quoted, position_);
    if (at == std::string::npos) {
      return false;
    }
    position_ = at + quoted.size();
    return true;
  }

  // The value that follows the key the scanner is sitting on.
  std::string Value() {
    size_t at = text_.find(':', position_);
    if (at == std::string::npos) {
      return {};
    }
    ++at;
    while (at < text_.size() && (text_[at] == ' ' || text_[at] == '\t' || text_[at] == '\n' ||
                                 text_[at] == '\r')) {
      ++at;
    }
    if (at >= text_.size()) {
      return {};
    }
    if (text_[at] == '"') {
      const size_t end = text_.find('"', at + 1);
      if (end == std::string::npos) {
        return {};
      }
      position_ = end + 1;
      return text_.substr(at + 1, end - at - 1);
    }
    const size_t end = text_.find_first_of(",}]", at);
    const size_t stop = end == std::string::npos ? text_.size() : end;
    position_ = stop;
    std::string value = text_.substr(at, stop - at);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\n' || value.back() == '\r')) {
      value.pop_back();
    }
    return value;
  }

  size_t position() const { return position_; }
  void seek(size_t position) { position_ = position; }
  size_t Find(const std::string& text, size_t from) const { return text_.find(text, from); }

 private:
  const std::string& text_;
  size_t position_ = 0;
};

uint32_t ToNumber(const std::string& text, int base = 10) {
  uint32_t value = 0;
  const char* first = text.data();
  const char* last = text.data() + text.size();
  std::from_chars(first, last, value, base);
  return value;
}

std::string Upper(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
  return text;
}

}  // namespace

std::string XboxUnityCatalog::ListUrl(uint32_t title_id) {
  char title[16] = {};
  std::snprintf(title, sizeof(title), "%08X", title_id);
  return std::string(kHost) + "/Resources/Lib/TitleUpdateInfo.php?titleid=" + title;
}

std::string XboxUnityCatalog::DownloadUrl(const std::string& update_id) {
  return std::string(kHost) + "/Resources/Lib/TitleUpdate.php?tuid=" + update_id;
}

bool XboxUnityCatalog::Parse(const std::string& body, std::vector<XboxUnityUpdate>& updates,
                             std::string& error) {
  updates.clear();
  if (body.find("\"MediaIDS\"") == std::string::npos) {
    error = "The archive did not answer with a list of updates.";
    return false;
  }

  Scanner scanner(body);
  std::string media_id;
  while (true) {
    // Each media ID is followed by its own updates, up to the next media ID.
    const size_t media_at = scanner.Find("\"MediaID\"", scanner.position());
    if (media_at == std::string::npos) {
      break;
    }
    scanner.seek(media_at);
    scanner.SeekKey("MediaID");
    media_id = Upper(scanner.Value());
    const size_t next_media = scanner.Find("\"MediaID\"", scanner.position());

    while (true) {
      const size_t update_at = scanner.Find("\"TitleUpdateID\"", scanner.position());
      if (update_at == std::string::npos ||
          (next_media != std::string::npos && update_at > next_media)) {
        break;
      }
      scanner.seek(update_at);
      scanner.SeekKey("TitleUpdateID");

      XboxUnityUpdate update;
      update.media_id = media_id;
      update.id = scanner.Value();
      const size_t end =
          std::min(scanner.Find("\"TitleUpdateID\"", scanner.position()),
                   next_media == std::string::npos ? std::string::npos : next_media);
      const auto field = [&](const std::string& key) -> std::string {
        const size_t mark = scanner.position();
        const size_t at = scanner.Find("\"" + key + "\"", mark);
        if (at == std::string::npos || (end != std::string::npos && at > end)) {
          scanner.seek(mark);
          return {};
        }
        scanner.seek(at);
        scanner.SeekKey(key);
        std::string value = scanner.Value();
        scanner.seek(mark);
        return value;
      };
      update.version = ToNumber(field("Version"));
      update.base_version = ToNumber(field("BaseVersion"));
      update.sha1 = Upper(field("hash"));
      update.name = field("Name");
      update.upload_date = field("UploadDate");
      // The archive lists a size in kilobytes.
      update.size = static_cast<uint64_t>(ToNumber(field("Size"))) * 1024ull;

      if (!update.id.empty()) {
        updates.push_back(std::move(update));
      }
      scanner.seek(scanner.position() + 1);
    }
    scanner.seek(next_media == std::string::npos ? body.size() : next_media);
    if (next_media == std::string::npos) {
      break;
    }
  }

  if (updates.empty()) {
    error = "The archive lists no updates for this title.";
    return false;
  }
  return true;
}

std::optional<XboxUnityUpdate> XboxUnityCatalog::Choose(const std::vector<XboxUnityUpdate>& updates,
                                                        uint32_t media_id, uint32_t base_version) {
  char wanted[16] = {};
  std::snprintf(wanted, sizeof(wanted), "%08X", media_id);

  const XboxUnityUpdate* best = nullptr;
  for (const XboxUnityUpdate& update : updates) {
    if (update.media_id != wanted || update.base_version != base_version) {
      continue;
    }
    if (!best || update.version > best->version) {
      best = &update;
    }
  }
  if (!best) {
    return std::nullopt;
  }
  return *best;
}

}  // namespace recomp
