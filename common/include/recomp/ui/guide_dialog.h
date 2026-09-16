#pragma once

#include <functional>
#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>

namespace recomp {

struct GuideActions {
  std::string game_display_name;
  bool has_achievements = false;
  std::function<void()> open_settings;
  std::function<void()> open_controls;
  std::function<void()> open_achievements;
  std::function<void()> exit_game;
  std::function<void()> on_closed;
};

// The compatibility Guide's screen, in the shape of the Xbox 360 Guide that a
// Series X shows over a backward-compatible title: a dark panel with the green
// header, one column of entries, and A/B along the bottom.
class GuideDialog final : public rex::ui::ImGuiDialog {
 public:
  GuideDialog(rex::ui::ImGuiDrawer* drawer, GuideActions actions);

  void RequestClose() { close_requested_ = true; }

 protected:
  void OnDraw(ImGuiIO& io) override;
  void OnClose() override;

 private:
  struct Entry {
    std::string label;
    std::function<void()> activate;
    bool closes_guide = true;
  };

  void BuildEntries();
  // Moves the selection and reports whether an entry was chosen.
  bool HandleInput(int entry_count);
  void DrawHeader(ImDrawList* draw_list, ImVec2 top_left, float width, float height);
  void DrawEntries(ImDrawList* draw_list, ImVec2 top_left, float width);
  void DrawFooter(ImDrawList* draw_list, ImVec2 bottom_left, float width);
  void DrawExitConfirmation(ImDrawList* draw_list, ImVec2 top_left, float width);

  GuideActions actions_;
  std::vector<Entry> entries_;
  int selected_ = 0;
  bool close_requested_ = false;
  bool confirming_exit_ = false;
  int exit_choice_ = 1;  // Cancel, as on the console.
};

}  // namespace recomp
