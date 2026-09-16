#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <rex/system/achievement_store.h>
#include <imgui.h>
#include <rex/ui/imgui_dialog.h>

namespace rex {
class Runtime;
namespace system {
class AchievementManager;
}  // namespace system
namespace ui {
class AchievementIconCache;
class ImmediateTexture;
}  // namespace ui
}  // namespace rex

namespace recomp {

class GuideResources;
struct GuideTheme;

struct GuideActions {
  std::string game_display_name;
  std::function<void()> open_settings;
  std::function<void()> open_controls;
  std::function<void()> exit_game;
  std::function<void()> on_closed;
  // The title's achievements, drawn on the guide's own screen. Null when the
  // game has none registered.
  rex::system::AchievementManager* achievements = nullptr;
  // For achievement icons that live in the title's XDBF rather than on disk.
  rex::Runtime* runtime = nullptr;
};

// The compatibility guide's screen, in the shape of the Xbox 360 guide a Series
// X shows over a backward-compatible title: a panel with the green header, one
// column of entries, and the button glyphs along the bottom.
//
// It wears whichever era recomp_guide_theme names, and draws with the console's
// own artwork when the player has supplied a copy (see GuideResources).
class GuideDialog final : public rex::ui::ImGuiDialog {
 public:
  GuideDialog(rex::ui::ImGuiDrawer* drawer, GuideActions actions);
  ~GuideDialog() override;

  void RequestClose() { close_requested_ = true; }
  // Opens straight onto the achievements list, for a game that asked for it.
  void ShowAchievements();

 protected:
  void OnDraw(ImGuiIO& io) override;
  void OnClose() override;

 private:
  enum class Page {
    kRoot,
    kAchievements,
    kExitConfirmation,
  };

  struct Entry {
    std::string label;
    std::string hint;
    std::function<void()> activate;
    bool closes_guide = true;
    Page opens = Page::kRoot;
  };

  struct AchievementRow {
    rex::system::AchievementInfo info;
    bool unlocked = false;
    uint64_t unlocked_at = 0;
  };

  void BuildEntries();
  void LoadAchievements();
  bool HasAchievements() const;

  // Moves the selection within a list and reports whether an entry was chosen.
  bool HandleInput(int& selection, int count, int page_rows);
  // A button held when the guide opened - a stuck stick on a phantom pad, or
  // the press that opened it - has to be let go before it counts.
  bool Pressed(std::initializer_list<ImGuiKey> keys, bool repeat);
  void ArmInput();

  void DrawHeader(ImDrawList* draw_list, ImVec2 top_left, float width, const char* title,
                  const std::string& subtitle);
  void DrawEntries(ImDrawList* draw_list, ImVec2 top_left, float width);
  void DrawAchievements(ImDrawList* draw_list, ImVec2 top_left, float width, int visible_rows);
  void DrawFooter(ImDrawList* draw_list, ImVec2 bottom_left, float width);
  void DrawExitConfirmation(ImDrawList* draw_list, ImVec2 top_left, float width);
  void DrawRowBackground(ImDrawList* draw_list, ImVec2 row_min, ImVec2 row_max, bool selected);
  // A glyph from the supplied artwork, or a drawn circle when there is none.
  void DrawButtonGlyph(ImDrawList* draw_list, ImVec2 center, const char* letter, ImU32 fallback);
  float DrawFooterHint(ImDrawList* draw_list, float x, float y, const char* letter, ImU32 fallback,
                       const char* label);
  rex::ui::ImmediateTexture* Artwork(const std::string& name);

  GuideActions actions_;
  const GuideTheme& theme_;
  std::unique_ptr<GuideResources> resources_;
  std::unique_ptr<rex::ui::AchievementIconCache> icons_;

  std::vector<Entry> entries_;
  std::vector<AchievementRow> achievements_;
  bool achievements_loaded_ = false;
  int unlocked_count_ = 0;
  int earned_gamerscore_ = 0;
  int total_gamerscore_ = 0;

  int frames_drawn_ = 0;
  std::vector<ImGuiKey> masked_keys_;

  Page page_ = Page::kRoot;
  int selected_ = 0;
  int achievement_selected_ = 0;
  int achievement_scroll_ = 0;
  bool close_requested_ = false;
  int exit_choice_ = 1;  // Cancel, as on the console.
};

}  // namespace recomp
