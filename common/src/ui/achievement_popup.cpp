#include "recomp/ui/achievement_popup.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>
#include <utility>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/overlay/achievement_icon_cache.h>

#include "recomp/ui/guide_fonts.h"
#include "recomp/ui/guide_resources.h"
#include "recomp/ui/guide_sounds.h"

namespace recomp {

namespace {

// scr_Notification runs 240 frames. The console plays XUI timelines at a
// fixed rate; 45 a second gives the unlock the five or so seconds it holds on
// screen.
constexpr double kFramesPerSecond = 45.0;
constexpr float kLastFrame = 240.0f;

// Notifications share the guide scene's 852 by 480 canvas; this one sits
// centred above the bottom of the title-safe area.
constexpr float kCanvasWidth = 852.0f;
constexpr float kCanvasHeight = 480.0f;
constexpr float kPopupWidth = 395.0f;
constexpr float kPopupTop = 370.0f;

// scr_Notification's geometry, in its own units.
constexpr float kBadgeX = 33.0f;  // centre of the logo and its badge
constexpr float kBadgeY = 29.6f;
constexpr float kBadgeRadius = 30.0f;
constexpr float kLogoRadius = 23.0f;  // logoback, 46 across
constexpr float kLogoImage = 58.0f;   // xenonLogo1
constexpr float kTrophyImage = 24.0f; // the Image presenter
constexpr float kBarLeft = 32.6f;     // bgCurve1's pivot, where the bar grows from
constexpr float kBarRight = 367.0f;
constexpr float kBarTop = 2.2f;
constexpr float kBarBottom = 58.0f;
constexpr float kTextLeft = 59.0f;
constexpr float kTextWidth = 283.0f;
constexpr float kTextPoints = 11.0f;

// Its colours.
const ImU32 kBarFill = IM_COL32(0x3C, 0x3C, 0x3C, 0xFF);
const ImU32 kBarEdge = IM_COL32(0x32, 0x32, 0x32, 0xFF);
const ImU32 kRim = IM_COL32(0xEB, 0xEB, 0xEB, 0x32);
const ImU32 kLogoBack = IM_COL32(0x0F, 0x0F, 0x0F, 0xFF);
const ImU32 kBadgeRing = IM_COL32(0x73, 0x73, 0x73, 0xFF);
const ImU32 kBurst = IM_COL32(0x0F, 0xEB, 0x0F, 0x96);
const ImU32 kBurstOuter = IM_COL32(0x82, 0xEB, 0x0F, 0x64);
const ImU32 kRingOfLight = IM_COL32(0x8C, 0xE7, 0x1E, 0xFF);
const ImU32 kRingOfLightCore = IM_COL32(0xC8, 0xEB, 0x64, 0xFF);
const ImU32 kText = IM_COL32(0xEB, 0xEB, 0xEB, 0xFF);

constexpr float kPi = 3.14159265f;

// A property's keyframes, (frame, value), eased linearly between as XUI does.
using Track = std::initializer_list<std::pair<float, float>>;

float Sample(Track track, float frame) {
  auto previous = *track.begin();
  if (frame <= previous.first) {
    return previous.second;
  }
  for (const auto& key : track) {
    if (frame <= key.first) {
      const float span = key.first - previous.first;
      const float t = span > 0.0f ? (frame - previous.first) / span : 1.0f;
      return previous.second + (key.second - previous.second) * t;
    }
    previous = key;
  }
  return previous.second;
}

ImU32 Fade(ImU32 color, float alpha) {
  const float a = float((color >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.0f, 1.0f);
  return (color & ~IM_COL32_A_MASK) | (ImU32(a) << IM_COL32_A_SHIFT);
}

ImFont* Font() {
  if (ImFont* font = GuideDisplayFont()) {
    return font;
  }
  return ImGui::GetFont();
}

}  // namespace

AchievementPopup::AchievementPopup(rex::ui::ImGuiDrawer* drawer, rex::Runtime* runtime)
    : AchievementNotificationDialog(drawer), runtime_(runtime) {}

AchievementPopup::~AchievementPopup() = default;

void AchievementPopup::Push(const rex::system::AchievementEvent& event) {
  std::lock_guard lock(mutex_);
  queue_.push_back(event);
}

rex::ui::ImmediateTexture* AchievementPopup::FirstArtwork(
    std::initializer_list<const char*> names) {
  if (!resources_) {
    return nullptr;
  }
  for (const char* name : names) {
    if (rex::ui::ImmediateTexture* texture = resources_->Get(name)) {
      return texture;
    }
  }
  return nullptr;
}

void AchievementPopup::OnDraw(ImGuiIO& io) {
  const auto now = std::chrono::steady_clock::now();
  {
    std::lock_guard lock(mutex_);
    if (showing_) {
      const double seconds = std::chrono::duration<double>(now - showing_->started).count();
      if (seconds * kFramesPerSecond > kLastFrame) {
        showing_.reset();
      }
    }
    if (!showing_ && !queue_.empty()) {
      showing_ = Showing{queue_.front(), now};
      queue_.pop_front();
      GuideSounds::Get().Play(GuideSounds::Cue::kNotification);
    }
    if (!showing_) {
      return;
    }
  }

  if (!resources_) {
    resources_ = std::make_unique<GuideResources>(imgui_drawer()->immediate_drawer());
    resources_->LoadIfNeeded();
    if (runtime_) {
      icons_ = std::make_unique<rex::ui::AchievementIconCache>(imgui_drawer()->immediate_drawer(),
                                                              runtime_);
    }
  }

  const float frame = float(std::chrono::duration<double>(now - showing_->started).count() *
                            kFramesPerSecond);
  const auto& achievement = showing_->event.achievement;

  const float scale = std::min(io.DisplaySize.x / kCanvasWidth, io.DisplaySize.y / kCanvasHeight);
  const ImVec2 origin((io.DisplaySize.x - kPopupWidth * scale) * 0.5f,
                      (io.DisplaySize.y - kCanvasHeight * scale) * 0.5f + kPopupTop * scale);
  const auto at = [&](float x, float y) {
    return ImVec2(origin.x + x * scale, origin.y + y * scale);
  };
  const ImVec2 badge = at(kBadgeX, kBadgeY);
  ImDrawList* draw = ImGui::GetForegroundDrawList();

  // The bar: stretches out of the badge past full width and settles, then
  // folds back into it.
  const float bar_scale = Sample({{23, 0.10f}, {27, 0.10f}, {44, 1.19f}, {51, 1.0f},
                                  {228, 1.0f}, {240, 0.076f}},
                                 frame);
  const float bar_alpha = Sample({{23, 0.0f}, {27, 0.95f}, {228, 0.95f}, {240, 0.0f}}, frame);
  if (bar_alpha > 0.0f) {
    const float right = kBarLeft + (kBarRight - kBarLeft) * bar_scale;
    const ImVec2 min = at(kBarLeft, kBarTop);
    const ImVec2 max = at(right, kBarBottom);
    const float radius = (max.y - min.y) * 0.5f;
    // Body, its darker edges and the soft light rim along top and bottom.
    draw->AddRectFilled(min, max, Fade(kBarFill, bar_alpha));
    draw->AddCircleFilled(ImVec2(max.x, (min.y + max.y) * 0.5f), radius, Fade(kBarFill, bar_alpha),
                          40);
    const float edge = std::max(1.0f, scale * 2.5f);
    draw->AddRectFilled(min, ImVec2(max.x, min.y + edge), Fade(kBarEdge, bar_alpha));
    draw->AddRectFilled(ImVec2(min.x, max.y - edge), max, Fade(kBarEdge, bar_alpha));
    draw->AddLine(ImVec2(min.x, min.y - edge * 0.5f), ImVec2(max.x, min.y - edge * 0.5f),
                  Fade(kRim, bar_alpha), edge);
    draw->AddLine(ImVec2(min.x, max.y + edge * 0.5f), ImVec2(max.x, max.y + edge * 0.5f),
                  Fade(kRim, bar_alpha), edge);
    draw->PathArcTo(ImVec2(max.x, (min.y + max.y) * 0.5f), radius + edge * 0.5f, -kPi * 0.5f,
                    kPi * 0.5f, 24);
    draw->PathStroke(Fade(kRim, bar_alpha), 0, edge);
  }

  // The round badge the logo sits in.
  const float round_alpha = Sample({{23, 0.0f}, {27, 0.95f}, {228, 0.95f}, {240, 0.0f}}, frame);
  if (round_alpha > 0.0f) {
    draw->AddCircleFilled(badge, kBadgeRadius * scale, Fade(kBarFill, round_alpha), 48);
    draw->AddCircle(badge, kBadgeRadius * scale + scale, Fade(kRim, round_alpha), 48,
                    std::max(1.0f, scale * 2.0f));
  }

  // Its grey rim and black back swell past size and settle; on the way out
  // they swell again and shrink away.
  const Track pop = {{0, 0.15f}, {28, 1.6f}, {39, 1.0f}, {196, 1.0f}, {210, 1.45f}, {215, 1.45f},
                     {233, 0.2f}};
  const float pop_scale = Sample(pop, frame);
  const float pop_alpha = frame > 233.0f ? 0.0f : Sample({{0, 0.0f}, {28, 1.0f}}, frame);
  if (pop_alpha > 0.0f) {
    draw->AddCircle(badge, 29.0f * scale * pop_scale, Fade(kBadgeRing, pop_alpha), 48,
                    std::max(1.0f, 4.0f * scale * pop_scale));
    const float back_alpha = Sample({{0, 0.0f}, {28, 1.0f}, {196, 1.0f}, {210, 0.68f},
                                     {215, 0.68f}, {233, 0.0f}},
                                    frame);
    draw->AddCircleFilled(badge, kLogoRadius * scale * std::min(pop_scale, 1.45f),
                          Fade(kLogoBack, back_alpha), 48);
  }

  // The green burst as the logo arrives, and again as it leaves.
  const bool bursting = (frame >= 5.0f && frame <= 48.0f) || (frame >= 186.0f && frame <= 210.0f);
  if (bursting) {
    const float burst_scale = Sample({{5, 0.09f}, {37, 1.12f}, {43, 0.9f}, {48, 0.72f},
                                      {186, 0.72f}, {205, 0.72f}, {210, 1.0f}},
                                     frame);
    const float burst_alpha = Sample({{5, 0.5f}, {37, 1.0f}, {43, 0.3f}, {48, 0.06f},
                                      {186, 0.0f}, {205, 0.5f}, {210, 0.0f}},
                                     frame);
    const float radius = 45.0f * scale * burst_scale;
    draw->AddCircle(badge, radius * 0.85f, Fade(kBurstOuter, burst_alpha), 48, radius * 0.25f);
    draw->AddCircle(badge, radius * 0.7f, Fade(kBurst, burst_alpha), 48, radius * 0.18f);
  }

  // The ring of light glints around the badge while the popup holds.
  if (frame >= 44.0f && frame <= 192.0f) {
    const float radius = (kBadgeRadius + 3.0f) * scale;
    const float spin = float(frame) * 0.04f;
    for (int quarter = 0; quarter < 4; ++quarter) {
      const float start = spin + kPi * 0.5f * float(quarter) + 0.2f;
      draw->PathArcTo(badge, radius, start, start + kPi * 0.3f, 12);
      draw->PathStroke(kRingOfLight, 0, std::max(1.5f, 3.0f * scale));
      draw->PathArcTo(badge, radius, start + kPi * 0.08f, start + kPi * 0.22f, 8);
      draw->PathStroke(kRingOfLightCore, 0, std::max(1.0f, 1.5f * scale));
    }
  }

  // The Xbox logo, taking turns with the achievement trophy.
  const float logo_alpha = frame < 5.0f || frame > 185.0f
                               ? 0.0f
                               : Sample({{5, 0.5f}, {37, 1.0f}, {60, 1.0f}, {65, 0.0f},
                                         {125, 0.0f}, {130, 1.0f}, {180, 1.0f}, {185, 0.0f}},
                                        frame);
  const float logo_scale = Sample({{5, 0.1f}, {37, 1.5f}, {43, 0.95f}}, frame);
  if (logo_alpha > 0.0f) {
    const float half = kLogoImage * 0.5f * scale * logo_scale;
    if (auto* logo = FirstArtwork({"xboxLogo.png", "xenonLogo.png"})) {
      draw->AddImage(reinterpret_cast<ImTextureID>(logo), ImVec2(badge.x - half, badge.y - half),
                     ImVec2(badge.x + half, badge.y + half), ImVec2(0, 0), ImVec2(1, 1),
                     Fade(IM_COL32(255, 255, 255, 255), logo_alpha));
    } else {
      // A green orb with its X, until the console's logo is supplied.
      draw->AddCircleFilled(badge, half * 0.62f, Fade(IM_COL32(0x5C, 0xB8, 0x1E, 0xFF), logo_alpha),
                            40);
      draw->AddCircleFilled(ImVec2(badge.x - half * 0.15f, badge.y - half * 0.2f), half * 0.3f,
                            Fade(IM_COL32(255, 255, 255, 60), logo_alpha), 24);
      const float arm = half * 0.32f;
      const float width = std::max(1.5f, half * 0.12f);
      draw->AddLine(ImVec2(badge.x - arm, badge.y - arm), ImVec2(badge.x + arm, badge.y + arm),
                    Fade(IM_COL32(255, 255, 255, 235), logo_alpha), width);
      draw->AddLine(ImVec2(badge.x + arm, badge.y - arm), ImVec2(badge.x - arm, badge.y + arm),
                    Fade(IM_COL32(255, 255, 255, 235), logo_alpha), width);
    }
  }
  const float trophy_alpha = Sample({{65, 0.0f}, {70, 1.0f}, {120, 1.0f}, {125, 0.0f},
                                     {185, 0.0f}, {186, 1.0f}, {192, 0.0f}},
                                    frame);
  if (trophy_alpha > 0.0f) {
    // The console's own name for it. The older names stay as fallbacks so a
    // folder someone extracted before this still works.
    rex::ui::ImmediateTexture* trophy =
        FirstArtwork({"ico_64x_trophy.png", "Achievement.png", "ico_32x_achievement.png"});
    // Without the console's trophy, the achievement's own picture, larger.
    const float size = trophy ? kTrophyImage : 40.0f;
    if (!trophy && icons_) {
      trophy = icons_->GetIcon(achievement);
    }
    if (trophy) {
      const float half = size * 0.5f * scale;
      draw->AddImage(reinterpret_cast<ImTextureID>(trophy), ImVec2(badge.x - half, badge.y - half),
                     ImVec2(badge.x + half, badge.y + half), ImVec2(0, 0), ImVec2(1, 1),
                     Fade(IM_COL32(255, 255, 255, 255), trophy_alpha));
    }
  }

  // "Achievement unlocked" over the gamerscore and name.
  const float text_alpha = Sample({{38, 0.0f}, {44, 1.0f}, {217, 1.0f}, {223, 0.0f}}, frame);
  if (text_alpha > 0.0f) {
    const float size = kTextPoints * (4.0f / 3.0f) * scale;
    const float width = kTextWidth * scale;
    std::string detail = std::to_string(achievement.gamerscore) + "G - " + achievement.label;
    ImFont* font = Font();
    while (detail.size() > 4 &&
           font->CalcTextSizeA(size, FLT_MAX, 0.0f, detail.c_str()).x > width) {
      detail.resize(detail.size() - 4);
      detail += "...";
    }
    const ImVec2 left = at(kTextLeft, 0.0f);
    const float middle = at(0.0f, (kBarTop + kBarBottom) * 0.5f).y;
    draw->AddText(font, size, ImVec2(left.x, middle - size * 1.05f), Fade(kText, text_alpha),
                  "Achievement unlocked");
    draw->AddText(font, size, ImVec2(left.x, middle + size * 0.02f), Fade(kText, text_alpha),
                  detail.c_str());
  }
}

}  // namespace recomp
