#pragma once

#include <string>

#include <imgui.h>

namespace recomp {

// The two faces the 360 guide had over its life. A Series X shows a
// backward-compatible title the later one, so that is what this opens with.
//
//   metro  - the 2011 dashboard: flat near-black panels, square edges, a solid
//            green band behind the selected row.
//   blades - the launch dashboard: a green blade with rounded corners, lit from
//            the top, the selection a lighter pane inside it.
struct GuideTheme {
  std::string name = "metro";
  bool blades = false;

  ImU32 dim = 0;             // over the game behind the panel
  ImU32 panel_top = 0;       // panel fill, top of the gradient
  ImU32 panel_bottom = 0;    // panel fill, bottom (same as top when flat)
  ImU32 border = 0;
  ImU32 accent = 0;          // the console's green
  ImU32 header_band = 0;
  ImU32 header_text = 0;
  ImU32 text = 0;
  ImU32 text_dim = 0;
  ImU32 text_selected = 0;
  ImU32 selection = 0;       // fill behind the selected row
  ImU32 selection_bar = 0;   // the sliver at its leading edge
  ImU32 separator = 0;
  ImU32 button_b = 0;        // the B glyph, when it has to be drawn

  float rounding = 0.0f;
  float entry_height = 54.0f;
  float header_height = 96.0f;
  float footer_height = 56.0f;
  float panel_width = 560.0f;
  float padding = 22.0f;
};

// The theme named by recomp_guide_theme, falling back to metro.
const GuideTheme& CurrentGuideTheme();

// Named directly, for tests and for a settings screen that previews one.
GuideTheme GuideThemeByName(const std::string& name);

}  // namespace recomp
