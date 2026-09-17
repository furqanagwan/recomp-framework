#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>
#include <rex/kernel/xam/system_ui.h>
#include <rex/ui/imgui_dialog.h>

namespace recomp {

class GuideResources;
class HeldKeyMask;
struct GuideTheme;

// The message box a title shows with XamShowMessageBoxUI - a save that will not
// load, a device that went away, a question with two or three answers - drawn
// over the game in the guide skin's style.
//
// The console's own message box scene is in xam.xex, which a system update only
// patches, so this follows the guide apps' skin (Guide.AccountRecovery.xex's
// skin.xur): the #EBEBEB pane, Label_Body text, green focus, its A and B legend
// and its TransOpen and TransFrom fades. Up and down move between the buttons,
// A chooses one and B backs out.
class MessageBoxDialog final : public rex::ui::ImGuiDialog {
 public:
  using Done = std::function<void(std::optional<uint32_t> button)>;

  MessageBoxDialog(rex::ui::ImGuiDrawer* drawer,
                   const rex::kernel::xam::MessageBoxUiRequest& request, Done done);
  ~MessageBoxDialog() override;

 protected:
  void OnDraw(ImGuiIO& io) override;
  void OnClose() override;

 private:
  void HandleInput();
  void Finish(std::optional<uint32_t> button);
  void DrawScene(ImDrawList* draw_list, const ImGuiIO& io);

  const GuideTheme& theme_;
  std::unique_ptr<GuideResources> resources_;
  std::unique_ptr<HeldKeyMask> held_keys_;
  rex::kernel::xam::MessageBoxUiRequest::Icon icon_;
  std::string title_;
  std::string text_;
  std::vector<std::string> buttons_;
  int selected_ = 0;
  Done done_;
  std::optional<uint32_t> result_;
  bool finished_ = false;
  double finished_at_ = 0.0;
  double opened_at_ = -1.0;
  int frames_drawn_ = 0;
};

}  // namespace recomp
