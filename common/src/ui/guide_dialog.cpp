#include "recomp/ui/guide_dialog.h"

#include <algorithm>
#include <cstdio>
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

#include "guide_theme.h"

REXCVAR_DEFINE_STRING(recomp_gamertag, "Player", "Recomp",
                      "The name the compatibility guide shows in its header, where a console "
                      "shows the signed-in gamertag.");

namespace recomp {

namespace {

// Rows on the achievements page are tall enough for an icon, a title and a
// line of description, as they are on the console.
constexpr float kAchievementRowHeight = 76.0f;
constexpr float kAchievementIconSize = 56.0f;
constexpr float kAchievementsWidth = 860.0f;
constexpr int kAchievementsVisibleRows = 6;
constexpr float kSummaryHeight = 54.0f;

// Every key the guide reads, so one held at the moment it opens can be held
// back until it is released.
constexpr ImGuiKey kWatchedKeys[] = {
    ImGuiKey_Enter,         ImGuiKey_Space,          ImGuiKey_Escape,
    ImGuiKey_UpArrow,       ImGuiKey_DownArrow,      ImGuiKey_LeftArrow,
    ImGuiKey_RightArrow,    ImGuiKey_PageUp,         ImGuiKey_PageDown,
    ImGuiKey_GamepadFaceDown, ImGuiKey_GamepadFaceRight,
    ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadDpadDown,
    ImGuiKey_GamepadL1,     ImGuiKey_GamepadR1,
    ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown,
};

void Text(ImDrawList* draw_list, ImVec2 position, ImU32 color, const std::string& text) {
  draw_list->AddText(position, color, text.c_str());
}

void TextScaled(ImDrawList* draw_list, ImVec2 position, ImU32 color, const std::string& text,
                float scale) {
  draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, position, color,
                     text.c_str());
}

// Text that has to fit a column: cut short with an ellipsis rather than run
// into whatever is drawn next to it.
std::string Trim(const std::string& text, float max_width) {
  if (ImGui::CalcTextSize(text.c_str()).x <= max_width) {
    return text;
  }
  std::string trimmed = text;
  while (!trimmed.empty() &&
         ImGui::CalcTextSize((trimmed + "...").c_str()).x > max_width) {
    trimmed.pop_back();
  }
  while (!trimmed.empty() && trimmed.back() == ' ') {
    trimmed.pop_back();
  }
  return trimmed + "...";
}

std::string FormatUnlockDate(uint64_t file_time) {
  if (file_time == 0) {
    return {};
  }
  // A Windows FILETIME: 100 ns ticks since 1601.
  const int64_t seconds = static_cast<int64_t>(file_time / 10000000ull) - 11644473600ll;
  if (seconds <= 0) {
    return {};
  }
  const std::time_t as_time = static_cast<std::time_t>(seconds);
  std::tm local{};
#if defined(_WIN32)
  if (localtime_s(&local, &as_time) != 0) {
    return {};
  }
#else
  if (!localtime_r(&as_time, &local)) {
    return {};
  }
#endif
  char buffer[32] = {};
  if (std::strftime(buffer, sizeof(buffer), "%d/%m/%Y", &local) == 0) {
    return {};
  }
  return buffer;
}

std::string GamerscoreText(int score) { return std::to_string(score) + " G"; }

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
}

GuideDialog::~GuideDialog() = default;

void GuideDialog::OnClose() {
  GuestInputGate::OnMenuHidden();
  if (actions_.on_closed) {
    actions_.on_closed();
  }
}

bool GuideDialog::HasAchievements() const { return !achievements_.empty(); }

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
  std::stable_sort(achievements_.begin(), achievements_.end(),
                   [](const AchievementRow& a, const AchievementRow& b) {
                     return a.unlocked && !b.unlocked;
                   });
}

