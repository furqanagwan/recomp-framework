#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>

#include <rex/ui/imgui_dialog.h>

namespace recomp {

struct SettingsContext {
  std::filesystem::path settings_file;
  std::filesystem::path game_data_root;
  std::filesystem::path user_data_root;
  std::filesystem::path dlc_folder;
  bool portable = false;
  std::function<void(bool)> apply_fullscreen;
  std::function<void()> on_closed;
};

class SettingsDialog final : public rex::ui::ImGuiDialog {
 public:
  SettingsDialog(rex::ui::ImGuiDrawer* drawer, SettingsContext context);

  void RequestClose() { close_requested_ = true; }

 protected:
  void OnDraw(ImGuiIO& io) override;
  void OnClose() override;

 private:
  void DrawVideoTab();
  void DrawControlsTab();
  void DrawGameFilesTab();
  bool DrawFooter();
  bool HasPendingRestartChanges() const;

  SettingsContext context_;
  std::map<std::string, std::string> values_at_open_;
  std::string status_;
  bool close_requested_ = false;
};

}
