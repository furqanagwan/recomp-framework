#include "recomp/ui/update_required_dialog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/windowed_app_context.h>

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

// Laid out on the guide's 852 by 480 HUD canvas, as the keyboard and the
// message box are. The console's own Update Required scene is in xam.xex, which
// no dashboard file here carries, so the proportions below are taken off a
// photograph of the screen: a header over two panes of equal width, the
// question and its rows on the left, what is being updated on the right.
constexpr float kCanvasWidth = 852.0f;
constexpr float kCanvasHeight = 480.0f;
constexpr float kCanvasToReference = kReferenceWidth / kCanvasWidth;

constexpr float kPaneX = 100.0f;
constexpr float kPaneWidth = 652.0f;
constexpr float kSplit = 0.5f;  // where the white pane gives way to the grey one
constexpr float kPadding = 21.0f;
constexpr float kHeaderTextSize = 22.0f;
constexpr float kHeaderGap = 12.0f;
constexpr float kIconSize = 20.0f;
constexpr float kIconGap = 9.0f;

// The pale pane is the guide skin's TwoThirdsPane; the darker one beside it is
// sampled from the photograph.
constexpr ImU32 kPaneFill = IM_COL32(0xEB, 0xEB, 0xEB, 0xFF);
constexpr ImU32 kAsidePaneFill = IM_COL32(0xA9, 0xAB, 0xAE, 0xFF);
constexpr ImU32 kBodyText = IM_COL32(0x0F, 0x12, 0x14, 0xFF);
constexpr ImU32 kAsideText = IM_COL32(0x16, 0x18, 0x1A, 0xFF);
constexpr float kBodyTextSize = 16.0f;
constexpr float kAsideTitleSize = 20.0f;
constexpr float kLineSpacing = 1.35f;

// The rows, as the guide draws a list: a green band on the one in hand, a hair
// rule between the rest.
constexpr float kRowHeight = 34.0f;
constexpr float kRowTextSize = 17.0f;
constexpr ImU32 kRowText = IM_COL32(0x2E, 0x34, 0x38, 0xFF);
constexpr ImU32 kRowFocusText = IM_COL32(0xEB, 0xEB, 0xEB, 0xFF);
constexpr ImU32 kFocus = IM_COL32(0x00, 0x8A, 0x00, 0xFF);
constexpr ImU32 kRule = IM_COL32(0xD2, 0xD5, 0xD9, 0xFF);
// The blue disc behind the "i", for when the dashboard's own icon is not there.
constexpr ImU32 kInfoDisc = IM_COL32(0x1B, 0x6F, 0xB8, 0xFF);
constexpr const char* kInfoIcon = "ico_64x_info.png";

// A progress bar the console did not have on this screen; it is drawn in the
// row band's colours so it belongs to the rest.
constexpr float kBarHeight = 6.0f;
constexpr ImU32 kBarTrack = IM_COL32(0xD2, 0xD5, 0xD9, 0xFF);

// The console's Update in Progress panel, which replaces the question once the
// player has chosen. Unlike the question it is one panel with a header band,
// and its only way out is B. Measured off a photograph of a television, so the
// proportions are close rather than exact and the colours are approximate.
constexpr float kProgressX = 106.0f;
constexpr float kProgressWidth = 640.0f;
constexpr float kProgressMinHeight = 232.0f;
constexpr float kBandHeight = 34.0f;
constexpr float kBandTextSize = 19.0f;
constexpr ImU32 kBandTop = IM_COL32(0xE9, 0xEE, 0xF1, 0xFF);
constexpr ImU32 kBandBottom = IM_COL32(0xD5, 0xDD, 0xE2, 0xFF);
constexpr ImU32 kPanelTop = IM_COL32(0xF6, 0xF8, 0xF9, 0xFF);
constexpr ImU32 kPanelBottom = IM_COL32(0xE4, 0xEA, 0xED, 0xFF);
constexpr ImU32 kPanelText = IM_COL32(0x2A, 0x33, 0x3A, 0xFF);
constexpr float kProgressBarHeight = 14.0f;
constexpr ImU32 kProgressTrack = IM_COL32(0xC6, 0xCC, 0xD0, 0xFF);
constexpr ImU32 kProgressFill = IM_COL32(0x3C, 0xB0, 0x35, 0xFF);

