#include "recomp/ui/guide_dialog.h"

#include <algorithm>

#include <rex/cvar.h>
#include "recomp/settings/user_settings_store.h"
#include "recomp/render/native_renderer.h"
#include "guide_theme.h"

namespace recomp {

void GuideDialog::ShowSettings(std::string section) {
  tab_ = GuideTab::kSettings;
  page_ = Page::kSettings;
  settings_section_ = std::move(section);
  setting_selected_ = 0;
  BuildSettings();
}

void GuideDialog::BuildSettings() {
  using Choices = std::vector<std::pair<std::string, std::string>>;
  const Choices toggle = {{"false", "Off"}, {"true", "On"}};
  setting_rows_.clear();
  if (settings_section_ == "controls") {
    setting_rows_ = {
        {"Controller backend", "input_backend",
         {{"gameinput", "GameInput"}, {"sdl", "SDL"}, {"xinput", "XInput"}},
         "GameInput is Microsoft's current API and reports charge; it needs the GameInput "
         "runtime, and falls back to XInput without it. SDL also supports PlayStation, "
         "Switch and other SDL-compatible pads."},
        {"Shared controllers", "recomp_shared_controllers", toggle,
         "On: all controllers control player 1. Turn off for local multiplayer."},
        {"Guide button", "recomp_guide_button_opens_guide", toggle,
         "Also open the guide with the controller Guide button. View + Menu always works."},
    };
  } else if (settings_section_ == "game_files") {
    setting_rows_ = {
        {"Game files", "", {}, actions_.settings.game_data_root.string()},
        {"Saves, cache & settings", "", {}, actions_.settings.user_data_root.string()},
        {"Downloadable Content", "", {},
         actions_.settings.dlc_folder.string() +
             "\nPackages in this folder are installed when the game starts.",
         Page::kDlc},
        {"Portable mode", "", {}, actions_.settings.portable ? "Portable mode is on." :
          "Create portable.txt next to the executable to keep saves beside the game."},
    };
  } else {
    settings_section_ = "video";
    setting_rows_ = {
        {"Fullscreen", "fullscreen", toggle, "Display the game fullscreen or in a window."},
        {"VSync", "vsync", toggle, "Game speed is tied to 60 Hz."},
        {"Render resolution", "resolution_scale",
         {{"1", "1x (720p)"}, {"2", "2x (1440p)"}, {"3", "3x (2160p)"}, {"4", "4x (2880p)"}},
         "Higher scaling increases image detail and GPU load."},
        {"Anti-aliasing", "swap_post_effect",
         {{"none", "Off"}, {"fxaa", "FXAA"}, {"fxaa_extreme", "FXAA (extreme)"}},
         "Smooth jagged edges in the final image."},
        {"Render targets", "render_target_path_d3d12",
         {{"rov", "Accurate (ROV)"}, {"rtv", "Fast (RTV)"}, {"", "SDK default"}},
         "Fast render targets can leave some scenes black after pausing."},
    };
    if (NativeRenderer::IsAvailable()) {
      setting_rows_.push_back(
          {"Native renderer", "recomp_native_renderer_enabled", toggle,
           "F8 also switches between native and emulated rendering while playing."});
    }
  }
  setting_rows_.push_back({"Save settings", "", {}, "Save these settings for the next launch."});
}

void GuideDialog::SaveSettings() {
  setting_acted_ = true;
  settings_status_ = UserSettingsStore(actions_.settings.settings_file).Save()
                         ? "Settings saved." : "Could not save settings.";
}

void GuideDialog::ChangeSetting(int direction) {
  setting_acted_ = true;
  if (setting_rows_.empty()) return;
  const auto& row = setting_rows_[static_cast<size_t>(setting_selected_)];
  if (row.opens != Page::kRoot) {
    // This row is a way in, not a value: the content list lives behind the one
    // that names the DLC folder, so B from it comes back here.
    settings_return_ = true;
    page_ = row.opens;
    return;
  }
  if (setting_selected_ == static_cast<int>(setting_rows_.size()) - 1) {
    SaveSettings();
    return;
  }
  if (row.choices.empty()) return;
  const std::string current = rex::cvar::GetFlagByName(row.cvar);
  const auto found = std::find_if(row.choices.begin(), row.choices.end(),
                                [&](const auto& choice) { return choice.first == current; });
  const int count = static_cast<int>(row.choices.size());
  const int index = found == row.choices.end() ? (direction > 0 ? -1 : 0)
                                              : static_cast<int>(found - row.choices.begin());
  const std::string& value = row.choices[static_cast<size_t>((index + direction + count) % count)].first;
  rex::cvar::SetFlagByName(row.cvar, value);
  if (row.cvar == "recomp_native_renderer_enabled") {
    NativeRenderer::SetEnabled(value == "true");
  }
  // The runtime only reports the Guide button while its pass-through is on, so
  // opening the guide with it needs both.
  if (row.cvar == "recomp_guide_button_opens_guide") {
    rex::cvar::SetFlagByName("guide_button", value);
  }
  if (rex::cvar::GetFlagByName(row.cvar) != value) {
    settings_status_ = "This setting could not be changed.";
    return;
  }
  if (row.cvar == "fullscreen" && actions_.settings.apply_fullscreen) {
    actions_.settings.apply_fullscreen(value == "true");
  }
  settings_status_ = "Changed. Press X to save.";
}

void GuideDialog::DrawSettings(ImDrawList* draw, ImVec2 top_left, float width) {
  const char* title = settings_section_ == "controls" ? "Controls" :
                      settings_section_ == "game_files" ? "Game Files" : "Scaling & Display";
  draw->PushClipRect(top_left, ImVec2(top_left.x + width, top_left.y + theme_.entry_height * 9), true);
  draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.3f,
                ImVec2(top_left.x + theme_.padding, top_left.y + 12), theme_.text, title);
  for (size_t i = 0; i < setting_rows_.size(); ++i) {
    const auto& row = setting_rows_[i];
    const bool selected = static_cast<int>(i) == setting_selected_;
    const ImVec2 low(top_left.x, top_left.y + (i + 1) * theme_.entry_height);
    const ImVec2 high(low.x + width, low.y + theme_.entry_height);
    DrawRowBackground(draw, low, high, selected);
    if (!selected) draw->AddLine(low, ImVec2(high.x, low.y), theme_.separator);
    std::string value;
    if (!row.cvar.empty()) {
      const auto current = rex::cvar::GetFlagByName(row.cvar);
      value = current;
      for (const auto& choice : row.choices) if (choice.first == current) value = choice.second;
      if (selected) value = "< " + value + " >";
    }
    const ImU32 color = selected ? theme_.text_selected : theme_.text;
    const float text_y = low.y + (theme_.entry_height - ImGui::GetFontSize()) * 0.5f;
    draw->AddText(ImVec2(low.x + theme_.padding, text_y), color, row.label.c_str());
    const float value_width = ImGui::CalcTextSize(value.c_str()).x;
    draw->AddText(ImVec2(high.x - theme_.padding - value_width, text_y), color, value.c_str());
  }
  const float detail_y = top_left.y + theme_.entry_height * 7 + 8;
  if (!setting_rows_.empty()) {
    const auto& description = setting_rows_[static_cast<size_t>(setting_selected_)].description;
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                  ImVec2(top_left.x + theme_.padding, detail_y), theme_.text_dim,
                  description.c_str(), nullptr, width - 2 * theme_.padding);
  }
  std::string status = settings_status_;
  for (const auto& [cvar, initial] : settings_at_open_) {
    if (rex::cvar::GetFlagByName(cvar) != initial) {
      status += " Restart required.";
      break;
    }
  }
  draw->AddText(ImVec2(top_left.x + theme_.padding, top_left.y + theme_.entry_height * 8 + 18),
                theme_.text, status.c_str());
  draw->PopClipRect();
}

}  // namespace recomp
