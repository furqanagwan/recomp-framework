#pragma once

#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <rex/ui/imgui_dialog.h>

#include "recomp/installer/disc_image_installer.h"
#include "recomp/platform/native_file_picker.h"

namespace rex::ui {
class WindowedAppContext;
}

namespace recomp {

struct DiscInstallRequest {
  std::string game_display_name;
  std::filesystem::path install_folder;
  void* owner_window = nullptr;
  std::function<void()> on_installed;
  std::function<void()> on_quit;
};

class DiscInstallDialog final : public rex::ui::ImGuiDialog {
 public:
  static void Show(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext& app_context,
                   DiscInstallRequest request);

  ~DiscInstallDialog() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  enum class Stage { kChoosingImage, kInstalling, kFailed };

  struct PendingPick {
    std::mutex mutex;
    bool completed = false;
    std::optional<std::filesystem::path> disc_image;
  };

  DiscInstallDialog(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext& app_context,
                    DiscInstallRequest request);

  void DrawChooseImage();
  void DrawInstalling();
  void RequestPickedDiscImage();
  void BeginInstallIfPicked();
  void FinishInstallIfDone();
  void BeginInstall(const std::filesystem::path& disc_image);

  rex::ui::WindowedAppContext& app_context_;
  DiscInstallRequest request_;
  NativeFilePicker file_picker_;
  std::shared_ptr<PendingPick> pending_pick_;
  Stage stage_ = Stage::kChoosingImage;
  std::array<char, 1024> typed_path_{};
  std::filesystem::path disc_image_;
  DiscImageInstaller installer_;
  InstallProgress progress_;
  std::thread worker_;
  std::atomic<bool> worker_finished_{false};
  std::atomic<bool> worker_succeeded_{false};
};

}
