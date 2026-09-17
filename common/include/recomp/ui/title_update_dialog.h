#pragma once

#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include <rex/ui/imgui_dialog.h>

#include "recomp/app/game_descriptor.h"
#include "recomp/installer/title_update_installer.h"
#include "recomp/platform/native_file_picker.h"

namespace rex::ui {
class WindowedAppContext;
}

namespace recomp {

struct TitleUpdateInstallRequest {
  std::string game_display_name;
  TitleUpdateDescriptor descriptor;
  std::filesystem::path install_folder;
  void* owner_window = nullptr;
  std::function<void()> on_installed;
  std::function<void()> on_quit;
};

class TitleUpdateDialog final : public rex::ui::ImGuiDialog {
 public:
  static void Show(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext& app_context,
                   TitleUpdateInstallRequest request);
  ~TitleUpdateDialog() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  struct PendingPick {
    std::mutex mutex;
    bool completed = false;
    std::optional<std::filesystem::path> package;
  };
  enum class Stage { kChoosing, kInstalling, kFailed };

  TitleUpdateDialog(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext& app_context,
                    TitleUpdateInstallRequest request);
  void RequestPackage();
  void BeginInstallIfPicked();
  void BeginInstall(const std::filesystem::path& package);
  void FinishInstallIfDone();

  rex::ui::WindowedAppContext& app_context_;
  TitleUpdateInstallRequest request_;
  NativeFilePicker file_picker_;
  TitleUpdateInstaller installer_;
  Stage stage_ = Stage::kChoosing;
  std::array<char, 4096> typed_path_{};
  std::shared_ptr<PendingPick> pending_pick_;
  std::filesystem::path package_;
  std::thread worker_;
  std::atomic<bool> worker_finished_{false};
  bool worker_succeeded_ = false;
};

}  // namespace recomp
