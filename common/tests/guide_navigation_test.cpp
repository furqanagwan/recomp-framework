#include "recomp/ui/guide_navigation.h"

using recomp::AdjacentGuideTab;
using recomp::GuideTab;

static_assert(AdjacentGuideTab(GuideTab::kGames, -1) == GuideTab::kGames);
static_assert(AdjacentGuideTab(GuideTab::kGames, 1) == GuideTab::kPlayer);
static_assert(AdjacentGuideTab(GuideTab::kPlayer, -1) == GuideTab::kGames);
static_assert(AdjacentGuideTab(GuideTab::kPlayer, 1) == GuideTab::kSettings);
static_assert(AdjacentGuideTab(GuideTab::kSettings, -1) == GuideTab::kPlayer);
static_assert(AdjacentGuideTab(GuideTab::kSettings, 1) == GuideTab::kSettings);
static_assert(AdjacentGuideTab(AdjacentGuideTab(GuideTab::kGames, 1), 1) ==
              GuideTab::kSettings);
static_assert(AdjacentGuideTab(GuideTab::kPlayer, 0) == GuideTab::kPlayer);

int main() {}
