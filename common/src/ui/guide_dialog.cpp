#include "recomp/ui/guide_dialog.h"

#include <algorithm>
#include <ctime>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/system/achievement_manager.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/overlay/achievement_icon_cache.h>

#include "recomp/input/controller_menu_watcher.h"
#include "recomp/input/imgui_gamepad_bridge.h"
#include "recomp/ui/guide_resources.h"
#include "recomp/ui/guide_fonts.h"
#include "recomp/settings/user_settings_store.h"

#include "guide_theme.h"

REXCVAR_DEFINE_STRING(recomp_gamertag, "Player", "Recomp",
                      "The name the compatibility guide shows on its own tab, where a console "
                      "shows the signed-in gamertag.");

namespace recomp {

namespace {

// Rows on the achievements page are tall enough for an icon, a title and a
// line of description, as they are on the console.
constexpr float kAchievementRowHeight = 76.0f;
constexpr float kAchievementIconSize = 56.0f;
constexpr float kAchievementsWidth = 880.0f;
constexpr int kAchievementsVisibleRows = 6;
constexpr float kSummaryHeight = 54.0f;
constexpr float kGamerTileSize = 56.0f;

// Every key the guide reads, so one held at the moment it opens can be held
// back until it is released.
constexpr ImGuiKey kWatchedKeys[] = {
    ImGuiKey_Enter,
    ImGuiKey_Space,
    ImGuiKey_Escape,
    ImGuiKey_UpArrow,
    ImGuiKey_DownArrow,
    ImGuiKey_LeftArrow,
    ImGuiKey_RightArrow,
    ImGuiKey_PageUp,
    ImGuiKey_PageDown,
    ImGuiKey_GamepadFaceDown,
    ImGuiKey_GamepadFaceRight,
    ImGuiKey_GamepadFaceUp,
    ImGuiKey_GamepadDpadUp,
    ImGuiKey_GamepadDpadDown,
    ImGuiKey_GamepadL1,
    ImGuiKey_GamepadR1,
    ImGuiKey_GamepadLStickUp,
    ImGuiKey_GamepadLStickDown,
    ImGuiKey_GamepadDpadLeft,
    ImGuiKey_GamepadDpadRight,
    ImGuiKey_GamepadLStickLeft,
    ImGuiKey_GamepadLStickRight,
    ImGuiKey_GamepadFaceLeft,
    ImGuiKey_S,
};

void Text(ImDrawList* draw_list, ImVec2 position, ImU32 color, const std::string& text) {
  draw_list->AddText(position, color, text.c_str());
}

void TextScaled(ImDrawList* draw_list, ImVec2 position, ImU32 color, const std::string& text,
                float scale) {
  draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, position, color, text.c_str());
}

// Over the game the guide has no panel behind it, so its text carries its own
// shadow, as the console's does.
void TextOverGame(ImDrawList* draw_list, ImVec2 position, ImU32 color, ImU32 shadow,
                  const std::string& text, float scale) {
  TextScaled(draw_list, ImVec2(position.x + 2.0f, position.y + 2.0f), shadow, text, scale);
  TextScaled(draw_list, position, color, text, scale);
}

// Rotate the whole label clockwise, retaining normal glyph spacing and UTF-8.
void TextDown(ImDrawList* draw_list, ImVec2 center, ImU32 color, const std::string& text) {
  const ImVec2 size = ImGui::CalcTextSize(text.c_str());
  const int first = draw_list->VtxBuffer.Size;
  draw_list->AddText(ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f), color,
                     text.c_str());
  for (int i = first; i < draw_list->VtxBuffer.Size; ++i) {
    ImVec2& position = draw_list->VtxBuffer[i].pos;
    const float x = position.x - center.x;
    const float y = position.y - center.y;
    position = ImVec2(center.x - y, center.y + x);
  }
}

// Text that has to fit a column: cut short with an ellipsis rather than run
// into whatever is drawn next to it.
std::string Trim(const std::string& text, float max_width) {
  if (ImGui::CalcTextSize(text.c_str()).x <= max_width) {
    return text;
  }
  std::string trimmed = text;
  while (!trimmed.empty() && ImGui::CalcTextSize((trimmed + "...").c_str()).x > max_width) {
    trimmed.pop_back();
  }
  while (!trimmed.empty() && trimmed.back() == ' ') {
    trimmed.pop_back();
  }
  return trimmed + "...";
}

