#pragma once

// The drawing the guide's screens share: its palette, how it scales with the
// screen, its type and its button glyphs.

#include <algorithm>
#include <cfloat>
#include <string>

#include <imgui.h>

#include "recomp/ui/guide_fonts.h"
#include "guide_theme.h"

namespace recomp::guide_scene {

// The guide a Series X draws over a backward-compatible title, measured from a
// capture of one. Numbers are pixels on that 1512 by 855 capture, relative to
// its centre, and scale with the screen so the guide covers the same share of
// it at 720p, 1080p or 4K.
inline constexpr float kReferenceWidth = 1512.0f;
inline constexpr float kReferenceHeight = 855.0f;

struct Palette {
  ImU32 slate;
  ImU32 slate_text;
  ImU32 pale;
  ImU32 pale_text;
  ImU32 list_top;
  ImU32 list_bottom;
  ImU32 rule;
  ImU32 text;
  ImU32 value;
  ImU32 focus_top;
  ImU32 focus_bottom;
  ImU32 focus_text;
  ImU32 chrome;
  ImU32 shadow;
};

// Colours from the capture, checked against the 360 skin's own values where
// the two agree (the row rule #D2D5D9, the focus green #008A00).
// The guide skin's scene transitions (Guide.AccountRecovery.xex's skin.xur), at
// XUI's 60 frames a second, both linear: TransOpen fades a scene in over 10
// frames, TransFrom fades it out over 15.
inline constexpr double kTransOpenSeconds = 10.0 / 60.0;
inline constexpr double kTransFromSeconds = 15.0 / 60.0;

inline const Palette kMetro = {
    IM_COL32(0x66, 0x7C, 0x8B, 0xFF), IM_COL32(0xEE, 0xF2, 0xF4, 0xFF),
    IM_COL32(0xC2, 0xC9, 0xCD, 0xFF), IM_COL32(0x3E, 0x4A, 0x52, 0xFF),
    IM_COL32(0xEC, 0xF0, 0xF2, 0xFF), IM_COL32(0xDA, 0xE0, 0xE3, 0xFF),
    IM_COL32(0xD2, 0xD5, 0xD9, 0xFF), IM_COL32(0x2E, 0x34, 0x38, 0xFF),
    IM_COL32(0x2E, 0x34, 0x38, 0xFF), IM_COL32(0x2C, 0xB0, 0x2C, 0xFF),
    IM_COL32(0x00, 0x8A, 0x00, 0xFF), IM_COL32(0xFF, 0xFF, 0xFF, 0xFF),
    IM_COL32(0xF4, 0xF4, 0xF4, 0xFF), IM_COL32(0x00, 0x00, 0x00, 0x90),
};

inline Palette BladesPalette(const GuideTheme& theme) {
  Palette palette = kMetro;
  palette.slate = theme.tab_fill;
  palette.slate_text = theme.tab_text;
  palette.pale = theme.tab_active_fill;
  palette.pale_text = theme.tab_active_text;
  palette.list_top = theme.panel_top;
  palette.list_bottom = theme.panel_bottom;
  palette.rule = theme.separator;
  palette.text = theme.text;
  palette.value = theme.text_dim;
  palette.focus_top = theme.selection;
  palette.focus_bottom = theme.selection;
  palette.focus_text = theme.text_selected;
  return palette;
}

inline ImU32 Fade(ImU32 color, float alpha) {
  const float a = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.0f, 1.0f);
  return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
}

inline float EaseOut(double t) {
  const float x = std::clamp(static_cast<float>(t), 0.0f, 1.0f);
  return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x);
}

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

struct Screen {
  ImVec2 center;
  float scale;

  ImVec2 At(float x, float y) const { return ImVec2(center.x + x * scale, center.y + y * scale); }
  float Size(float units) const { return units * scale; }
};

inline Screen FitScreen(const ImGuiIO& io) {
  return {ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
          std::min(io.DisplaySize.x / kReferenceWidth, io.DisplaySize.y / kReferenceHeight)};
}

inline ImFont* DisplayFont() {
  if (ImFont* font = GuideDisplayFont()) {
    return font;
  }
  return ImGui::GetFont();
}

inline void DrawText(ImDrawList* draw_list, ImVec2 position, float size, ImU32 color,
              const std::string& text) {
  draw_list->AddText(DisplayFont(), size, position, color, text.c_str());
}

inline float TextWidth(float size, const std::string& text) {
  return DisplayFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
}

// The face buttons' colours, sampled from the middle of shrdres.xzp's
// A-, B-, X- and Y-Button_32.png.
inline ImU32 ButtonColor(const char* letter) {
  switch (letter[0]) {
    case 'A':
      return IM_COL32(0x6C, 0xB7, 0x33, 0xFF);
    case 'B':
      return IM_COL32(0xB1, 0x2B, 0x36, 0xFF);
    case 'X':
      return IM_COL32(0x30, 0x64, 0xA1, 0xFF);
    case 'Y':
      return IM_COL32(0xE0, 0xB4, 0x1C, 0xFF);
    default:
      return IM_COL32(0x4F, 0x4F, 0x4F, 0xFF);
  }
}

// The file a face button's picture comes from, as the guide skin's legend_A
// names it.
inline std::string ButtonPicture(const char* letter) {
  return std::string(letter) + "-Button_32.png";
}

// A face button as the guide skin's legend visuals draw one: the console's flat
// disc (its picture when supplied, otherwise the disc's colour) with the letter
// over it in #F5F5F5 and a faint shadow.
inline void DrawGlyph(ImDrawList* draw_list, ImVec2 center, float diameter, const char* letter,
                      float alpha, ImTextureID picture = ImTextureID{}) {
  const float radius = diameter * 0.5f;
  if (picture) {
    // The disc covers about 22 of the picture's 32 pixels; scale it so the disc
    // is the size asked for.
    const float half = radius * 32.0f / 22.0f;
    draw_list->AddImage(picture, ImVec2(center.x - half, center.y - half),
                        ImVec2(center.x + half, center.y + half), ImVec2(0, 0), ImVec2(1, 1),
                        Fade(IM_COL32(255, 255, 255, 255), alpha));
  } else {
    draw_list->AddCircleFilled(center, radius, Fade(ButtonColor(letter), alpha), 32);
  }
  // X-Button_full.png's letter is 11 of its disc's 21 pixels tall; Segoe's
  // capitals are about 0.7 of the type size.
  const float size = diameter * 0.75f;
  const ImVec2 position(center.x - TextWidth(size, letter) * 0.5f, center.y - size * 0.56f);
  const float shadow = std::max(1.0f, diameter / 32.0f);
  DrawText(draw_list, ImVec2(position.x + shadow, position.y + shadow), size,
           Fade(IM_COL32(0, 0, 0, 0x2E), alpha), letter);
  DrawText(draw_list, position, size, Fade(IM_COL32(0xF5, 0xF5, 0xF5, 0xFF), alpha), letter);
}

}  // namespace recomp::guide_scene
