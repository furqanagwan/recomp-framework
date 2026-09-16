#pragma once

struct ImFont;
struct ImFontAtlas;

namespace recomp {
void ConfigureGuideFonts(ImFontAtlas* atlas);
ImFont* GuideFont();
}  // namespace recomp
