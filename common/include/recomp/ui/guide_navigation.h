#pragma once

namespace recomp {

enum class GuideTab { kGames, kPlayer, kSettings };

// One bumper press moves one position; neither end wraps around.
constexpr GuideTab AdjacentGuideTab(GuideTab tab, int direction) {
  const int next = static_cast<int>(tab) + (direction > 0 ? 1 : direction < 0 ? -1 : 0);
  return static_cast<GuideTab>(next < 0 ? 0 : next > 2 ? 2 : next);
}

}  // namespace recomp
