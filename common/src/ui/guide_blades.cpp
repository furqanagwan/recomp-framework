#include "recomp/ui/guide_dialog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>

#include <imgui.h>

#include <rex/cvar.h>

#include "recomp/ui/guide_fonts.h"
#include "guide_theme.h"

REXCVAR_DECLARE(std::string, recomp_gamertag);

namespace recomp {

namespace {

// The guide a Series X draws over a backward-compatible title, measured from a
// capture of one. Numbers are pixels on that 1512 by 855 capture, relative to
// its centre, and scale with the screen so the guide covers the same share of
// it at 720p, 1080p or 4K.
constexpr float kReferenceWidth = 1512.0f;
constexpr float kReferenceHeight = 855.0f;

// The blades: one band across the middle of the screen, 1170 wide.
constexpr float kBladesLeft = 168.0f - 756.0f;
constexpr float kBladesRight = 1338.0f - 756.0f;
constexpr float kBladesTop = 140.0f - 427.5f;
constexpr float kBladesBottom = 705.0f - 427.5f;
// A tab not in view is a slate blade: 82 wide on the left, 88 on the right.
constexpr float kLeftTabWidth = 82.0f;
constexpr float kRightTabWidth = 88.0f;
// The tab in view is a pale blade, 115 wide, against its list.
constexpr float kActiveTabWidth = 115.0f;
constexpr float kTabLabelTop = 172.0f - 427.5f;
constexpr float kTabLabelSize = 40.0f;
constexpr float kActiveTabLabelSize = 42.0f;

// Rows: 75 high from the top of the list, text 46 high set 28 in.
constexpr float kRowsTop = 143.0f - 427.5f;
constexpr float kRowHeight = 75.0f;
constexpr float kRowTextInset = 28.0f;
constexpr float kRowTextSize = 46.0f;
constexpr float kRowValueInset = 30.0f;
constexpr float kRowRule = 2.5f;

// Above the blades: the title on the left, the gamer picture over the middle,
// the ring of light and the clock on the right.
constexpr float kTitleLeft = 248.0f - 756.0f;
constexpr float kTitleTop = 64.0f - 427.5f;
constexpr float kTitleSize = 50.0f;
constexpr float kTileLeft = 706.0f - 756.0f;
constexpr float kTileTop = 42.0f - 427.5f;
constexpr float kTileSize = 90.0f;
constexpr float kRingX = 1232.0f - 756.0f;
constexpr float kRingY = 65.0f - 427.5f;
constexpr float kRingRadius = 20.0f;
constexpr float kClockRight = 1252.0f - 756.0f;
constexpr float kClockTop = 90.0f - 427.5f;
constexpr float kClockSize = 48.0f;

// The legend under the blades.
constexpr float kLegendLeft = 262.0f - 756.0f;
constexpr float kLegendY = 745.0f - 427.5f;
constexpr float kLegendGlyph = 36.0f;
constexpr float kLegendTextSize = 44.0f;
constexpr float kLegendGlyphGap = 16.0f;
constexpr float kLegendItemGap = 30.0f;

// How long the console takes: the guide opening, tabs sliding, a row's
// highlight arriving (XuiButtonGuide's Focus runs 12 frames).
constexpr double kOpenSeconds = 0.22;
constexpr double kTabSeconds = 0.20;
constexpr double kFocusSeconds = 0.20;

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
const Palette kMetro = {
    IM_COL32(0x66, 0x7C, 0x8B, 0xFF), IM_COL32(0xEE, 0xF2, 0xF4, 0xFF),
    IM_COL32(0xC2, 0xC9, 0xCD, 0xFF), IM_COL32(0x3E, 0x4A, 0x52, 0xFF),
    IM_COL32(0xEC, 0xF0, 0xF2, 0xFF), IM_COL32(0xDA, 0xE0, 0xE3, 0xFF),
    IM_COL32(0xD2, 0xD5, 0xD9, 0xFF), IM_COL32(0x2E, 0x34, 0x38, 0xFF),
    IM_COL32(0x2E, 0x34, 0x38, 0xFF), IM_COL32(0x2C, 0xB0, 0x2C, 0xFF),
    IM_COL32(0x00, 0x8A, 0x00, 0xFF), IM_COL32(0xFF, 0xFF, 0xFF, 0xFF),
    IM_COL32(0xF4, 0xF4, 0xF4, 0xFF), IM_COL32(0x00, 0x00, 0x00, 0x90),
};

Palette BladesPalette(const GuideTheme& theme) {
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

ImU32 Fade(ImU32 color, float alpha) {
  const float a = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.0f, 1.0f);
  return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
}

float EaseOut(double t) {
  const float x = std::clamp(static_cast<float>(t), 0.0f, 1.0f);
  return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x);
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

struct Screen {
  ImVec2 center;
  float scale;

