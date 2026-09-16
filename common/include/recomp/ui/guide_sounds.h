#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <rex/audio/ui_sound.h>

namespace recomp {

class GuideResources;

// The console's interface sounds, for the guide and the notifications drawn
// over a game.
//
// They come from the same player-supplied folder as the guide's artwork (see
// GuideResources): hud.xex's blade sounds, xam.xex's open, close and
// notification chimes, the skin's button clicks. Each decodes the first time
// it plays. With nothing supplied, or recomp_guide_sounds off, it stays quiet.
class GuideSounds {
 public:
  enum class Cue {
    kOpen,          // the guide appears
    kClose,         // and goes
    kFocus,         // the highlight moves to another row
    kSelect,        // A on a row
    kBack,          // B back out of a page
    kTabSwitch,     // the blades move to another tab
    kNotification,  // a notification, such as an achievement, pops up
  };

  static GuideSounds& Get();

  // from_tab picks the console's per-tab blade sound for kTabSwitch.
  void Play(Cue cue, int from_tab = 0);

 private:
  GuideSounds();
  ~GuideSounds();

  const rex::audio::PcmSound* Load(const std::string& name);

  std::mutex mutex_;
  std::unique_ptr<GuideResources> resources_;
  std::unique_ptr<rex::audio::UiSoundPlayer> player_;
  // Decoded sounds by file name; a null entry marks one that is missing or
  // did not decode, so it is not tried again.
  std::unordered_map<std::string, std::unique_ptr<rex::audio::PcmSound>> sounds_;
};

}  // namespace recomp
