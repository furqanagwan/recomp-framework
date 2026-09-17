#include "recomp/ui/xbox_guide.h"

#include <chrono>
#include <thread>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "recomp/ui/guide_dialog.h"
#include "recomp/ui/virtual_keyboard_dialog.h"

REXCVAR_DEFINE_DOUBLE(recomp_guide_open_after_seconds, 0.0, "Recomp",
                      "Open the compatibility guide this many seconds after the game starts. "
                      "For trying the guide without a controller, and for screenshots in "
                      "scripted runs. 0 disables it.")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_STRING(recomp_guide_open_page, "root", "Recomp",
                      "Which screen recomp_guide_open_after_seconds opens on: root, "
                      "achievements, or settings.")
    .allowed({"root", "achievements", "settings"});

REXCVAR_DEFINE_DOUBLE(recomp_keyboard_open_after_seconds, 0.0, "Recomp",
                      "Open the on-screen keyboard this many seconds after the game starts, "
                      "as if the title had asked for a name. For screenshots in scripted runs. "
                      "0 disables it.")
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
  rex::kernel::xam::SetKeyboardUiHandler(
      [this](const rex::kernel::xam::KeyboardUiRequest& request,
             rex::kernel::xam::KeyboardUiResult done) {
        // On a kernel worker thread, which waits for `done`.
        if (!actions_.on_ui_thread) {
          return false;
        }
        actions_.on_ui_thread([this, request, done] { ShowKeyboard(request, done); });
        return true;
      });
  ScheduleDebugOpen();
}

void XboxGuide::ShowKeyboard(const rex::kernel::xam::KeyboardUiRequest& request,
                             rex::kernel::xam::KeyboardUiResult done) {
  if (keyboard_ || !drawer_) {
    // A console shows one keyboard at a time.
    done(std::nullopt);
    return;
  }
  // The keyboard takes the controller; a guide already open makes way for it.
  Close();
  REXLOG_INFO("Keyboard: opening for user {} ({} characters)", request.user_index,
              request.max_length);
  keyboard_ = new VirtualKeyboardDialog(drawer_, request,
                                        [this, done](std::optional<std::u16string> text) {
                                          keyboard_ = nullptr;
                                          done(std::move(text));
                                        });
}

void XboxGuide::ScheduleDebugOpen() {
  const double keyboard_seconds = REXCVAR_GET(recomp_keyboard_open_after_seconds);
  if (keyboard_seconds > 0.0 && actions_.on_ui_thread) {
    REXLOG_INFO("Keyboard: opening in {:.1f} s (recomp_keyboard_open_after_seconds)",
                keyboard_seconds);
    std::thread([this, keyboard_seconds] {
      std::this_thread::sleep_for(std::chrono::duration<double>(keyboard_seconds));
      rex::kernel::xam::KeyboardUiRequest request;
      request.title = u"Enter your name:";
      request.default_text = u"Player";
      request.max_length = 16;
      actions_.on_ui_thread([this, request] {
        ShowKeyboard(request, [](std::optional<std::u16string> text) {
          REXLOG_INFO("Keyboard: recomp_keyboard_open_after_seconds {}",
                      text ? "entered text" : "cancelled");
        });
      });
    }).detach();
  }

  const double seconds = REXCVAR_GET(recomp_guide_open_after_seconds);
  if (seconds <= 0.0 || !actions_.on_ui_thread) {
    return;
  }
  REXLOG_INFO("Guide: opening in {:.1f} s (recomp_guide_open_after_seconds)", seconds);
  std::thread([this, seconds] {
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    actions_.on_ui_thread([this] {
      Open("recomp_guide_open_after_seconds");
      const std::string page = REXCVAR_GET(recomp_guide_open_page);
      if (menu_ && page == "achievements") {
        menu_->ShowAchievements();
      } else if (menu_ && page == "settings") {
        menu_->ShowSettings("video");
      }
    });
  }).detach();
}

void XboxGuide::Uninstall() {
  rex::kernel::xam::SetSystemUiHandler(nullptr);
  rex::kernel::xam::SetKeyboardUiHandler(nullptr);
  if (system_ui_active_) {
    rex::kernel::xam::SetSystemUiActive(false);
    system_ui_active_ = false;
  }
  menu_ = nullptr;
  drawer_ = nullptr;
}

void XboxGuide::Open(std::string_view reason) {
  if (menu_ || !drawer_) {
    return;
  }
  if (keyboard_) {
    // The keyboard owns the controller until the player finishes typing.
    REXLOG_INFO("Guide: not opening for {} while the keyboard is up", reason);
    return;
  }
  REXLOG_INFO("Guide: opening for {}", reason);
  menu_ = new GuideDialog(
      drawer_, GuideActions{
                   .game_display_name = actions_.game_display_name,
                   .settings = actions_.settings,
                   .exit_game = actions_.exit_game,
                   .on_closed =
                       [this] {
                         menu_ = nullptr;
                         if (system_ui_active_) {
                           rex::kernel::xam::SetSystemUiActive(false);
                           system_ui_active_ = false;
                         }
                       },
                   .achievements = actions_.achievements,
                   .runtime = actions_.runtime,
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
    Open("a bind");
  }
}

void XboxGuide::Show(SystemUi ui) {
  Open(rex::kernel::xam::SystemUiName(ui));
  if (ui == SystemUi::kAchievements && menu_) {
    // The only screen the guide has of its own beyond the root one.
    menu_->ShowAchievements();
  }
}

}  // namespace recomp
