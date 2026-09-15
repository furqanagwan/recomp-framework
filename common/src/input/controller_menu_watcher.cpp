#include "recomp/input/controller_menu_watcher.h"

#include <chrono>
#include <optional>

#include <rex/input/input_system.h>
#include <rex/ui/windowed_app_context.h>

namespace recomp {

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kPollInterval = std::chrono::milliseconds(16);
constexpr auto kChordPressWindow = std::chrono::milliseconds(250);

class ButtonPressTimer {
 public:
  void Update(bool down, Clock::time_point now) {
    if (!down) {
      pressed_at_.reset();
    } else if (!pressed_at_) {
      pressed_at_ = now;
    }
  }

  bool down() const { return pressed_at_.has_value(); }
  Clock::time_point pressed_at() const { return *pressed_at_; }

 private:
  std::optional<Clock::time_point> pressed_at_;
};

class ViewMenuChord {
 public:
  bool PressedTogether(uint16_t buttons, Clock::time_point now) {
    view_.Update(buttons & rex::input::X_INPUT_GAMEPAD_BACK, now);
    menu_.Update(buttons & rex::input::X_INPUT_GAMEPAD_START, now);
    if (!view_.down() || !menu_.down()) {
      triggered_ = false;
      return false;
    }
    if (triggered_) {
      return false;
    }
    const auto gap = view_.pressed_at() > menu_.pressed_at() ? view_.pressed_at() - menu_.pressed_at()
                                                             : menu_.pressed_at() - view_.pressed_at();
    triggered_ = gap <= kChordPressWindow;
    return triggered_;
  }

 private:
  ButtonPressTimer view_;
  ButtonPressTimer menu_;
  bool triggered_ = false;
};

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

}

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

    const bool chord = view_menu_chord.PressedTogether(buttons, Clock::now());
    const bool guide = guide_button.Pressed(buttons);
    if ((chord || guide) && !GuestInputGate::AnyMenuVisible()) {
      app_context_->CallInUIThreadDeferred(open_menu_);
    }
    std::this_thread::sleep_for(kPollInterval);
  }
}

}