constexpr float kLegendGap = 25.0f;
constexpr float kLegendGlyph = 20.0f;
constexpr float kLegendTextSize = 20.0f;
constexpr float kLegendGlyphGap = 7.0f;
constexpr float kLegendItemGap = 16.0f;

constexpr ImGuiKey kWatchedKeys[] = {
    ImGuiKey_Enter,        ImGuiKey_KeypadEnter,      ImGuiKey_Escape,
    ImGuiKey_UpArrow,      ImGuiKey_DownArrow,        ImGuiKey_GamepadFaceDown,
    ImGuiKey_GamepadFaceRight, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadDpadDown,
    ImGuiKey_GamepadLStickUp,  ImGuiKey_GamepadLStickDown,
};

std::vector<std::string> Wrap(const std::string& text, float size, float width) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) {
      end = text.size();
    }
    const std::string paragraph = text.substr(start, end - start);
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

std::string HumanSize(uint64_t bytes) {
  char buffer[32] = {};
  if (bytes >= 1024ull * 1024ull) {
    std::snprintf(buffer, sizeof(buffer), "%.1f MB",
                  static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else {
    std::snprintf(buffer, sizeof(buffer), "%llu KB",
                  static_cast<unsigned long long>((bytes + 1023) / 1024));
  }
  return buffer;
}

}  // namespace

void UpdateRequiredDialog::Show(rex::ui::ImGuiDrawer* drawer,
                                rex::ui::WindowedAppContext& app_context,
                                UpdateRequiredRequest request) {
  new UpdateRequiredDialog(drawer, app_context, std::move(request));
}

UpdateRequiredDialog::UpdateRequiredDialog(rex::ui::ImGuiDrawer* drawer,
                                           rex::ui::WindowedAppContext& app_context,
                                           UpdateRequiredRequest request)
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
  BuildRows();
  GuestInputGate::OnMenuShown();
  GuideSounds::Get().Play(GuideSounds::Cue::kOpen);
}

UpdateRequiredDialog::~UpdateRequiredDialog() {
  progress_.cancelled = true;
  if (worker_.joinable()) {
    worker_.join();
  }
}

void UpdateRequiredDialog::BuildRows() {
  rows_.clear();
  if (HttpDownload::IsAvailable()) {
    rows_.push_back({"Download Now", [this] { BeginDownload(); }});
  }
  if (NativeFilePicker::IsAvailable()) {
    rows_.push_back({"Install From This PC", [this] { RequestPackage(); }});
  }
  if (request_.can_play_without_update) {
    rows_.push_back({"Play Without the Update", [this] { Decline(); }});
  } else {
    rows_.push_back({"Quit", [this] {
                       Close();
                       if (request_.on_quit) {
                         request_.on_quit();
                       }
                     }});
  }
  selected_ = std::min(selected_, static_cast<int>(rows_.size()) - 1);
}

void UpdateRequiredDialog::OnDraw(ImGuiIO& io) {
  ImGuiGamepadBridge::FeedPrimaryController(io);
  BeginInstallIfPicked();
  JoinWorkerIfDone();

  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("##recomp_update_required", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoInputs);
  DrawScene(ImGui::GetWindowDrawList(), io);
  ImGui::End();

  if (frames_drawn_ < 2) {
    if (++frames_drawn_ == 2) {
      held_keys_->Arm(kWatchedKeys);
    }
    return;
  }
  if (stage_ == Stage::kAsking || stage_ == Stage::kFailed) {
    HandleInput();
  } else if (held_keys_->Pressed({ImGuiKey_Escape, ImGuiKey_GamepadFaceRight}, false)) {
    // B stops a download in its tracks; the question comes back.
    progress_.cancelled = true;
  }
}

