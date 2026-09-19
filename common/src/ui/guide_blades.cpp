#include "recomp/ui/guide_dialog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/ui/overlay/achievement_icon_cache.h>

#include "recomp/ui/guide_fonts.h"
#include "guide_scene.h"
#include "guide_theme.h"
#include "recomp/input/controller_menu_watcher.h"
#include "recomp/ui/guide_resources.h"

REXCVAR_DECLARE(std::string, recomp_gamertag);

namespace recomp {

namespace {

using namespace guide_scene;

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
// The bottom of the list, which pages with more than rows use for detail.
constexpr float kListBottom = 705.0f - 427.5f;
// Achievements: a summary band, then rows tall enough for an icon, a title
// and a line of description, in the same type as the menu.
constexpr float kSummaryHeight = 70.0f;
constexpr float kAchievementRowHeight = 82.0f;
constexpr float kAchievementIcon = 62.0f;
constexpr float kAchievementTitleSize = 38.0f;
constexpr float kAchievementDetailSize = 28.0f;
// Settings: a title row, then one row a setting, with the selected setting's
// description under them.
// The content list: one row per piece, with its state on the right, and a
// footer saying where in the list this is - as the marketplace page did.
constexpr float kDlcRowHeight = 62.0f;
constexpr float kDlcTextSize = 40.0f;
constexpr float kDlcFooterHeight = 52.0f;
constexpr float kDlcFooterSize = 30.0f;

constexpr float kSettingRowHeight = 62.0f;
constexpr float kSettingTextSize = 40.0f;
constexpr float kDetailTextSize = 28.0f;

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

// The charge indicator sits immediately left of the clock, as the console puts
// it: a pad outline with the quadrant its player owns lit, then a battery with
// one bar per reported level.
constexpr float kBatteryGap = 22.0f;
constexpr float kPadWidth = 44.0f;
constexpr float kPadHeight = 30.0f;
constexpr float kBatteryWidth = 38.0f;
constexpr float kBatteryHeight = 20.0f;
constexpr float kBatteryCapWidth = 5.0f;
constexpr float kBatteryCapHeight = 9.0f;
constexpr float kBatteryBars = 3.0f;

// The legend under the blades.
constexpr float kLegendLeft = 262.0f - 756.0f;
constexpr float kLegendY = 745.0f - 427.5f;
constexpr float kLegendGlyph = 36.0f;
constexpr float kLegendTextSize = 44.0f;
constexpr float kLegendGlyphGap = 16.0f;
constexpr float kLegendItemGap = 30.0f;

// How long the console takes: the guide opening, tabs sliding, a row's
// highlight arriving (XuiButtonGuide's Focus runs 12 frames).
constexpr float kPi = 3.14159265f;
constexpr double kOpenSeconds = 0.22;
constexpr double kTabSeconds = 0.20;
constexpr double kFocusSeconds = 0.20;




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
  // Keep the chosen achievement on screen.
  const int achievement_rows = static_cast<int>((kListBottom - kRowsTop - kSummaryHeight) /
                                                kAchievementRowHeight);
  achievement_scroll_ = std::clamp(achievement_scroll_,
                                   std::max(0, achievement_selected_ - achievement_rows + 1),
                                   std::max(0, achievement_selected_));
  // The same for the content list, which shares the list area.
  const int dlc_rows =
      static_cast<int>((kListBottom - kRowsTop - kDlcFooterHeight) / kDlcRowHeight);
  dlc_scroll_ = std::clamp(dlc_scroll_, std::max(0, dlc_selected_ - dlc_rows + 1),
                           std::max(0, dlc_selected_));
  int selection = selected_;
  switch (page_) {
    case Page::kExitConfirmation:
      selection = 3 + exit_choice_;
      break;
    case Page::kSettings:
      selection = 1 + setting_selected_;
      break;
    case Page::kAchievements:
      selection = achievement_selected_ - achievement_scroll_;
      break;
    case Page::kDlc:
      selection = dlc_selected_ - dlc_scroll_;
      break;
    case Page::kRoot:
      break;
  }
  const int page = static_cast<int>(page_);
  if (drawn_page_ != page) {
    drawn_page_ = page;
    drawn_selected_ = -1;
  }
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
  draw_list->PathArcTo(ring, ring_radius, 0.0f, kPi * 2.0f, 40);
  draw_list->PathStroke(Fade(IM_COL32(0x9A, 0xA0, 0xA4, 255), open), 0, ring_width);
  draw_list->PathArcTo(ring, ring_radius, kPi * 1.05f, kPi * 1.45f, 12);
  draw_list->PathStroke(Fade(IM_COL32(0x9C, 0xE0, 0x2C, 255), open), 0, ring_width);

