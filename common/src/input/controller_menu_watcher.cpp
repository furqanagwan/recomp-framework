#include "recomp/input/controller_menu_watcher.h"

#include <chrono>
#include <optional>

#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/ui/windowed_app_context.h>

REXCVAR_DEFINE_BOOL(recomp_guide_button_opens_guide, false, "Recomp",
                    "Open the compatibility guide with the Xbox button. Off by default: on "
                    "Windows that button belongs to Game Bar, and the guide has its own "
                    "chord (View + Menu).")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace recomp {

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kPollInterval = std::chrono::milliseconds(8);
// How long a single View or Menu press is held back waiting for the other.
// Long enough to press both, short enough not to feel like a delay.
constexpr auto kChordPressWindow = std::chrono::milliseconds(180);
constexpr uint16_t kChordButtons =
    rex::input::X_INPUT_GAMEPAD_BACK | rex::input::X_INPUT_GAMEPAD_START;

// Detects View + Menu without the game seeing either button first.
//
// While one of them is down and the chord is still possible, both are held back
// from the game. The chord opens the guide and the buttons stay swallowed; a
// press that turns out not to be part of it is handed to the game instead, so a
// quick tap of Menu still pauses.
class ViewMenuChord {
 public:
  enum class Result { kNothing, kOpenGuide };

  Result Update(uint16_t buttons, Clock::time_point now) {
    const uint16_t chord_buttons = buttons & kChordButtons;
    const bool both_down = chord_buttons == kChordButtons;

    if (!chord_buttons) {
      // Everything released: a held-back press the chord never used is the
      // player's, so let the game have it.
      if (holding_) {
        StopHolding();
        if (!consumed_ && held_buttons_) {
          GuestInputGate::ReplayButtons(held_buttons_);
        }
      }
      consumed_ = false;
      held_buttons_ = 0;
      return Result::kNothing;
    }

    if (!holding_ && !consumed_) {
      holding_ = true;
      held_since_ = now;
      GuestInputGate::HoldChordButtons(true);
    }
    held_buttons_ |= chord_buttons;

    if (both_down && holding_) {
      // The chord: neither button reaches the game.
      StopHolding();
      consumed_ = true;
      held_buttons_ = 0;
      return Result::kOpenGuide;
    }

    // One button alone for longer than the window is not a chord; the game
    // takes over from here while it stays down.
    if (holding_ && now - held_since_ > kChordPressWindow) {
      StopHolding();
      consumed_ = true;
      held_buttons_ = 0;
    }
    return Result::kNothing;
  }

  void Reset() {
    if (holding_) {
      StopHolding();
    }
    consumed_ = false;
    held_buttons_ = 0;
  }

 private:
  void StopHolding() {
    holding_ = false;
    GuestInputGate::HoldChordButtons(false);
  }

  bool holding_ = false;
  // True once this press has been dealt with: the guide took it, or the game
  // now owns the button.
  bool consumed_ = false;
  uint16_t held_buttons_ = 0;
  Clock::time_point held_since_{};
};

// The Xbox button, which this framework normally leaves to the host shell.
class GuideButtonPress {
 public:
  bool Pressed(uint16_t buttons) {
    const bool down = buttons & rex::input::X_INPUT_GAMEPAD_GUIDE;
    const bool pressed = down && !was_down_;
    was_down_ = down;
    return pressed;
  }

 private:
  bool was_down_ = false;
};

}  // namespace

ControllerMenuWatcher::~ControllerMenuWatcher() {
  Stop();
}

void ControllerMenuWatcher::Start(rex::input::InputSystem* input,
                                  rex::ui::WindowedAppContext* app_context,
                                  std::function<void()> open_menu) {
  Stop();
  if (!input || !app_context) {
    return;
  }
  input_ = input;
  app_context_ = app_context;
  open_menu_ = std::move(open_menu);
  GuestInputGate::Install(input);
  running_ = true;
  thread_ = std::thread([this] { WatchLoop(); });
}

void ControllerMenuWatcher::Stop() {
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
  GuestInputGate::Uninstall();
}

void ControllerMenuWatcher::WatchLoop() {
  ViewMenuChord view_menu_chord;
  GuideButtonPress guide_button;
  while (running_) {
    rex::input::X_INPUT_GAMEPAD gamepad{};
    const bool connected = GuestInputGate::ReadControllerForMenu(gamepad);
    const uint16_t buttons = connected ? uint16_t(gamepad.buttons) : 0;
    GuestInputGate::ReleaseIfControllerIdle(gamepad);

    bool open = false;
    if (GuestInputGate::AnyMenuVisible()) {
      // The guide reads the controller itself; nothing to hold back.
      view_menu_chord.Reset();
    } else {
      open = view_menu_chord.Update(buttons, Clock::now()) == ViewMenuChord::Result::kOpenGuide;
    }
    // The Xbox button belongs to the host shell (Game Bar on Windows), so it is
    // left alone unless this machine has no such shell to hand it to.
    if (guide_button.Pressed(buttons) && REXCVAR_GET(recomp_guide_button_opens_guide) &&
        !GuestInputGate::AnyMenuVisible()) {
      open = true;
    }
    if (open) {
      app_context_->CallInUIThreadDeferred(open_menu_);
    }
    std::this_thread::sleep_for(kPollInterval);
  }
}

}  // namespace recomp