bool LocalTime(std::time_t moment, std::tm& out) {
#if defined(_WIN32)
  return localtime_s(&out, &moment) == 0;
#else
  return localtime_r(&moment, &out) != nullptr;
#endif
}

std::string FormatUnlockDate(uint64_t file_time) {
  if (file_time == 0) {
    return {};
  }
  // A Windows FILETIME: 100 ns ticks since 1601.
  const int64_t seconds = static_cast<int64_t>(file_time / 10000000ull) - 11644473600ll;
  std::tm local{};
  if (seconds <= 0 || !LocalTime(static_cast<std::time_t>(seconds), local)) {
    return {};
  }
  char buffer[32] = {};
  if (std::strftime(buffer, sizeof(buffer), "%d/%m/%Y", &local) == 0) {
    return {};
  }
  return buffer;
}

std::string ClockText() {
  std::tm local{};
  if (!LocalTime(std::time(nullptr), local)) {
    return {};
  }
  char buffer[16] = {};
  if (std::strftime(buffer, sizeof(buffer), "%H:%M", &local) == 0) {
    return {};
  }
  return buffer;
}

std::string GamerscoreText(int score) {
  return std::to_string(score) + " G";
}

}  // namespace

GuideDialog::GuideDialog(rex::ui::ImGuiDrawer* drawer, GuideActions actions)
    : ImGuiDialog(drawer), actions_(std::move(actions)), theme_(CurrentGuideTheme()) {
  GuestInputGate::OnMenuShown();
  if (drawer) {
    resources_ = std::make_unique<GuideResources>(drawer->immediate_drawer());
    resources_->LoadIfNeeded();
    if (actions_.runtime) {
      icons_ = std::make_unique<rex::ui::AchievementIconCache>(drawer->immediate_drawer(),
                                                               actions_.runtime);
    }
  }
  LoadAchievements();
  BuildEntries();
  for (const auto& setting : UserSettingsStore::Settings()) {
    if (setting.requires_restart) {
      settings_at_open_[std::string(setting.cvar)] = rex::cvar::GetFlagByName(setting.cvar);
    }
  }
}

GuideDialog::~GuideDialog() = default;

void GuideDialog::OnClose() {
  GuestInputGate::OnMenuHidden();
  if (actions_.on_closed) {
    actions_.on_closed();
  }
}

bool GuideDialog::HasAchievements() const {
  return !achievements_.empty();
}

void GuideDialog::LoadAchievements() {
  if (achievements_loaded_ || !actions_.achievements) {
    return;
  }
  achievements_loaded_ = true;
  for (const auto& info : actions_.achievements->ListAchievements()) {
    AchievementRow row;
    row.unlocked = actions_.achievements->IsUnlocked(info.id);
    row.unlocked_at = row.unlocked ? actions_.achievements->GetUnlockTime(info.id) : 0;
    total_gamerscore_ += static_cast<int>(info.gamerscore);
    if (row.unlocked) {
      ++unlocked_count_;
      earned_gamerscore_ += static_cast<int>(info.gamerscore);
    }
    row.info = info;
    achievements_.push_back(std::move(row));
  }
  // Unlocked first, as the console's list orders them.
  std::stable_sort(
      achievements_.begin(), achievements_.end(),
      [](const AchievementRow& a, const AchievementRow& b) { return a.unlocked && !b.unlocked; });
}

