#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace recomp::memory {

// Copies from a host pointer into guest memory without letting a concurrently
// unmapped guest page crash the process. A failed copy may have modified part
// of destination; callers must discard it when false is returned.
bool TryReadGuestMemory(const void* guest_host_address, void* destination, size_t size) noexcept;

template <typename T>
  requires std::is_trivially_copyable_v<T>
std::optional<T> TryReadGuest(const T* guest_host_address) noexcept {
  std::array<std::byte, sizeof(T)> bytes{};
  if (!TryReadGuestMemory(guest_host_address, bytes.data(), bytes.size())) {
    return std::nullopt;
  }
  return std::bit_cast<T>(bytes);
}

}  // namespace recomp::memory
