#pragma once

struct ImGuiStyle;

namespace rex::ui {
struct Style;
}

namespace recomp {

class MonochromeTheme {
 public:
  static void Apply(ImGuiStyle& imgui_style, rex::ui::Style& overlay_style);
};

}
