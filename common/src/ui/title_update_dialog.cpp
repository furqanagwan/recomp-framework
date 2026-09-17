#include "recomp/ui/title_update_dialog.h"

#include <imgui.h>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/windowed_app_context.h>

#include "recomp/input/imgui_gamepad_bridge.h"
#include "recomp/ui/dialog_layout.h"

namespace recomp {

void TitleUpdateDialog::Show(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext& app_context,
                             TitleUpdateInstallRequest request) {
  new TitleUpdateDialog(drawer, app_context, std::move(request));
}

TitleUpdateDialog::TitleUpdateDialog(rex::ui::ImGuiDrawer* drawer,
                                     rex::ui::WindowedAppContext& app_context,
                                     TitleUpdateInstallRequest request)
    : ImGuiDialog(drawer),
      app_context_(app_context),
      request_(std::move(request)),
      file_picker_(request_.owner_window) {}

TitleUpdateDialog::~TitleUpdateDialog() {
  if (worker_.joinable()) {
    worker_.join();
  }
}

void TitleUpdateDialog::OnDraw(ImGuiIO& io) {
  ImGuiGamepadBridge::FeedPrimaryController(io);
  BeginInstallIfPicked();
  FinishInstallIfDone();
  if (stage_ == Stage::kInstalling && worker_succeeded_) {
    return;
  }

  DialogLayout::DrawBackdrop("##recomp_title_update_backdrop", io, 1.0f);
  DialogLayout::BeginCenteredPanel((request_.game_display_name + " Setup").c_str(), io, 640.0f);
  ImGui::TextUnformatted("Title Update");
  ImGui::Separator();
  ImGui::Spacing();
  if (stage_ == Stage::kInstalling) {
    ImGui::TextWrapped("Installing %s from %s...", request_.descriptor.label.c_str(),
                       package_.filename().string().c_str());
    ImGui::TextDisabled("The package is verified before the game starts.");
  } else {
    ImGui::TextWrapped(
        "This build targets %s. Select your Xbox 360 title update package. A disc-only build "
        "does not require this update.",
        request_.descriptor.label.c_str());
    ImGui::Spacing();
    if (stage_ == Stage::kFailed) {
      ImGui::TextWrapped("Installation failed: %s", installer_.error().c_str());
      ImGui::Spacing();
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##title_update_path", "Path to title update package",
                             typed_path_.data(), typed_path_.size());
    if (NativeFilePicker::IsAvailable()) {
      ImGui::BeginDisabled(pending_pick_ != nullptr);
      if (ImGui::Button("Browse", ImVec2(120.0f, 0.0f))) {
        RequestPackage();
      }
      ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Install", ImVec2(120.0f, 0.0f)) && typed_path_[0] != '\0') {
      BeginInstall(rex::to_path(typed_path_.data()));
    }
    ImGui::SameLine();
    if (ImGui::Button("Quit", ImVec2(120.0f, 0.0f))) {
      Close();
      if (request_.on_quit)
        request_.on_quit();
    }
  }
  ImGui::End();
}

void TitleUpdateDialog::RequestPackage() {
  auto pending = std::make_shared<PendingPick>();
  pending_pick_ = pending;
  file_picker_.PickContentPackage("Select " + request_.descriptor.label,
                                  [pending](std::optional<std::filesystem::path> package) {
                                    std::lock_guard<std::mutex> lock(pending->mutex);
                                    pending->package = std::move(package);
                                    pending->completed = true;
                                  });
}

void TitleUpdateDialog::BeginInstallIfPicked() {
  if (!pending_pick_)
    return;
  std::optional<std::filesystem::path> package;
  {
    std::lock_guard<std::mutex> lock(pending_pick_->mutex);
    if (!pending_pick_->completed)
      return;
    package = std::move(pending_pick_->package);
  }
  pending_pick_.reset();
  if (package && stage_ != Stage::kInstalling)
    BeginInstall(*package);
}

void TitleUpdateDialog::BeginInstall(const std::filesystem::path& package) {
  package_ = package;
  stage_ = Stage::kInstalling;
  worker_finished_ = false;
  worker_succeeded_ = false;
  worker_ = std::thread([this] {
    worker_succeeded_ = installer_.Install(package_, request_.install_folder, request_.descriptor);
    worker_finished_ = true;
  });
}

void TitleUpdateDialog::FinishInstallIfDone() {
  if (stage_ != Stage::kInstalling || !worker_finished_)
    return;
  worker_.join();
  if (!worker_succeeded_) {
    REXLOG_ERROR("Title update installation failed: {}", installer_.error());
    stage_ = Stage::kFailed;
    return;
  }
  Close();
  app_context_.CallInUIThreadDeferred(std::move(request_.on_installed));
}

}  // namespace recomp
