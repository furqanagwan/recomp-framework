#include "recomp/ui/settings_dialog.h"

#include <array>
#include <span>
#include <string>
#include <utility>

#include <imgui.h>

#include <rex/cvar.h>

#include "recomp/input/controller_menu_watcher.h"
#include "recomp/input/imgui_gamepad_bridge.h"
#include "recomp/settings/user_settings_store.h"
#include "recomp/ui/dialog_layout.h"

namespace recomp {

namespace {

using Choice = std::pair<const char*, const char*>;

constexpr float kPanelWidth = 620.0f;
constexpr float kFooterButtonWidth = 120.0f;

constexpr std::array<Choice, 4> kResolutionScales = {{
    {"1", "1x (720p)"}, {"2", "2x (1440p)"}, {"3", "3x (2160p)"}, {"4", "4x (2880p)"}}};
constexpr std::array<Choice, 3> kAntiAliasing = {{
    {"none", "Off"}, {"fxaa", "FXAA"}, {"fxaa_extreme", "FXAA (extreme)"}}};
constexpr std::array<Choice, 3> kD3D12RenderTargets = {{
    {"rov", "Accurate (ROV)"}, {"rtv", "Fast (RTV)"}, {"", "SDK default"}}};
constexpr std::array<Choice, 3> kVulkanRenderTargets = {{
    {"fsi", "Accurate (FSI)"}, {"fbo", "Fast"}, {"", "SDK default"}}};
constexpr std::array<Choice, 2> kInputBackends = {{
    {"sdl", "SDL (Xbox, PlayStation, Switch, Steam Deck)"}, {"xinput", "XInput (Xbox only)"}}};

bool CheckboxForCvar(const char* label, std::string_view cvar) {
  bool value = rex::cvar::Query<bool>(cvar);
  if (!ImGui::Checkbox(label, &value)) {
    return false;
  }
  rex::cvar::SetFlagByName(cvar, value ? "true" : "false");
  return true;
}

void ComboForCvar(const char* label, std::string_view cvar, std::span<const Choice> choices) {
  const std::string current = rex::cvar::GetFlagByName(cvar);
  const char* preview = current.c_str();
  for (const auto& [value, text] : choices) {
    if (current == value) {
      preview = text;
    }
  }
  if (!ImGui::BeginCombo(label, preview)) {
    return;
  }
  for (const auto& [value, text] : choices) {
    const bool selected = current == value;
    if (ImGui::Selectable(text, selected) && !selected) {
      rex::cvar::SetFlagByName(cvar, value);
    }
  }
  ImGui::EndCombo();
}

}

SettingsDialog::SettingsDialog(rex::ui::ImGuiDrawer* drawer, SettingsContext context)
    : ImGuiDialog(drawer), context_(std::move(context)) {
  GuestInputGate::OnMenuShown();
  for (const auto& setting : UserSettingsStore::Settings()) {
    if (setting.requires_restart) {
      values_at_open_[std::string(setting.cvar)] = rex::cvar::GetFlagByName(setting.cvar);
    }
  }
}

void SettingsDialog::OnClose() {
  GuestInputGate::OnMenuHidden();
  if (context_.on_closed) {
    context_.on_closed();
  }
}

void SettingsDialog::OnDraw(ImGuiIO& io) {
  if (close_requested_) {
    Close();
    return;
  }
  ImGuiGamepadBridge::FeedPrimaryController(io);
  DialogLayout::DrawBackdrop("##recomp_settings_backdrop", io, 1.0f);
  bool open = true;
  DialogLayout::BeginCenteredPanel("Settings", io, kPanelWidth, &open);

  if (ImGui::BeginTabBar("##recomp_settings_tabs")) {
    if (ImGui::BeginTabItem("Video")) {
      DrawVideoTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Controls")) {
      DrawControlsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Game Files")) {
      DrawGameFilesTab();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  const bool close = DrawFooter();
  ImGui::End();
  if (!open || close) {
    Close();
  }
}

void SettingsDialog::DrawVideoTab() {
  ImGui::Spacing();
  if (CheckboxForCvar("Fullscreen", "fullscreen") && context_.apply_fullscreen) {
    context_.apply_fullscreen(rex::cvar::Query<bool>("fullscreen"));
  }
  CheckboxForCvar("VSync", "vsync");
  ImGui::SameLine();
  ImGui::TextDisabled("(game speed is tied to 60 Hz)");
  ComboForCvar("Render resolution", "resolution_scale", kResolutionScales);
  ComboForCvar("Anti-aliasing", "swap_post_effect", kAntiAliasing);
#if defined(_WIN32)
  ComboForCvar("Render targets", "render_target_path_d3d12", kD3D12RenderTargets);
#else
  ComboForCvar("Render targets", "render_target_path_vulkan", kVulkanRenderTargets);
#endif
  ImGui::TextDisabled("Fast render targets can leave some scenes black after pausing.");
}

void SettingsDialog::DrawControlsTab() {
  ImGui::Spacing();
  ComboForCvar("Controller backend", "input_backend", kInputBackends);
  CheckboxForCvar("All controllers control player 1", "recomp_shared_controllers");
  ImGui::TextDisabled("Turn off for local multiplayer with one controller per player.");
  ImGui::Spacing();
  ImGui::TextDisabled("System menu: press View + Menu together, or Esc.");
  CheckboxForCvar("Guide button also opens the system menu", "guide_button");
  ImGui::TextWrapped("Leave off on Windows, Xbox mode and Steam Deck, where the system uses that button.");
}

void SettingsDialog::DrawGameFilesTab() {
  ImGui::Spacing();
  ImGui::TextDisabled("Game files");
  ImGui::TextWrapped("%s", context_.game_data_root.string().c_str());
  ImGui::Spacing();
  ImGui::TextDisabled("Saves, cache and settings");
  ImGui::TextWrapped("%s", context_.user_data_root.string().c_str());
  ImGui::Spacing();
  ImGui::TextDisabled("DLC");
  ImGui::TextWrapped("%s", context_.dlc_folder.string().c_str());
  ImGui::TextWrapped("Put downloadable content packages here; they are installed the next time the game starts.");
  ImGui::Spacing();
  ImGui::TextWrapped("%s", context_.portable
                               ? "Portable mode is on."
                               : "Create an empty portable.txt next to the executable to keep saves beside it.");
  ImGui::TextWrapped("To reinstall, delete the game files folder and start the game again.");
}

bool SettingsDialog::DrawFooter() {
  ImGui::Spacing();
  ImGui::Separator();
  if (HasPendingRestartChanges()) {
    ImGui::TextWrapped("Some changes apply after restarting the game.");
  }
  if (!status_.empty()) {
    ImGui::TextDisabled("%s", status_.c_str());
  }
  if (ImGui::Button("Save", ImVec2(kFooterButtonWidth, 0.0f))) {
    status_ = UserSettingsStore(context_.settings_file).Save() ? "Saved." : "Could not save settings.";
  }
  ImGui::SameLine();
  const bool close_clicked = ImGui::Button("Close", ImVec2(kFooterButtonWidth, 0.0f));
  ImGui::SameLine();
  ImGui::TextDisabled("B / Esc close");
  const bool back = !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) && DialogLayout::BackPressed();
  return close_clicked || back;
}

bool SettingsDialog::HasPendingRestartChanges() const {
  for (const auto& [cvar, value] : values_at_open_) {
    if (rex::cvar::GetFlagByName(cvar) != value) {
      return true;
    }
  }
  return false;
}

}
