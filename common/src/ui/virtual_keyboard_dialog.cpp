#include "recomp/ui/virtual_keyboard_dialog.h"

#include <algorithm>
#include <cmath>

#include <imgui_internal.h>

#include <rex/logging.h>
#include <rex/string.h>
#include <rex/ui/imgui_drawer.h>

#include "recomp/input/controller_menu_watcher.h"
#include "recomp/input/imgui_gamepad_bridge.h"
#include "recomp/ui/guide_resources.h"
#include "recomp/ui/guide_sounds.h"
#include "guide_scene.h"
#include "guide_theme.h"

namespace recomp {

namespace {

using namespace guide_scene;
using Key = VirtualKeyboard::Key;
using KeyKind = VirtualKeyboard::KeyKind;
using Page = VirtualKeyboard::Page;

// The console's keyboard is a HUD scene on the guide's 852 by 480 canvas
// (vkmedia's KeyboardMain.xur), with the keys in a 574 by 350 placeholder
// (KeyboardBase.xur). Positions below are in those units, as the scenes give
// them; kCanvasToReference maps them onto the guide's reference screen.
constexpr float kCanvasWidth = 852.0f;
constexpr float kCanvasHeight = 480.0f;
constexpr float kCanvasToReference = kReferenceWidth / kCanvasWidth;

// KeyboardMain: the header label and the keyboard's placeholder.
constexpr float kHeaderX = 156.0f;
constexpr float kHeaderY = 36.0f;
constexpr float kHeaderTextSize = 22.0f;
constexpr float kSceneX = 138.0f;
constexpr float kSceneY = 61.0f;
constexpr float kSceneWidth = 574.0f;
constexpr float kSceneHeight = 350.0f;

// KeyboardBase: the grey band behind the prompt and the text field
// (ShaderFigure), the prompt (XuiText1) and the field (FormattedText).
constexpr float kBandHeight = 148.0f;
constexpr ImU32 kBandTop = IM_COL32(0x97, 0x9D, 0xA1, 0xFF);
constexpr ImU32 kBandBottom = IM_COL32(0x69, 0x6E, 0x73, 0xFF);
constexpr float kPromptX = 21.0f;
constexpr float kPromptY = 14.0f;
constexpr float kPromptWidth = 533.0f;
constexpr float kPromptTextSize = 16.0f;
constexpr float kFieldX = 21.0f;
constexpr float kFieldY = 110.0f;
constexpr float kFieldWidth = 473.0f;
constexpr float kFieldHeight = 28.0f;
constexpr float kFieldTextSize = 16.0f;
// scr_Edit and its caret (evk_EditCaret): 3 wide, green, on and off each half
// second.
constexpr ImU32 kFieldFill = IM_COL32(0xEB, 0xEB, 0xEB, 0xFF);
constexpr ImU32 kFieldText = IM_COL32(0x0F, 0x0F, 0x0F, 0xEB);
constexpr float kCaretWidth = 3.0f;
constexpr double kCaretHalfPeriod = 0.5;

// The keys group sits at (18, 68) in the placeholder; its character keys are
// 32 by 25 on a 34 by 27 pitch from (100, 92), and the side keys 92 by 52 at
// x 3 and 443.
constexpr float kKeysX = 18.0f;
constexpr float kKeysY = 68.0f;
constexpr float kCharacterLeft = 100.0f;
constexpr float kCharacterTop = 92.0f;
constexpr float kPitchX = 34.0f;
constexpr float kPitchY = 27.0f;
constexpr float kKeyGap = 2.0f;
constexpr float kLeftColumnX = 3.0f;
constexpr float kRightColumnX = 443.0f;
constexpr float kSideKeyWidth = 92.0f;
constexpr float kCharacterTextSize = 16.0f;
constexpr float kLabelTextSize = 12.0f;
constexpr float kSideGlyphWidth = 55.0f;
constexpr float kSideGlyphHeight = 30.0f;
constexpr float kRowGlyphSize = 22.0f;

// btn_KbrdChar: an edge, a green highlight on focus that brightens while the
// key is pressed (frames 16 to 21 of its timeline, at 60 frames a second), and
// grey text that turns light on the highlight.
constexpr ImU32 kKeyEdge = IM_COL32(0xB4, 0xBA, 0xBE, 0xFF);
constexpr ImU32 kKeyText = IM_COL32(0x58, 0x60, 0x66, 0xFF);
constexpr ImU32 kKeyTextShadow = IM_COL32(0xEB, 0xEB, 0xEB, 0xFF);
constexpr ImU32 kKeyFocusText = IM_COL32(0xEB, 0xEB, 0xEB, 0xFF);
constexpr ImU32 kHighlight = IM_COL32(0x00, 0x8A, 0x00, 0xFF);
constexpr ImU32 kHighlightPressed = IM_COL32(0x1C, 0xB6, 0x1C, 0xFF);
constexpr double kPressSeconds = 5.0 / 60.0;

// The legend under the scene: KeyboardMain's LegendA, B, X and Y.
constexpr float kLegendY = 436.0f;
constexpr float kLegendGlyph = 20.0f;
constexpr float kLegendTextSize = 20.0f;
constexpr float kLegendGlyphGap = 7.0f;
constexpr float kLegendItemGap = 16.0f;
constexpr double kOpenSeconds = 0.22;

constexpr ImGuiKey kWatchedKeys[] = {
    ImGuiKey_Enter,           ImGuiKey_KeypadEnter,       ImGuiKey_Escape,
    ImGuiKey_Backspace,       ImGuiKey_Delete,            ImGuiKey_LeftArrow,
    ImGuiKey_RightArrow,      ImGuiKey_Home,              ImGuiKey_End,
    ImGuiKey_GamepadFaceDown, ImGuiKey_GamepadFaceRight,  ImGuiKey_GamepadFaceLeft,
    ImGuiKey_GamepadFaceUp,   ImGuiKey_GamepadDpadUp,     ImGuiKey_GamepadDpadDown,
    ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight,  ImGuiKey_GamepadLStickUp,
    ImGuiKey_GamepadLStickDown, ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight,
    ImGuiKey_GamepadL1,       ImGuiKey_GamepadR1,         ImGuiKey_GamepadL2,
    ImGuiKey_GamepadR2,       ImGuiKey_GamepadL3,         ImGuiKey_GamepadStart,
};

std::string Utf8(const std::u16string& text) { return rex::string::to_utf8(text); }

ImU32 Mix(ImU32 from, ImU32 to, float t) {
  const auto channel = [&](int shift) {
    const float a = static_cast<float>((from >> shift) & 0xFF);
    const float b = static_cast<float>((to >> shift) & 0xFF);
    return static_cast<ImU32>(Lerp(a, b, std::clamp(t, 0.0f, 1.0f))) << shift;
  };
  return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) | channel(IM_COL32_B_SHIFT) |
         channel(IM_COL32_A_SHIFT);
}

bool IsSideKey(const Key& key) {
  return key.column == 0 || key.column == VirtualKeyboard::kColumns - 1;
}

// A key's rectangle in the placeholder's units.
void KeyRect(const Key& key, float& x, float& y, float& width, float& height) {
  y = kKeysY + kCharacterTop + kPitchY * static_cast<float>(key.row);
  height = kPitchY * static_cast<float>(key.height) - kKeyGap;
  if (IsSideKey(key)) {
    x = kKeysX + (key.column == 0 ? kLeftColumnX : kRightColumnX);
    width = kSideKeyWidth;
  } else {
    x = kKeysX + kCharacterLeft + kPitchX * static_cast<float>(key.column - 1);
    width = kPitchX * static_cast<float>(key.width) - kKeyGap;
  }
}

std::string KeyLabel(const VirtualKeyboard& keyboard, const Key& key) {
  switch (key.kind) {
    case KeyKind::kCharacter:
      return Utf8(std::u16string(1, keyboard.CharacterFor(key)));
    case KeyKind::kCaps:
      return "Caps";
    case KeyKind::kSymbols:
      return keyboard.page() == Page::kSymbols ? "Letters" : "Symbols";
    case KeyKind::kAccents:
      return keyboard.page() == Page::kAccents ? "Letters" : "Accents";
    case KeyKind::kSpace:
      return "Space";
    case KeyKind::kBackspace:
      return "Backspace";
    case KeyKind::kDone:
      return "Done";
    case KeyKind::kCursorLeft:
    case KeyKind::kCursorRight:
      return "Cursor";
  }
  return {};
}

// The picture the console puts on a key (vkmedia), and what to draw without it.
struct KeyGlyph {
  const char* file;
  const char* fallback;
};

KeyGlyph GlyphFor(KeyKind kind) {
  switch (kind) {
    case KeyKind::kCursorLeft:
      return {"LB.png", "LB"};
    case KeyKind::kCursorRight:
      return {"RB.png", "RB"};
    case KeyKind::kSymbols:
      return {"LT.png", "LT"};
    case KeyKind::kAccents:
      return {"RT.png", "RT"};
    case KeyKind::kCaps:
      return {"Caps.png", "LS"};
    case KeyKind::kDone:
      return {"Done.png", "START"};
    case KeyKind::kBackspace:
      return {"btn_x.png", "X"};
    case KeyKind::kSpace:
      return {"btn_y.png", "Y"};
    case KeyKind::kCharacter:
      break;
  }
  return {nullptr, nullptr};
}

// A button without its picture: a small grey cap with its name.
void DrawCapGlyph(ImDrawList* draw_list, ImVec2 center, float height, const char* label,
                  ImU32 color, float alpha) {
  const float text_size = height * 0.62f;
  const float width = std::max(height * 1.3f, TextWidth(text_size, label) + height * 0.6f);
  const ImVec2 min(center.x - width * 0.5f, center.y - height * 0.5f);
  const ImVec2 max(center.x + width * 0.5f, center.y + height * 0.5f);
  draw_list->AddRectFilled(min, max, Fade(color, alpha), height * 0.3f);
  DrawText(draw_list,
           ImVec2(center.x - TextWidth(text_size, label) * 0.5f, center.y - text_size * 0.55f),
           text_size, Fade(IM_COL32(255, 255, 255, 255), alpha), label);
}

}  // namespace

