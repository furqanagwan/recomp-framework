#include "recomp/ui/message_box_dialog.h"

#include <algorithm>

#include <rex/string.h>
#include <rex/ui/imgui_drawer.h>

#include "recomp/input/controller_menu_watcher.h"
#include "recomp/input/imgui_gamepad_bridge.h"
#include "recomp/ui/guide_resources.h"
#include "recomp/ui/guide_sounds.h"
#include "guide_scene.h"
#include "guide_theme.h"
#include "held_key_mask.h"

namespace recomp {

namespace {

using namespace guide_scene;
using Icon = rex::kernel::xam::MessageBoxUiRequest::Icon;

// Laid out on the guide's 852 by 480 HUD canvas, as the keyboard is.
constexpr float kCanvasWidth = 852.0f;
constexpr float kCanvasHeight = 480.0f;
constexpr float kCanvasToReference = kReferenceWidth / kCanvasWidth;

// The header label, over the game (Label_Head).
constexpr float kHeaderX = 156.0f;
constexpr float kHeaderTextSize = 22.0f;
// The pane: as wide as the keyboard's, centred, as tall as its contents.
constexpr float kPaneX = 138.0f;
constexpr float kPaneWidth = 574.0f;
constexpr float kPadding = 21.0f;
constexpr ImU32 kPaneFill = IM_COL32(0xEB, 0xEB, 0xEB, 0xFF);  // TwoThirdsPane
// Label_Body: 16 point, #0F1214.
constexpr float kBodyTextSize = 16.0f;
constexpr ImU32 kBodyText = IM_COL32(0x0F, 0x12, 0x14, 0xFF);
constexpr float kIconSize = 48.0f;
constexpr const char* kWarningIcon = "ico_64x_warning.png";  // dash.xex's memory package
// The buttons: rows like the guide's, with its green focus.
constexpr float kButtonHeight = 36.0f;
constexpr float kButtonTextSize = 18.0f;
constexpr ImU32 kButtonText = IM_COL32(0x2E, 0x34, 0x38, 0xFF);
constexpr ImU32 kFocusText = IM_COL32(0xEB, 0xEB, 0xEB, 0xFF);
constexpr ImU32 kFocus = IM_COL32(0x00, 0x8A, 0x00, 0xFF);
constexpr ImU32 kRule = IM_COL32(0xD2, 0xD5, 0xD9, 0xFF);
// The legend: legend_A's 18 point label beside a 32 unit button, half size here
// as on the keyboard.
constexpr float kLegendGap = 25.0f;
constexpr float kLegendGlyph = 20.0f;
constexpr float kLegendTextSize = 20.0f;
constexpr float kLegendGlyphGap = 7.0f;
constexpr float kLegendItemGap = 16.0f;

constexpr ImGuiKey kWatchedKeys[] = {
    ImGuiKey_Enter,
    ImGuiKey_KeypadEnter,
    ImGuiKey_Escape,
    ImGuiKey_UpArrow,
    ImGuiKey_DownArrow,
    ImGuiKey_GamepadFaceDown,
    ImGuiKey_GamepadFaceRight,
    ImGuiKey_GamepadDpadUp,
    ImGuiKey_GamepadDpadDown,
    ImGuiKey_GamepadLStickUp,
    ImGuiKey_GamepadLStickDown,
};

std::string Utf8(const std::u16string& text) {
  return rex::string::to_utf8(text);
}

// Wraps text to a width at word boundaries, keeping the title's own line breaks.
std::vector<std::string> WrapText(const std::string& text, float size, float width) {
  std::vector<std::string> lines;
  if (text.empty()) {
    return lines;
  }
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) {
      end = text.size();
    }
    std::string paragraph = text.substr(start, end - start);
    if (!paragraph.empty() && paragraph.back() == '\r') {
      paragraph.pop_back();
    }
    std::string line;
    size_t word_start = 0;
    while (word_start <= paragraph.size()) {
      size_t word_end = paragraph.find(' ', word_start);
      if (word_end == std::string::npos) {
        word_end = paragraph.size();
      }
      const std::string word = paragraph.substr(word_start, word_end - word_start);
      const std::string candidate = line.empty() ? word : line + " " + word;
      if (!line.empty() && TextWidth(size, candidate) > width) {
        lines.push_back(line);
        line = word;
      } else {
        line = candidate;
      }
      word_start = word_end + 1;
    }
    lines.push_back(line);
    start = end + 1;
  }
  return lines;
}

}  // namespace

