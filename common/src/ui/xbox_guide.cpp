#include "recomp/ui/xbox_guide.h"

#include <chrono>
#include <thread>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "recomp/ui/guide_dialog.h"

REXCVAR_DEFINE_DOUBLE(recomp_guide_open_after_seconds, 0.0, "Recomp",
                      "Open the compatibility guide this many seconds after the game starts. "
                      "For trying the guide without a controller, and for screenshots in "
                      "scripted runs. 0 disables it.")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace recomp {

using rex::kernel::xam::SystemUi;

void XboxGuide::Install(rex::ui::ImGuiDrawer* drawer, Actions actions) {
  drawer_ = drawer;
  actions_ = std::move(actions);
  rex::kernel::xam::SetSystemUiHandler([this](SystemUi ui, uint32_t) {
    // Called on the guest thread: hand the work to the UI thread and return, so
    // the title carries on while the guide opens.
    if (actions_.on_ui_thread) {
      actions_.on_ui_thread([this, ui] { Show(ui); });
      return true;
    }
    return false;
  });
  ScheduleDebugOpen();
}

void XboxGuide::ScheduleDebugOpen() {
  const double seconds = REXCVAR_GET(recomp_guide_open_after_seconds);
  if (seconds <= 0.0 || !actions_.on_ui_thread) {
    return;
  }
  REXLOG_INFO("Guide: opening in {:.1f} s (recomp_guide_open_after_seconds)", seconds);
  std::thread([this, seconds] {
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    actions_.on_ui_thread([this] { Open(); });
  }).detach();
}

void XboxGuide::Uninstall() {
  rex::kernel::xam::SetSystemUiHandler(nullptr);
  if (system_ui_active_) {
    rex::kernel::xam::SetSystemUiActive(false);
    system_ui_active_ = false;
  }
  menu_ = nullptr;
  drawer_ = nullptr;
}

void XboxGuide::Open() {
  if (menu_ || !drawer_) {
    return;
  }
  menu_ = new GuideDialog(
      drawer_, GuideActions{
                   .game_display_name = actions_.game_display_name,
                   .has_achievements = actions_.has_achievements,
                   .open_settings = [this] { actions_.open_settings(""); },
                   .open_controls = [this] { actions_.open_settings("controls"); },
                   .open_achievements = actions_.open_achievements,
                   .exit_game = actions_.exit_game,
                   .on_closed =
                       [this] {
                         menu_ = nullptr;
                         if (system_ui_active_) {
                           rex::kernel::xam::SetSystemUiActive(false);
                           system_ui_active_ = false;
                         }
                       },
               });
  // Tell the title system UI is up, as a console does while the Guide shows.
  rex::kernel::xam::SetSystemUiActive(true);
  system_ui_active_ = true;
}

void XboxGuide::Close() {
  if (menu_) {
    menu_->RequestClose();
  }
}

void XboxGuide::Toggle() {
  if (menu_) {
    Close();
  } else {
    Open();
  }
}

void XboxGuide::Show(SystemUi ui) {
  if (ui == SystemUi::kAchievements && actions_.has_achievements &&
      actions_.open_achievements) {
    actions_.open_achievements();
    return;
  }
  REXLOG_INFO("Guide: showing the guide for the {} screen",
              rex::kernel::xam::SystemUiName(ui));
  Open();
}

}  // namespace recomp
