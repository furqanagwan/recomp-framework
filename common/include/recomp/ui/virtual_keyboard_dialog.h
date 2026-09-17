#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>
#include <rex/kernel/xam/system_ui.h>
#include <rex/ui/imgui_dialog.h>

#include "recomp/ui/virtual_keyboard.h"

namespace recomp {

class GuideResources;
class HeldKeyMask;
struct GuideTheme;

// The keyboard a title opens to have the player type a name (XamShowKeyboardUI),
// laid out and animated as the console's keyboard scene is (see
// VirtualKeyboard), with its key sounds and button pictures when the guide's
// resources include vkmedia and Dash.Search's packages.
//
// A controller works it as on a console: the D-pad or stick moves between keys,
// A presses one, X deletes, Y adds a space, the bumpers move the cursor, LT and
// RT change the page, clicking the left stick toggles caps, START finishes and B
// backs out. A PC keyboard types straight into it, with Enter to finish and
// Escape to back out.
class VirtualKeyboardDialog final : public rex::ui::ImGuiDialog {
 public:
  using Done = std::function<void(std::optional<std::u16string> text)>;

  VirtualKeyboardDialog(rex::ui::ImGuiDrawer* drawer,
                        const rex::kernel::xam::KeyboardUiRequest& request, Done done);
  ~VirtualKeyboardDialog() override;

 protected:
  void OnDraw(ImGuiIO& io) override;
  void OnClose() override;

 private:
  void HandleInput(ImGuiIO& io);
  // Flashes a key the way the console's does while it is pressed.
  void Press(const VirtualKeyboard::Key& key);
  void PressKind(VirtualKeyboard::KeyKind kind);
  void Finish(std::optional<std::u16string> text);
  void DrawScene(ImDrawList* draw_list, const ImGuiIO& io);


  const GuideTheme& theme_;
  std::unique_ptr<GuideResources> resources_;
  VirtualKeyboard keyboard_;
  std::string title_;
  std::string description_;
  Done done_;
  std::optional<std::u16string> result_;
  bool finished_ = false;
  double finished_at_ = 0.0;

  // Where the text field's cursor was drawn, for an input method's candidate window.
  ImVec2 caret_position_;
  float caret_height_ = 0.0f;
  const VirtualKeyboard::Key* pressed_key_ = nullptr;
  double pressed_at_ = 0.0;
  int frames_drawn_ = 0;
  double opened_at_ = -1.0;
  std::unique_ptr<HeldKeyMask> held_keys_;
};

}  // namespace recomp