VirtualKeyboardDialog::VirtualKeyboardDialog(rex::ui::ImGuiDrawer* drawer,
                                             const rex::kernel::xam::KeyboardUiRequest& request,
                                             Done done)
    : ImGuiDialog(drawer),
      theme_(CurrentGuideTheme()),
      keyboard_(request.default_text, request.max_length),
      title_(Utf8(request.title)),
      description_(Utf8(request.description)),
      done_(std::move(done)) {
  if (title_.empty()) {
    title_ = description_;
    description_.clear();
  }
  if (drawer) {
    resources_ = std::make_unique<GuideResources>(drawer->immediate_drawer());
    resources_->LoadIfNeeded();
  }
  GuestInputGate::OnMenuShown();
  GuideSounds::Get().Play(GuideSounds::Cue::kOpen);
}

VirtualKeyboardDialog::~VirtualKeyboardDialog() = default;

void VirtualKeyboardDialog::Finish(std::optional<std::u16string> text) {
  if (finished_) {
    return;
  }
  finished_ = true;
  result_ = std::move(text);
  Close();
}

void VirtualKeyboardDialog::OnClose() {
  GuideSounds::Get().Play(result_ ? GuideSounds::Cue::kClose : GuideSounds::Cue::kBack);
  GuestInputGate::OnMenuHidden();
  if (done_) {
    auto done = std::move(done_);
    done_ = nullptr;
    done(std::move(result_));
  }
}

