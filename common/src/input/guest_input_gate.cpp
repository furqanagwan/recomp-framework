#include "recomp/input/controller_menu_watcher.h"

#include <chrono>

#include <rex/input/input_system.h>
#include <rex/system/xtypes.h>

namespace recomp {

namespace {

using rex::X_RESULT;

constexpr uint32_t kPrimaryUser = 0;
constexpr uint8_t kTriggerReleasedThreshold = 30;

using Clock = std::chrono::steady_clock;

constexpr uint16_t kChordButtons =
    rex::input::X_INPUT_GAMEPAD_BACK | rex::input::X_INPUT_GAMEPAD_START;
// Long enough for the game to notice a tap it would otherwise have missed.
constexpr auto kReplayDuration = std::chrono::milliseconds(100);

std::atomic<rex::input::InputSystem*> g_input_system{nullptr};
std::atomic<int> g_visible_menus{0};
std::atomic<bool> g_awaiting_button_release{false};
std::atomic<bool> g_holding_chord_buttons{false};
std::atomic<uint16_t> g_replay_buttons{0};
std::atomic<Clock::time_point> g_replay_until{Clock::time_point{}};
thread_local bool t_reading_for_menu = false;

bool IsControllerIdle(const rex::input::X_INPUT_GAMEPAD& gamepad) {
  return uint16_t(gamepad.buttons) == 0 && gamepad.left_trigger < kTriggerReleasedThreshold &&
         gamepad.right_trigger < kTriggerReleasedThreshold;
}

}  // namespace

void GuestInputGate::Install(rex::input::InputSystem* input) {
  g_input_system = input;
  if (!input) {
    return;
  }
  input->SetActiveCallback([]() {
    return t_reading_for_menu || (g_visible_menus.load() == 0 && !g_awaiting_button_release.load());
  });
  input->SetStateFilter([](uint32_t, rex::input::X_INPUT_STATE& state) {
    if (t_reading_for_menu) {
      return;  // the watcher needs to see the buttons it is holding back
    }
    uint16_t buttons = static_cast<uint16_t>(state.gamepad.buttons);
    if (g_holding_chord_buttons.load(std::memory_order_acquire)) {
      buttons &= static_cast<uint16_t>(~kChordButtons);
    }
    const uint16_t replay = g_replay_buttons.load(std::memory_order_acquire);
    if (replay) {
      if (Clock::now() < g_replay_until.load(std::memory_order_acquire)) {
        buttons |= replay;
      } else {
        g_replay_buttons.store(0, std::memory_order_release);
      }
    }
    state.gamepad.buttons = buttons;
  });
}

void GuestInputGate::HoldChordButtons(bool hold) {
  g_holding_chord_buttons.store(hold, std::memory_order_release);
}

void GuestInputGate::ReplayButtons(uint16_t buttons) {
  if (!buttons) {
    return;
  }
  g_replay_until.store(Clock::now() + kReplayDuration, std::memory_order_release);
  g_replay_buttons.store(buttons, std::memory_order_release);
}

void GuestInputGate::Uninstall() {
  if (auto* input = g_input_system.load()) {
    input->SetStateFilter(nullptr);
  }
  g_holding_chord_buttons.store(false, std::memory_order_release);
  g_replay_buttons.store(0, std::memory_order_release);
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

bool GuestInputGate::ReadBatteryForMenu(rex::input::X_INPUT_BATTERY_INFORMATION& battery) {
  auto* input = g_input_system.load();
  if (!input) {
    return false;
  }
  rex::input::X_INPUT_BATTERY_INFORMATION read{};
  t_reading_for_menu = true;
  const auto result = input->GetBatteryInformation(kPrimaryUser, 0, &read);
  t_reading_for_menu = false;
  if (result != X_ERROR_SUCCESS ||
      read.type == rex::input::X_INPUT_BATTERY_TYPE_DISCONNECTED) {
    return false;
  }
  battery = read;
  return true;
}

}  // namespace recomp
