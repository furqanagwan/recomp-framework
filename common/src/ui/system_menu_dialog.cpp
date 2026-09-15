#include "recomp/ui/system_menu_dialog.h"

#include <imgui.h>

#include "recomp/input/controller_menu_watcher.h"
#include "recomp/input/imgui_gamepad_bridge.h"
#include "recomp/ui/dialog_layout.h"

namespace recomp {

namespace {

constexpr float kPanelWidth = 360.0f;
constexpr float kOptionHeight = 44.0f;
constexpr float kTitleScale = 1.6f;

bool MenuOption(const char* label) {
  return ImGui::Button(label, ImVec2(-1.0f, kOptionHeight));
}

}

SystemMenuDialog::SystemMenuDialog(rex::ui::ImGuiDrawer* drawer, SystemMenuActions actions)
    : ImGuiDialog(drawer), actions_(std::move(actions)) {
  GuestInputGate::OnMenuShown();
}

void SystemMenuDialog::OnClose() {
  GuestInputGate::OnMenuHidden();
  if (actions_.on_closed) {
    actions_.on_closed();
  }
}

void SystemMenuDialog::OnDraw(ImGuiIO& io) {
  if (close_requested_) {
    Close();
    return;
  }
  ImGuiGamepadBridge::FeedPrimaryController(io);
  DialogLayout::DrawBackdrop("##recomp_system_menu_backdrop", io, 0.75f);
  DialogLayout::BeginSidePanel("##recomp_system_menu", io, kPanelWidth);

  ImGui::Dummy(ImVec2(0.0f, io.DisplaySize.y * 0.12f));
  ImGui::SetWindowFontScale(kTitleScale);
  ImGui::TextUnformatted(actions_.game_display_name.c_str());
  ImGui::SetWindowFontScale(1.0f);
  ImGui::Separator();
  ImGui::Spacing();

  if (confirming_exit_) {
    DrawExitConfirmation();
    ImGui::End();
    return;
  }
  const bool close = DrawMainOptions();
  ImGui::End();
  if (close) {
    Close();
  }
}

bool SystemMenuDialog::DrawMainOptions() {
  if (focus_first_option_) {
    ImGui::SetWindowFocus();
    ImGui::SetKeyboardFocusHere();
    focus_first_option_ = false;
  }
  bool close = false;
  if (MenuOption("Resume")) {
    close = true;
  }
  if (MenuOption("Settings")) {
    close = true;
    actions_.open_settings();
  }
  if (MenuOption("Exit Game")) {
    confirming_exit_ = true;
    focus_first_option_ = true;
  }
  ImGui::Spacing();
  ImGui::TextDisabled("A select   B resume   View + Menu opens");
  return close || DialogLayout::BackPressed();
}

void SystemMenuDialog::DrawExitConfirmation() {
  ImGui::TextWrapped("Exit %s? Unsaved progress will be lost.", actions_.game_display_name.c_str());
  ImGui::Spacing();
  if (focus_first_option_) {
    ImGui::SetKeyboardFocusHere();
    focus_first_option_ = false;
  }
  if (MenuOption("Exit Game##confirm")) {
    Close();
    actions_.exit_game();
    return;
  }
  if (MenuOption("Cancel") || DialogLayout::BackPressed()) {
    confirming_exit_ = false;
    focus_first_option_ = true;
  }
}

}
