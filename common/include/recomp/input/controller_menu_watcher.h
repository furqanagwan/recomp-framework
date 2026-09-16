#pragma once

#include <atomic>
#include <functional>
#include <thread>

#include <rex/input/input.h>

namespace rex::input {
class InputSystem;
}

namespace rex::ui {
class WindowedAppContext;
}

namespace recomp {

class ControllerMenuWatcher {
 public:
  ControllerMenuWatcher() = default;
  ControllerMenuWatcher(const ControllerMenuWatcher&) = delete;
  ControllerMenuWatcher& operator=(const ControllerMenuWatcher&) = delete;
  ~ControllerMenuWatcher();

  void Start(rex::input::InputSystem* input, rex::ui::WindowedAppContext* app_context,
             std::function<void()> open_menu);
  void Stop();

 private:
  void WatchLoop();

  rex::input::InputSystem* input_ = nullptr;
  rex::ui::WindowedAppContext* app_context_ = nullptr;
  std::function<void()> open_menu_;
  std::thread thread_;
  std::atomic<bool> running_{false};
};

class GuestInputGate {
 public:
  static void Install(rex::input::InputSystem* input);
  // Holds View and Menu back from the game while they might still be the
  // guide's chord, so the game does not react to Menu before the guide opens.
  static void HoldChordButtons(bool hold);
  // Hands the game a press of these buttons it never saw, for the moment a
  // held-back button turns out not to have been part of the chord.
  static void ReplayButtons(uint16_t buttons);
  static void Uninstall();
  static void OnMenuShown();
  static void OnMenuHidden();
  static bool AnyMenuVisible();
  static void ReleaseIfControllerIdle(const rex::input::X_INPUT_GAMEPAD& gamepad);
  static bool ReadControllerForMenu(rex::input::X_INPUT_GAMEPAD& gamepad);
};

}  // namespace recomp
