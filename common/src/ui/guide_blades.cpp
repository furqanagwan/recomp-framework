#include "recomp/ui/guide_dialog.h"

#include <algorithm>
#include <ctime>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/ui/immediate_drawer.h>

#include "guide_theme.h"

REXCVAR_DECLARE(std::string, recomp_gamertag);

namespace recomp {

namespace {

// The guide's own scene, hud.xex's GuideMain.xur (system software 17559),
// lays everything out on an 852 by 480 canvas. These are its numbers.
constexpr float kCanvasWidth = 852.0f;
constexpr float kCanvasHeight = 480.0f;

// Blade_Center, the light blade the current tab's list sits on.
constexpr float kCenterX = 231.0f;
constexpr float kCenterY = 122.0f;
constexpr float kCenterWidth = 386.0f;
constexpr float kBladeHeight = 235.0f;
// Blade_Focus, the strip down the centre blade's left edge that carries the
// current tab's name.
constexpr float kFocusX = 243.0f;
constexpr float kFocusY = 135.0f;
constexpr float kFocusWidth = 42.0f;
constexpr float kFocusHeight = 209.0f;
// The tab's own scene: a column of 28-high buttons.
constexpr float kListX = 285.0f;
constexpr float kListY = 135.0f;
constexpr float kListWidth = 323.0f;
constexpr float kRowHeight = 28.0f;
// The other tabs are grey blades fanned out 30 apart on either side, each
// mostly under its neighbour, so a strip of each shows.
constexpr float kBladeStep = 30.0f;
constexpr float kSideBladeWidth = 72.0f;
// Tab names: 11 point #EBEBEB on the grey blades, 12 point #586066 on the
// focus strip, starting 146 down.
constexpr float kTabTextY = 146.0f;
const ImU32 kTabText = IM_COL32(0xEB, 0xEB, 0xEB, 0xFF);
const ImU32 kFocusText = IM_COL32(0x58, 0x60, 0x66, 0xFF);
// The "Xbox Guide" label above the blades: Label_Head, 12 point white at 92%.
constexpr float kHeaderX = 243.0f;
constexpr float kHeaderY = 103.0f;
const ImU32 kHeaderText = IM_COL32(0xFF, 0xFF, 0xFF, 0xEB);

// huduiskin.xex's skin scene, for the pieces GuideMain names by visual.
//
// XuiButtonGuide, one row: a 1-high #D2D5D9 rule above and below; text 12
// point at (10, 2), #333A40 with a #EBEBEB shadow at 59%; focused, a #008A00
// highlight from 1 above the row to its bottom and the text turns #EBEBEB.
const ImU32 kRowRule = IM_COL32(0xD2, 0xD5, 0xD9, 0xFF);
const ImU32 kRowText = IM_COL32(0x33, 0x3A, 0x40, 0xFF);
const ImU32 kRowTextShadow = IM_COL32(0xEB, 0xEB, 0xEB, 0x96);
const ImU32 kRowFocus = IM_COL32(0x00, 0x8A, 0x00, 0xFF);
const ImU32 kRowFocusText = IM_COL32(0xEB, 0xEB, 0xEB, 0xFF);
const ImU32 kRowValueText = IM_COL32(0x65, 0x6D, 0x72, 0xFF);
constexpr float kRowTextX = 10.0f;
constexpr float kRowTextY = 2.0f;
constexpr float kRowTextHeight = 22.0f;
// btn_Count_achiev puts the count in a second label ending 283 across.
constexpr float kRowValueRight = 283.0f;
// HUD_Bladedark and HUD_Bladegrey are nine-grids over the blade textures,
// with these corners in texture pixels and canvas units alike.
constexpr float kDarkGridEdge = 25.0f;
constexpr float kGreyGridSide = 30.0f;
constexpr float kGreyGridEnd = 25.0f;

// XUI point sizes are points: 4/3 of a canvas unit.
constexpr float kPointToCanvas = 4.0f / 3.0f;

const char* const kTabNames[] = {"Games & Apps", "Home", "Settings"};

struct Canvas {
  ImVec2 origin;
  float scale;