  ImVec2 At(float x, float y) const { return ImVec2(center.x + x * scale, center.y + y * scale); }
  float Size(float units) const { return units * scale; }
};

Screen FitScreen(const ImGuiIO& io) {
  return {ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
          std::min(io.DisplaySize.x / kReferenceWidth, io.DisplaySize.y / kReferenceHeight)};
}

ImFont* DisplayFont() {
  if (ImFont* font = GuideDisplayFont()) {
    return font;
  }
  return ImGui::GetFont();
}

void DrawText(ImDrawList* draw_list, ImVec2 position, float size, ImU32 color,
              const std::string& text) {
  draw_list->AddText(DisplayFont(), size, position, color, text.c_str());
}

float TextWidth(float size, const std::string& text) {
  return DisplayFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
}

// A tab name runs down its blade, reading top to bottom.
void DrawTextDown(ImDrawList* draw_list, float center_x, float top, float size, ImU32 color,
                  const std::string& text) {
  const int first = draw_list->VtxBuffer.Size;
  const ImVec2 origin(center_x, top);
  draw_list->AddText(DisplayFont(), size, ImVec2(origin.x, origin.y - size * 0.5f), color,
                     text.c_str());
  for (int i = first; i < draw_list->VtxBuffer.Size; ++i) {
    ImVec2& position = draw_list->VtxBuffer[i].pos;
    const float x = position.x - origin.x;
    const float y = position.y - origin.y;
    position = ImVec2(origin.x - y, origin.y + x);
  }
}

std::string Clock() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
#if defined(_WIN32)
  if (localtime_s(&local, &now) != 0) {
    return {};
  }
#else
  if (!localtime_r(&now, &local)) {
    return {};
  }
#endif
  char buffer[16] = {};
  return std::strftime(buffer, sizeof(buffer), "%H:%M", &local) ? buffer : "";
}

// Where each blade's left and right edges sit with a given tab in view: the
// tabs before it stacked on the left, the pale blade and list, the rest on the
// right. Index 3 is the list.
std::array<std::pair<float, float>, 4> Layout(int active) {
  std::array<std::pair<float, float>, 4> edges{};
  float x = kBladesLeft;
  for (int i = 0; i < active; ++i) {
    edges[i] = {x, x + kLeftTabWidth};
    x += kLeftTabWidth;
  }
  edges[active] = {x, x + kActiveTabWidth};
  x += kActiveTabWidth;
  float right = kBladesRight;
  for (int i = 2; i > active; --i) {
    edges[i] = {right - kRightTabWidth, right};
    right -= kRightTabWidth;
  }
  edges[3] = {x, right};
  return edges;
}

void DrawGlyph(ImDrawList* draw_list, ImVec2 center, float diameter, ImU32 color,
               const char* letter, float alpha) {
  const float radius = diameter * 0.5f;
  draw_list->AddCircleFilled(center, radius, Fade(color, alpha), 32);
  // A lighter cap, as the console's glossy buttons have.
  draw_list->AddCircleFilled(ImVec2(center.x, center.y - radius * 0.28f), radius * 0.62f,
                             Fade(IM_COL32(255, 255, 255, 46), alpha), 24);
  const float size = diameter * 0.78f;
  DrawText(draw_list,
           ImVec2(center.x - TextWidth(size, letter) * 0.5f, center.y - size * 0.52f), size,
           Fade(IM_COL32(255, 255, 255, 255), alpha), letter);
}

}  // namespace