void GuideDialog::BuildEntries() {
  entries_.clear();
  if (tab_ == GuideTab::kGames) {
    Entry entry;
    entry.label = "Achievements";
    entry.closes_guide = false;
    if (HasAchievements()) {
      entry.value = std::to_string(unlocked_count_) + "/" + std::to_string(achievements_.size());
      entry.opens = Page::kAchievements;
    } else {
      // A title with none still gets the row, as on a console; it leads nowhere.
      entry.value = "None";
    }
    entries_.push_back(std::move(entry));
  } else if (tab_ == GuideTab::kSettings) {
    if (!actions_.settings.settings_file.empty()) {
      for (const auto& section :
           {std::pair{"Scaling & Display", "video"}, std::pair{"Controls", "controls"},
            std::pair{"Game Files", "game_files"}}) {
        const std::string target = section.second;
        entries_.push_back(
            {section.first, "", [this, target] { ShowSettings(target); }, false, Page::kSettings});
      }
    }
  } else {
    entries_.push_back({"Leave Game", "", nullptr, false, Page::kExitConfirmation});
    entries_.push_back({"Resume Game", "", nullptr, true, Page::kRoot});
  }
}

void GuideDialog::SwitchTab(int direction) {
  const GuideTab next = AdjacentGuideTab(tab_, direction);
  if (next == tab_)
    return;
  tab_ = next;
  page_ = Page::kRoot;
  selected_ = 0;
  BuildEntries();
}

void GuideDialog::ShowAchievements() {
  if (!HasAchievements()) {
    return;
  }
  tab_ = GuideTab::kGames;
  selected_ = 0;
  BuildEntries();
  page_ = Page::kAchievements;
}

rex::ui::ImmediateTexture* GuideDialog::Artwork(const std::string& name) {
  return resources_ ? resources_->Get(name) : nullptr;
}

rex::ui::ImmediateTexture* GuideDialog::FirstArtwork(std::initializer_list<const char*> names) {
  for (const char* name : names) {
    if (rex::ui::ImmediateTexture* texture = Artwork(name)) {
      return texture;
    }
  }
  return nullptr;
}

void GuideDialog::ArmInput() {
  masked_keys_.clear();
  for (ImGuiKey key : kWatchedKeys) {
    if (ImGui::IsKeyDown(key)) {
      masked_keys_.push_back(key);
    }
  }
}

bool GuideDialog::Pressed(std::initializer_list<ImGuiKey> keys, bool repeat) {
  bool pressed = false;
  for (ImGuiKey key : keys) {
    auto masked = std::find(masked_keys_.begin(), masked_keys_.end(), key);
    if (masked != masked_keys_.end()) {
      if (ImGui::IsKeyDown(key)) {
        continue;
      }
      masked_keys_.erase(masked);
    }
    pressed = ImGui::IsKeyPressed(key, repeat) || pressed;
  }
  return pressed;
}

bool GuideDialog::HandleInput(int& selection, int count, int page_rows) {
  if (count <= 0) {
    return Pressed({ImGuiKey_Enter, ImGuiKey_Space, ImGuiKey_GamepadFaceDown}, false);
  }
  if (Pressed({ImGuiKey_DownArrow, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown}, true)) {
    selection = (selection + 1) % count;
  }
  if (Pressed({ImGuiKey_UpArrow, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp}, true)) {
    selection = (selection + count - 1) % count;
  }
  if (page_rows > 0 && Pressed({ImGuiKey_PageDown}, true)) {
    selection = std::min(selection + page_rows, count - 1);
  }
  if (page_rows > 0 && Pressed({ImGuiKey_PageUp}, true)) {
    selection = std::max(selection - page_rows, 0);
  }
  selection = std::clamp(selection, 0, count - 1);
  return Pressed({ImGuiKey_Enter, ImGuiKey_Space, ImGuiKey_GamepadFaceDown}, false);
}

void GuideDialog::DrawRowBackground(ImDrawList* draw_list, ImVec2 row_min, ImVec2 row_max,
                                    bool selected) {
  if (!selected) {
    return;
  }
  draw_list->AddRectFilled(row_min, row_max, theme_.selection, theme_.rounding);
}

void GuideDialog::DrawButtonGlyph(ImDrawList* draw_list, ImVec2 center, const char* letter,
                                  ImU32 fallback) {
  const std::string name = std::string(letter) + "-Button.png";
  if (rex::ui::ImmediateTexture* texture = Artwork(name)) {
    const float half = 12.0f;
    draw_list->AddImage(reinterpret_cast<ImTextureID>(texture),
                        ImVec2(center.x - half, center.y - half),
                        ImVec2(center.x + half, center.y + half));
    return;
  }
  draw_list->AddCircleFilled(center, 11.0f, fallback, 24);
  const ImVec2 size = ImGui::CalcTextSize(letter);
  draw_list->AddText(ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f),
                     IM_COL32(255, 255, 255, 255), letter);
}