void VirtualKeyboardDialog::ArmInput() {
  masked_keys_.clear();
  for (ImGuiKey key : kWatchedKeys) {
    if (ImGui::IsKeyDown(key)) {
      masked_keys_.push_back(key);
    }
  }
}

bool VirtualKeyboardDialog::Pressed(std::initializer_list<ImGuiKey> keys, bool repeat) {
  bool pressed = false;
  for (ImGuiKey key : keys) {
    auto masked = std::find(masked_keys_.begin(), masked_keys_.end(), key);
    if (masked != masked_keys_.end()) {
      if (ImGui::IsKeyDown(key)) {
        continue;
      }
      masked_keys_.erase(masked);
    }
    pressed = ImGui::IsKeyPressed(key, repeat) || pressed;
  }
  return pressed;
}

void VirtualKeyboardDialog::OnDraw(ImGuiIO& io) {
  ImGuiGamepadBridge::FeedPrimaryController(io);

  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("##recomp_keyboard", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoInputs);
  DrawScene(ImGui::GetWindowDrawList(), io);
  ImGui::End();

  // The runtime only turns on the window's text input (and so delivers typed
  // characters) while ImGui says a widget wants it, which an ImGui text field
  // would do; this keyboard draws its own field.
  ImGuiPlatformImeData& ime = ImGui::GetCurrentContext()->PlatformImeData;
  ime.WantTextInput = true;
  ime.WantVisible = true;
  ime.InputPos = caret_position_;
  ime.InputLineHeight = caret_height_;

  // As in the guide: whatever is held when the keyboard opens (the A that chose
  // the name field) waits to be released before it counts.
  if (frames_drawn_ < 2) {
    if (++frames_drawn_ == 2) {
      ArmInput();
    }
    io.InputQueueCharacters.resize(0);
    return;
  }
  HandleInput(io);
}

