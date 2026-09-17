#pragma once

#include <algorithm>
#include <initializer_list>
#include <span>
#include <vector>

#include <imgui.h>

namespace recomp {

// Keys held when a screen opens - the A that chose the option that opened it, a
// stick resting off centre - count only once they have been let go.
class HeldKeyMask {
 public:
  void Arm(std::span<const ImGuiKey> watched) {
    masked_.clear();
    for (ImGuiKey key : watched) {
      if (ImGui::IsKeyDown(key)) {
        masked_.push_back(key);
      }
    }
  }

  bool Pressed(std::initializer_list<ImGuiKey> keys, bool repeat) {
    bool pressed = false;
    for (ImGuiKey key : keys) {
      auto masked = std::find(masked_.begin(), masked_.end(), key);
      if (masked != masked_.end()) {
        if (ImGui::IsKeyDown(key)) {
          continue;
        }
        masked_.erase(masked);
      }
      pressed = ImGui::IsKeyPressed(key, repeat) || pressed;
    }
    return pressed;
  }

 private:
  std::vector<ImGuiKey> masked_;
};

}  // namespace recomp