float GuideDialog::DrawHint(ImDrawList* draw_list, float x, float y, const char* letter,
                            ImU32 fallback, const char* label) {
  DrawButtonGlyph(draw_list, ImVec2(x, y), letter, fallback);
  const ImVec2 position(x + 18.0f, y - ImGui::GetFontSize() * 0.5f);
  draw_list->AddText(ImVec2(position.x + 2.0f, position.y + 2.0f), theme_.chrome_shadow, label);
  draw_list->AddText(position, theme_.chrome_text, label);
  return position.x + ImGui::CalcTextSize(label).x + 30.0f;
}

void GuideDialog::DrawChrome(ImDrawList* draw_list, ImVec2 panel_min, ImVec2 panel_max) {
  const float baseline = panel_min.y - theme_.header_height;

  // "Xbox Guide", the gamer tile and the clock, in the places the console puts
  // them: title left, tile over the middle, clock right.
  TextOverGame(draw_list, ImVec2(panel_min.x + 8.0f, baseline + 14.0f), theme_.chrome_text,
               theme_.chrome_shadow, "Xbox Guide", 1.6f);

  const float tile_x = (panel_min.x + panel_max.x) * 0.5f - kGamerTileSize * 0.5f;
  const ImVec2 tile_min(tile_x, panel_min.y - kGamerTileSize - 8.0f);
  const ImVec2 tile_max(tile_min.x + kGamerTileSize, tile_min.y + kGamerTileSize);
  // Only a picture the player put there by name: the packages hold sheets and
  // placeholders that read as nothing at this size.
  rex::ui::ImmediateTexture* tile = FirstArtwork({"gamerpic.png", "gamertile.png"});
  if (tile) {
    draw_list->AddImage(reinterpret_cast<ImTextureID>(tile), tile_min, tile_max);
  } else {
    const std::string gamertag = REXCVAR_GET(recomp_gamertag);
    draw_list->AddRectFilled(tile_min, tile_max, theme_.accent);
    const std::string initial(1, gamertag.empty() ? 'P' : gamertag.front());
    const float scale = 2.0f;
    const ImVec2 size = ImGui::CalcTextSize(initial.c_str());
    TextScaled(draw_list,
               ImVec2((tile_min.x + tile_max.x) * 0.5f - size.x * scale * 0.5f,
                      (tile_min.y + tile_max.y) * 0.5f - size.y * scale * 0.5f),
               IM_COL32(255, 255, 255, 255), initial, scale);
  }
  draw_list->AddRect(tile_min, tile_max, theme_.chrome_text, 0.0f, 0, 2.0f);

  const std::string clock = ClockText();
  if (!clock.empty()) {
    const float width = ImGui::CalcTextSize(clock.c_str()).x * 1.4f;
    TextOverGame(draw_list, ImVec2(panel_max.x - 8.0f - width, baseline + 18.0f),
                 theme_.chrome_text, theme_.chrome_shadow, clock, 1.4f);
  }
}

float GuideDialog::DrawTabs(ImDrawList* draw_list, ImVec2 panel_min, ImVec2 panel_max) {
  const float width = theme_.tab_width;
  const int active = static_cast<int>(tab_);
  const std::string labels[] = {"Games", REXCVAR_GET(recomp_gamertag), "Settings"};
  for (int i = 0; i < 3; ++i) {
    // Tabs up to the active one sit on the left; remaining tabs sit on the right.
    const float x = i <= active ? panel_min.x + i * width : panel_max.x - (3 - i) * width;
    const ImVec2 low(x, panel_min.y);
    const ImVec2 high(x + width, panel_max.y);
    draw_list->AddRectFilled(low, high, i == active ? theme_.tab_active_fill : theme_.tab_fill);
    // Neighbouring tabs of the same colour still read as separate tabs.
    draw_list->AddLine(ImVec2(high.x - 1.0f, low.y), ImVec2(high.x - 1.0f, high.y),
                       theme_.separator);
    std::string label = Trim(labels[i], panel_max.y - panel_min.y - 28.0f);
    TextDown(
        draw_list,
        ImVec2(x + width * 0.5f, panel_min.y + 14.0f + ImGui::CalcTextSize(label.c_str()).x * 0.5f),
        i == active ? theme_.tab_active_text : theme_.tab_text, label);
  }
  return panel_min.x + (active + 1) * width;
}

