#include "guide_theme.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include <rex/cvar.h>
#include <rex/logging.h>
#include "recomp/ui/guide_fonts.h"

REXCVAR_DEFINE_STRING(recomp_guide_font, "", "Recomp",
                      "Optional TTF/OTF font for the guide. Empty uses Windows Segoe UI when available.")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_STRING(recomp_guide_theme, "metro", "Recomp",
                      "Which era of the Xbox 360 guide to wear: metro (the later dashboard, "
                      "what a Series X shows) or blades (the launch one).")
    .allowed({"metro", "blades"});

namespace recomp {

namespace {

ImFont* guide_font = nullptr;

// The guide a Series X puts over a 360 title: a light list between blue tabs,
// the selected row in the dashboard's green, and the title, gamer tile and
// clock sitting on the game above it.
GuideTheme MetroTheme() {
  GuideTheme theme;
  theme.name = "metro";
  theme.blades = false;
  theme.dim = IM_COL32(0, 0, 0, 120);
  theme.panel_top = IM_COL32(220, 223, 223, 255);
  theme.panel_bottom = IM_COL32(192, 198, 199, 255);
  theme.border = IM_COL32(206, 206, 202, 255);
  theme.accent = IM_COL32(16, 124, 16, 255);
  theme.header_band = IM_COL32(0, 0, 0, 0);
  theme.header_text = IM_COL32(245, 245, 245, 255);
  theme.text = IM_COL32(36, 40, 41, 255);
  theme.text_dim = IM_COL32(126, 126, 122, 255);
  theme.text_selected = IM_COL32(255, 255, 255, 255);
  theme.selection = IM_COL32(16, 112, 0, 255);
  theme.selection_bar = theme.selection;
  theme.separator = IM_COL32(214, 214, 210, 255);
  theme.button_b = IM_COL32(200, 46, 38, 255);
  theme.tab_fill = IM_COL32(76, 87, 94, 255);
  theme.tab_text = IM_COL32(250, 252, 255, 255);
  theme.tab_active_fill = IM_COL32(185, 189, 193, 255);
  theme.tab_active_text = IM_COL32(62, 65, 68, 255);
  theme.chrome_text = IM_COL32(245, 245, 245, 255);
  theme.chrome_shadow = IM_COL32(0, 0, 0, 140);
  theme.rounding = 0.0f;
  theme.entry_height = 48.0f;
  theme.tab_width = 44.0f;
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

void ConfigureGuideFonts(ImFontAtlas* atlas) {
  guide_font = nullptr;
  std::filesystem::path path(REXCVAR_GET(recomp_guide_font));
#if defined(_WIN32)
  if (path.empty()) path = "C:/Windows/Fonts/segoeui.ttf";
#endif
  std::error_code ec;
  if (!path.empty() && std::filesystem::is_regular_file(path, ec)) {
    ImFontConfig config;
    config.OversampleH = 3;
    config.OversampleV = 2;
    config.PixelSnapH = false;
    guide_font = atlas->AddFontFromFileTTF(path.string().c_str(), 16.0f, &config);
    if (guide_font) REXLOG_INFO("Guide: using font {}", path.string());
  }
  if (!guide_font) REXLOG_WARN("Guide: no usable guide font; using the runtime font");
}

ImFont* GuideFont() { return guide_font; }

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
