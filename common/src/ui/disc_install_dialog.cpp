#include "recomp/ui/disc_install_dialog.h"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include <rex/logging.h>
#include <rex/ui/windowed_app_context.h>

#include "recomp/input/imgui_gamepad_bridge.h"
#include "recomp/ui/dialog_layout.h"

namespace recomp {

namespace {

std::string FormatGigabytes(uint64_t bytes) {
  char text[32];
  std::snprintf(text, sizeof(text), "%.2f GB", double(bytes) / (1024.0 * 1024.0 * 1024.0));
  return text;
}

}

void DiscInstallDialog::Show(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext& app_context,
                             DiscInstallRequest request) {
  new DiscInstallDialog(drawer, app_context, std::move(request));
}

DiscInstallDialog::DiscInstallDialog(rex::ui::ImGuiDrawer* drawer,
                                     rex::ui::WindowedAppContext& app_context,
                                     DiscInstallRequest request)
    : ImGuiDialog(drawer),
      app_context_(app_context),
      request_(std::move(request)),
      file_picker_(request_.owner_window) {}

DiscInstallDialog::~DiscInstallDialog() {
  if (worker_.joinable()) {
    worker_.join();
  }
}

void DiscInstallDialog::OnDraw(ImGuiIO& io) {
  ImGuiGamepadBridge::FeedPrimaryController(io);
  BeginInstallIfPicked();
  FinishInstallIfDone();
  if (stage_ == Stage::kInstalling && worker_succeeded_) {
    return;
  }

  DialogLayout::DrawBackdrop("##recomp_install_backdrop", io, 1.0f);
  DialogLayout::BeginCenteredPanel((request_.game_display_name + " Setup").c_str(), io, 640.0f);
  ImGui::TextUnformatted("Game Files");
  ImGui::Separator();
  ImGui::Spacing();
  if (stage_ == Stage::kInstalling) {
    DrawInstalling();
  } else {
    DrawChooseImage();
  }
  ImGui::End();
}

void DiscInstallDialog::DrawChooseImage() {
  ImGui::TextWrapped("%s game files were not found. Select your own Xbox 360 disc image to install them.",
                     request_.game_display_name.c_str());
  ImGui::Spacing();
  ImGui::TextDisabled("Install folder");
  ImGui::TextWrapped("%s", request_.install_folder.string().c_str());
  if (stage_ == Stage::kFailed) {
    ImGui::Spacing();
    ImGui::TextWrapped("Installation failed: %s", installer_.error().c_str());
  }
  ImGui::Spacing();
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint("##disc_image_path", "Path to .iso", typed_path_.data(), typed_path_.size());

  if (NativeFilePicker::IsAvailable()) {
    ImGui::BeginDisabled(pending_pick_ != nullptr);
    if (ImGui::Button("Browse", ImVec2(120.0f, 0.0f))) {
      RequestPickedDiscImage();
    }
    ImGui::EndDisabled();
  }
  ImGui::SameLine();
  if (ImGui::Button("Install", ImVec2(120.0f, 0.0f)) && typed_path_[0] != '\0') {
    BeginInstall(std::filesystem::u8path(typed_path_.data()));
    return;
  }
  ImGui::SameLine();
  if (ImGui::Button("Quit", ImVec2(120.0f, 0.0f))) {
    Close();
    if (request_.on_quit) {
      request_.on_quit();
    }
  }
}

void DiscInstallDialog::DrawInstalling() {
  const uint64_t total = progress_.total_bytes.load();
  const uint64_t copied = progress_.copied_bytes.load();
  const float fraction = total ? float(double(copied) / double(total)) : 0.0f;
  ImGui::TextWrapped("Installing from %s", disc_image_.filename().string().c_str());
  ImGui::Spacing();
  ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), "");
  ImGui::Text("%s / %s", FormatGigabytes(copied).c_str(), FormatGigabytes(total).c_str());
  ImGui::TextDisabled("This only happens once.");
}

void DiscInstallDialog::RequestPickedDiscImage() {
  auto pending_pick = std::make_shared<PendingPick>();
  pending_pick_ = pending_pick;
  file_picker_.PickDiscImage("Select your " + request_.game_display_name + " disc image",
                             [pending_pick](std::optional<std::filesystem::path> disc_image) {
                               std::lock_guard<std::mutex> lock(pending_pick->mutex);
                               pending_pick->disc_image = std::move(disc_image);
                               pending_pick->completed = true;
                             });
}

void DiscInstallDialog::BeginInstallIfPicked() {
  if (!pending_pick_) {
    return;
  }
  std::optional<std::filesystem::path> disc_image;
  {
    std::lock_guard<std::mutex> lock(pending_pick_->mutex);
    if (!pending_pick_->completed) {
      return;
    }
    disc_image = std::move(pending_pick_->disc_image);
  }
  pending_pick_.reset();
  if (disc_image && stage_ != Stage::kInstalling) {
    BeginInstall(*disc_image);
  }
}

void DiscInstallDialog::FinishInstallIfDone() {
  if (stage_ != Stage::kInstalling || !worker_finished_) {
    return;
  }
  worker_.join();
  if (!worker_succeeded_) {
    REXLOG_ERROR("Game file installation failed: {}", installer_.error());
    stage_ = Stage::kFailed;
    return;
  }
  REXLOG_INFO("Game files installed to {}", request_.install_folder.string());
  Close();
  app_context_.CallInUIThreadDeferred(std::move(request_.on_installed));
}

void DiscInstallDialog::BeginInstall(const std::filesystem::path& disc_image) {
  disc_image_ = disc_image;
  progress_.copied_bytes = 0;
  progress_.total_bytes = 0;
  worker_finished_ = false;
  worker_succeeded_ = false;
  stage_ = Stage::kInstalling;
  REXLOG_INFO("Installing game files from {} to {}", disc_image.string(), request_.install_folder.string());
  worker_ = std::thread([this] {
    worker_succeeded_ = installer_.Install(disc_image_, request_.install_folder, progress_);
    worker_finished_ = true;
  });
}

}