MessageBoxDialog::MessageBoxDialog(rex::ui::ImGuiDrawer* drawer,
                                   const rex::kernel::xam::MessageBoxUiRequest& request, Done done)
    : ImGuiDialog(drawer),
      theme_(CurrentGuideTheme()),
      held_keys_(std::make_unique<HeldKeyMask>()),
      icon_(request.icon),
      title_(Utf8(request.title)),
      text_(Utf8(request.text)),
      done_(std::move(done)) {
  for (const auto& button : request.buttons) {
    buttons_.push_back(Utf8(button));
  }
  if (buttons_.empty()) {
    buttons_.push_back("OK");
  }
  selected_ =
      std::clamp(static_cast<int>(request.active_button), 0, static_cast<int>(buttons_.size()) - 1);
  if (drawer) {
    resources_ = std::make_unique<GuideResources>(drawer->immediate_drawer());
    resources_->LoadIfNeeded();
  }
  GuestInputGate::OnMenuShown();
  GuideSounds::Get().Play(GuideSounds::Cue::kOpen);
}

MessageBoxDialog::~MessageBoxDialog() = default;

void MessageBoxDialog::Finish(std::optional<uint32_t> button) {
  if (finished_) {
    return;
  }
  finished_ = true;
  result_ = button;
  finished_at_ = ImGui::GetTime();
  GuideSounds::Get().Play(button ? GuideSounds::Cue::kSelect : GuideSounds::Cue::kBack);
}

void MessageBoxDialog::OnClose() {
  GuestInputGate::OnMenuHidden();
  if (done_) {
    auto done = std::move(done_);
    done_ = nullptr;
    done(result_);
  }
}

void MessageBoxDialog::OnDraw(ImGuiIO& io) {
  ImGuiGamepadBridge::FeedPrimaryController(io);

  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("##recomp_message_box", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoInputs);
  DrawScene(ImGui::GetWindowDrawList(), io);
  ImGui::End();

  if (finished_) {
    if (ImGui::GetTime() - finished_at_ >= kTransFromSeconds) {
      Close();
    }
    return;
  }
  if (frames_drawn_ < 2) {
    if (++frames_drawn_ == 2) {
      held_keys_->Arm(kWatchedKeys);
    }
    return;
  }
  HandleInput();
}

void MessageBoxDialog::HandleInput() {
  const int before = selected_;
  const int count = static_cast<int>(buttons_.size());
  if (held_keys_->Pressed(
          {ImGuiKey_DownArrow, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown}, true)) {
    selected_ = std::min(selected_ + 1, count - 1);
  }
  if (held_keys_->Pressed({ImGuiKey_UpArrow, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp},
                          true)) {
    selected_ = std::max(selected_ - 1, 0);
  }
  if (selected_ != before) {
    GuideSounds::Get().Play(GuideSounds::Cue::kFocus);
  }
  if (held_keys_->Pressed({ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_GamepadFaceDown},
                          false)) {
    Finish(static_cast<uint32_t>(selected_));
    return;
  }
  if (held_keys_->Pressed({ImGuiKey_Escape, ImGuiKey_GamepadFaceRight}, false)) {
    Finish(std::nullopt);
  }
}