void GuideDialog::DrawEntries(ImDrawList* draw_list, ImVec2 top_left, float width) {
  for (size_t i = 0; i < entries_.size(); ++i) {
    const bool selected = static_cast<int>(i) == selected_;
    const ImVec2 row_min(top_left.x, top_left.y + theme_.entry_height * static_cast<float>(i));
    const ImVec2 row_max(top_left.x + width, row_min.y + theme_.entry_height);
    DrawRowBackground(draw_list, row_min, row_max, selected);
    if (i > 0 && !selected) {
      draw_list->AddLine(ImVec2(row_min.x + 12.0f, row_min.y), ImVec2(row_max.x - 12.0f, row_min.y),
                         theme_.separator);
    }

    const Entry& entry = entries_[i];
    const ImU32 color = selected ? theme_.text_selected : theme_.text;
    const float text_y = row_min.y + theme_.entry_height * 0.5f - ImGui::GetFontSize() * 0.7f;
    TextScaled(draw_list, ImVec2(row_min.x + theme_.padding, text_y), color, entry.label, 1.3f);
    if (!entry.value.empty()) {
      const float value_width = ImGui::CalcTextSize(entry.value.c_str()).x * 1.3f;
      TextScaled(draw_list, ImVec2(row_max.x - theme_.padding - value_width, text_y),
                 selected ? theme_.text_selected : theme_.text_dim, entry.value, 1.3f);
    }
  }
}

