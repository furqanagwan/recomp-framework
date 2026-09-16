#include "recomp/ui/guide_dialog.h"

#include <algorithm>

#include <imgui.h>

#include "recomp/input/controller_menu_watcher.h"
#include "recomp/input/imgui_gamepad_bridge.h"

namespace recomp {

namespace {

// The 360 Guide's proportions: a tall panel a little left of centre, entries in
// one column, green where the console used green.
constexpr float kPanelWidth = 560.0f;
constexpr float kHeaderHeight = 92.0f;
constexpr float kEntryHeight = 54.0f;
constexpr float kFooterHeight = 56.0f;
constexpr float kPanelPadding = 22.0f;

const ImU32 kPanelColor = IM_COL32(12, 12, 12, 240);
const ImU32 kPanelBorder = IM_COL32(48, 48, 48, 255);
const ImU32 kXboxGreen = IM_COL32(16, 124, 16, 255);
const ImU32 kXboxGreenDim = IM_COL32(16, 124, 16, 90);
const ImU32 kTextColor = IM_COL32(235, 235, 235, 255);
const ImU32 kTextDim = IM_COL32(150, 150, 150, 255);
const ImU32 kSelectedText = IM_COL32(255, 255, 255, 255);
const ImU32 kButtonB = IM_COL32(170, 40, 40, 255);

bool PressedAny(std::initializer_list<ImGuiKey> keys, bool repeat) {
  for (ImGuiKey key : keys) {
    if (ImGui::IsKeyPressed(key, repeat)) {
      return true;
    }
  }
  return false;
}

void DrawGlyph(ImDrawList* draw_list, ImVec2 center, ImU32 color, const char* letter) {
  draw_list->AddCircleFilled(center, 11.0f, color, 24);
  const ImVec2 size = ImGui::CalcTextSize(letter);
  draw_list->AddText(ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f),
                     IM_COL32(255, 255, 255, 255), letter);
}

}  // namespace

GuideDialog::GuideDialog(rex::ui::ImGuiDrawer* drawer, GuideActions actions)
    : ImGuiDialog(drawer), actions_(std::move(actions)) {
  GuestInputGate::OnMenuShown();
  BuildEntries();
}

void GuideDialog::OnClose() {
  GuestInputGate::OnMenuHidden();
  if (actions_.on_closed) {
    actions_.on_closed();
  }
}

void GuideDialog::BuildEntries() {
  entries_.clear();
  entries_.push_back({"Resume Game", nullptr, true});
  if (actions_.has_achievements && actions_.open_achievements) {
    entries_.push_back({"Achievements", actions_.open_achievements, true});
  }
  if (actions_.open_settings) {
    entries_.push_back({"Settings", actions_.open_settings, true});
  }
  if (actions_.open_controls) {
    entries_.push_back({"Controls", actions_.open_controls, true});
  }
  entries_.push_back({"Exit Game", nullptr, false});
}

bool GuideDialog::HandleInput(int entry_count) {
  if (PressedAny({ImGuiKey_DownArrow, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown}, true)) {
    selected_ = (selected_ + 1) % entry_count;
  }
  if (PressedAny({ImGuiKey_UpArrow, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp}, true)) {
    selected_ = (selected_ + entry_count - 1) % entry_count;
  }
  selected_ = std::clamp(selected_, 0, entry_count - 1);
  return PressedAny({ImGuiKey_Enter, ImGuiKey_Space, ImGuiKey_GamepadFaceDown}, false);
}

void GuideDialog::DrawHeader(ImDrawList* draw_list, ImVec2 top_left, float width, float height) {
  // Green band and the sphere the console puts in the corner.
  draw_list->AddRectFilled(top_left, ImVec2(top_left.x + width, top_left.y + 4.0f), kXboxGreen);
  const ImVec2 sphere(top_left.x + kPanelPadding + 16.0f, top_left.y + height * 0.5f);
  draw_list->AddCircleFilled(sphere, 18.0f, kXboxGreen, 32);
  draw_list->AddCircle(sphere, 18.0f, IM_COL32(255, 255, 255, 60), 32, 2.0f);
  draw_list->AddText(ImVec2(sphere.x - 6.0f, sphere.y - ImGui::GetFontSize() * 0.5f),
                     IM_COL32(255, 255, 255, 230), "X");

  const float text_x = sphere.x + 34.0f;
  ImGui::SetWindowFontScale(1.5f);
  draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.5f,
                     ImVec2(text_x, top_left.y + height * 0.5f - ImGui::GetFontSize() * 1.1f),
                     kTextColor, "Xbox Guide");
  ImGui::SetWindowFontScale(1.0f);
  draw_list->AddText(ImVec2(text_x, top_left.y + height * 0.5f + 6.0f), kTextDim,
                     actions_.game_display_name.c_str());
  draw_list->AddLine(ImVec2(top_left.x, top_left.y + height),
                     ImVec2(top_left.x + width, top_left.y + height), kPanelBorder);
}

void GuideDialog::DrawEntries(ImDrawList* draw_list, ImVec2 top_left, float width) {
  for (size_t i = 0; i < entries_.size(); ++i) {
    const bool selected = static_cast<int>(i) == selected_;
    const ImVec2 row_min(top_left.x, top_left.y + kEntryHeight * static_cast<float>(i));
    const ImVec2 row_max(top_left.x + width, row_min.y + kEntryHeight);
    if (selected) {
      draw_list->AddRectFilled(row_min, row_max, kXboxGreenDim);
      draw_list->AddRectFilled(row_min, ImVec2(row_min.x + 4.0f, row_max.y), kXboxGreen);
    }
    draw_list->AddText(
        ImVec2(row_min.x + kPanelPadding + 12.0f, row_min.y + kEntryHeight * 0.5f -
                                                      ImGui::GetFontSize() * 0.5f),
        selected ? kSelectedText : kTextDim, entries_[i].label.c_str());
  }
}