void MessageBoxDialog::DrawScene(ImDrawList* draw_list, const ImGuiIO& io) {
  const Screen screen = FitScreen(io);
  const Palette palette = theme_.blades ? BladesPalette(theme_) : kMetro;
  const double now = ImGui::GetTime();
  if (opened_at_ < 0.0) {
    opened_at_ = now;
  }
  const float alpha =
      finished_
          ? std::clamp(1.0f - static_cast<float>((now - finished_at_) / kTransFromSeconds), 0.0f,
                       1.0f)
          : std::clamp(static_cast<float>((now - opened_at_) / kTransOpenSeconds), 0.0f, 1.0f);

  const auto at = [&](float x, float y) {
    return screen.At((x - kCanvasWidth * 0.5f) * kCanvasToReference,
                     (y - kCanvasHeight * 0.5f) * kCanvasToReference);
  };
  const auto size = [&](float units) { return screen.Size(units * kCanvasToReference); };

  // Measure first: the pane is as tall as its text and buttons, centred.
  rex::ui::ImmediateTexture* icon =
      icon_ != Icon::kNone && resources_ ? resources_->Get(kWarningIcon) : nullptr;
  const float text_left = kPadding + (icon ? kIconSize + kPadding : 0.0f);
  const float text_width_units = kPaneWidth - text_left - kPadding;
  const auto lines = WrapText(text_, size(kBodyTextSize), size(text_width_units));
  const float line_height = kBodyTextSize * 1.3f;
  const float text_height =
      std::max(static_cast<float>(lines.size()) * line_height, icon ? kIconSize : 0.0f);
  const float pane_height = kPadding + text_height + kPadding +
                            kButtonHeight * static_cast<float>(buttons_.size()) + kPadding;
  const float pane_top = (kCanvasHeight - pane_height) * 0.5f;

  draw_list->AddRectFilled(ImVec2(0.0f, 0.0f), io.DisplaySize, Fade(theme_.dim, alpha));

  const auto shadowed = [&](ImVec2 position, float text_size, const std::string& text) {
    const float offset = std::max(1.0f, size(1.0f));
    DrawText(draw_list, ImVec2(position.x + offset, position.y + offset), text_size,
             Fade(palette.shadow, alpha), text);
    DrawText(draw_list, position, text_size, Fade(palette.chrome, alpha), text);
  };
  if (!title_.empty()) {
    shadowed(at(kHeaderX, pane_top - kHeaderTextSize - 12.0f), size(kHeaderTextSize), title_);
  }

  const ImVec2 pane_min = at(kPaneX, pane_top);
  const ImVec2 pane_max = at(kPaneX + kPaneWidth, pane_top + pane_height);
  draw_list->AddRectFilled(pane_min, pane_max, Fade(kPaneFill, alpha));

  if (icon) {
    const ImVec2 icon_min = at(kPaneX + kPadding, pane_top + kPadding);
    draw_list->AddImage(reinterpret_cast<ImTextureID>(icon), icon_min,
                        ImVec2(icon_min.x + size(kIconSize), icon_min.y + size(kIconSize)),
                        ImVec2(0, 0), ImVec2(1, 1), Fade(IM_COL32(255, 255, 255, 255), alpha));
  }
  for (size_t i = 0; i < lines.size(); ++i) {
    DrawText(draw_list,
             at(kPaneX + text_left, pane_top + kPadding + line_height * static_cast<float>(i)),
             size(kBodyTextSize), Fade(kBodyText, alpha), lines[i]);
  }

  const float buttons_top = pane_top + kPadding + text_height + kPadding;
  for (size_t i = 0; i < buttons_.size(); ++i) {
    const float row_top = buttons_top + kButtonHeight * static_cast<float>(i);
    const ImVec2 row_min = at(kPaneX, row_top);
    const ImVec2 row_max = at(kPaneX + kPaneWidth, row_top + kButtonHeight);
    const bool focused = static_cast<int>(i) == selected_;
    if (focused) {
      draw_list->AddRectFilled(row_min, row_max, Fade(kFocus, alpha));
    } else if (i + 1 < buttons_.size()) {
      draw_list->AddRectFilled(ImVec2(row_min.x, row_max.y - std::max(1.0f, size(1.0f))), row_max,
                               Fade(kRule, alpha));
    }
    const float text_size = size(kButtonTextSize);
    DrawText(draw_list,
             ImVec2(row_min.x + size(kPadding), (row_min.y + row_max.y) * 0.5f - text_size * 0.58f),
             text_size, Fade(focused ? kFocusText : kButtonText, alpha), buttons_[i]);
  }

  // A and B, centred under the pane.
  struct Hint {
    const char* button;
    const char* label;
  };
  const Hint hints[] = {{"A", "Select"}, {"B", "Back"}};
  const float legend_text = size(kLegendTextSize);
  const float glyph = size(kLegendGlyph);
  float total = 0.0f;
  for (const Hint& hint : hints) {
    total +=
        glyph + size(kLegendGlyphGap) + TextWidth(legend_text, hint.label) + size(kLegendItemGap);
  }
  total -= size(kLegendItemGap);
  float x = screen.center.x - total * 0.5f;
  const float y = at(0.0f, pane_top + pane_height + kLegendGap).y;
  for (const Hint& hint : hints) {
    DrawGlyph(draw_list, ImVec2(x + glyph * 0.5f, y), glyph, hint.button, alpha,
              reinterpret_cast<ImTextureID>(resources_ ? resources_->Get(ButtonPicture(hint.button))
                                                       : nullptr));
    const ImVec2 position(x + glyph + size(kLegendGlyphGap), y - legend_text * 0.55f);
    shadowed(position, legend_text, hint.label);
    x = position.x + TextWidth(legend_text, hint.label) + size(kLegendItemGap);
  }
}

}  // namespace recomp