void GuideDialog::DrawAchievements(ImDrawList* draw_list, ImVec2 top_left, float width,
                                   int visible_rows) {
  // Summary band: what the console shows above the list.
  const std::string summary = std::to_string(unlocked_count_) + " of " +
                              std::to_string(achievements_.size()) + " achievements - " +
                              GamerscoreText(earned_gamerscore_) + " of " +
                              GamerscoreText(total_gamerscore_);
  Text(draw_list, ImVec2(top_left.x + theme_.padding, top_left.y + 12.0f), theme_.text, summary);

  const float bar_y = top_left.y + kSummaryHeight - 16.0f;
  const float bar_width = width - theme_.padding * 2.0f;
  const float fraction = achievements_.empty() ? 0.0f
                                               : static_cast<float>(unlocked_count_) /
                                                     static_cast<float>(achievements_.size());
  const ImVec2 bar_min(top_left.x + theme_.padding, bar_y);
  draw_list->AddRectFilled(bar_min, ImVec2(bar_min.x + bar_width, bar_min.y + 6.0f),
                           theme_.separator, 3.0f);
  draw_list->AddRectFilled(bar_min, ImVec2(bar_min.x + bar_width * fraction, bar_min.y + 6.0f),
                           theme_.accent, 3.0f);

  // Keep the selection on screen.
  achievement_scroll_ =
      std::clamp(achievement_scroll_, std::max(0, achievement_selected_ - visible_rows + 1),
                 std::max(0, achievement_selected_));
  const int last =
      std::min(achievement_scroll_ + visible_rows, static_cast<int>(achievements_.size()));

  const float list_top = top_left.y + kSummaryHeight;
  for (int index = achievement_scroll_; index < last; ++index) {
    const AchievementRow& row = achievements_[static_cast<size_t>(index)];
    const float row_y =
        list_top + kAchievementRowHeight * static_cast<float>(index - achievement_scroll_);
    const bool selected = index == achievement_selected_;
    const ImVec2 row_min(top_left.x + 4.0f, row_y);
    const ImVec2 row_max(top_left.x + width - 4.0f, row_y + kAchievementRowHeight - 4.0f);
    DrawRowBackground(draw_list, row_min, row_max, selected);
    if (!selected) {
      draw_list->AddLine(ImVec2(row_min.x + 8.0f, row_min.y), ImVec2(row_max.x - 8.0f, row_min.y),
                         theme_.separator);
    }

    // Icon: the title's own, or the console's placeholder for one not yet won.
    const ImVec2 icon_min(row_min.x + 14.0f,
                          row_y + (kAchievementRowHeight - kAchievementIconSize) * 0.5f - 2.0f);
    const ImVec2 icon_max(icon_min.x + kAchievementIconSize, icon_min.y + kAchievementIconSize);
    rex::ui::ImmediateTexture* icon = icons_ ? icons_->GetIcon(row.info) : nullptr;
    if (!icon) {
      const bool secret =
          !row.unlocked && row.info.description.empty() && row.info.unachieved_description.empty();
      icon = Artwork(secret ? "secretAchievement.png" : "unearnedAchievement.png");
    }
    if (icon) {
      const ImU32 tint = row.unlocked ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 120);
      draw_list->AddImage(reinterpret_cast<ImTextureID>(icon), icon_min, icon_max, ImVec2(0, 0),
                          ImVec2(1, 1), tint);
    } else {
      draw_list->AddRectFilled(icon_min, icon_max, row.unlocked ? theme_.accent : theme_.separator,
                               4.0f);
    }

    const float text_x = icon_max.x + 16.0f;
    const std::string score = GamerscoreText(static_cast<int>(row.info.gamerscore));
    const std::string date = row.unlocked ? FormatUnlockDate(row.unlocked_at) : std::string();
    const float right_width = std::max(ImGui::CalcTextSize(score.c_str()).x,
                                       date.empty() ? 0.0f : ImGui::CalcTextSize(date.c_str()).x);
    const float text_width = row_max.x - text_x - right_width - 28.0f;

    const ImU32 title_color =
        selected ? theme_.text_selected : (row.unlocked ? theme_.text : theme_.text_dim);
    Text(draw_list, ImVec2(text_x, row_y + 16.0f), title_color, Trim(row.info.label, text_width));
    const std::string& description = row.unlocked || row.info.unachieved_description.empty()
                                         ? row.info.description
                                         : row.info.unachieved_description;
    Text(draw_list, ImVec2(text_x, row_y + 40.0f),
         selected ? theme_.text_selected : theme_.text_dim, Trim(description, text_width));

    const float right_x = row_max.x - 14.0f - right_width;
    Text(draw_list, ImVec2(right_x, row_y + 16.0f), selected ? theme_.text_selected : theme_.accent,
         score);
    if (!date.empty()) {
      Text(draw_list, ImVec2(right_x, row_y + 40.0f),
           selected ? theme_.text_selected : theme_.text_dim, date);
    }
  }

  // A scrollbar, so a long list reads as one.
  if (static_cast<int>(achievements_.size()) > visible_rows) {
    const float track_height = kAchievementRowHeight * static_cast<float>(visible_rows);
    const float thumb_height =
        track_height * static_cast<float>(visible_rows) / static_cast<float>(achievements_.size());
    const float thumb_top = list_top + track_height * static_cast<float>(achievement_scroll_) /
                                           static_cast<float>(achievements_.size());
    const float x = top_left.x + width - 6.0f;
    draw_list->AddRectFilled(ImVec2(x, list_top), ImVec2(x + 3.0f, list_top + track_height),
                             theme_.separator, 2.0f);
    draw_list->AddRectFilled(ImVec2(x, thumb_top), ImVec2(x + 3.0f, thumb_top + thumb_height),
                             theme_.accent, 2.0f);
  }
}