void UpdateRequiredDialog::HandleInput() {
  if (rows_.empty()) {
    return;
  }
  const int before = selected_;
  const int count = static_cast<int>(rows_.size());
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
    GuideSounds::Get().Play(GuideSounds::Cue::kSelect);
    rows_[selected_].chosen();
    return;
  }
  if (held_keys_->Pressed({ImGuiKey_Escape, ImGuiKey_GamepadFaceRight}, false)) {
    // B is the console's Back, which on this screen is declining the update.
    GuideSounds::Get().Play(GuideSounds::Cue::kBack);
    if (request_.can_play_without_update) {
      Decline();
    } else {
      Close();
      if (request_.on_quit) {
        request_.on_quit();
      }
    }
  }
}

void UpdateRequiredDialog::Decline() {
  REXLOG_INFO("Title update declined; starting without it");
  Close();
  if (request_.on_declined) {
    app_context_.CallInUIThreadDeferred(std::move(request_.on_declined));
  }
}

void UpdateRequiredDialog::Fail(const std::string& message) {
  REXLOG_ERROR("Title update: {}", message);
  message_ = message;
  stage_ = Stage::kFailed;
  BuildRows();
}

void UpdateRequiredDialog::BeginDownload() {
  stage_ = Stage::kLooking;
  message_.clear();
  progress_.received = 0;
  progress_.total = 0;
  progress_.cancelled = false;
  worker_finished_ = false;
  worker_succeeded_ = false;

  worker_ = std::thread([this] {
    std::string error;
    const std::string list_url = XboxUnityCatalog::ListUrl(request_.descriptor.title_id);
    const std::filesystem::path listing = request_.download_folder / "updates.json";
    if (!HttpDownload::Fetch(list_url, listing, progress_, error)) {
      std::lock_guard<std::mutex> lock(worker_message_mutex_);
      worker_message_ = "The update archive could not be reached. " + error;
      worker_finished_ = true;
      return;
    }
    std::string body;
    {
      std::ifstream file(listing, std::ios::binary);
      std::ostringstream text;
      text << file.rdbuf();
      body = text.str();
    }
    std::error_code ec;
    std::filesystem::remove(listing, ec);

    std::vector<XboxUnityUpdate> updates;
    if (!XboxUnityCatalog::Parse(body, updates, error)) {
      std::lock_guard<std::mutex> lock(worker_message_mutex_);
      worker_message_ = error;
      worker_finished_ = true;
      return;
    }
    const auto chosen = XboxUnityCatalog::Choose(updates, request_.descriptor.media_id,
                                                 request_.descriptor.version);
    if (!chosen) {
      std::lock_guard<std::mutex> lock(worker_message_mutex_);
      worker_message_ = "The archive has no update for this disc.";
      worker_finished_ = true;
      return;
    }
    found_ = chosen;

    progress_.received = 0;
    progress_.total = chosen->size;
    const std::filesystem::path package = request_.download_folder / ("tu" + chosen->id);
    if (!HttpDownload::Fetch(XboxUnityCatalog::DownloadUrl(chosen->id), package, progress_,
                             error)) {
      std::lock_guard<std::mutex> lock(worker_message_mutex_);
      worker_message_ = error;
      worker_finished_ = true;
      return;
    }
    package_ = package;
    worker_succeeded_ = true;
    worker_finished_ = true;
  });
  stage_ = Stage::kDownloading;
}

void UpdateRequiredDialog::RequestPackage() {
  auto pending = std::make_shared<PendingPick>();
  pending_pick_ = pending;
  file_picker_.PickContentPackage("Select " + request_.descriptor.label,
                                  [pending](std::optional<std::filesystem::path> package) {
                                    std::lock_guard<std::mutex> lock(pending->mutex);
                                    pending->package = std::move(package);
                                    pending->completed = true;
                                  });
}

void UpdateRequiredDialog::BeginInstallIfPicked() {
  if (!pending_pick_) {
    return;
  }
  std::optional<std::filesystem::path> package;
  {
    std::lock_guard<std::mutex> lock(pending_pick_->mutex);
    if (!pending_pick_->completed) {
      return;
    }
    package = std::move(pending_pick_->package);
  }
  pending_pick_.reset();
  if (package && stage_ != Stage::kInstalling) {
    BeginInstall(*package);
  }
}

