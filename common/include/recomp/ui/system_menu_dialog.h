#pragma once

#include <functional>
#include <string>

#include <rex/ui/imgui_dialog.h>

namespace recomp {

struct SystemMenuActions {
  std::string game_display_name;
  std::function<void()> open_settings;
  std::function<void()> exit_game;
  std::function<void()> on_closed;
};

class SystemMenuDialog final : public rex::ui::ImGuiDialog {
 public:
  SystemMenuDialog(rex::ui::ImGuiDrawer* drawer, SystemMenuActions actions);

  void RequestClose() { close_requested_ = true; }

 protected:
  void OnDraw(ImGuiIO& io) override;
  void OnClose() override;

 private:
  bool DrawMainOptions();
  void DrawExitConfirmation();

  SystemMenuActions actions_;
  bool close_requested_ = false;
  bool confirming_exit_ = false;
  bool focus_first_option_ = true;
};

}
