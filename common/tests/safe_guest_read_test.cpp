#include "recomp/memory/safe_guest_read.h"

#include <array>
#include <cstdint>
#include <cstdio>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

struct GuestValue {
  uint32_t first;
  uint32_t second;
};

int Fail(const char* message) {
  std::fprintf(stderr, "%s\n", message);
  return 1;
}

}  // namespace

int main() {
  const GuestValue source{0x12345678u, 0x9ABCDEF0u};
  const auto value = recomp::memory::TryReadGuest(&source);
  if (!value || value->first != source.first || value->second != source.second) {
    return Fail("valid guest read failed");
  }

  std::array<uint8_t, sizeof(GuestValue)> destination{};
  if (recomp::memory::TryReadGuestMemory(nullptr, destination.data(), destination.size())) {
    return Fail("null guest read succeeded");
  }

#if defined(_WIN32)
  SYSTEM_INFO system_info{};
  GetSystemInfo(&system_info);
  void* page =
      VirtualAlloc(nullptr, system_info.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (page == nullptr || !VirtualFree(page, 0, MEM_RELEASE)) {
    return Fail("could not create unmapped test page");
  }
#else
  const long page_size = sysconf(_SC_PAGESIZE);
  void* page = mmap(nullptr, static_cast<size_t>(page_size), PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (page == MAP_FAILED || munmap(page, static_cast<size_t>(page_size)) != 0) {
    return Fail("could not create unmapped test page");
  }
#endif

  if (recomp::memory::TryReadGuestMemory(page, destination.data(), destination.size())) {
    return Fail("unmapped guest read succeeded");
  }
  if (recomp::memory::TryReadGuestMemory(page, destination.data(), destination.size())) {
    return Fail("retried unmapped guest read succeeded");
  }
  if (!recomp::memory::TryReadGuest(&source)) {
    return Fail("valid read after a fault failed");
  }
  return 0;
}
