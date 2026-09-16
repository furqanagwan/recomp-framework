#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>
#include <rex/system/achievement_store.h>
#include <rex/ui/imgui_dialog.h>
#include "recomp/ui/guide_navigation.h"
#include "recomp/ui/settings_dialog.h"

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
  SettingsContext settings;
  std::function<void()> exit_game;
  std::function<void()> on_closed;
  // The title's achievements, drawn on the guide's own screen. Null when the
  // game has none registered.
  rex::system::AchievementManager* achievements = nullptr;
  // For achievement icons that live in the title's XDBF rather than on disk.
  rex::Runtime* runtime = nullptr;
};

// The compatibility guide's screen, in the shape of the Xbox 360 guide a Series
// X shows over a backward-compatible title: the list between its tabs, with the
// title, the gamer tile and the clock on the game above it.
//
// It wears whichever era recomp_guide_theme names - metro, as a Series X does,
// or blades - and draws with the console's own artwork when the player has
// supplied a copy (see GuideResources).
class GuideDialog final : public rex::ui::ImGuiDialog {
 public:
  GuideDialog(rex::ui::ImGuiDrawer* drawer, GuideActions actions);
  ~GuideDialog() override;

  void RequestClose() { close_requested_ = true; }
  // Opens straight onto the achievements list, for a game that asked for it.
  void ShowAchievements();
  void ShowSettings(std::string section);

 protected:
  void OnDraw(ImGuiIO& io) override;
  void OnClose() override;

 private:
  enum class Page {
    kRoot,
    kAchievements,
    kSettings,
    kExitConfirmation,
  };

  struct Entry {
    std::string label;
    // The count the console shows at the right of a row.
    std::string value;
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
  void SwitchTab(int direction);
  void BuildSettings();
  void ChangeSetting(int direction);
  void SaveSettings();
  void DrawSettings(ImDrawList* draw_list, ImVec2 top_left, float width);
  void LoadAchievements();
  bool HasAchievements() const;

  // Moves the selection within a list and reports whether an entry was chosen.
  bool HandleInput(int& selection, int count, int page_rows);
  // A button held when the guide opened - a stuck stick on a phantom pad, or
  // the press that opened it - has to be let go before it counts.
  bool Pressed(std::initializer_list<ImGuiKey> keys, bool repeat);
  void ArmInput();

  // The root screen as hud.xex's GuideMain scene lays it out: the current tab's
  // list on the light centre blade, the other tabs as grey blades either side.
  void DrawBladeScene(ImDrawList* draw_list, const ImGuiIO& io);
  // The title, gamer tile and clock, which sit on the game above the panel.
  void DrawChrome(ImDrawList* draw_list, ImVec2 panel_min, ImVec2 panel_max);
  // The Games and Settings tabs down the sides, and the player's own between
  // them, as the console stacks them.
  float DrawTabs(ImDrawList* draw_list, ImVec2 panel_min, ImVec2 panel_max);
  void DrawEntries(ImDrawList* draw_list, ImVec2 top_left, float width);
  void DrawAchievements(ImDrawList* draw_list, ImVec2 top_left, float width, int visible_rows);
  void DrawHints(ImDrawList* draw_list, ImVec2 bottom_left, float width);
  void DrawExitConfirmation(ImDrawList* draw_list, ImVec2 top_left, float width);
  void DrawRowBackground(ImDrawList* draw_list, ImVec2 row_min, ImVec2 row_max, bool selected);
  // A glyph from the supplied artwork, or a drawn circle when there is none.
  void DrawButtonGlyph(ImDrawList* draw_list, ImVec2 center, const char* letter, ImU32 fallback);
  float DrawHint(ImDrawList* draw_list, float x, float y, const char* letter, ImU32 fallback,
                 const char* label);
  rex::ui::ImmediateTexture* Artwork(const std::string& name);
  // The first of these the supplied artwork has, if any.
  rex::ui::ImmediateTexture* FirstArtwork(std::initializer_list<const char*> names);

  GuideActions actions_;
  const GuideTheme& theme_;
  std::unique_ptr<GuideResources> resources_;
  std::unique_ptr<rex::ui::AchievementIconCache> icons_;

  std::vector<Entry> entries_;
  struct SettingRow {
    std::string label;
    std::string cvar;
    std::vector<std::pair<std::string, std::string>> choices;
    std::string description;
  };
  std::vector<SettingRow> setting_rows_;
  std::map<std::string, std::string> settings_at_open_;
  std::string settings_section_;
  std::string settings_status_;
  int setting_selected_ = 0;
  std::vector<AchievementRow> achievements_;
  bool achievements_loaded_ = false;
  int unlocked_count_ = 0;
  int earned_gamerscore_ = 0;
  int total_gamerscore_ = 0;

  int frames_drawn_ = 0;
  // What the blade scene last drew, so it can animate from it.
  double opened_at_ = -1.0;
  int drawn_tab_ = -1;
  int tab_from_ = -1;
  double tab_changed_at_ = 0.0;
  int drawn_selected_ = -1;
  int selected_from_ = -1;
  double selected_changed_at_ = 0.0;
  std::vector<ImGuiKey> masked_keys_;

  Page page_ = Page::kRoot;
  GuideTab tab_ = GuideTab::kPlayer;
  int selected_ = 0;
  int achievement_selected_ = 0;
  int achievement_scroll_ = 0;
  bool close_requested_ = false;
  int exit_choice_ = 1;  // Cancel, as on the console.
};

}  // namespace recomp
