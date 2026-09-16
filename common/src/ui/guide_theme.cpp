#include "guide_theme.h"

#include <algorithm>
#include <cctype>

#include <rex/cvar.h>

REXCVAR_DEFINE_STRING(recomp_guide_theme, "metro", "Recomp",
                      "Which era of the Xbox 360 guide to wear: metro (the later dashboard, "
                      "what a Series X shows) or blades (the launch one).")
    .allowed({"metro", "blades"});

namespace recomp {

namespace {

// The guide a Series X puts over a 360 title: a light list between blue tabs,
// the selected row in the dashboard's green, and the title, gamer tile and
// clock sitting on the game above it.
GuideTheme MetroTheme() {
  GuideTheme theme;
  theme.name = "metro";
  theme.blades = false;
  theme.dim = IM_COL32(0, 0, 0, 120);
  theme.panel_top = IM_COL32(243, 243, 241, 255);
  theme.panel_bottom = IM_COL32(243, 243, 241, 255);
  theme.border = IM_COL32(206, 206, 202, 255);
  theme.accent = IM_COL32(16, 124, 16, 255);
  theme.header_band = IM_COL32(0, 0, 0, 0);
  theme.header_text = IM_COL32(245, 245, 245, 255);
  theme.text = IM_COL32(58, 58, 56, 255);
  theme.text_dim = IM_COL32(126, 126, 122, 255);
  theme.text_selected = IM_COL32(255, 255, 255, 255);
  theme.selection = IM_COL32(60, 179, 20, 255);
  theme.selection_bar = IM_COL32(60, 179, 20, 255);
  theme.separator = IM_COL32(214, 214, 210, 255);
  theme.button_b = IM_COL32(200, 46, 38, 255);
  theme.tab_fill = IM_COL32(150, 186, 216, 255);
  theme.tab_text = IM_COL32(250, 252, 255, 255);
  theme.tab_active_fill = IM_COL32(232, 232, 228, 255);
  theme.tab_active_text = IM_COL32(96, 96, 92, 255);
  theme.chrome_text = IM_COL32(245, 245, 245, 255);
  theme.chrome_shadow = IM_COL32(0, 0, 0, 140);
  theme.rounding = 0.0f;
  theme.entry_height = 56.0f;
  theme.header_height = 74.0f;
  theme.footer_height = 52.0f;
  theme.panel_width = 720.0f;
  return theme;
}

GuideTheme BladesTheme() {
  GuideTheme theme;
  theme.name = "blades";
  theme.blades = true;
  theme.dim = IM_COL32(0, 0, 0, 150);
  theme.panel_top = IM_COL32(46, 96, 30, 246);
  theme.panel_bottom = IM_COL32(12, 38, 10, 246);
  theme.border = IM_COL32(150, 206, 110, 120);
  theme.accent = IM_COL32(158, 208, 72, 255);
  theme.header_band = IM_COL32(190, 226, 140, 200);
  theme.header_text = IM_COL32(248, 252, 240, 255);
  theme.text = IM_COL32(238, 246, 226, 255);
  theme.text_dim = IM_COL32(186, 206, 166, 255);
  theme.text_selected = IM_COL32(28, 46, 16, 255);
  theme.selection = IM_COL32(206, 232, 158, 245);
  theme.selection_bar = IM_COL32(248, 252, 240, 255);
  theme.separator = IM_COL32(150, 206, 110, 70);
  theme.button_b = IM_COL32(186, 52, 42, 255);
  theme.tab_fill = IM_COL32(40, 86, 28, 255);
  theme.tab_text = IM_COL32(226, 240, 208, 255);
  theme.tab_active_fill = IM_COL32(120, 168, 74, 255);
  theme.tab_active_text = IM_COL32(248, 252, 240, 255);
  theme.chrome_text = IM_COL32(238, 246, 226, 255);
  theme.chrome_shadow = IM_COL32(0, 0, 0, 120);
  theme.rounding = 8.0f;
  return theme;
}

}  // namespace

GuideTheme GuideThemeByName(const std::string& name) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lower == "blades" ? BladesTheme() : MetroTheme();
}

const GuideTheme& CurrentGuideTheme() {
  // Re-read on each open so the setting takes effect without a restart, but
  // stay stable for the frames of one open guide.
  static GuideTheme theme;
  static std::string loaded_from;
  const std::string requested = REXCVAR_GET(recomp_guide_theme);
  if (requested != loaded_from) {
    theme = GuideThemeByName(requested);
    loaded_from = requested;
  }
  return theme;
}

}  // namespace recomp
