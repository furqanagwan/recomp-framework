#pragma once

struct ImFont;
struct ImFontAtlas;

namespace recomp {
void ConfigureGuideFonts(ImFontAtlas* atlas);
ImFont* GuideFont();
// The same face rasterised large, for the guide's full-screen scene: drawn
// scaled down it stays sharp up to 4K, where the 16 pixel font would blur.
ImFont* GuideDisplayFont();
}  // namespace recomp