  const std::string clock = Clock();
  float chrome_right = kClockRight;
  if (!clock.empty()) {
    const float size = screen.Size(kClockSize);
    shadowed(ImVec2(screen.At(kClockRight, kClockTop).x - TextWidth(size, clock),
                    screen.At(kClockRight, kClockTop).y),
             size, clock);
    chrome_right -= TextWidth(kClockSize, clock) + kBatteryGap;
  }
  DrawControllerCharge(draw_list, screen, chrome_right, kClockTop, open);

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
  const float text_left = list_min.x + screen.Size(kRowTextInset) + slide;
  const float text_right = list_max.x - screen.Size(kRowValueInset) + slide;

  // How strongly an on-screen row is highlighted, as it fades on or off.
  const auto highlight_of = [&](int index) {
    if (index == drawn_selected_) {
      return focus_t;
    }
    if (index == selected_from_) {
      return 1.0f - focus_t;
    }
    return 0.0f;
  };
  // One row band from top to top + height (reference units), with its rule
  // and highlight; returns its screen rectangle and whether it reads as chosen.
  struct Band {
    ImVec2 min;
    ImVec2 max;
    bool focused;
  };
  const auto draw_band = [&](float band_top, float band_height, int index) {
    const ImVec2 min(list_min.x, screen.At(0.0f, band_top).y);
    const ImVec2 max(list_max.x, screen.At(0.0f, band_top + band_height).y);
    draw_list->AddRectFilled(ImVec2(min.x, max.y - std::max(1.0f, screen.Size(kRowRule))), max,
                             Fade(palette.rule, list_alpha));
    const float highlight = index < 0 ? 0.0f : highlight_of(index);
    if (highlight > 0.0f) {
      draw_list->AddRectFilledMultiColor(min, max, Fade(palette.focus_top, highlight * open),
                                         Fade(palette.focus_top, highlight * open),
                                         Fade(palette.focus_bottom, highlight * open),
                                         Fade(palette.focus_bottom, highlight * open));
    }
    return Band{min, max, highlight >= 0.5f};
  };
  const auto text_color = [&](bool focused) {
    return Fade(focused ? palette.focus_text : palette.text, list_alpha);
  };
  const auto detail_color = [&](bool focused) {
    return Fade(focused ? palette.focus_text : palette.value, list_alpha);
  };
  // A label on the left and a value on the right, centred in a band.
  const auto draw_pair = [&](const Band& band, float size, const std::string& label,
                             const std::string& value) {
    const float y = (band.min.y + band.max.y - size) * 0.5f - screen.Size(3.0f);
    DrawText(draw_list, ImVec2(text_left, y), size, text_color(band.focused), label);
    if (!value.empty()) {
      DrawText(draw_list, ImVec2(text_right - TextWidth(size, value), y), size,
               detail_color(band.focused), value);
    }
  };
  const auto trim = [&](std::string text, float size, float width) {
    if (TextWidth(size, text) <= width) {
      return text;
    }
    while (!text.empty() && TextWidth(size, text + "...") > width) {
      text.pop_back();
    }
    return text + "...";
  };

