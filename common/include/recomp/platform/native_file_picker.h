#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace recomp {

class NativeFilePicker {
 public:
  using PickedHandler = std::function<void(std::optional<std::filesystem::path>)>;

  explicit NativeFilePicker(void* owner_window) : owner_window_(owner_window) {}

  static bool IsAvailable();
  void PickDiscImage(const std::string& title, PickedHandler on_picked) const;

 private:
  void* owner_window_;
};

}
