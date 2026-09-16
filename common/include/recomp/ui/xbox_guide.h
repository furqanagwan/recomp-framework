#pragma once

#include <functional>
#include <string_view>
#include <string>

#include <rex/kernel/xam/system_ui.h>

namespace rex {
class Runtime;
namespace system {
class AchievementManager;
}  // namespace system
namespace ui {
class ImGuiDrawer;
}  // namespace ui
}  // namespace rex

namespace recomp {

class GuideDialog;

// The Xbox 360 compatibility Guide: the shell between the host and the
// recompiled game, in the place the console's dashboard Guide occupies.
//
// Two routes open it and both end here:
//   - the player pressing View + Menu on a controller, and
//   - the game asking for a dashboard screen (XamShowGuideUI and friends).
//
// The host's own shell is not ours to replace: on Windows the Xbox button
// belongs to Game Bar, and the Xbox PC app owns launching. This guide owns only
// what the 360 game needs.
//
// While it is open the game keeps running and rendering behind it, but stops
// receiving controller input, and the title is told system UI is up (XN_SYS_UI)
// so it pauses itself the way it would on a console. Opening never blocks the
// guest thread that asked for it.
class XboxGuide {
 public:
  struct Actions {
    std::string game_display_name;
    // Opens the settings screen at a section ("video", "controls") or the first
    // one when empty.
    std::function<void(std::string)> open_settings;
    std::function<void()> exit_game;
    // The title's achievements, which the guide shows on its own screen.
    rex::system::AchievementManager* achievements = nullptr;
    // For achievement icons that live in the title's XDBF.
    rex::Runtime* runtime = nullptr;
    // Runs a callback on the UI thread: a game can ask for the guide from any
    // of its threads.
    std::function<void(std::function<void()>)> on_ui_thread;
  };

  void Install(rex::ui::ImGuiDrawer* drawer, Actions actions);
  void Uninstall();

  // The reason names what asked for the guide, for the log: a chord, a title's
  // request, a bind.
  void Open(std::string_view reason = "a request");
  void Close();
  void Toggle();
  bool IsOpen() const { return menu_ != nullptr; }

  // The screen a game asked for. Anything the guide has no screen of its own
  // for opens the guide, which is what a console shows for most of them.
  void Show(rex::kernel::xam::SystemUi ui);

 private:
  void ScheduleDebugOpen();

  rex::ui::ImGuiDrawer* drawer_ = nullptr;
  Actions actions_;
  GuideDialog* menu_ = nullptr;
  bool system_ui_active_ = false;
};

}  // namespace recomp
