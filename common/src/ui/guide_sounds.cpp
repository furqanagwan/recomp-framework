#include "recomp/ui/guide_sounds.h"

#include <algorithm>
#include <cmath>
#include <span>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "recomp/ui/guide_resources.h"

REXCVAR_DEFINE_BOOL(recomp_guide_sounds, true, "Recomp",
                    "Play the console's interface sounds in the guide and for notifications, "
                    "when the guide's resources include them.");

REXCVAR_DEFINE_DOUBLE(recomp_guide_volume, 0.8, "Recomp",
                      "Volume of the guide's interface sounds, 0 to 1.")
    .range(0.0, 1.0);

namespace recomp {

namespace {

// The files each cue plays, first found wins: the guide's own sounds from
// hud.xex and xam.xex's xam package, then the shared ones from shrdres. Static,
// so the lists outlive the call that hands them out.
std::span<const char* const> Files(GuideSounds::Cue cue) {
  static constexpr const char* kOpen[] = {"HUD_open.xma", "BladeOpen.xma"};
  static constexpr const char* kClose[] = {"HUD_close.xma", "btn_Back.xma"};
  static constexpr const char* kFocus[] = {"btn_Focus.xma"};
  static constexpr const char* kSelect[] = {"btn_selectG.xma", "btn_Select.xma"};
  static constexpr const char* kBack[] = {"btn_backG.xma", "btn_Back.xma"};
  static constexpr const char* kTabSwitch[] = {"tab_Switch.xma"};
  static constexpr const char* kNotification[] = {"NotifyPopup.xma"};
  switch (cue) {
    case GuideSounds::Cue::kOpen:
      return kOpen;
    case GuideSounds::Cue::kClose:
      return kClose;
    case GuideSounds::Cue::kFocus:
      return kFocus;
    case GuideSounds::Cue::kSelect:
      return kSelect;
    case GuideSounds::Cue::kBack:
      return kBack;
    case GuideSounds::Cue::kTabSwitch:
      return kTabSwitch;
    case GuideSounds::Cue::kNotification:
      return kNotification;
  }
  return {};
}

float PeakLevel(const rex::audio::PcmSound& sound) {
  float peak = 0.0f;
  for (float sample : sound.samples) {
    peak = std::max(peak, std::abs(sample));
  }
  return peak;
}

}  // namespace

GuideSounds& GuideSounds::Get() {
  static GuideSounds sounds;
  return sounds;
}

GuideSounds::GuideSounds() = default;

GuideSounds::~GuideSounds() = default;

const rex::audio::PcmSound* GuideSounds::Load(const std::string& name) {
  auto cached = sounds_.find(name);
  if (cached != sounds_.end()) {
    return cached->second.get();
  }
  std::unique_ptr<rex::audio::PcmSound> sound;
  if (const std::vector<uint8_t>* bytes = resources_->Bytes(name)) {
    if (auto decoded = rex::audio::DecodeXmaFile(*bytes)) {
      REXLOG_INFO("Guide: sound {} decoded, {:.2f} s, {} ch at {} Hz, peak {:.2f}", name,
                  double(decoded->samples.size()) / decoded->channels / decoded->sample_rate,
                  decoded->channels, decoded->sample_rate, PeakLevel(*decoded));
      sound = std::make_unique<rex::audio::PcmSound>(std::move(*decoded));
    } else {
      REXLOG_WARN("Guide: sound {} did not decode", name);
    }
  }
  const rex::audio::PcmSound* raw = sound.get();
  sounds_.emplace(name, std::move(sound));
  return raw;
}

void GuideSounds::Play(Cue cue, int from_tab) {
  if (!REXCVAR_GET(recomp_guide_sounds)) {
    return;
  }
  std::lock_guard lock(mutex_);
  if (!resources_) {
    // Sounds need the files, not the drawer.
    resources_ = std::make_unique<GuideResources>(nullptr);
    resources_->LoadIfNeeded();
  }

  const rex::audio::PcmSound* sound = nullptr;
  if (cue == Cue::kTabSwitch) {
    // hud.xex's GuideMain plays BladeSwitch_N leaving the Nth tab.
    sound = Load("BladeSwitch_" + std::to_string(std::clamp(from_tab, 0, 3) + 1) + ".xma");
  }
  for (const char* file : Files(cue)) {
    if (sound) {
      break;
    }
    sound = Load(file);
  }
  if (!sound) {
    return;
  }
  if (!player_) {
    player_ = std::make_unique<rex::audio::UiSoundPlayer>();
  }
  player_->Play(*sound, float(REXCVAR_GET(recomp_guide_volume)));
}

}  // namespace recomp