void VirtualKeyboardDialog::Press(const Key& key) {
  pressed_key_ = &key;
  pressed_at_ = ImGui::GetTime();
}

void VirtualKeyboardDialog::PressKind(KeyKind kind) {
  for (const Key& key : keyboard_.keys()) {
    if (key.kind == kind) {
      Press(key);
      return;
    }
  }
}

void VirtualKeyboardDialog::HandleInput(ImGuiIO& io) {
  auto& sounds = GuideSounds::Get();
  using Result = VirtualKeyboard::Result;
  const auto sound_for = [&](Result result) {
    sounds.Play(result == Result::kRefused ? GuideSounds::Cue::kKeyRefused
                                           : GuideSounds::Cue::kKeySelect);
  };

  // A PC keyboard types straight in. With mnk_mode on, the same keys also reach
  // the controller (X is START, Backspace is B, E is RT), so the controller is
  // left alone while any key on the keyboard is held.
  bool typing = !io.InputQueueCharacters.empty();
  for (int key = ImGuiKey_Keyboard_BEGIN; key < ImGuiKey_Keyboard_END && !typing; ++key) {
    typing = ImGui::IsKeyDown(static_cast<ImGuiKey>(key));
  }

  if (Pressed({ImGuiKey_Enter, ImGuiKey_KeypadEnter}, false) ||
      (!typing && Pressed({ImGuiKey_GamepadStart}, false))) {
    Finish(keyboard_.text());
    return;
  }
  if (Pressed({ImGuiKey_Escape}, false) ||
      (!typing && Pressed({ImGuiKey_GamepadFaceRight}, false))) {
    Finish(std::nullopt);
    return;
  }

  for (ImWchar character : io.InputQueueCharacters) {
    if (character >= 0x20 && character != 0x7F) {
      keyboard_.Insert(static_cast<char16_t>(character));
    }
  }
  io.InputQueueCharacters.resize(0);
  if (Pressed({ImGuiKey_Backspace}, true)) keyboard_.Backspace();
  if (Pressed({ImGuiKey_Delete}, true)) keyboard_.Delete();
  if (Pressed({ImGuiKey_LeftArrow}, true)) keyboard_.MoveCursor(-1);
  if (Pressed({ImGuiKey_RightArrow}, true)) keyboard_.MoveCursor(1);
  if (Pressed({ImGuiKey_Home}, false)) keyboard_.MoveCursorToEnd(false);
  if (Pressed({ImGuiKey_End}, false)) keyboard_.MoveCursorToEnd(true);

  // The controller.
  if (typing) {
    return;
  }
  int columns = 0;
  int rows = 0;
  if (Pressed({ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft}, true)) --columns;
  if (Pressed({ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight}, true)) ++columns;
  if (Pressed({ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp}, true)) --rows;
  if (Pressed({ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown}, true)) ++rows;
  if (columns != 0 || rows != 0) {
    keyboard_.MoveSelection(columns, rows);
    sounds.Play(GuideSounds::Cue::kKeyFocus);
  }

  if (Pressed({ImGuiKey_GamepadFaceDown}, true)) {
    Press(keyboard_.selected());
    const Result result = keyboard_.Activate();
    if (result == Result::kDone) {
      Finish(keyboard_.text());
      return;
    }
    sound_for(result);
  }
  // The buttons named on keys press those keys, as the scene's PressKey does.
  const auto button = [&](KeyKind kind, bool changed) {
    PressKind(kind);
    sound_for(changed ? Result::kChanged : Result::kRefused);
  };
  if (Pressed({ImGuiKey_GamepadFaceLeft}, true)) {
    button(KeyKind::kBackspace, keyboard_.Backspace());
  }
  if (Pressed({ImGuiKey_GamepadFaceUp}, true)) {
    button(KeyKind::kSpace, keyboard_.Insert(u' '));
  }
  if (Pressed({ImGuiKey_GamepadL1}, true)) {
    button(KeyKind::kCursorLeft, keyboard_.MoveCursor(-1));
  }
  if (Pressed({ImGuiKey_GamepadR1}, true)) {
    button(KeyKind::kCursorRight, keyboard_.MoveCursor(1));
  }
  if (Pressed({ImGuiKey_GamepadL2}, false)) {
    keyboard_.TogglePage(Page::kSymbols);
    button(KeyKind::kSymbols, true);
  }
  if (Pressed({ImGuiKey_GamepadR2}, false)) {
    keyboard_.TogglePage(Page::kAccents);
    button(KeyKind::kAccents, true);
  }
  if (Pressed({ImGuiKey_GamepadL3}, false)) {
    keyboard_.ToggleCaps();
    button(KeyKind::kCaps, true);
  }
}