  switch (page_) {
    case Page::kRoot:
      for (size_t i = 0; i < entries_.size(); ++i) {
        const float band_top = kRowsTop + kRowHeight * static_cast<float>(i);
        draw_pair(draw_band(band_top, kRowHeight, static_cast<int>(i)),
                  screen.Size(kRowTextSize), entries_[i].label, entries_[i].value);
      }
      break;

    case Page::kExitConfirmation: {
      const Band question = draw_band(kRowsTop, kRowHeight, -1);
      draw_pair(question, screen.Size(kRowTextSize), "Leave " + actions_.game_display_name + "?",
                "");
      const Band note = draw_band(kRowsTop + kRowHeight, kRowHeight, -1);
      const float size = screen.Size(kDetailTextSize + 6.0f);
      DrawText(draw_list, ImVec2(text_left, (note.min.y + note.max.y - size) * 0.5f), size,
               detail_color(false), "Unsaved progress will be lost.");
      draw_pair(draw_band(kRowsTop + kRowHeight * 3.0f, kRowHeight, 3), screen.Size(kRowTextSize),
                "Leave Game", "");
      draw_pair(draw_band(kRowsTop + kRowHeight * 4.0f, kRowHeight, 4), screen.Size(kRowTextSize),
                "Cancel", "");
      break;
    }

    case Page::kAchievements: {
      // Summary: how many, how much of the title's gamerscore, and a bar.
      const Band summary = draw_band(kRowsTop, kSummaryHeight, -1);
      const float summary_size = screen.Size(kAchievementTitleSize);
      const std::string count = std::to_string(unlocked_count_) + " of " +
                                std::to_string(achievements_.size()) + " unlocked";
      const std::string score = std::to_string(earned_gamerscore_) + " / " +
                                std::to_string(total_gamerscore_) + " G";
      const float summary_y = summary.min.y + screen.Size(6.0f);
      DrawText(draw_list, ImVec2(text_left, summary_y), summary_size, text_color(false), count);
      DrawText(draw_list, ImVec2(text_right - TextWidth(summary_size, score), summary_y),
               summary_size, detail_color(false), score);
      const float bar_y = summary.max.y - screen.Size(16.0f);
      const float bar_height = std::max(2.0f, screen.Size(6.0f));
      const float fraction = achievements_.empty() ? 0.0f
                                                   : static_cast<float>(unlocked_count_) /
                                                         static_cast<float>(achievements_.size());
      draw_list->AddRectFilled(ImVec2(text_left, bar_y), ImVec2(text_right, bar_y + bar_height),
                               Fade(palette.rule, list_alpha));
      draw_list->AddRectFilled(ImVec2(text_left, bar_y),
                               ImVec2(Lerp(text_left, text_right, fraction), bar_y + bar_height),
                               Fade(palette.focus_bottom, list_alpha));

      const int last = std::min(achievement_scroll_ + achievement_rows,
                                static_cast<int>(achievements_.size()));
      for (int i = achievement_scroll_; i < last; ++i) {
        const AchievementRow& row = achievements_[static_cast<size_t>(i)];
        const int on_screen = i - achievement_scroll_;
        const Band band = draw_band(kRowsTop + kSummaryHeight +
                                        kAchievementRowHeight * static_cast<float>(on_screen),
                                    kAchievementRowHeight, on_screen);
        const float icon = screen.Size(kAchievementIcon);
        const ImVec2 icon_min(text_left, (band.min.y + band.max.y - icon) * 0.5f);
        const ImVec2 icon_max(icon_min.x + icon, icon_min.y + icon);
        rex::ui::ImmediateTexture* texture = icons_ ? icons_->GetIcon(row.info) : nullptr;
        if (!texture) {
          texture = Artwork("unearnedAchievement.png");
        }
        // Earned: the picture in full and the row in the list's own colours.
        // Not yet earned: the picture faded and the words greyed, so what is
        // left to get reads apart from what is done.
        const float icon_alpha = (row.unlocked ? 1.0f : 0.4f) * list_alpha;
        if (texture) {
          draw_list->AddImage(reinterpret_cast<ImTextureID>(texture), icon_min, icon_max,
                              ImVec2(0, 0), ImVec2(1, 1),
                              Fade(IM_COL32(255, 255, 255, 255), icon_alpha));
        } else {
          draw_list->AddRectFilled(icon_min, icon_max, Fade(palette.slate, icon_alpha));
        }
        const ImU32 locked = Fade(palette.value, list_alpha * 0.8f);
        const ImU32 title_color =
            band.focused || row.unlocked ? text_color(band.focused) : locked;
        const ImU32 detail = band.focused || row.unlocked ? detail_color(band.focused) : locked;
        const ImU32 score_color = band.focused    ? detail_color(true)
                                  : row.unlocked ? Fade(palette.focus_bottom, list_alpha)
                                                 : locked;

        const float title_size = screen.Size(kAchievementTitleSize);
        const float detail_size = screen.Size(kAchievementDetailSize);
        const std::string gamerscore = std::to_string(row.info.gamerscore) + " G";
        const float left = icon_max.x + screen.Size(18.0f);
        const float width = text_right - left - TextWidth(title_size, gamerscore) -
                            screen.Size(20.0f);
        const float title_y = band.min.y + screen.Size(8.0f);
        DrawText(draw_list, ImVec2(left, title_y), title_size, title_color,
                 trim(row.info.label, title_size, width));
        DrawText(draw_list, ImVec2(text_right - TextWidth(title_size, gamerscore), title_y),
                 title_size, score_color, gamerscore);
        const std::string& description =
            row.unlocked || row.info.unachieved_description.empty()
                ? row.info.description
                : row.info.unachieved_description;
        DrawText(draw_list, ImVec2(left, title_y + title_size + screen.Size(2.0f)), detail_size,
                 detail, trim(description, detail_size, text_right - left));
      }

      // Where in the list this is.
      if (static_cast<int>(achievements_.size()) > achievement_rows) {
        const float track_top = screen.At(0.0f, kRowsTop + kSummaryHeight).y;
        const float track_bottom = list_max.y;
        const float total = static_cast<float>(achievements_.size());
        const float thumb_top =
            Lerp(track_top, track_bottom, static_cast<float>(achievement_scroll_) / total);
        const float thumb_bottom =
            Lerp(track_top, track_bottom, static_cast<float>(last) / total);
        const float x = list_max.x - screen.Size(8.0f);
        draw_list->AddRectFilled(ImVec2(x, thumb_top), ImVec2(x + screen.Size(4.0f), thumb_bottom),
                                 Fade(palette.slate, list_alpha));
      }
      break;
    }

    case Page::kDlc: {
      const int last = std::min(dlc_scroll_ + dlc_rows, static_cast<int>(dlc_.size()));
      const float size = screen.Size(kDlcTextSize);
      for (int i = dlc_scroll_; i < last; ++i) {
        const DlcRow& row = dlc_[static_cast<size_t>(i)];
        const int on_screen = i - dlc_scroll_;
        const Band band = draw_band(kRowsTop + kDlcRowHeight * static_cast<float>(on_screen),
                                    kDlcRowHeight, on_screen);
        // Content the player has is stated rather than offered; what is
        // missing carries the verb, because pressing A on it does install it.
        const std::string state = row.browse ? "" : row.installed ? "Installed" : "Install";
        const float state_width = state.empty() ? 0.0f : TextWidth(size, state);
        const float y = (band.min.y + band.max.y - size) * 0.5f - screen.Size(3.0f);
        DrawText(draw_list, ImVec2(text_left, y), size, text_color(band.focused),
                 trim(row.label, size, text_right - text_left - state_width - screen.Size(24.0f)));
        if (!state.empty()) {
          // Installed reads as settled, so it takes the quieter colour even
          // when the row is focused; Install is the one asking to be pressed.
          const ImU32 state_color = row.installed
                                        ? detail_color(band.focused)
                                        : (band.focused ? detail_color(true)
                                                        : Fade(palette.focus_bottom, list_alpha));
          DrawText(draw_list, ImVec2(text_right - state_width, y), size, state_color, state);
        }
      }

      // The footer: what happened last, or where in the list this is.
      const Band footer = draw_band(kListBottom - kDlcFooterHeight, kDlcFooterHeight, -1);
      const float footer_size = screen.Size(kDlcFooterSize);
      const float footer_y = (footer.min.y + footer.max.y - footer_size) * 0.5f;
      const std::string position =
          std::to_string(dlc_selected_ + 1) + " of " + std::to_string(dlc_.size());
      DrawText(draw_list, ImVec2(text_left, footer_y), footer_size, detail_color(false),
               dlc_status_.empty()
                   ? position
                   : trim(dlc_status_, footer_size, text_right - text_left - screen.Size(8.0f)));
      if (!dlc_status_.empty()) {
        DrawText(draw_list, ImVec2(text_right - TextWidth(footer_size, position), footer_y),
                 footer_size, detail_color(false), position);
      }
      break;
    }

    case Page::kSettings: {
      const char* title = settings_section_ == "controls"     ? "Controls"
                          : settings_section_ == "game_files" ? "Game Files"
                                                              : "Scaling & Display";
      draw_pair(draw_band(kRowsTop, kSettingRowHeight, -1), screen.Size(kSettingTextSize + 4.0f),
                title, "");
      for (size_t i = 0; i < setting_rows_.size(); ++i) {
        const auto& row = setting_rows_[i];
        std::string value;
        if (!row.cvar.empty()) {
          value = rex::cvar::GetFlagByName(row.cvar);
          for (const auto& choice : row.choices) {
            if (choice.first == value) {
              value = choice.second;
            }
          }
        }
        const float band_top = kRowsTop + kSettingRowHeight * static_cast<float>(i + 1);
        draw_pair(draw_band(band_top, kSettingRowHeight, static_cast<int>(i + 1)),
                  screen.Size(kSettingTextSize), row.label, value);
      }

      // The selected setting's description, then what happened.
      float detail_y = screen.At(0.0f, kRowsTop + kSettingRowHeight *
                                           static_cast<float>(setting_rows_.size() + 1))
                           .y +
                       screen.Size(10.0f);
      const float size = screen.Size(kDetailTextSize);
      if (!setting_rows_.empty()) {
        const std::string& description =
            setting_rows_[static_cast<size_t>(setting_selected_)].description;
        draw_list->AddText(DisplayFont(), size, ImVec2(text_left, detail_y), detail_color(false),
                           description.c_str(), nullptr, text_right - text_left);
        detail_y += size * 2.2f;
      }
      std::string status = settings_status_;
      for (const auto& [cvar, initial] : settings_at_open_) {
        if (rex::cvar::GetFlagByName(cvar) != initial) {
          status += status.empty() ? "Restart required." : " Restart required.";
          break;
        }
      }
      if (!status.empty()) {
        DrawText(draw_list, ImVec2(text_left, detail_y), size, text_color(false), status);
      }
      break;
    }
  }
  draw_list->PopClipRect();