void UpdateRequiredDialog::BeginInstall(const std::filesystem::path& package) {
  if (worker_.joinable()) {
    worker_.join();
  }
  package_ = package;
  stage_ = Stage::kInstalling;
  message_.clear();
  worker_finished_ = false;
  worker_succeeded_ = false;
  worker_ = std::thread([this] {
    worker_succeeded_ = installer_.Install(package_, request_.install_folder, request_.descriptor);
    worker_finished_ = true;
  });
}

void UpdateRequiredDialog::JoinWorkerIfDone() {
  if (!worker_finished_ || !worker_.joinable()) {
    return;
  }
  worker_.join();
  worker_finished_ = false;

  if (stage_ == Stage::kDownloading || stage_ == Stage::kLooking) {
    if (!worker_succeeded_) {
      std::string message;
      {
        std::lock_guard<std::mutex> lock(worker_message_mutex_);
        message = worker_message_;
      }
      // A download the player stopped is not a failure to explain away.
      if (progress_.cancelled) {
        stage_ = Stage::kAsking;
        message_.clear();
        BuildRows();
        return;
      }
      Fail(message.empty() ? "The update could not be downloaded." : message);
      return;
    }
    BeginInstall(package_);
    return;
  }

  if (stage_ == Stage::kInstalling) {
    if (!worker_succeeded_) {
      Fail(installer_.error());
      return;
    }
    REXLOG_INFO("Title update installed from {}", rex::path_to_utf8(package_));
    Close();
    app_context_.CallInUIThreadDeferred(std::move(request_.on_installed));
  }
}

std::vector<std::string> UpdateRequiredDialog::BodyLines(float width, float text_size) const {
  std::string text;
  switch (stage_) {
    case Stage::kAsking:
      text = "An update is available for this game.\n\nDo you want to download it now?\n\n"
             "The update is optional. Without it the game plays as it did on the disc.";
      break;
    case Stage::kLooking:
      text = "Looking for this game's update...";
      break;
    case Stage::kDownloading:
      text = "Downloading the update...";
      break;
    case Stage::kInstalling:
      text = "Installing the update. It is checked against this build before the game starts.";
      break;
    case Stage::kFailed:
      text = message_ + "\n\nYou can try again, install a package you already have, or " +
             (request_.can_play_without_update ? "carry on without the update."
                                               : "quit.");
      break;
  }
  return Wrap(text, text_size, width);
}

std::string UpdateRequiredDialog::StatusLine() const {
  if (stage_ != Stage::kDownloading) {
    return {};
  }
  const uint64_t received = progress_.received;
  const uint64_t total = progress_.total;
  if (total == 0) {
    return HumanSize(received);
  }
  return HumanSize(received) + " of " + HumanSize(total);
}