void GuideDialog::DrawHints(ImDrawList* draw_list, ImVec2 bottom_left, float width) {
  const float y = bottom_left.y + theme_.footer_height * 0.5f;

  // Measure first so the row sits centred under the panel, as the console's
  // does.
  struct Hint {
    const char* letter;
    ImU32 fallback;
    const char* label;
  };
  std::vector<Hint> hints;
  if (page_ != Page::kAchievements) {
    hints.push_back({"A", theme_.accent, page_ == Page::kSettings ? "Change" : "Select"});
  }
  hints.push_back({"B", theme_.button_b, page_ == Page::kRoot ? "Close" : "Back"});
  if (page_ == Page::kRoot && actions_.exit_game) {
    hints.push_back({"Y", IM_COL32(222, 178, 20, 255), "Leave Game"});
  }

  if (page_ == Page::kSettings)
    hints.push_back({"X", IM_COL32(45, 120, 205, 255), "Save"});

  float total = 0.0f;
  for (const Hint& hint : hints) {
    total += 18.0f + ImGui::CalcTextSize(hint.label).x + 30.0f;
  }
  float x = bottom_left.x + (width - total) * 0.5f + 11.0f;
  for (const Hint& hint : hints) {
    x = DrawHint(draw_list, x, y, hint.letter, hint.fallback, hint.label);
  }
}

void GuideDialog::DrawExitConfirmation(ImDrawList* draw_list, ImVec2 top_left, float width) {
  const std::string question = "Leave " + actions_.game_display_name + "?";
  TextScaled(draw_list, ImVec2(top_left.x + theme_.padding, top_left.y + 12.0f), theme_.text,
             question, 1.3f);
  Text(draw_list, ImVec2(top_left.x + theme_.padding, top_left.y + 44.0f), theme_.text_dim,
       "Unsaved progress will be lost.");

  static const char* kChoices[] = {"Leave Game", "Cancel"};
  for (int i = 0; i < 2; ++i) {
    const bool selected = i == exit_choice_;
    const ImVec2 row_min(top_left.x,
                         top_left.y + 80.0f + theme_.entry_height * static_cast<float>(i));
    const ImVec2 row_max(top_left.x + width, row_min.y + theme_.entry_height);
    DrawRowBackground(draw_list, row_min, row_max, selected);
    TextScaled(draw_list,
               ImVec2(row_min.x + theme_.padding,
                      row_min.y + theme_.entry_height * 0.5f - ImGui::GetFontSize() * 0.7f),
               selected ? theme_.text_selected : theme_.text, kChoices[i], 1.3f);
  }
}

