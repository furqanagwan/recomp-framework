#include "recomp/input/controller_menu_watcher.h"

#include <rex/input/input_system.h>
#include <rex/system/xtypes.h>

namespace recomp {

namespace {

using rex::X_RESULT;

constexpr uint32_t kPrimaryUser = 0;
constexpr uint8_t kTriggerReleasedThreshold = 30;

std::atomic<rex::input::InputSystem*> g_input_system{nullptr};
std::atomic<int> g_visible_menus{0};
std::atomic<bool> g_awaiting_button_release{false};
thread_local bool t_reading_for_menu = false;

bool IsControllerIdle(const rex::input::X_INPUT_GAMEPAD& gamepad) {
  return uint16_t(gamepad.buttons) == 0 && gamepad.left_trigger < kTriggerReleasedThreshold &&
         gamepad.right_trigger < kTriggerReleasedThreshold;
}

}

void GuestInputGate::Install(rex::input::InputSystem* input) {
  g_input_system = input;
  if (!input) {
    return;
  }
  input->SetActiveCallback([]() {
    return t_reading_for_menu ||
           (g_visible_menus.load() == 0 && !g_awaiting_button_release.load());
  });
}

void GuestInputGate::Uninstall() {
  g_input_system = nullptr;
}

void GuestInputGate::OnMenuShown() {
  ++g_visible_menus;
}

void GuestInputGate::OnMenuHidden() {
  if (--g_visible_menus == 0) {
    g_awaiting_button_release = true;
  }
}

bool GuestInputGate::AnyMenuVisible() {
  return g_visible_menus.load() > 0;
}

void GuestInputGate::ReleaseIfControllerIdle(const rex::input::X_INPUT_GAMEPAD& gamepad) {
  if (g_awaiting_button_release.load() && IsControllerIdle(gamepad)) {
    g_awaiting_button_release = false;
  }
}

bool GuestInputGate::ReadControllerForMenu(rex::input::X_INPUT_GAMEPAD& gamepad) {
  auto* input = g_input_system.load();
  if (!input) {
    return false;
  }
  rex::input::X_INPUT_STATE state{};
  t_reading_for_menu = true;
  const auto result = input->GetState(kPrimaryUser, &state);
  t_reading_for_menu = false;
  if (result != X_ERROR_SUCCESS) {
    return false;
  }
  gamepad = state.gamepad;
  return true;
}

}
