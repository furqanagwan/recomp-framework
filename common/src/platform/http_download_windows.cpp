#include "recomp/platform/http_download.h"

#include <fstream>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <winhttp.h>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/string/utf8.h>

#pragma comment(lib, "winhttp.lib")

namespace recomp {

namespace {

constexpr wchar_t kUserAgent[] = L"recomp-framework";
constexpr DWORD kChunkSize = 64 * 1024;

// A handle that closes itself, so every early return below is safe.
class Handle {
 public:
  Handle() = default;
  explicit Handle(HINTERNET handle) : handle_(handle) {}
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  ~Handle() {
    if (handle_) {
      WinHttpCloseHandle(handle_);
    }
  }
  Handle& operator=(HINTERNET handle) {
    if (handle_) {
      WinHttpCloseHandle(handle_);
    }
    handle_ = handle;
    return *this;
  }
  operator HINTERNET() const { return handle_; }

 private:
  HINTERNET handle_ = nullptr;
};

std::string LastErrorText() {
  const DWORD code = GetLastError();
  char* buffer = nullptr;
  const DWORD length = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_HMODULE,
      GetModuleHandleW(L"winhttp.dll"), code, 0, reinterpret_cast<char*>(&buffer), 0, nullptr);
  std::string text = length ? std::string(buffer, length) : std::string();
  if (buffer) {
    LocalFree(buffer);
  }
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
    text.pop_back();
  }
  if (text.empty()) {
    text = "error " + std::to_string(code);
  }
  return text;
}

}  // namespace

bool HttpDownload::IsAvailable() { return true; }

bool HttpDownload::Fetch(const std::string& url, const std::filesystem::path& destination,
                         Progress& progress, std::string& error) {
  // wchar_t is UTF-16 here, so the SDK's conversion transfers element for element.
  const std::u16string url16 = rex::string::to_utf16(url);
  const std::wstring wide_url(url16.begin(), url16.end());

  URL_COMPONENTS components = {};
  components.dwStructSize = sizeof(components);
  components.dwHostNameLength = 1;
  components.dwUrlPathLength = 1;
  components.dwExtraInfoLength = 1;
  if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &components)) {
    error = "That download address could not be read: " + LastErrorText();
    return false;
  }
  const std::wstring host(components.lpszHostName, components.dwHostNameLength);
  std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
  if (components.dwExtraInfoLength) {
    path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
  }

  Handle session(WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) {
    error = "No connection could be opened: " + LastErrorText();
    return false;
  }
  Handle connection(WinHttpConnect(session, host.c_str(), components.nPort, 0));
  if (!connection) {
    error = "Could not reach " + rex::string::to_utf8(std::u16string(host.begin(), host.end())) + ": " + LastErrorText();
    return false;
  }
  const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
  Handle request(WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
                                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
  if (!request) {
    error = "The request could not be made: " + LastErrorText();
    return false;
  }
  if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                          0) ||
      !WinHttpReceiveResponse(request, nullptr)) {
    error = "Could not reach " + rex::string::to_utf8(std::u16string(host.begin(), host.end())) + ": " + LastErrorText();
    return false;
  }

  DWORD status = 0;
  DWORD status_size = sizeof(status);
  if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                          WINHTTP_NO_HEADER_INDEX) &&
      status != 200) {
    error = "The server answered " + std::to_string(status) + ".";
    return false;
  }

  uint64_t declared = 0;
  DWORD declared_size = sizeof(declared);
  if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER64,
                          WINHTTP_HEADER_NAME_BY_INDEX, &declared, &declared_size,
                          WINHTTP_NO_HEADER_INDEX)) {
    progress.total = declared;
  }

  std::error_code ec;
  std::filesystem::create_directories(destination.parent_path(), ec);
  std::filesystem::path partial = destination;
  partial += ".part";
  std::ofstream file(partial, std::ios::binary | std::ios::trunc);
  if (!file) {
    error = "Could not write to " + rex::path_to_utf8(partial);
    return false;
  }

  std::vector<char> chunk(kChunkSize);
  for (;;) {
    if (progress.cancelled) {
      file.close();
      std::filesystem::remove(partial, ec);
      error = "The download was stopped.";
      return false;
    }
    DWORD read = 0;
    if (!WinHttpReadData(request, chunk.data(), static_cast<DWORD>(chunk.size()), &read)) {
      file.close();
      std::filesystem::remove(partial, ec);
      error = "The download failed: " + LastErrorText();
      return false;
    }
    if (read == 0) {
      break;
    }
    file.write(chunk.data(), read);
    if (!file) {
      file.close();
      std::filesystem::remove(partial, ec);
      error = "Could not write to " + rex::path_to_utf8(partial);
      return false;
    }
    progress.received += read;
  }
  file.close();

  std::filesystem::remove(destination, ec);
  std::filesystem::rename(partial, destination, ec);
  if (ec) {
    std::filesystem::remove(partial, ec);
    error = "Could not save the download to " + rex::path_to_utf8(destination);
    return false;
  }
  REXLOG_INFO("Downloaded {} bytes to {}", progress.received.load(), rex::path_to_utf8(destination));
  return true;
}

}  // namespace recomp
