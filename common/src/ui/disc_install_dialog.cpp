#include "recomp/ui/disc_install_dialog.h"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/windowed_app_context.h>

#include <rex/ui/imgui_drawer.h>

#include "recomp/input/controller_menu_watcher.h"
#include "recomp/input/imgui_gamepad_bridge.h"
#include "recomp/ui/guide_resources.h"
#include "recomp/ui/guide_sounds.h"
#include "recomp/ui/virtual_keyboard_dialog.h"
#include "guide_scene.h"
#include "guide_theme.h"
#include "held_key_mask.h"

namespace recomp {

using namespace guide_scene;

namespace {

// The same canvas, panes and list metrics as the update prompt, so the two
// screens a player may meet before the game starts are plainly the same UI.
constexpr float kCanvasWidth = 852.0f;
constexpr float kCanvasHeight = 480.0f;
constexpr float kCanvasToReference = kReferenceWidth / kCanvasWidth;

constexpr float kPaneX = 100.0f;
constexpr float kPaneWidth = 652.0f;
constexpr float kPadding = 21.0f;
constexpr float kHeaderTextSize = 22.0f;
constexpr float kHeaderGap = 12.0f;
constexpr float kIconSize = 20.0f;
constexpr float kIconGap = 9.0f;

constexpr ImU32 kPaneFill = IM_COL32(0xEB, 0xEB, 0xEB, 0xFF);
constexpr ImU32 kBodyText = IM_COL32(0x0F, 0x12, 0x14, 0xFF);
constexpr float kBodyTextSize = 16.0f;
constexpr float kLineSpacing = 1.35f;

constexpr float kRowHeight = 34.0f;
constexpr float kRowTextSize = 17.0f;
constexpr ImU32 kRowText = IM_COL32(0x2E, 0x34, 0x38, 0xFF);
constexpr ImU32 kRowFocusText = IM_COL32(0xEB, 0xEB, 0xEB, 0xFF);
constexpr ImU32 kFocus = IM_COL32(0x00, 0x8A, 0x00, 0xFF);
constexpr ImU32 kRule = IM_COL32(0xD2, 0xD5, 0xD9, 0xFF);
constexpr ImU32 kInfoDisc = IM_COL32(0x1B, 0x6F, 0xB8, 0xFF);
constexpr const char* kInfoIcon = "ico_64x_info.png";

constexpr float kBarHeight = 6.0f;

// Up, down, A and B, plus their keyboard equivalents - the same set the update
// prompt watches, so a key held from a previous screen does not carry through.
// This is the first screen a player ever reaches, before any of the game's own
// controls are explained, so it takes the spellings someone is likely to try
// without being told: the arrow cluster, the numpad's arrows (which arrive as
// Keypad2/Keypad8 rather than the arrows when Num Lock is off), W and S, and
// Space as well as Enter to choose.
constexpr ImGuiKey kWatchedKeys[] = {
    ImGuiKey_DownArrow,        ImGuiKey_UpArrow,          ImGuiKey_Keypad2,
    ImGuiKey_Keypad8,          ImGuiKey_S,                ImGuiKey_W,
    ImGuiKey_Enter,            ImGuiKey_KeypadEnter,      ImGuiKey_Space,
    ImGuiKey_Escape,           ImGuiKey_GamepadDpadDown,
    ImGuiKey_GamepadDpadUp,    ImGuiKey_GamepadLStickDown, ImGuiKey_GamepadLStickUp,
    ImGuiKey_GamepadFaceDown,  ImGuiKey_GamepadFaceRight,
};

std::string FormatGigabytes(uint64_t bytes) {
  char text[32];
  std::snprintf(text, sizeof(text), "%.2f GB", double(bytes) / (1024.0 * 1024.0 * 1024.0));
  return text;
}

// A path is one long word, so wrapping on spaces does nothing with it. The
// console elides in the middle instead, which keeps the drive and the file name
// - the two parts worth reading - and drops the directories between them.
std::string ElideToWidth(const std::string& text, float size, float width) {
  if (TextWidth(size, text) <= width || text.size() < 8) {
    return text;
  }
  size_t keep = text.size() / 2;
  while (keep > 4) {
    const size_t head = keep / 2;
    const size_t tail = keep - head;
    const std::string candidate =
        text.substr(0, head) + "..." + text.substr(text.size() - tail);
    if (TextWidth(size, candidate) <= width) {
      return candidate;
    }
    --keep;
  }
  return "...";
}

std::u16string ToUtf16(const std::string& text) {
  std::u16string out;
  out.reserve(text.size());
  for (unsigned char c : text) {
    out.push_back(static_cast<char16_t>(c));
  }
  return out;
}

std::string FromUtf16(const std::u16string& text) {
  std::string out;
  out.reserve(text.size());
  for (char16_t c : text) {
    out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
  }
  return out;
}

}  // namespace

void DiscInstallDialog::Show(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext& app_context,
                             DiscInstallRequest request) {
  new DiscInstallDialog(drawer, app_context, std::move(request));
}

DiscInstallDialog::DiscInstallDialog(rex::ui::ImGuiDrawer* drawer,
                                     rex::ui::WindowedAppContext& app_context,
                                     DiscInstallRequest request)
    : ImGuiDialog(drawer),
      app_context_(app_context),
      request_(std::move(request)),
      theme_(CurrentGuideTheme()),
      held_keys_(std::make_unique<HeldKeyMask>()),
      file_picker_(request_.owner_window) {
  if (drawer) {
    resources_ = std::make_unique<GuideResources>(drawer->immediate_drawer());
    resources_->LoadIfNeeded();
  }
  opened_at_ = ImGui::GetTime();
  BuildRows();
  GuestInputGate::OnMenuShown();
  GuideSounds::Get().Play(GuideSounds::Cue::kOpen);
}

void DiscInstallDialog::BuildRows() {
  rows_.clear();
  if (NativeFilePicker::IsAvailable()) {
    rows_.push_back({"Browse for a disc image", [this] { RequestPickedDiscImage(); }});
  }
  // The console would never show a text field; typing a path goes through the
  // same keyboard a title gets from XamShowKeyboardUI.
  rows_.push_back({"Enter the path to a disc image", [this] { AskForTypedPath(); }});
  rows_.push_back({"Quit", [this] { Quit(); }});
  selected_ = 0;
}

void DiscInstallDialog::Quit() {
  Close();
  if (request_.on_quit) {
    request_.on_quit();
  }
}

void DiscInstallDialog::AskForTypedPath() {
  rex::kernel::xam::KeyboardUiRequest keyboard;
  keyboard.title = ToUtf16("Disc image");
  keyboard.description = ToUtf16("Enter the full path to your " + request_.game_display_name +
                                 " disc image.");
  keyboard.default_text = ToUtf16(std::string(typed_path_.data()));
  keyboard.max_length = static_cast<uint32_t>(typed_path_.size() - 1);
  new VirtualKeyboardDialog(imgui_drawer(), keyboard,
                            [this](std::optional<std::u16string> text) {
                              if (!text || text->empty()) {
                                return;
                              }
                              const std::string path = FromUtf16(*text);
                              std::snprintf(typed_path_.data(), typed_path_.size(), "%s",
                                            path.c_str());
                              BeginInstall(rex::to_path(path));
                            });
}

DiscInstallDialog::~DiscInstallDialog() {
  if (worker_.joinable()) {
    worker_.join();
  }
}

void DiscInstallDialog::OnDraw(ImGuiIO& io) {
  ImGuiGamepadBridge::FeedPrimaryController(io);
  BeginInstallIfPicked();
  FinishInstallIfDone();
  if (stage_ == Stage::kInstalling && worker_succeeded_) {
    return;
  }

  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("##recomp_disc_install", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoInputs);
  DrawScene(ImGui::GetWindowDrawList(), io);
  ImGui::End();

  // The first frames go by before the held-key mask is armed, so a button still
  // down from whatever opened this screen is not read as a press on it.
  if (frames_drawn_ < 2) {
    if (++frames_drawn_ == 2) {
      held_keys_->Arm(kWatchedKeys);
    }
    return;
  }
  if (stage_ != Stage::kInstalling) {
    HandleInput();
  }
}

void DiscInstallDialog::HandleInput() {
  if (rows_.empty()) {
    return;
  }
  const int before = selected_;
  const int count = static_cast<int>(rows_.size());
  if (held_keys_->Pressed({ImGuiKey_DownArrow, ImGuiKey_Keypad2, ImGuiKey_S,
                           ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown},
                          true)) {
    selected_ = std::min(selected_ + 1, count - 1);
  }
  if (held_keys_->Pressed({ImGuiKey_UpArrow, ImGuiKey_Keypad8, ImGuiKey_W,
                           ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp},
                          true)) {
    selected_ = std::max(selected_ - 1, 0);
  }
  if (selected_ != before) {
    GuideSounds::Get().Play(GuideSounds::Cue::kFocus);
  }
  if (held_keys_->Pressed(
          {ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Space, ImGuiKey_GamepadFaceDown},
          false)) {
    GuideSounds::Get().Play(GuideSounds::Cue::kSelect);
    rows_[selected_].chosen();
    return;
  }
  // There is no game to go back to yet, so B is the same as choosing Quit.
  if (held_keys_->Pressed({ImGuiKey_Escape, ImGuiKey_GamepadFaceRight}, false)) {
    GuideSounds::Get().Play(GuideSounds::Cue::kBack);
    Quit();
  }
}

void DiscInstallDialog::DrawScene(ImDrawList* draw_list, ImGuiIO& io) {
  const Screen screen = FitScreen(io);
  const Palette palette = theme_.blades ? BladesPalette(theme_) : kMetro;
  const double now = ImGui::GetTime();
  const float alpha =
      std::clamp(static_cast<float>((now - opened_at_) / kTransOpenSeconds), 0.0f, 1.0f);

  // Canvas units to pixels: the guide's own 852x480 scene, letterboxed into
  // whatever the window is.
  const auto at = [&](float x, float y) {
    return screen.At((x - kCanvasWidth * 0.5f) * kCanvasToReference,
                     (y - kCanvasHeight * 0.5f) * kCanvasToReference);
  };
  const auto size = [&](float units) { return screen.Size(units * kCanvasToReference); };

  draw_list->AddRectFilled(ImVec2(0.0f, 0.0f), io.DisplaySize, Fade(theme_.dim, alpha));

  const std::string title = request_.game_display_name;
  const float title_size = size(kHeaderTextSize);
  const float icon = size(kIconSize);
  const float gap = size(kIconGap);
  const float title_width = TextWidth(title_size, title);
  const ImVec2 header = at(kPaneX, 96.0f);
  const ImVec2 icon_center(header.x + icon * 0.5f, header.y + icon * 0.5f);

  rex::ui::ImmediateTexture* info = resources_ ? resources_->Get(kInfoIcon) : nullptr;
  if (info) {
    draw_list->AddImage(reinterpret_cast<ImTextureID>(info),
                        ImVec2(header.x, header.y), ImVec2(header.x + icon, header.y + icon),
                        ImVec2(0, 0), ImVec2(1, 1), Fade(IM_COL32(255, 255, 255, 255), alpha));
  } else {
    draw_list->AddCircleFilled(icon_center, icon * 0.5f, Fade(kInfoDisc, alpha), 32);
    const float letter = icon * 0.62f;
    DrawText(draw_list, ImVec2(icon_center.x - TextWidth(letter, "i") * 0.5f,
                               icon_center.y - letter * 0.62f),
             letter, Fade(IM_COL32(0xF5, 0xF5, 0xF5, 0xFF), alpha), "i");
  }
  const ImVec2 title_at(header.x + icon + gap, header.y - size(2.0f));
  const float shadow = size(1.0f);
  DrawText(draw_list, ImVec2(title_at.x + shadow, title_at.y + shadow), title_size,
           Fade(palette.shadow, alpha), title);
  DrawText(draw_list, title_at, title_size, Fade(palette.chrome, alpha), title);
  (void)title_width;

  // The body lines, then the list, inside one pale pane.
  std::vector<std::string> lines;
  if (stage_ == Stage::kInstalling) {
    lines.push_back("Installing from " + disc_image_.filename().string());
    lines.push_back("This only happens once.");
  } else if (stage_ == Stage::kFailed) {
    lines.push_back("That disc image could not be installed.");
    lines.push_back(installer_.error());
  } else {
    lines.push_back(request_.game_display_name + " needs its game files before it can start.");
    lines.push_back("Select your own Xbox 360 disc image to install them.");
  }
  lines.push_back("Installing to " + request_.install_folder.string());

  const float body_size = size(kBodyTextSize);
  const float line_height = body_size * kLineSpacing;
  const float pad = size(kPadding);
  const float text_width = size(kPaneWidth) - pad * 2.0f;
  for (std::string& line : lines) {
    line = ElideToWidth(line, body_size, text_width);
  }
  const float rows_height =
      stage_ == Stage::kInstalling ? size(kBarHeight) + line_height : rows_.size() * size(kRowHeight);
  const float pane_height =
      pad + lines.size() * line_height + size(kHeaderGap) + rows_height + pad;
  const ImVec2 pane_min = at(kPaneX, 128.0f);
  const ImVec2 pane_max(pane_min.x + size(kPaneWidth), pane_min.y + pane_height);
  draw_list->AddRectFilled(pane_min, pane_max, Fade(kPaneFill, alpha));

  float y = pane_min.y + pad;
  for (const std::string& line : lines) {
    DrawText(draw_list, ImVec2(pane_min.x + pad, y), body_size, Fade(kBodyText, alpha), line);
    y += line_height;
  }
  y += size(kHeaderGap);

  if (stage_ == Stage::kInstalling) {
    const uint64_t total = progress_.total_bytes.load();
    const uint64_t copied = progress_.copied_bytes.load();
    const float fraction = total ? float(double(copied) / double(total)) : 0.0f;
    const ImVec2 bar_min(pane_min.x + pad, y);
    const ImVec2 bar_max(pane_max.x - pad, y + size(kBarHeight));
    draw_list->AddRectFilled(bar_min, bar_max, Fade(kRule, alpha));
    draw_list->AddRectFilled(bar_min, ImVec2(bar_min.x + (bar_max.x - bar_min.x) * fraction,
                                             bar_max.y),
                             Fade(kFocus, alpha));
    const std::string counted =
        FormatGigabytes(copied) + " / " + (total ? FormatGigabytes(total) : std::string("..."));
    DrawText(draw_list, ImVec2(bar_min.x, bar_max.y + size(6.0f)), body_size,
             Fade(kBodyText, alpha), counted);
    return;
  }

  const float row_height = size(kRowHeight);
  const float row_text = size(kRowTextSize);
  for (size_t i = 0; i < rows_.size(); ++i) {
    const bool focused = static_cast<int>(i) == selected_;
    const ImVec2 row_min(pane_min.x + pad, y + i * row_height);
    const ImVec2 row_max(pane_max.x - pad, row_min.y + row_height);
    if (focused) {
      draw_list->AddRectFilled(row_min, row_max, Fade(kFocus, alpha));
    } else if (i + 1 < rows_.size()) {
      draw_list->AddRectFilled(ImVec2(row_min.x, row_max.y - size(1.0f)), row_max,
                               Fade(kRule, alpha));
    }
    DrawText(draw_list,
             ImVec2(row_min.x + size(10.0f), row_min.y + (row_height - row_text) * 0.5f),
             row_text, Fade(focused ? kRowFocusText : kRowText, alpha), rows_[i].label);
  }
}

void DiscInstallDialog::RequestPickedDiscImage() {
  auto pending_pick = std::make_shared<PendingPick>();
  pending_pick_ = pending_pick;
  file_picker_.PickDiscImage("Select your " + request_.game_display_name + " disc image",
                             [pending_pick](std::optional<std::filesystem::path> disc_image) {
                               std::lock_guard<std::mutex> lock(pending_pick->mutex);
                               pending_pick->disc_image = std::move(disc_image);
                               pending_pick->completed = true;
                             });
}

void DiscInstallDialog::BeginInstallIfPicked() {
  if (!pending_pick_) {
    return;
  }
  std::optional<std::filesystem::path> disc_image;
  {
    std::lock_guard<std::mutex> lock(pending_pick_->mutex);
    if (!pending_pick_->completed) {
      return;
    }
    disc_image = std::move(pending_pick_->disc_image);
  }
  pending_pick_.reset();
  if (disc_image && stage_ != Stage::kInstalling) {
    BeginInstall(*disc_image);
  }
}

void DiscInstallDialog::FinishInstallIfDone() {
  if (stage_ != Stage::kInstalling || !worker_finished_) {
    return;
  }
  worker_.join();
  if (!worker_succeeded_) {
    REXLOG_ERROR("Game file installation failed: {}", installer_.error());
    stage_ = Stage::kFailed;
    return;
  }
  REXLOG_INFO("Game files installed to {}", request_.install_folder.string());
  Close();
  app_context_.CallInUIThreadDeferred(std::move(request_.on_installed));
}

void DiscInstallDialog::BeginInstall(const std::filesystem::path& disc_image) {
  disc_image_ = disc_image;
  progress_.copied_bytes = 0;
  progress_.total_bytes = 0;
  worker_finished_ = false;
  worker_succeeded_ = false;
  stage_ = Stage::kInstalling;
  REXLOG_INFO("Installing game files from {} to {}", disc_image.string(),
              request_.install_folder.string());
  worker_ = std::thread([this] {
    worker_succeeded_ = installer_.Install(disc_image_, request_.install_folder, progress_);
    worker_finished_ = true;
  });
}

}  // namespace recomp
