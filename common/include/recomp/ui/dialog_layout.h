#pragma once

struct ImGuiIO;

namespace recomp {

class DialogLayout {
 public:
  static void DrawBackdrop(const char* id, const ImGuiIO& io, float opacity);
  static void BeginCenteredPanel(const char* title, const ImGuiIO& io, float max_width,
                                 bool* open = nullptr);
  static void BeginSidePanel(const char* id, const ImGuiIO& io, float max_width);
  static bool BackPressed();
};

}