void UpdateRequiredDialog::DrawScene(ImDrawList* draw_list, const ImGuiIO& io) {
  const Screen screen = FitScreen(io);
  const Palette palette = theme_.blades ? BladesPalette(theme_) : kMetro;
  const double now = ImGui::GetTime();
  if (opened_at_ < 0.0) {
    opened_at_ = now;
  }
  const float alpha = std::clamp(static_cast<float>((now - opened_at_) / kTransOpenSeconds), 0.0f,
                                 1.0f);

  const auto at = [&](float x, float y) {
    return screen.At((x - kCanvasWidth * 0.5f) * kCanvasToReference,
                     (y - kCanvasHeight * 0.5f) * kCanvasToReference);
  };
  const auto size = [&](float units) { return screen.Size(units * kCanvasToReference); };

  // Once the player has chosen, the console replaced the question with its own
  // Update in Progress panel. That is a different scene, not a state of this
  // one.
  if (stage_ == Stage::kLooking || stage_ == Stage::kDownloading ||
      stage_ == Stage::kInstalling) {
    DrawProgress(draw_list, io, screen, alpha);
    return;
  }

  const float left_width = kPaneWidth * kSplit;
  const float text_width = left_width - kPadding * 2.0f;
  const auto lines = BodyLines(size(text_width), size(kBodyTextSize));
  const float line_height = kBodyTextSize * kLineSpacing;
  const bool busy = false;
  const float rows_height = kRowHeight * static_cast<float>(rows_.size());
  const float bar_height = 0.0f;
  const float body_height = static_cast<float>(lines.size()) * line_height;
  // The rows run to the foot of the pane, as the console's do, so there is no
  // padding under the last one.
  const float pane_height = kPadding + body_height + bar_height + kPadding + rows_height;
  const float pane_top = (kCanvasHeight - pane_height) * 0.5f;

  draw_list->AddRectFilled(ImVec2(0.0f, 0.0f), io.DisplaySize, Fade(theme_.dim, alpha));

  // The header: the console's information icon and "Update Required", over the
  // panes and outside them, with the guide's drop shadow.
  const float header_y = pane_top - kHeaderTextSize - kHeaderGap;
  rex::ui::ImmediateTexture* icon = resources_ ? resources_->Get(kInfoIcon) : nullptr;
  const ImVec2 icon_center = at(kPaneX + kIconSize * 0.5f, header_y + kHeaderTextSize * 0.5f);
  if (icon) {
    const float half = size(kIconSize * 0.5f);
    draw_list->AddImage(reinterpret_cast<ImTextureID>(icon),
                        ImVec2(icon_center.x - half, icon_center.y - half),
                        ImVec2(icon_center.x + half, icon_center.y + half), ImVec2(0, 0),
                        ImVec2(1, 1), Fade(IM_COL32(255, 255, 255, 255), alpha));
  } else {
    draw_list->AddCircleFilled(icon_center, size(kIconSize * 0.5f), Fade(kInfoDisc, alpha), 32);
    const float letter = size(kIconSize * 0.62f);
    DrawText(draw_list,
             ImVec2(icon_center.x - TextWidth(letter, "i") * 0.5f, icon_center.y - letter * 0.56f),
             letter, Fade(IM_COL32(0xF5, 0xF5, 0xF5, 0xFF), alpha), "i");
  }
  const auto shadowed = [&](ImVec2 position, float text_size, const std::string& text) {
    const float offset = std::max(1.0f, size(1.0f));
    DrawText(draw_list, ImVec2(position.x + offset, position.y + offset), text_size,
             Fade(palette.shadow, alpha), text);
    DrawText(draw_list, position, text_size, Fade(palette.chrome, alpha), text);
  };
  shadowed(at(kPaneX + kIconSize + kIconGap, header_y), size(kHeaderTextSize), "Update Required");

  // The two panes.
  draw_list->AddRectFilled(at(kPaneX, pane_top), at(kPaneX + left_width, pane_top + pane_height),
                           Fade(kPaneFill, alpha));
  draw_list->AddRectFilled(at(kPaneX + left_width, pane_top),
                           at(kPaneX + kPaneWidth, pane_top + pane_height),
                           Fade(kAsidePaneFill, alpha));

  for (size_t i = 0; i < lines.size(); ++i) {
    DrawText(draw_list,
             at(kPaneX + kPadding, pane_top + kPadding + line_height * static_cast<float>(i)),
             size(kBodyTextSize), Fade(kBodyText, alpha), lines[i]);
  }

  // The rows, at the foot of the pale pane.
  const float rows_top = pane_top + pane_height - rows_height;
  for (size_t i = 0; i < rows_.size() && !busy; ++i) {
    const float row_top = rows_top + kRowHeight * static_cast<float>(i);
    const ImVec2 row_min = at(kPaneX, row_top);
    const ImVec2 row_max = at(kPaneX + left_width, row_top + kRowHeight);
    const bool focused = static_cast<int>(i) == selected_;
    if (focused) {
      draw_list->AddRectFilled(row_min, row_max, Fade(kFocus, alpha));
    } else if (i + 1 < rows_.size()) {
      draw_list->AddRectFilled(ImVec2(row_min.x, row_max.y - std::max(1.0f, size(1.0f))), row_max,
                               Fade(kRule, alpha));
    }
    const float text_size = size(kRowTextSize);
    DrawText(draw_list,
             ImVec2(row_min.x + size(kPadding), (row_min.y + row_max.y) * 0.5f - text_size * 0.58f),
             text_size, Fade(focused ? kRowFocusText : kRowText, alpha), rows_[i].label);
  }

  // The pane beside it: what is being updated, as the console listed the game
  // and the download's size.
  float aside_y = pane_top + kPadding;
  const float aside_x = kPaneX + left_width + kPadding;
  DrawText(draw_list, at(aside_x, aside_y), size(kAsideTitleSize), Fade(kAsideText, alpha),
           request_.game_display_name);
  aside_y += kAsideTitleSize * kLineSpacing * 1.4f;

  std::vector<std::string> aside;
  aside.push_back(request_.descriptor.label);
  if (found_) {
    aside.push_back(HumanSize(found_->size));
  } else {
    uint64_t declared = 0;
    for (const auto& patch : request_.descriptor.code_patches) {
      declared += patch.size;
    }
    if (declared > 0) {
      aside.push_back("at least " + HumanSize(declared));
    }
  }
  if (const std::string status = StatusLine(); !status.empty()) {
    aside.push_back(status);
  }
  for (const std::string& line : aside) {
    DrawText(draw_list, at(aside_x, aside_y), size(kBodyTextSize), Fade(kAsideText, alpha), line);
    aside_y += kBodyTextSize * kLineSpacing;
  }

  // A and B under the panes, as every other guide screen has them.
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

void UpdateRequiredDialog::DrawProgress(ImDrawList* draw_list, const ImGuiIO& io,
                                       const Screen& screen, float alpha) {
  const auto at = [&](float x, float y) {
    return screen.At((x - kCanvasWidth * 0.5f) * kCanvasToReference,
                     (y - kCanvasHeight * 0.5f) * kCanvasToReference);
  };
  const auto size = [&](float units) { return screen.Size(units * kCanvasToReference); };

  const float text_width = kProgressWidth - kPadding * 2.0f;
  const std::string body =
      "Please don't turn off your computer while the update is being installed. "
      "The game will restart when the update is complete.";
  const auto lines = Wrap(body, size(kBodyTextSize), size(text_width));
  const float line_height = kBodyTextSize * kLineSpacing;

  const float label_block = kBodyTextSize * kLineSpacing + kProgressBarHeight;
  const float content = kPadding + static_cast<float>(lines.size()) * line_height + kPadding +
                        label_block + kPadding;
  const float panel_height = std::max(kProgressMinHeight, kBandHeight + content);
  const float panel_top = (kCanvasHeight - panel_height) * 0.5f;

  draw_list->AddRectFilled(ImVec2(0.0f, 0.0f), io.DisplaySize, Fade(theme_.dim, alpha));

  // The header band, then the panel under it, both lit from the top.
  draw_list->AddRectFilledMultiColor(at(kProgressX, panel_top),
                                     at(kProgressX + kProgressWidth, panel_top + kBandHeight),
                                     Fade(kBandTop, alpha), Fade(kBandTop, alpha),
                                     Fade(kBandBottom, alpha), Fade(kBandBottom, alpha));
  draw_list->AddRectFilledMultiColor(at(kProgressX, panel_top + kBandHeight),
                                     at(kProgressX + kProgressWidth, panel_top + panel_height),
                                     Fade(kPanelTop, alpha), Fade(kPanelTop, alpha),
                                     Fade(kPanelBottom, alpha), Fade(kPanelBottom, alpha));

  // The information icon sits in the band, with the title beside it.
  rex::ui::ImmediateTexture* icon = resources_ ? resources_->Get(kInfoIcon) : nullptr;
  const ImVec2 icon_center =
      at(kProgressX + kPadding * 0.6f + kIconSize * 0.5f, panel_top + kBandHeight * 0.5f);
  if (icon) {
    const float half = size(kIconSize * 0.5f);
    draw_list->AddImage(reinterpret_cast<ImTextureID>(icon),
                        ImVec2(icon_center.x - half, icon_center.y - half),
                        ImVec2(icon_center.x + half, icon_center.y + half), ImVec2(0, 0),
                        ImVec2(1, 1), Fade(IM_COL32(255, 255, 255, 255), alpha));
  } else {
    draw_list->AddCircleFilled(icon_center, size(kIconSize * 0.5f), Fade(kInfoDisc, alpha), 32);
    const float letter = size(kIconSize * 0.62f);
    DrawText(draw_list,
             ImVec2(icon_center.x - TextWidth(letter, "i") * 0.5f, icon_center.y - letter * 0.56f),
             letter, Fade(IM_COL32(0xF5, 0xF5, 0xF5, 0xFF), alpha), "i");
  }
  const float band_text = size(kBandTextSize);
  DrawText(draw_list,
           ImVec2(at(kProgressX + kPadding * 0.6f + kIconSize + kIconGap, 0.0f).x,
                  icon_center.y - band_text * 0.58f),
           band_text, Fade(kPanelText, alpha), "Update in Progress");

  float y = panel_top + kBandHeight + kPadding;
  for (const std::string& line : lines) {
    DrawText(draw_list, at(kProgressX + kPadding, y), size(kBodyTextSize), Fade(kPanelText, alpha),
             line);
    y += line_height;
  }

  // The label and its bar sit at the foot of the panel, as the console has them.
  const float bar_top = panel_top + panel_height - kPadding - kProgressBarHeight;
  const float label_y = bar_top - kBodyTextSize * kLineSpacing;
  std::string label = "Downloading...";
  if (stage_ == Stage::kLooking) {
    label = "Looking for the update...";
  } else if (stage_ == Stage::kInstalling) {
    label = "Installing...";
  }
  if (const std::string status = StatusLine(); !status.empty()) {
    label += "  " + status;
  }
  DrawText(draw_list, at(kProgressX + kPadding, label_y), size(kBodyTextSize),
           Fade(kPanelText, alpha), label);

  const ImVec2 track_min = at(kProgressX + kPadding, bar_top);
  const ImVec2 track_max =
      at(kProgressX + kProgressWidth - kPadding, bar_top + kProgressBarHeight);
  draw_list->AddRectFilled(track_min, track_max, Fade(kProgressTrack, alpha));
  const uint64_t total = progress_.total;
  float fraction = 0.0f;
  if (stage_ == Stage::kInstalling) {
    fraction = 1.0f;
  } else if (total > 0) {
    fraction = std::clamp(static_cast<float>(progress_.received) / static_cast<float>(total), 0.0f,
                          1.0f);
  } else {
    // Nothing to measure yet: the console still showed a sliver moving, so the
    // bar creeps rather than sitting empty.
    fraction = 0.06f + 0.04f * static_cast<float>(std::sin(ImGui::GetTime() * 3.0));
  }
  draw_list->AddRectFilled(
      track_min, ImVec2(track_min.x + (track_max.x - track_min.x) * fraction, track_max.y),
      Fade(kProgressFill, alpha));

  // B Cancel, under the panel at its left edge.
  const float legend_text = size(kLegendTextSize);
  const float glyph = size(kLegendGlyph);
  const float legend_y = at(0.0f, panel_top + panel_height + kLegendGap * 0.6f).y;
  const float x = at(kProgressX, 0.0f).x;
  DrawGlyph(draw_list, ImVec2(x + glyph * 0.5f, legend_y), glyph, "B", alpha,
            reinterpret_cast<ImTextureID>(resources_ ? resources_->Get(ButtonPicture("B"))
                                                     : nullptr));
  const ImVec2 position(x + glyph + size(kLegendGlyphGap), legend_y - legend_text * 0.55f);
  const float offset = std::max(1.0f, size(1.0f));
  const Palette palette = theme_.blades ? BladesPalette(theme_) : kMetro;
  DrawText(draw_list, ImVec2(position.x + offset, position.y + offset), legend_text,
           Fade(palette.shadow, alpha), "Cancel");
  DrawText(draw_list, position, legend_text, Fade(palette.chrome, alpha), "Cancel");
}

}  // namespace recomp