void GuideDialog::DrawBladeScene(ImDrawList* draw_list, const ImGuiIO& io) {
  const Screen screen = FitScreen(io);
  const Palette palette = theme_.blades ? BladesPalette(theme_) : kMetro;
  const double now = ImGui::GetTime();
  const int active = static_cast<int>(tab_);

  // Animation bookkeeping: note when the guide opened, and when the tab or the
  // selection last changed and from what.
  if (opened_at_ < 0.0) {
    opened_at_ = now;
    drawn_tab_ = active;
    drawn_selected_ = selected_;
  }
  if (drawn_tab_ != active) {
    tab_from_ = drawn_tab_;
    tab_changed_at_ = now;
    drawn_tab_ = active;
    drawn_selected_ = selected_;
    selected_from_ = -1;
  }
  const int selection = page_ == Page::kExitConfirmation ? 3 + exit_choice_ : selected_;
  if (drawn_selected_ != selection) {
    selected_from_ = drawn_selected_;
    selected_changed_at_ = now;
    drawn_selected_ = selection;
  }

  const float open = EaseOut((now - opened_at_) / kOpenSeconds);
  const float tab_t = tab_from_ < 0 ? 1.0f : EaseOut((now - tab_changed_at_) / kTabSeconds);
  const float focus_t = EaseOut((now - selected_changed_at_) / kFocusSeconds);

  // The blades open out from the middle as the guide appears.
  const auto spread = [&](float x) { return x * Lerp(0.82f, 1.0f, open); };
  const auto target = Layout(active);
  const auto source = tab_from_ < 0 ? target : Layout(tab_from_);
  const auto edge = [&](int index) {
    return std::pair{spread(Lerp(source[index].first, target[index].first, tab_t)),
                     spread(Lerp(source[index].second, target[index].second, tab_t))};
  };
  const float top = kBladesTop;
  const float bottom = kBladesBottom;

  // Title, gamer picture, ring of light and clock.
  const ImU32 chrome = Fade(palette.chrome, open);
  const ImU32 shadow = Fade(palette.shadow, open);
  const auto shadowed = [&](ImVec2 at, float size, const std::string& text) {
    const float offset = std::max(1.0f, screen.Size(2.0f));
    DrawText(draw_list, ImVec2(at.x + offset, at.y + offset), size, shadow, text);
    DrawText(draw_list, at, size, chrome, text);
  };
  shadowed(screen.At(kTitleLeft, kTitleTop), screen.Size(kTitleSize), "Xbox Guide");

  const ImVec2 tile_min = screen.At(kTileLeft, kTileTop);
  const ImVec2 tile_max = screen.At(kTileLeft + kTileSize, kTileTop + kTileSize);
  if (rex::ui::ImmediateTexture* tile = FirstArtwork({"gamerpic.png", "gamertile.png"})) {
    draw_list->AddImage(reinterpret_cast<ImTextureID>(tile), tile_min, tile_max, ImVec2(0, 0),
                        ImVec2(1, 1), Fade(IM_COL32(255, 255, 255, 255), open));
  } else {
    const std::string gamertag = REXCVAR_GET(recomp_gamertag);
    const std::string initial(1, gamertag.empty() ? 'P' : gamertag.front());
    draw_list->AddRectFilledMultiColor(tile_min, tile_max, Fade(IM_COL32(0x3E, 0x78, 0xC4, 255), open),
                                       Fade(IM_COL32(0x3E, 0x78, 0xC4, 255), open),
                                       Fade(IM_COL32(0x1C, 0x3E, 0x78, 255), open),
                                       Fade(IM_COL32(0x1C, 0x3E, 0x78, 255), open));
    const float size = screen.Size(58.0f);
    DrawText(draw_list,
             ImVec2((tile_min.x + tile_max.x - TextWidth(size, initial)) * 0.5f,
                    (tile_min.y + tile_max.y) * 0.5f - size * 0.55f),
             size, chrome, initial);
  }

  // The ring of light, player one's quarter lit.
  const ImVec2 ring = screen.At(kRingX, kRingY);
  const float ring_radius = screen.Size(kRingRadius);
  const float ring_width = std::max(1.5f, screen.Size(4.0f));
  draw_list->PathArcTo(ring, ring_radius, 0.0f, IM_PI * 2.0f, 40);
  draw_list->PathStroke(Fade(IM_COL32(0x9A, 0xA0, 0xA4, 255), open), 0, ring_width);
  draw_list->PathArcTo(ring, ring_radius, IM_PI * 1.05f, IM_PI * 1.45f, 12);
  draw_list->PathStroke(Fade(IM_COL32(0x9C, 0xE0, 0x2C, 255), open), 0, ring_width);

  const std::string clock = Clock();
  if (!clock.empty()) {
    const float size = screen.Size(kClockSize);
    shadowed(ImVec2(screen.At(kClockRight, kClockTop).x - TextWidth(size, clock),
                    screen.At(kClockRight, kClockTop).y),
             size, clock);
  }

  // Blades. The pale one and the list go last so their edges lie over the
  // slate tabs'.
  const std::string tab_names[] = {"Games", REXCVAR_GET(recomp_gamertag), "Settings"};
  const auto blade_rect = [&](std::pair<float, float> x) {
    return std::pair{screen.At(x.first, top), screen.At(x.second, bottom)};
  };
  for (int i = 0; i < 3; ++i) {
    if (i == active) {
      continue;
    }
    const auto [min, max] = blade_rect(edge(i));
    draw_list->AddRectFilled(min, max, Fade(palette.slate, open));
    draw_list->AddRectFilled(ImVec2(max.x - std::max(1.0f, screen.Size(2.0f)), min.y), max,
                             Fade(IM_COL32(0, 0, 0, 40), open));
    DrawTextDown(draw_list, (min.x + max.x) * 0.5f, screen.At(0.0f, kTabLabelTop).y,
                 screen.Size(kTabLabelSize), Fade(palette.slate_text, open), tab_names[i]);
  }

  const auto [list_min, list_max] = blade_rect(edge(3));
  draw_list->AddRectFilledMultiColor(list_min, list_max, Fade(palette.list_top, open),
                                     Fade(palette.list_top, open), Fade(palette.list_bottom, open),
                                     Fade(palette.list_bottom, open));
  {
    const auto [min, max] = blade_rect(edge(active));
    draw_list->AddRectFilled(min, max, Fade(palette.pale, open));
    DrawTextDown(draw_list, (min.x + max.x) * 0.5f, screen.At(0.0f, kTabLabelTop).y,
                 screen.Size(kActiveTabLabelSize), Fade(palette.pale_text, open),
                 tab_names[active]);
  }

  // The list. Moving to another tab slides the new list in from its side.
  const float list_alpha = open * tab_t;
  const float slide = tab_from_ < 0 ? 0.0f
                                    : (1.0f - tab_t) * screen.Size(60.0f) *
                                          (active > tab_from_ ? 1.0f : -1.0f);
  draw_list->PushClipRect(list_min, list_max, true);
  const float row_text = screen.Size(kRowTextSize);
  const auto row_rect = [&](int index) {
    const float y = kRowsTop + kRowHeight * static_cast<float>(index);
    return std::pair{ImVec2(list_min.x, screen.At(0.0f, y).y),
                     ImVec2(list_max.x, screen.At(0.0f, y + kRowHeight).y)};
  };
  const auto draw_row = [&](int index, const std::string& label, const std::string& value) {
    const auto [min, max] = row_rect(index);
    float highlight = 0.0f;
    if (index == drawn_selected_) {
      highlight = focus_t;
    } else if (index == selected_from_) {
      highlight = 1.0f - focus_t;
    }
    draw_list->AddRectFilled(ImVec2(min.x, max.y - std::max(1.0f, screen.Size(kRowRule))),
                             max, Fade(palette.rule, list_alpha));
    if (highlight > 0.0f) {
      draw_list->AddRectFilledMultiColor(min, max, Fade(palette.focus_top, highlight * open),
                                         Fade(palette.focus_top, highlight * open),
                                         Fade(palette.focus_bottom, highlight * open),
                                         Fade(palette.focus_bottom, highlight * open));
    }
    const float text_y = (min.y + max.y - row_text) * 0.5f - screen.Size(3.0f);
    const ImU32 text = highlight >= 0.5f ? palette.focus_text : palette.text;
    DrawText(draw_list, ImVec2(min.x + screen.Size(kRowTextInset) + slide, text_y), row_text,
             Fade(text, list_alpha), label);
    if (!value.empty()) {
      DrawText(draw_list,
               ImVec2(max.x - screen.Size(kRowValueInset) - TextWidth(row_text, value) + slide,
                      text_y),
               row_text, Fade(highlight >= 0.5f ? palette.focus_text : palette.value, list_alpha),
               value);
    }
  };

  if (page_ == Page::kExitConfirmation) {
    const auto [min, max] = row_rect(0);
    DrawText(draw_list,
             ImVec2(min.x + screen.Size(kRowTextInset), (min.y + max.y - row_text) * 0.5f),
             row_text, Fade(palette.text, open), "Leave " + actions_.game_display_name + "?");
    const auto [note_min, note_max] = row_rect(1);
    const float note = screen.Size(36.0f);
    DrawText(draw_list,
             ImVec2(note_min.x + screen.Size(kRowTextInset), (note_min.y + note_max.y - note) * 0.5f),
             note, Fade(palette.value, open), "Unsaved progress will be lost.");
    draw_row(3, "Leave Game", "");
    draw_row(4, "Cancel", "");
  } else {
    for (size_t i = 0; i < entries_.size(); ++i) {
      draw_row(static_cast<int>(i), entries_[i].label, entries_[i].value);
    }
  }
  draw_list->PopClipRect();

  // The legend.
  struct Hint {
    const char* letter;
    ImU32 color;
    const char* label;
  };
  std::vector<Hint> hints = {{"A", IM_COL32(0x4C, 0xB0, 0x2A, 255), "Select"},
                             {"B", IM_COL32(0xD8, 0x2C, 0x2C, 255), "Back"}};
  if (page_ == Page::kRoot && actions_.exit_game) {
    hints.push_back({"Y", IM_COL32(0xF0, 0xB0, 0x1C, 255), "Leave Game"});
  }
  const float legend_text = screen.Size(kLegendTextSize);
  const float glyph = screen.Size(kLegendGlyph);
  float x = screen.At(kLegendLeft, 0.0f).x;
  const float y = screen.At(0.0f, kLegendY).y;
  for (const Hint& hint : hints) {
    DrawGlyph(draw_list, ImVec2(x + glyph * 0.5f, y), glyph, hint.color, hint.letter, open);
    const ImVec2 text(x + glyph + screen.Size(kLegendGlyphGap), y - legend_text * 0.55f);
    shadowed(text, legend_text, hint.label);
    x = text.x + TextWidth(legend_text, hint.label) + screen.Size(kLegendItemGap);
  }
}

}  // namespace recomp