void VirtualKeyboardDialog::DrawScene(ImDrawList* draw_list, const ImGuiIO& io) {
  const Screen screen = FitScreen(io);
  const Palette palette = theme_.blades ? BladesPalette(theme_) : kMetro;
  const double now = ImGui::GetTime();
  if (opened_at_ < 0.0) {
    opened_at_ = now;
  }
  const float open = EaseOut((now - opened_at_) / kOpenSeconds);

  // Canvas units to the screen.
  const auto at = [&](float x, float y) {
    return screen.At((x - kCanvasWidth * 0.5f) * kCanvasToReference,
                     (y - kCanvasHeight * 0.5f) * kCanvasToReference);
  };
  const auto size = [&](float units) { return screen.Size(units * kCanvasToReference); };
  const auto in_scene = [&](float x, float y) { return at(kSceneX + x, kSceneY + y); };

  draw_list->AddRectFilled(ImVec2(0.0f, 0.0f), io.DisplaySize, Fade(theme_.dim, open));

  const auto shadowed = [&](ImVec2 position, float text_size, ImU32 color, ImU32 shadow,
                            const std::string& text) {
    const float offset = std::max(1.0f, size(1.0f));
    DrawText(draw_list, ImVec2(position.x + offset, position.y + offset), text_size,
             Fade(shadow, open), text);
    DrawText(draw_list, position, text_size, Fade(color, open), text);
  };

  // The header, over the game above the scene.
  shadowed(at(kHeaderX, kHeaderY), size(kHeaderTextSize), palette.chrome, palette.shadow, title_);

  // The scene: the panel, and the band across its top.
  const ImVec2 scene_min = in_scene(0.0f, 0.0f);
  const ImVec2 scene_max = in_scene(kSceneWidth, kSceneHeight);
  draw_list->AddRectFilledMultiColor(scene_min, scene_max, Fade(palette.list_top, open),
                                     Fade(palette.list_top, open), Fade(palette.list_bottom, open),
                                     Fade(palette.list_bottom, open));
  const ImVec2 band_max = in_scene(kSceneWidth, kBandHeight);
  draw_list->AddRectFilledMultiColor(scene_min, band_max, Fade(kBandTop, open),
                                     Fade(kBandTop, open), Fade(kBandBottom, open),
                                     Fade(kBandBottom, open));

  if (!description_.empty()) {
    const float text_size = size(kPromptTextSize);
    const ImVec2 position = in_scene(kPromptX, kPromptY);
    draw_list->AddText(DisplayFont(), text_size, position,
                       Fade(IM_COL32(0xFF, 0xFF, 0xFF, 0xFF), open), description_.c_str(),
                       nullptr, size(kPromptWidth));
  }

  // The text field, with the cursor where the next character goes.
  const ImVec2 field_min = in_scene(kFieldX, kFieldY);
  const ImVec2 field_max = in_scene(kFieldX + kFieldWidth, kFieldY + kFieldHeight);
  draw_list->AddRectFilled(field_min, field_max, Fade(kFieldFill, open));
  const float field_text = size(kFieldTextSize);
  const float inset = size(6.0f);
  const float text_left = field_min.x + inset;
  const float text_top = (field_min.y + field_max.y) * 0.5f - field_text * 0.58f;
  const std::string before = Utf8(keyboard_.text().substr(0, keyboard_.cursor()));
  const float cursor_offset = TextWidth(field_text, before);
  const float room = field_max.x - text_left - inset;
  const float scroll = std::max(0.0f, cursor_offset - room);
  draw_list->PushClipRect(field_min, field_max, true);
  DrawText(draw_list, ImVec2(text_left - scroll, text_top), field_text, Fade(kFieldText, open),
           Utf8(keyboard_.text()));
  caret_position_ = ImVec2(text_left - scroll + cursor_offset, field_min.y);
  caret_height_ = field_max.y - field_min.y;
  if (std::fmod(now - opened_at_, kCaretHalfPeriod * 2.0) < kCaretHalfPeriod) {
    draw_list->AddRectFilled(caret_position_,
                             ImVec2(caret_position_.x + std::max(1.0f, size(kCaretWidth)),
                                    field_max.y - size(2.0f)),
                             Fade(kHighlight, open));
  }
  draw_list->PopClipRect();

  // The keys.
  const float press_t =
      pressed_key_ ? static_cast<float>((now - pressed_at_) / kPressSeconds) : 2.0f;
  for (const Key& key : keyboard_.keys()) {
    float x, y, width, height;
    KeyRect(key, x, y, width, height);
    const ImVec2 min = in_scene(x, y);
    const ImVec2 max = in_scene(x + width, y + height);
    const bool focused = keyboard_.IsSelected(key);
    const bool pressing = &key == pressed_key_ && press_t <= 1.0f;
    draw_list->AddRect(min, max, Fade(kKeyEdge, open), 0.0f, 0, std::max(1.0f, size(1.0f)));
    if (focused || pressing) {
      const ImU32 fill = pressing ? Mix(kHighlight, kHighlightPressed, press_t) : kHighlight;
      draw_list->AddRectFilled(ImVec2(min.x + 1.0f, min.y + 1.0f), max,
                               Fade(fill, focused ? open : open * (1.0f - press_t)));
    }
    const bool lit = focused || pressing;
    const ImU32 text_color = lit ? kKeyFocusText : kKeyText;
    const bool latched = (key.kind == KeyKind::kCaps && keyboard_.caps()) ||
                         (key.kind == KeyKind::kSymbols && keyboard_.page() == Page::kSymbols) ||
                         (key.kind == KeyKind::kAccents && keyboard_.page() == Page::kAccents);
    if (latched) {
      draw_list->AddRectFilled(ImVec2(min.x + 1.0f, max.y - std::max(2.0f, size(3.0f))), max,
                               Fade(lit ? kKeyFocusText : kHighlight, open));
    }

    const std::string label = KeyLabel(keyboard_, key);
    const KeyGlyph glyph = GlyphFor(key.kind);
    rex::ui::ImmediateTexture* picture =
        glyph.file && resources_ ? resources_->Get(glyph.file) : nullptr;
    const auto draw_label = [&](ImVec2 center, float text_size) {
      const ImVec2 position(center.x - TextWidth(text_size, label) * 0.5f,
                            center.y - text_size * 0.58f);
      if (!lit) {
        DrawText(draw_list, ImVec2(position.x + 1.0f, position.y + 1.0f), text_size,
                 Fade(kKeyTextShadow, open), label);
      }
      DrawText(draw_list, position, text_size, Fade(text_color, open), label);
    };

    if (key.kind == KeyKind::kCharacter) {
      draw_label(ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f), size(kCharacterTextSize));
    } else if (IsSideKey(key)) {
      // btn_topImageJ: the button's picture over the key's name.
      const ImVec2 glyph_center((min.x + max.x) * 0.5f, min.y + size(4.0f + kSideGlyphHeight * 0.5f));
      if (picture) {
        const float half_width = size(kSideGlyphWidth) * 0.5f;
        const float half_height = size(kSideGlyphHeight) * 0.5f;
        // The pictures are light grey on nothing; tinted with the text they
        // read on the light key and the green highlight alike.
        draw_list->AddImage(reinterpret_cast<ImTextureID>(picture),
                            ImVec2(glyph_center.x - half_width, glyph_center.y - half_height),
                            ImVec2(glyph_center.x + half_width, glyph_center.y + half_height),
                            ImVec2(0, 0), ImVec2(1, 1), Fade(text_color, open));
      } else {
        DrawCapGlyph(draw_list, glyph_center, size(18.0f), glyph.fallback,
                     IM_COL32(0x4A, 0x52, 0x58, 0xFF), open);
      }
      draw_label(ImVec2((min.x + max.x) * 0.5f, max.y - size(10.0f)), size(kLabelTextSize));
    } else {
      // btn_LeftImageJ (Backspace) and btn_RightImageJ (Space): the X or Y
      // button beside the name.
      const float glyph_size = size(kRowGlyphSize);
      const bool left = key.kind == KeyKind::kBackspace;
      const ImVec2 glyph_center(left ? min.x + size(4.0f) + glyph_size * 0.5f
                                     : max.x - size(4.0f) - glyph_size * 0.5f,
                                (min.y + max.y) * 0.5f);
      if (picture) {
        const float half = glyph_size * 0.5f;
        draw_list->AddImage(reinterpret_cast<ImTextureID>(picture),
                            ImVec2(glyph_center.x - half, glyph_center.y - half),
                            ImVec2(glyph_center.x + half, glyph_center.y + half), ImVec2(0, 0),
                            ImVec2(1, 1), Fade(IM_COL32(255, 255, 255, 255), open));
      } else {
        DrawGlyph(draw_list, glyph_center, glyph_size * 0.8f,
                  left ? IM_COL32(0x2A, 0x7A, 0xD8, 255) : IM_COL32(0xF0, 0xB0, 0x1C, 255),
                  glyph.fallback, open);
      }
      const float text_size = size(kLabelTextSize);
      const float center_x = left ? (glyph_center.x + glyph_size * 0.5f + max.x) * 0.5f
                                  : (min.x + glyph_center.x - glyph_size * 0.5f) * 0.5f;
      draw_label(ImVec2(center_x, (min.y + max.y) * 0.5f), text_size);
    }
  }
  if (press_t > 1.0f) {
    pressed_key_ = nullptr;
  }

  // The legend, centred under the scene.
  struct Hint {
    const char* button;
    ImU32 color;
    const char* label;
  };
  const Hint hints[] = {
      {"A", IM_COL32(0x4C, 0xB0, 0x2A, 255), "Select"},
      {"B", IM_COL32(0xD8, 0x2C, 0x2C, 255), "Back"},
      {"X", IM_COL32(0x2A, 0x7A, 0xD8, 255), "Backspace"},
      {"Y", IM_COL32(0xF0, 0xB0, 0x1C, 255), "Space"},
  };
  const float legend_text = size(kLegendTextSize);
  const float glyph = size(kLegendGlyph);
  float total = 0.0f;
  for (const Hint& hint : hints) {
    total += glyph + size(kLegendGlyphGap) + TextWidth(legend_text, hint.label) +
             size(kLegendItemGap);
  }
  total -= size(kLegendItemGap);
  float x = screen.center.x - total * 0.5f;
  const float y = at(0.0f, kLegendY).y;
  for (const Hint& hint : hints) {
    DrawGlyph(draw_list, ImVec2(x + glyph * 0.5f, y), glyph, hint.color, hint.button, open);
    const ImVec2 position(x + glyph + size(kLegendGlyphGap), y - legend_text * 0.55f);
    shadowed(position, legend_text, palette.chrome, palette.shadow, hint.label);
    x = position.x + TextWidth(legend_text, hint.label) + size(kLegendItemGap);
  }
}

}  // namespace recomp