void GuideDialog::BuildEntries() {
  entries_.clear();
  entries_.push_back({"Resume Game", "", nullptr, true, Page::kRoot});
  if (HasAchievements()) {
    Entry entry;
    entry.label = "Achievements";
    entry.hint = std::to_string(unlocked_count_) + " of " +
                 std::to_string(achievements_.size()) + " - " + GamerscoreText(earned_gamerscore_);
    entry.closes_guide = false;
    entry.opens = Page::kAchievements;
    entries_.push_back(std::move(entry));
  }
  if (actions_.open_settings) {
    entries_.push_back({"Settings", "Resolution, frame rate, display", actions_.open_settings, true,
                        Page::kRoot});
  }
  if (actions_.open_controls) {
    entries_.push_back({"Controls", "", actions_.open_controls, true, Page::kRoot});
  }
  entries_.push_back({"Exit Game", "", nullptr, false, Page::kExitConfirmation});
}

void GuideDialog::ShowAchievements() {
  if (HasAchievements()) {
    page_ = Page::kAchievements;
  }
}

rex::ui::ImmediateTexture* GuideDialog::Artwork(const std::string& name) {
  return resources_ ? resources_->Get(name) : nullptr;
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
  if (Pressed({ImGuiKey_DownArrow, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown},
                 true)) {
    selection = (selection + 1) % count;
  }
  if (Pressed({ImGuiKey_UpArrow, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp}, true)) {
    selection = (selection + count - 1) % count;
  }
  if (page_rows > 0 &&
      Pressed({ImGuiKey_PageDown, ImGuiKey_GamepadR1, ImGuiKey_RightArrow}, true)) {
    selection = std::min(selection + page_rows, count - 1);
  }
  if (page_rows > 0 &&
      Pressed({ImGuiKey_PageUp, ImGuiKey_GamepadL1, ImGuiKey_LeftArrow}, true)) {
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
  draw_list->AddRectFilled(row_min, ImVec2(row_min.x + 4.0f, row_max.y), theme_.selection_bar,
                           theme_.rounding);
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

float GuideDialog::DrawFooterHint(ImDrawList* draw_list, float x, float y, const char* letter,
                                  ImU32 fallback, const char* label) {
  DrawButtonGlyph(draw_list, ImVec2(x, y), letter, fallback);
  draw_list->AddText(ImVec2(x + 20.0f, y - ImGui::GetFontSize() * 0.5f), theme_.text_dim, label);
  return x + 20.0f + ImGui::CalcTextSize(label).x + 34.0f;
}

void GuideDialog::DrawHeader(ImDrawList* draw_list, ImVec2 top_left, float width,
                             const char* title, const std::string& subtitle) {
  const float height = theme_.header_height;
  if (theme_.blades) {
    // A blade is lit along its top edge rather than banded.
    draw_list->AddRectFilledMultiColor(top_left, ImVec2(top_left.x + width, top_left.y + height),
                                       theme_.header_band, theme_.header_band, IM_COL32(0, 0, 0, 0),
                                       IM_COL32(0, 0, 0, 0));
  } else {
    draw_list->AddRectFilled(top_left, ImVec2(top_left.x + width, top_left.y + 4.0f),
                             theme_.header_band);
  }

  const ImVec2 badge(top_left.x + theme_.padding + 18.0f, top_left.y + height * 0.5f);
  if (rex::ui::ImmediateTexture* logo = Artwork("livelogo.png")) {
    const float half_height = 18.0f;
    const float aspect = logo->height > 0 ? static_cast<float>(logo->width) /
                                                static_cast<float>(logo->height)
                                          : 1.0f;
    draw_list->AddImage(reinterpret_cast<ImTextureID>(logo),
                        ImVec2(badge.x - half_height * aspect, badge.y - half_height),
                        ImVec2(badge.x + half_height * aspect, badge.y + half_height));
  } else {
    draw_list->AddCircleFilled(badge, 18.0f, theme_.accent, 32);
    draw_list->AddCircle(badge, 18.0f, IM_COL32(255, 255, 255, 70), 32, 2.0f);
    const ImVec2 size = ImGui::CalcTextSize("X");
    draw_list->AddText(ImVec2(badge.x - size.x * 0.5f, badge.y - size.y * 0.5f),
                       IM_COL32(255, 255, 255, 235), "X");
  }

  const float text_x = badge.x + 40.0f;
  TextScaled(draw_list, ImVec2(text_x, top_left.y + height * 0.5f - ImGui::GetFontSize() * 1.1f),
             theme_.header_text, title, 1.5f);
  if (!subtitle.empty()) {
    Text(draw_list, ImVec2(text_x, top_left.y + height * 0.5f + 6.0f), theme_.text_dim,
         Trim(subtitle, width - (text_x - top_left.x) - theme_.padding - 150.0f));
  }

  // Where a console puts the signed-in player, with their score for this title.
  const std::string gamertag = REXCVAR_GET(recomp_gamertag);
  if (!gamertag.empty()) {
    const ImVec2 size = ImGui::CalcTextSize(gamertag.c_str());
    Text(draw_list,
         ImVec2(top_left.x + width - theme_.padding - size.x,
                top_left.y + height * 0.5f - ImGui::GetFontSize() - 2.0f),
         theme_.text, gamertag);
  }
  if (HasAchievements()) {
    const std::string score = GamerscoreText(earned_gamerscore_);
    const ImVec2 size = ImGui::CalcTextSize(score.c_str());
    Text(draw_list,
         ImVec2(top_left.x + width - theme_.padding - size.x, top_left.y + height * 0.5f + 4.0f),
         theme_.accent, score);
  }

  draw_list->AddLine(ImVec2(top_left.x, top_left.y + height),
                     ImVec2(top_left.x + width, top_left.y + height), theme_.separator);
}

void GuideDialog::DrawEntries(ImDrawList* draw_list, ImVec2 top_left, float width) {
  for (size_t i = 0; i < entries_.size(); ++i) {
    const bool selected = static_cast<int>(i) == selected_;
    const ImVec2 row_min(top_left.x, top_left.y + theme_.entry_height * static_cast<float>(i));
    const ImVec2 row_max(top_left.x + width, row_min.y + theme_.entry_height);
    DrawRowBackground(draw_list, row_min, row_max, selected);

    const float text_x = row_min.x + theme_.padding + 12.0f;
    const ImU32 color = selected ? theme_.text_selected : theme_.text;
    const Entry& entry = entries_[i];
    if (entry.hint.empty()) {
      Text(draw_list,
           ImVec2(text_x, row_min.y + theme_.entry_height * 0.5f - ImGui::GetFontSize() * 0.5f),
           color, entry.label);
    } else {
      Text(draw_list, ImVec2(text_x, row_min.y + 11.0f), color, entry.label);
      Text(draw_list, ImVec2(text_x, row_min.y + 31.0f),
           selected ? color : theme_.text_dim, entry.hint);
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
  Text(draw_list, ImVec2(top_left.x + theme_.padding + 12.0f, top_left.y + 10.0f), theme_.text,
       summary);

  const float bar_y = top_left.y + kSummaryHeight - 16.0f;
  const float bar_width = width - (theme_.padding + 12.0f) * 2.0f;
  const float fraction = achievements_.empty()
                             ? 0.0f
                             : static_cast<float>(unlocked_count_) /
                                   static_cast<float>(achievements_.size());
  const ImVec2 bar_min(top_left.x + theme_.padding + 12.0f, bar_y);
  draw_list->AddRectFilled(bar_min, ImVec2(bar_min.x + bar_width, bar_min.y + 6.0f),
                           theme_.separator, 3.0f);
  draw_list->AddRectFilled(bar_min, ImVec2(bar_min.x + bar_width * fraction, bar_min.y + 6.0f),
                           theme_.accent, 3.0f);

  // Keep the selection on screen.
  achievement_scroll_ = std::clamp(achievement_scroll_,
                                   std::max(0, achievement_selected_ - visible_rows + 1),
                                   std::max(0, achievement_selected_));
  const int last = std::min(achievement_scroll_ + visible_rows,
                            static_cast<int>(achievements_.size()));

  const float list_top = top_left.y + kSummaryHeight;
  for (int index = achievement_scroll_; index < last; ++index) {
    const AchievementRow& row = achievements_[static_cast<size_t>(index)];
    const float row_y = list_top + kAchievementRowHeight *
                                       static_cast<float>(index - achievement_scroll_);
    const bool selected = index == achievement_selected_;
    const ImVec2 row_min(top_left.x + 8.0f, row_y);
    const ImVec2 row_max(top_left.x + width - 8.0f, row_y + kAchievementRowHeight - 4.0f);
    DrawRowBackground(draw_list, row_min, row_max, selected);

    // Icon: the title's own, or the console's placeholder for one not yet won.
    const ImVec2 icon_min(row_min.x + 14.0f,
                          row_y + (kAchievementRowHeight - kAchievementIconSize) * 0.5f - 2.0f);
    const ImVec2 icon_max(icon_min.x + kAchievementIconSize,
                          icon_min.y + kAchievementIconSize);
    rex::ui::ImmediateTexture* icon = icons_ ? icons_->GetIcon(row.info) : nullptr;
    if (!icon) {
      const bool secret = !row.unlocked && row.info.description.empty() &&
                          row.info.unachieved_description.empty();
      icon = Artwork(secret ? "secretAchievement.png" : "unearnedAchievement.png");
    }
    if (icon) {
      const ImU32 tint = row.unlocked ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 110);
      draw_list->AddImage(reinterpret_cast<ImTextureID>(icon), icon_min, icon_max, ImVec2(0, 0),
                          ImVec2(1, 1), tint);
    } else {
      draw_list->AddRectFilled(icon_min, icon_max,
                               row.unlocked ? theme_.accent : theme_.separator, 4.0f);
    }

    const float text_x = icon_max.x + 16.0f;
    const std::string score = GamerscoreText(static_cast<int>(row.info.gamerscore));
    const std::string date = row.unlocked ? FormatUnlockDate(row.unlocked_at) : std::string();
    const float right_width = std::max(ImGui::CalcTextSize(score.c_str()).x,
                                       date.empty() ? 0.0f : ImGui::CalcTextSize(date.c_str()).x);
    const float text_width = row_max.x - text_x - right_width - 28.0f;

    const ImU32 title_color = selected ? theme_.text_selected
                                       : (row.unlocked ? theme_.text : theme_.text_dim);
    Text(draw_list, ImVec2(text_x, row_y + 14.0f), title_color,
         Trim(row.info.label, text_width));
    const std::string& description =
        row.unlocked || row.info.unachieved_description.empty() ? row.info.description
                                                                : row.info.unachieved_description;
    Text(draw_list, ImVec2(text_x, row_y + 38.0f),
         selected ? theme_.text_selected : theme_.text_dim, Trim(description, text_width));

    const float right_x = row_max.x - 14.0f - right_width;
    Text(draw_list, ImVec2(right_x, row_y + 14.0f),
         selected ? theme_.text_selected : theme_.accent, score);
    if (!date.empty()) {
      Text(draw_list, ImVec2(right_x, row_y + 38.0f),
           selected ? theme_.text_selected : theme_.text_dim, date);
    }
  }

  // A scrollbar, so a long list reads as one.
  if (static_cast<int>(achievements_.size()) > visible_rows) {
    const float track_top = list_top;
    const float track_height = kAchievementRowHeight * static_cast<float>(visible_rows);
    const float thumb_height = track_height * static_cast<float>(visible_rows) /
                               static_cast<float>(achievements_.size());
    const float thumb_top = track_top + track_height *
                                            static_cast<float>(achievement_scroll_) /
                                            static_cast<float>(achievements_.size());
    const float x = top_left.x + width - 6.0f;
    draw_list->AddRectFilled(ImVec2(x, track_top), ImVec2(x + 3.0f, track_top + track_height),
                             theme_.separator, 2.0f);
    draw_list->AddRectFilled(ImVec2(x, thumb_top), ImVec2(x + 3.0f, thumb_top + thumb_height),
                             theme_.accent, 2.0f);
  }
}

void GuideDialog::DrawFooter(ImDrawList* draw_list, ImVec2 bottom_left, float width) {
  draw_list->AddLine(bottom_left, ImVec2(bottom_left.x + width, bottom_left.y), theme_.separator);
  const float y = bottom_left.y + theme_.footer_height * 0.5f;
  float x = bottom_left.x + theme_.padding + 11.0f;
  if (page_ != Page::kAchievements) {
    x = DrawFooterHint(draw_list, x, y, "A", theme_.accent, "Select");
  }
  DrawFooterHint(draw_list, x, y, "B", theme_.button_b,
                 page_ == Page::kRoot ? "Close" : "Back");

  const char* chord = "View + Menu";
  const ImVec2 size = ImGui::CalcTextSize(chord);
  draw_list->AddText(
      ImVec2(bottom_left.x + width - theme_.padding - size.x, y - ImGui::GetFontSize() * 0.5f),
      theme_.text_dim, chord);
}

void GuideDialog::DrawExitConfirmation(ImDrawList* draw_list, ImVec2 top_left, float width) {
  const std::string question = "Exit " + actions_.game_display_name + "?";
  Text(draw_list, ImVec2(top_left.x + theme_.padding + 12.0f, top_left.y + 8.0f), theme_.text,
       question);
  Text(draw_list, ImVec2(top_left.x + theme_.padding + 12.0f, top_left.y + 34.0f), theme_.text_dim,
       "Unsaved progress will be lost.");

  static const char* kChoices[] = {"Exit Game", "Cancel"};
  for (int i = 0; i < 2; ++i) {
    const bool selected = i == exit_choice_;
    const ImVec2 row_min(top_left.x, top_left.y + 74.0f + theme_.entry_height *
                                                              static_cast<float>(i));
    const ImVec2 row_max(top_left.x + width, row_min.y + theme_.entry_height);
    DrawRowBackground(draw_list, row_min, row_max, selected);
    Text(draw_list,
         ImVec2(row_min.x + theme_.padding + 12.0f,
                row_min.y + theme_.entry_height * 0.5f - ImGui::GetFontSize() * 0.5f),
         selected ? theme_.text_selected : theme_.text, kChoices[i]);
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
  const char* title = "Xbox Guide";
  std::string subtitle = actions_.game_display_name;
  switch (page_) {
    case Page::kRoot:
      body_height = theme_.entry_height * static_cast<float>(entries_.size());
      break;
    case Page::kAchievements:
      panel_width = std::min(kAchievementsWidth, io.DisplaySize.x - 80.0f);
      body_height = kSummaryHeight + kAchievementRowHeight * static_cast<float>(visible_rows);
      title = "Achievements";
      break;
    case Page::kExitConfirmation:
      body_height = 74.0f + theme_.entry_height * 2.0f;
      break;
  }
  const float panel_height = theme_.header_height + body_height + theme_.footer_height;
  const ImVec2 panel_pos((io.DisplaySize.x - panel_width) * 0.5f,
                         (io.DisplaySize.y - panel_height) * 0.5f);
  const ImVec2 panel_end(panel_pos.x + panel_width, panel_pos.y + panel_height);

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
  if (theme_.panel_top == theme_.panel_bottom) {
    draw_list->AddRectFilled(panel_pos, panel_end, theme_.panel_top, theme_.rounding);
  } else {
    draw_list->AddRectFilledMultiColor(panel_pos, panel_end, theme_.panel_top, theme_.panel_top,
                                       theme_.panel_bottom, theme_.panel_bottom);
  }
  draw_list->AddRect(panel_pos, panel_end, theme_.border, theme_.rounding);

  DrawHeader(draw_list, panel_pos, panel_width, title, subtitle);
  const ImVec2 body_pos(panel_pos.x, panel_pos.y + theme_.header_height);
  switch (page_) {
    case Page::kRoot:
      DrawEntries(draw_list, body_pos, panel_width);
      break;
    case Page::kAchievements:
      DrawAchievements(draw_list, body_pos, panel_width, visible_rows);
      break;
    case Page::kExitConfirmation:
      DrawExitConfirmation(draw_list, body_pos, panel_width);
      break;
  }
  DrawFooter(draw_list, ImVec2(panel_pos.x, panel_pos.y + theme_.header_height + body_height),
             panel_width);
  ImGui::End();

  // The controller state the guide sees on its first frames is whatever was
  // held when it opened - the chord that opened it, or a pad resting off
  // centre. Take a reading, then start listening.
  if (frames_drawn_ < 2) {
    if (++frames_drawn_ == 2) {
      ArmInput();
    }
    return;
  }

  const bool back = Pressed({ImGuiKey_Escape, ImGuiKey_GamepadFaceRight}, false);

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

  const bool chosen = HandleInput(selected_, static_cast<int>(entries_.size()), 0);
  if (back) {
    Close();
    return;
  }
  if (!chosen) {
    return;
  }
  const Entry& entry = entries_[static_cast<size_t>(selected_)];
  if (!entry.closes_guide) {
    page_ = entry.opens;
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