void GuideDialog::OnDraw(ImGuiIO& io) {
  if (close_requested_) {
    Close();
    return;
  }
  ImGuiGamepadBridge::FeedPrimaryController(io);

  const int visible_rows =
      std::min(kAchievementsVisibleRows, static_cast<int>(achievements_.size()));
  float panel_width = std::min(theme_.panel_width, io.DisplaySize.x - 80.0f);
  float body_height = 0.0f;
  switch (page_) {
    case Page::kRoot:
      body_height = theme_.entry_height * static_cast<float>(std::max(size_t{6}, entries_.size()));
      break;
    case Page::kSettings:
      body_height = theme_.entry_height * 9.0f;
      break;
    case Page::kAchievements:
      panel_width = std::min(kAchievementsWidth, io.DisplaySize.x - 80.0f);
      body_height = kSummaryHeight + kAchievementRowHeight * static_cast<float>(visible_rows);
      break;
    case Page::kExitConfirmation:
      body_height = 80.0f + theme_.entry_height * 2.0f;
      break;
  }
  const ImVec2 panel_min((io.DisplaySize.x - panel_width) * 0.5f,
                         (io.DisplaySize.y - body_height) * 0.5f);
  const ImVec2 panel_max(panel_min.x + panel_width, panel_min.y + body_height);

  if (GuideFont())
    ImGui::PushFont(GuideFont());
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("##recomp_guide", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoInputs);
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  // The game keeps rendering behind the guide, dimmed as on a console.
  draw_list->AddRectFilled(ImVec2(0.0f, 0.0f), io.DisplaySize, theme_.dim);

  if (page_ == Page::kRoot || page_ == Page::kExitConfirmation) {
    DrawBladeScene(draw_list, io);
  } else {
    DrawChrome(draw_list, panel_min, panel_max);

    if (theme_.panel_top == theme_.panel_bottom) {
      draw_list->AddRectFilled(panel_min, panel_max, theme_.panel_top, theme_.rounding);
    } else {
      draw_list->AddRectFilledMultiColor(panel_min, panel_max, theme_.panel_top, theme_.panel_top,
                                         theme_.panel_bottom, theme_.panel_bottom);
    }

    const float list_x = DrawTabs(draw_list, panel_min, panel_max);
    const float list_width = panel_width - 3.0f * theme_.tab_width;
    const ImVec2 list_min(list_x, panel_min.y);
    switch (page_) {
      case Page::kRoot:
        DrawEntries(draw_list, list_min, list_width);
        break;
      case Page::kSettings:
        DrawSettings(draw_list, list_min, list_width);
        break;
      case Page::kAchievements:
        DrawAchievements(draw_list, list_min, list_width, visible_rows);
        break;
      case Page::kExitConfirmation:
        DrawExitConfirmation(draw_list, list_min, list_width);
        break;
    }
    DrawHints(draw_list, ImVec2(panel_min.x, panel_max.y), panel_width);
  }
  ImGui::End();
  if (GuideFont())
    ImGui::PopFont();

  // The controller state the guide sees on its first frames is whatever was
  // held when it opened - the chord that opened it, or a pad resting off
  // centre. Take a reading, then start listening.
  if (frames_drawn_ < 2) {
    if (++frames_drawn_ == 2) {
      ArmInput();
    }
    return;
  }

  if (page_ != Page::kExitConfirmation) {
    const bool left = page_ == Page::kSettings
                          ? Pressed({ImGuiKey_GamepadL1}, false)
                          : Pressed({ImGuiKey_GamepadL1, ImGuiKey_LeftArrow}, false);
    const bool right = page_ == Page::kSettings
                           ? Pressed({ImGuiKey_GamepadR1}, false)
                           : Pressed({ImGuiKey_GamepadR1, ImGuiKey_RightArrow}, false);
    if (left != right) {
      SwitchTab(right ? 1 : -1);
      return;
    }
  }

  const bool back = Pressed({ImGuiKey_Escape, ImGuiKey_GamepadFaceRight}, false);

  if (page_ == Page::kSettings) {
    if (back) {
      page_ = Page::kRoot;
      return;
    }
    const bool chosen = HandleInput(setting_selected_, static_cast<int>(setting_rows_.size()), 0);
    const bool left =
        Pressed({ImGuiKey_LeftArrow, ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft}, true);
    const bool right = Pressed(
        {ImGuiKey_RightArrow, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight}, true);
    if (chosen)
      ChangeSetting(1);
    else if (left != right)
      ChangeSetting(right ? 1 : -1);
    if (Pressed({ImGuiKey_GamepadFaceLeft, ImGuiKey_S}, false))
      SaveSettings();
    return;
  }

  if (page_ == Page::kAchievements) {
    HandleInput(achievement_selected_, static_cast<int>(achievements_.size()), visible_rows);
    if (back) {
      page_ = Page::kRoot;
    }
    return;
  }

  if (page_ == Page::kExitConfirmation) {
    const bool chosen = HandleInput(exit_choice_, 2, 0);
    if (back) {
      page_ = Page::kRoot;
      return;
    }
    if (chosen) {
      if (exit_choice_ == 0 && actions_.exit_game) {
        actions_.exit_game();
      } else {
        page_ = Page::kRoot;
      }
    }
    return;
  }

  // Y leaves the game from anywhere on the root screen, as it does on a
  // console.
  if (actions_.exit_game && Pressed({ImGuiKey_GamepadFaceUp}, false)) {
    page_ = Page::kExitConfirmation;
    exit_choice_ = 1;
    return;
  }

  const bool chosen = HandleInput(selected_, static_cast<int>(entries_.size()), 0);
  if (back) {
    Close();
    return;
  }
  if (!chosen) {
    return;
  }
  if (entries_.empty())
    return;
  const Entry& entry = entries_[static_cast<size_t>(selected_)];
  if (!entry.closes_guide) {
    page_ = entry.opens;
    if (entry.activate) {
      auto activate = entry.activate;
      activate();
    }
    if (page_ == Page::kExitConfirmation) {
      exit_choice_ = 1;
    }
    return;
  }
  if (entry.activate) {
    entry.activate();
  }
  Close();
}

}  // namespace recomp
