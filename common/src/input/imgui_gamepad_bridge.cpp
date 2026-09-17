#include "recomp/input/imgui_gamepad_bridge.h"

#include <algorithm>
#include <array>

#include <imgui.h>

#include "recomp/input/controller_menu_watcher.h"

namespace recomp {

namespace {

constexpr float kStickDeadzone = 8000.0f;
constexpr float kStickMaximum = 32767.0f;
// XINPUT_GAMEPAD_TRIGGER_THRESHOLD.
constexpr uint8_t kTriggerThreshold = 30;

struct ButtonMapping {
  ImGuiKey key;
  uint16_t button;
};

constexpr std::array<ButtonMapping, 13> kButtonMappings = {{
    {ImGuiKey_GamepadDpadUp, rex::input::X_INPUT_GAMEPAD_DPAD_UP},
    {ImGuiKey_GamepadDpadDown, rex::input::X_INPUT_GAMEPAD_DPAD_DOWN},
    {ImGuiKey_GamepadDpadLeft, rex::input::X_INPUT_GAMEPAD_DPAD_LEFT},
    {ImGuiKey_GamepadDpadRight, rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT},
    {ImGuiKey_GamepadFaceDown, rex::input::X_INPUT_GAMEPAD_A},
    {ImGuiKey_GamepadFaceRight, rex::input::X_INPUT_GAMEPAD_B},
    {ImGuiKey_GamepadFaceLeft, rex::input::X_INPUT_GAMEPAD_X},
    {ImGuiKey_GamepadFaceUp, rex::input::X_INPUT_GAMEPAD_Y},
    {ImGuiKey_GamepadL1, rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER},
    {ImGuiKey_GamepadR1, rex::input::X_INPUT_GAMEPAD_RIGHT_SHOULDER},
    {ImGuiKey_GamepadStart, rex::input::X_INPUT_GAMEPAD_START},
    {ImGuiKey_GamepadBack, rex::input::X_INPUT_GAMEPAD_BACK},
    {ImGuiKey_GamepadL3, rex::input::X_INPUT_GAMEPAD_LEFT_THUMB},
}};

float NormalizedStickTravel(float value) {
  return std::clamp((value - kStickDeadzone) / (kStickMaximum - kStickDeadzone), 0.0f, 1.0f);
}

void FeedStickAxis(ImGuiIO& io, ImGuiKey negative, ImGuiKey positive, int16_t axis) {
  const float value = float(axis);
  io.AddKeyAnalogEvent(negative, value < -kStickDeadzone, NormalizedStickTravel(-value));
  io.AddKeyAnalogEvent(positive, value > kStickDeadzone, NormalizedStickTravel(value));
}

}  // namespace

void ImGuiGamepadBridge::FeedPrimaryController(ImGuiIO& io) {
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
  rex::input::X_INPUT_GAMEPAD gamepad{};
  if (!GuestInputGate::ReadControllerForMenu(gamepad)) {
    return;
  }
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  const uint16_t buttons = gamepad.buttons;
  for (const auto& mapping : kButtonMappings) {
    io.AddKeyEvent(mapping.key, (buttons & mapping.button) != 0);
  }
  FeedStickAxis(io, ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight,
                int16_t(gamepad.thumb_lx));
  FeedStickAxis(io, ImGuiKey_GamepadLStickDown, ImGuiKey_GamepadLStickUp,
                int16_t(gamepad.thumb_ly));
  io.AddKeyAnalogEvent(ImGuiKey_GamepadL2, gamepad.left_trigger > kTriggerThreshold,
                       gamepad.left_trigger / 255.0f);
  io.AddKeyAnalogEvent(ImGuiKey_GamepadR2, gamepad.right_trigger > kTriggerThreshold,
                       gamepad.right_trigger / 255.0f);
}

}  // namespace recomp