void GuideDialog::DrawFooter(ImDrawList* draw_list, ImVec2 bottom_left, float width) {
  draw_list->AddLine(bottom_left, ImVec2(bottom_left.x + width, bottom_left.y), kPanelBorder);
  const float y = bottom_left.y + kFooterHeight * 0.5f;
  float x = bottom_left.x + kPanelPadding + 11.0f;
  DrawGlyph(draw_list, ImVec2(x, y), kXboxGreen, "A");
  draw_list->AddText(ImVec2(x + 20.0f, y - ImGui::GetFontSize() * 0.5f), kTextDim, "Select");
  x += 110.0f;
  DrawGlyph(draw_list, ImVec2(x, y), kButtonB, "B");
  draw_list->AddText(ImVec2(x + 20.0f, y - ImGui::GetFontSize() * 0.5f), kTextDim, "Back");
  const char* chord = "View + Menu";
  const ImVec2 size = ImGui::CalcTextSize(chord);
  draw_list->AddText(ImVec2(bottom_left.x + width - kPanelPadding - size.x,
                            y - ImGui::GetFontSize() * 0.5f),
                     kTextDim, chord);
}

void GuideDialog::DrawExitConfirmation(ImDrawList* draw_list, ImVec2 top_left, float width) {
  const std::string question = "Exit " + actions_.game_display_name + "?";
  draw_list->AddText(ImVec2(top_left.x + kPanelPadding + 12.0f, top_left.y + 8.0f), kTextColor,
                     question.c_str());
  draw_list->AddText(ImVec2(top_left.x + kPanelPadding + 12.0f, top_left.y + 34.0f), kTextDim,
                     "Unsaved progress will be lost.");

  static const char* kChoices[] = {"Exit Game", "Cancel"};
  for (int i = 0; i < 2; ++i) {
    const bool selected = i == exit_choice_;
    const ImVec2 row_min(top_left.x, top_left.y + 74.0f + kEntryHeight * static_cast<float>(i));
    const ImVec2 row_max(top_left.x + width, row_min.y + kEntryHeight);
    if (selected) {
      draw_list->AddRectFilled(row_min, row_max, kXboxGreenDim);
      draw_list->AddRectFilled(row_min, ImVec2(row_min.x + 4.0f, row_max.y), kXboxGreen);
    }
    draw_list->AddText(ImVec2(row_min.x + kPanelPadding + 12.0f,
                              row_min.y + kEntryHeight * 0.5f - ImGui::GetFontSize() * 0.5f),
                       selected ? kSelectedText : kTextDim, kChoices[i]);
  }
}

void GuideDialog::OnDraw(ImGuiIO& io) {
  if (close_requested_) {
    Close();
    return;
  }
  ImGuiGamepadBridge::FeedPrimaryController(io);

  const float panel_width = std::min(kPanelWidth, io.DisplaySize.x - 80.0f);
  const int rows = confirming_exit_ ? 2 : static_cast<int>(entries_.size());
  const float body_height = confirming_exit_ ? 74.0f + kEntryHeight * 2.0f
                                             : kEntryHeight * static_cast<float>(rows);
  const float panel_height = kHeaderHeight + body_height + kFooterHeight;
  const ImVec2 panel_pos((io.DisplaySize.x - panel_width) * 0.5f,
                         (io.DisplaySize.y - panel_height) * 0.5f);

  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("##recomp_guide", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoInputs);
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  // The game keeps rendering behind the guide, dimmed as on a console.
  draw_list->AddRectFilled(ImVec2(0.0f, 0.0f), io.DisplaySize, IM_COL32(0, 0, 0, 150));
  draw_list->AddRectFilled(panel_pos, ImVec2(panel_pos.x + panel_width, panel_pos.y + panel_height),
                           kPanelColor);
  draw_list->AddRect(panel_pos, ImVec2(panel_pos.x + panel_width, panel_pos.y + panel_height),
                     kPanelBorder);

  DrawHeader(draw_list, panel_pos, panel_width, kHeaderHeight);
  const ImVec2 body_pos(panel_pos.x, panel_pos.y + kHeaderHeight);
  if (confirming_exit_) {
    DrawExitConfirmation(draw_list, body_pos, panel_width);
  } else {
    DrawEntries(draw_list, body_pos, panel_width);
  }
  DrawFooter(draw_list, ImVec2(panel_pos.x, panel_pos.y + kHeaderHeight + body_height),
             panel_width);
  ImGui::End();

  const bool back = PressedAny({ImGuiKey_Escape, ImGuiKey_GamepadFaceRight}, false);
  if (confirming_exit_) {
    int choice = exit_choice_;
    std::swap(selected_, choice);
    const bool chosen = HandleInput(2);
    std::swap(selected_, choice);
    exit_choice_ = choice;
    if (back) {
      confirming_exit_ = false;
      return;
    }
    if (chosen) {
      if (exit_choice_ == 0 && actions_.exit_game) {
        actions_.exit_game();
      } else {
        confirming_exit_ = false;
      }
    }
    return;
  }

  const bool chosen = HandleInput(static_cast<int>(entries_.size()));
  if (back) {
    Close();
    return;
  }
  if (!chosen) {
    return;
  }
  const Entry& entry = entries_[static_cast<size_t>(selected_)];
  if (!entry.closes_guide) {
    confirming_exit_ = true;
    exit_choice_ = 1;
    return;
  }
  if (entry.activate) {
    entry.activate();
  }
  Close();
}

}  // namespace recomp
