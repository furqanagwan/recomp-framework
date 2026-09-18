#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <imgui.h>
#include <rex/ui/imgui_dialog.h>

#include "recomp/app/game_descriptor.h"
#include "recomp/installer/title_update_installer.h"
#include "recomp/installer/xboxunity_catalog.h"
#include "recomp/platform/http_download.h"
#include "recomp/platform/native_file_picker.h"

namespace rex::ui {
class WindowedAppContext;
}

namespace recomp {

namespace guide_scene {
struct Screen;
}

class GuideResources;
class HeldKeyMask;
struct GuideTheme;

struct UpdateRequiredRequest {
  std::string game_display_name;
  TitleUpdateDescriptor descriptor;
  std::filesystem::path install_folder;
  // Where a downloaded package is kept, so a failed install can be retried
  // without fetching it again.
  std::filesystem::path download_folder;
  void* owner_window = nullptr;
  // False when this build has no disc-compiled executable to fall back to, in
  // which case declining leaves nothing to run and the last row quits instead.
  bool can_play_without_update = false;
  std::function<void()> on_installed;
  std::function<void()> on_declined;
  std::function<void()> on_quit;
};

// The screen the console showed when a game had an update waiting, drawn in the
// guide's skin over the game.
//
// An update was always the player's choice: the console asked, and declining
// carried on with the disc. This keeps that, so the rows are Download Now, a
// package from the player's own machine, and carrying on without it. The
// console's line about being signed out of Xbox Live is left off, there being
// no Xbox Live to sign out of.
//
// Xbox Live no longer serves these packages, so Download Now asks XboxUnity's
// archive instead. Nothing depends on it: when the archive is unreachable, has
// no update for this disc, or hands back a package that fails its digest check,
// the screen says so and the other two rows still work.
class UpdateRequiredDialog final : public rex::ui::ImGuiDialog {
 public:
  static void Show(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext& app_context,
                   UpdateRequiredRequest request);
  ~UpdateRequiredDialog() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  enum class Stage {
    kAsking,      // the console's question, with its rows
    kLooking,     // asking the archive what it has
    kDownloading, // fetching the package
    kInstalling,  // unpacking and verifying it
    kFailed,      // something went wrong; the rows come back
  };
  struct Row {
    std::string label;
    std::function<void()> chosen;
  };
  struct PendingPick {
    std::mutex mutex;
    bool completed = false;
    std::optional<std::filesystem::path> package;
  };

  UpdateRequiredDialog(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext& app_context,
                       UpdateRequiredRequest request);

  void BuildRows();
  void HandleInput();
  void DrawScene(ImDrawList* draw_list, const ImGuiIO& io);
  void DrawProgress(ImDrawList* draw_list, const ImGuiIO& io,
                    const guide_scene::Screen& screen, float alpha);
  std::vector<std::string> BodyLines(float width, float text_size) const;
  std::string StatusLine() const;

  void BeginDownload();
  void RequestPackage();
  void BeginInstallIfPicked();
  void BeginInstall(const std::filesystem::path& package);
  void JoinWorkerIfDone();
  void Fail(const std::string& message);
  void Decline();

  rex::ui::WindowedAppContext& app_context_;
  UpdateRequiredRequest request_;
  const GuideTheme& theme_;
  std::unique_ptr<GuideResources> resources_;
  std::unique_ptr<HeldKeyMask> held_keys_;
  NativeFilePicker file_picker_;
  TitleUpdateInstaller installer_;

  Stage stage_ = Stage::kAsking;
  std::vector<Row> rows_;
  int selected_ = 0;
  std::string message_;
  std::optional<XboxUnityUpdate> found_;
  std::filesystem::path package_;

  HttpDownload::Progress progress_;
  std::thread worker_;
  std::atomic<bool> worker_finished_{false};
  bool worker_succeeded_ = false;
  std::mutex worker_message_mutex_;
  std::string worker_message_;

  std::shared_ptr<PendingPick> pending_pick_;
  double opened_at_ = -1.0;
  int frames_drawn_ = 0;
};

}  // namespace recomp