  ImVec2 At(float x, float y) const { return ImVec2(origin.x + x * scale, origin.y + y * scale); }
  float Size(float units) const { return units * scale; }
  float Font(float points) const { return points * kPointToCanvas * scale; }
};

Canvas FitCanvas(const ImGuiIO& io) {
  const float scale = std::min(io.DisplaySize.x / kCanvasWidth, io.DisplaySize.y / kCanvasHeight);
  return {ImVec2((io.DisplaySize.x - kCanvasWidth * scale) * 0.5f,
                 (io.DisplaySize.y - kCanvasHeight * scale) * 0.5f),
          scale};
}

void DrawText(ImDrawList* draw_list, ImVec2 position, float size, ImU32 color,
              const std::string& text) {
  draw_list->AddText(ImGui::GetFont(), size, position, color, text.c_str());
}

float TextWidth(float size, const std::string& text) {
  return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
}

// A tab name runs down its blade, reading top to bottom as on the console.
void DrawTextDown(ImDrawList* draw_list, float center_x, float top, float size, ImU32 color,
                  const std::string& text) {
  const int first = draw_list->VtxBuffer.Size;
  const ImVec2 origin(center_x, top);
  draw_list->AddText(ImGui::GetFont(), size, ImVec2(origin.x, origin.y - size * 0.5f), color,
                     text.c_str());
  for (int i = first; i < draw_list->VtxBuffer.Size; ++i) {
    ImVec2& position = draw_list->VtxBuffer[i].pos;
    const float x = position.x - origin.x;
    const float y = position.y - origin.y;
    position = ImVec2(origin.x - y, origin.y + x);
  }
}

// A XuiNineGrid: the texture's corners stay their size and its middle
// stretches, so a 100 by 250 blade dresses a blade of any size.
void DrawNineSlice(ImDrawList* draw_list, rex::ui::ImmediateTexture* texture, ImVec2 min,
                   ImVec2 max, float side, float end, float scale) {
  const auto id = reinterpret_cast<ImTextureID>(texture);
  const float u = side / static_cast<float>(texture->width);
  const float v = end / static_cast<float>(texture->height);
  const float dx = std::min(side * scale, (max.x - min.x) * 0.5f);
  const float dy = std::min(end * scale, (max.y - min.y) * 0.5f);
  const float xs[] = {min.x, min.x + dx, max.x - dx, max.x};
  const float ys[] = {min.y, min.y + dy, max.y - dy, max.y};
  const float us[] = {0.0f, u, 1.0f - u, 1.0f};
  const float vs[] = {0.0f, v, 1.0f - v, 1.0f};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      draw_list->AddImage(id, ImVec2(xs[column], ys[row]), ImVec2(xs[column + 1], ys[row + 1]),
                          ImVec2(us[column], vs[row]), ImVec2(us[column + 1], vs[row + 1]));
    }
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

}  // namespace

void GuideDialog::DrawBladeScene(ImDrawList* draw_list, const ImGuiIO& io) {
  const Canvas canvas = FitCanvas(io);
  const int active = static_cast<int>(tab_);
  const int tab_count = static_cast<int>(std::size(kTabNames));
  rex::ui::ImmediateTexture* grey = Artwork("Blade_grey.png");
  rex::ui::ImmediateTexture* dark = Artwork("Blade_dark.png");
  const auto draw_blade = [&](rex::ui::ImmediateTexture* texture, ImU32 fallback, ImVec2 min,
                              ImVec2 max) {
    if (texture) {
      const bool is_dark = texture == dark;
      DrawNineSlice(draw_list, texture, min, max, is_dark ? kDarkGridEdge : kGreyGridSide,
                    is_dark ? kDarkGridEdge : kGreyGridEnd, canvas.scale);
      return;
    }
    // Without the console's artwork: its colours, and a shadow down the edge.
    draw_list->AddRectFilled(ImVec2(min.x - 3.0f, min.y + 2.0f), ImVec2(max.x + 3.0f, max.y + 4.0f),
                             IM_COL32(0, 0, 0, 60));
    draw_list->AddRectFilled(min, max, fallback);
  };

  // Header: "Xbox Guide" over the centre blade's left, the clock over its right,
  // the player's tile between them.
  const float header_size = canvas.Font(12.0f);
  const ImVec2 header = canvas.At(kHeaderX, kHeaderY);
  DrawText(draw_list, header, header_size, kHeaderText, "Xbox Guide");

  const std::string clock = Clock();
  if (!clock.empty()) {
    const ImVec2 at = canvas.At(kCenterX + kCenterWidth - 12.0f, kHeaderY);
    DrawText(draw_list, ImVec2(at.x - TextWidth(header_size, clock), at.y), header_size,
             kHeaderText, clock);
  }

  const ImVec2 tile_min = canvas.At(kCenterX + kCenterWidth * 0.5f - 18.0f, kCenterY - 42.0f);
  const ImVec2 tile_max(tile_min.x + canvas.Size(36.0f), tile_min.y + canvas.Size(36.0f));
  if (rex::ui::ImmediateTexture* tile = FirstArtwork({"gamerpic.png", "gamertile.png"})) {
    draw_list->AddImage(reinterpret_cast<ImTextureID>(tile), tile_min, tile_max);
  } else {
    const std::string gamertag = REXCVAR_GET(recomp_gamertag);
    const std::string initial(1, gamertag.empty() ? 'P' : gamertag.front());
    draw_list->AddRectFilled(tile_min, tile_max, theme_.accent);
    const float size = canvas.Font(16.0f);
    DrawText(draw_list,
             ImVec2((tile_min.x + tile_max.x - TextWidth(size, initial)) * 0.5f,
                    (tile_min.y + tile_max.y - size) * 0.5f),
             size, IM_COL32(255, 255, 255, 255), initial);
  }
  draw_list->AddRect(tile_min, tile_max, IM_COL32(255, 255, 255, 230), 0.0f, 0,
                     std::max(1.0f, canvas.Size(1.5f)));

  // Grey blades, outermost first so each nearer blade lies over the one past it.
  const float tab_size = canvas.Font(11.0f);
  for (int i = 0; i < active; ++i) {
    const float x = kCenterX - kBladeStep * static_cast<float>(active - i);
    draw_blade(grey, theme_.tab_fill, canvas.At(x, kCenterY),
               canvas.At(x + kSideBladeWidth, kCenterY + kBladeHeight));
  }
  for (int i = tab_count - 1; i > active; --i) {
    const float visible_end = kCenterX + kCenterWidth + kBladeStep * static_cast<float>(i - active);
    draw_blade(grey, theme_.tab_fill, canvas.At(visible_end - kSideBladeWidth, kCenterY),
               canvas.At(visible_end, kCenterY + kBladeHeight));
  }
  for (int i = 0; i < tab_count; ++i) {
    if (i == active) {
      continue;
    }
    // Each name runs down the strip of its blade that shows.
    const float strip_start = i < active
                                  ? kCenterX - kBladeStep * static_cast<float>(active - i)
                                  : kCenterX + kCenterWidth +
                                        kBladeStep * static_cast<float>(i - active - 1);
    DrawTextDown(draw_list, canvas.At(strip_start + kBladeStep * 0.5f, 0.0f).x,
                 canvas.At(0.0f, kTabTextY).y, tab_size, kTabText, kTabNames[i]);
  }

  // The centre blade, its focus strip and the current tab's name.
  draw_blade(dark, theme_.panel_top, canvas.At(kCenterX, kCenterY),
             canvas.At(kCenterX + kCenterWidth, kCenterY + kBladeHeight));
  draw_list->AddRectFilled(canvas.At(kFocusX, kFocusY),
                           canvas.At(kFocusX + kFocusWidth, kFocusY + kFocusHeight),
                           IM_COL32(0, 0, 0, 14));
  DrawTextDown(draw_list, canvas.At(kFocusX + kFocusWidth * 0.5f, 0.0f).x,
               canvas.At(0.0f, kTabTextY - 1.0f).y, canvas.Font(12.0f), kFocusText,
               kTabNames[active]);

  // The tab's list, one XuiButtonGuide per row.
  const float row_size = canvas.Font(12.0f);
  const float padding = canvas.Size(kRowTextX);
  const float rule = std::max(1.0f, canvas.Size(1.0f));
  const auto row_rect = [&](int index) {
    const float y = kListY + kRowHeight * static_cast<float>(index);
    return std::pair{canvas.At(kListX, y), canvas.At(kListX + kListWidth, y + kRowHeight)};
  };
  const auto draw_row = [&](int index, const std::string& label, const std::string& value,
                            bool selected) {
    const auto [min, max] = row_rect(index);
    draw_list->AddRectFilled(ImVec2(min.x, min.y - rule), ImVec2(max.x, min.y), kRowRule);
    draw_list->AddRectFilled(ImVec2(min.x, max.y - rule), max, kRowRule);
    if (selected) {
      draw_list->AddRectFilled(ImVec2(min.x, min.y - rule), max, kRowFocus);
    }
    // Centred in the label's 22-high box, 2 below the row's top.
    const float text_y = min.y + canvas.Size(kRowTextY) +
                         (canvas.Size(kRowTextHeight) - row_size) * 0.5f;
    const float shadow = std::max(1.0f, canvas.Size(0.75f));
    if (!selected) {
      DrawText(draw_list, ImVec2(min.x + padding + shadow, text_y + shadow), row_size,
               kRowTextShadow, label);
    }
    DrawText(draw_list, ImVec2(min.x + padding, text_y), row_size,
             selected ? kRowFocusText : kRowText, label);
    if (!value.empty()) {
      const float right = min.x + canvas.Size(kRowValueRight);
      DrawText(draw_list, ImVec2(right - TextWidth(row_size, value), text_y), row_size,
               selected ? kRowFocusText : kRowValueText, value);
    }
  };

  if (page_ == Page::kExitConfirmation) {
    const auto [min, max] = row_rect(0);
    DrawText(draw_list, ImVec2(min.x + padding, (min.y + max.y - row_size) * 0.5f), row_size,
             theme_.text, "Leave " + actions_.game_display_name + "?");
    const auto [note_min, note_max] = row_rect(1);
    DrawText(draw_list,
             ImVec2(note_min.x + padding, (note_min.y + note_max.y - canvas.Font(11.0f)) * 0.5f),
             canvas.Font(11.0f), theme_.text_dim, "Unsaved progress will be lost.");
    draw_row(3, "Leave Game", "", exit_choice_ == 0);
    draw_row(4, "Cancel", "", exit_choice_ == 1);
  } else {
    for (size_t i = 0; i < entries_.size(); ++i) {
      draw_row(static_cast<int>(i), entries_[i].label, entries_[i].value,
               static_cast<int>(i) == selected_);
    }
  }

  // The legend sits on the game under the blades.
  struct Hint {
    const char* letter;
    ImU32 fallback;
    const char* label;
  };
  std::vector<Hint> hints = {{"A", theme_.accent, "Select"},
                             {"B", theme_.button_b,
                              page_ == Page::kRoot ? "Close" : "Back"}};
  if (page_ == Page::kRoot && actions_.exit_game) {
    hints.push_back({"Y", IM_COL32(222, 178, 20, 255), "Leave Game"});
  }
  const float hint_size = canvas.Font(12.0f);
  const float glyph = canvas.Size(14.0f);
  const float gap = canvas.Size(14.0f);
  float total = 0.0f;
  for (const Hint& hint : hints) {
    total += glyph + canvas.Size(4.0f) + TextWidth(hint_size, hint.label) + gap;
  }
  const ImVec2 legend = canvas.At(kCenterX + kCenterWidth * 0.5f, kCenterY + kBladeHeight + 18.0f);
  float x = legend.x - (total - gap) * 0.5f;
  for (const Hint& hint : hints) {
    const ImVec2 center(x + glyph * 0.5f, legend.y);
    if (rex::ui::ImmediateTexture* texture = Artwork(std::string(hint.letter) + "-Button.png")) {
      draw_list->AddImage(reinterpret_cast<ImTextureID>(texture),
                          ImVec2(center.x - glyph * 0.5f, center.y - glyph * 0.5f),
                          ImVec2(center.x + glyph * 0.5f, center.y + glyph * 0.5f));
    } else {
      draw_list->AddCircleFilled(center, glyph * 0.5f, hint.fallback, 24);
      const float letter_size = hint_size * 0.8f;
      DrawText(draw_list,
               ImVec2(center.x - TextWidth(letter_size, hint.letter) * 0.5f,
                      center.y - letter_size * 0.5f),
               letter_size, IM_COL32(255, 255, 255, 255), hint.letter);
    }
    const ImVec2 text(x + glyph + canvas.Size(4.0f), legend.y - hint_size * 0.5f);
    DrawText(draw_list, ImVec2(text.x + 1.0f, text.y + 1.0f), hint_size, theme_.chrome_shadow,
             hint.label);
    DrawText(draw_list, text, hint_size, theme_.chrome_text, hint.label);
    x = text.x + TextWidth(hint_size, hint.label) + gap;
  }
}

}  // namespace recomp