  // The legend.
  struct Hint {
    const char* letter;
    const char* label;
  };
  const Hint a_select = {"A", "Select"};
  const Hint b_back = {"B", "Back"};
  std::vector<Hint> hints;
  switch (page_) {
    case Page::kRoot:
      hints = {a_select, b_back};
      if (actions_.exit_game) {
        hints.push_back({"Y", "Leave Game"});
      }
      break;
    case Page::kExitConfirmation:
      hints = {a_select, b_back};
      break;
    case Page::kAchievements:
      hints = {b_back};
      break;
    case Page::kDlc:
      hints = {{"A", "Install"}, b_back};
      break;
    case Page::kSettings:
      hints = {{"A", "Change"}, b_back, {"X", "Save"}};
      break;
  }
  const float legend_text = screen.Size(kLegendTextSize);
  const float glyph = screen.Size(kLegendGlyph);
  float x = screen.At(kLegendLeft, 0.0f).x;
  const float y = screen.At(0.0f, kLegendY).y;
  for (const Hint& hint : hints) {
    DrawGlyph(draw_list, ImVec2(x + glyph * 0.5f, y), glyph, hint.letter, open,
              reinterpret_cast<ImTextureID>(Artwork(ButtonPicture(hint.letter))));
    const ImVec2 text(x + glyph + screen.Size(kLegendGlyphGap), y - legend_text * 0.55f);
    shadowed(text, legend_text, hint.label);
    x = text.x + TextWidth(legend_text, hint.label) + screen.Size(kLegendItemGap);
  }
}


void GuideDialog::DrawControllerCharge(ImDrawList* draw_list, const Screen& screen, float right,
                                       float top, float open) {
  rex::input::X_INPUT_BATTERY_INFORMATION battery{};
  if (!GuestInputGate::ReadBatteryForMenu(battery)) {
    return;
  }
  // A wired pad has a charge nobody needs to watch, so the console showed the
  // pad without a meter. Anything else gets both.
  const bool wired = battery.type == rex::input::X_INPUT_BATTERY_TYPE_WIRED;
  const float total = kPadWidth + (wired ? 0.0f : 8.0f + kBatteryWidth + kBatteryCapWidth);
  const float left = right - total;
  const ImU32 chrome = Fade(IM_COL32(0xE8, 0xE8, 0xE8, 0xFF), open);
  const float line = std::max(1.0f, screen.Size(2.5f));

  // The pad: a rounded body with player one's quadrant lit, the same green the
  // ring of light uses.
  const ImVec2 pad_min = screen.At(left, top - kPadHeight * 0.15f);
  const ImVec2 pad_max = screen.At(left + kPadWidth, top - kPadHeight * 0.15f + kPadHeight);
  draw_list->AddRect(pad_min, pad_max, chrome, screen.Size(10.0f), 0, line);
  const ImVec2 lit_min(pad_min.x + line * 2.0f, pad_min.y + line * 2.0f);
  const ImVec2 lit_max((pad_min.x + pad_max.x) * 0.5f - line * 0.5f,
                       (pad_min.y + pad_max.y) * 0.5f - line * 0.5f);
  draw_list->AddRectFilled(lit_min, lit_max, Fade(IM_COL32(0x9C, 0xE0, 0x2C, 0xFF), open),
                           screen.Size(4.0f));
  if (wired) {
    return;
  }

  const float meter_left = left + kPadWidth + 8.0f;
  const float meter_top = top + (kPadHeight - kBatteryHeight) * 0.5f - kPadHeight * 0.15f;
  const ImVec2 body_min = screen.At(meter_left, meter_top);
  const ImVec2 body_max = screen.At(meter_left + kBatteryWidth, meter_top + kBatteryHeight);
  draw_list->AddRect(body_min, body_max, chrome, screen.Size(3.0f), 0, line);
  draw_list->AddRectFilled(
      screen.At(meter_left + kBatteryWidth, meter_top + (kBatteryHeight - kBatteryCapHeight) * 0.5f),
      screen.At(meter_left + kBatteryWidth + kBatteryCapWidth,
                meter_top + (kBatteryHeight + kBatteryCapHeight) * 0.5f),
      chrome, screen.Size(2.0f));

  const int bars = std::min(static_cast<int>(battery.level), static_cast<int>(kBatteryBars));
  // Empty reads as a warning rather than as nothing to say, so it is the one
  // level that changes colour.
  const ImU32 fill = battery.level == rex::input::X_INPUT_BATTERY_LEVEL_EMPTY
                         ? Fade(IM_COL32(0xE0, 0x3C, 0x2C, 0xFF), open)
                         : Fade(IM_COL32(0x9C, 0xE0, 0x2C, 0xFF), open);
  const float inset = 3.0f;
  const float bar_span = (kBatteryWidth - inset * 2.0f) / kBatteryBars;
  for (int bar = 0; bar < bars; ++bar) {
    const float bar_left = meter_left + inset + bar_span * float(bar);
    draw_list->AddRectFilled(screen.At(bar_left + 1.0f, meter_top + inset),
                             screen.At(bar_left + bar_span - 1.0f,
                                       meter_top + kBatteryHeight - inset),
                             fill);
  }
  if (bars == 0) {
    // Nothing left to draw as a bar, but the meter should still read as empty
    // rather than as absent, so the body is outlined in the warning colour.
    draw_list->AddRect(body_min, body_max, fill, screen.Size(3.0f), 0, line);
  }
}

}  // namespace recomp
