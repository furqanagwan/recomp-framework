#include "recomp/app/game_recomp_app.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <thread>

#include <rex/cvar.h>
#include <rex/input/device_assignment.h>
#include <rex/input/input_system.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/platform/env.h>
#include <rex/system.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

#include "recomp/debug/guest_image_dump.h"
#include "recomp/debug/native_render_probe.h"
#include "recomp/installer/content_package_installer.h"
#include "recomp/installer/disc_image_installer.h"
#include "recomp/installer/title_update_installer.h"
#include "recomp/settings/user_settings_store.h"
#include "recomp/ui/disc_install_dialog.h"
#include "recomp/ui/monochrome_theme.h"
#include "recomp/ui/settings_dialog.h"
#include "recomp/ui/title_update_dialog.h"
#include "recomp/platform/application_restart.h"
#include "recomp/render/native_renderer.h"
#include "recomp/ui/update_required_dialog.h"
#include "recomp/ui/xbox_guide.h"
#include "recomp/ui/achievement_popup.h"
#include "recomp/ui/guide_fonts.h"

REXCVAR_DECLARE(bool, recomp_shared_controllers);

REXCVAR_DEFINE_DOUBLE(recomp_achievement_popup_after_seconds, 0.0, "Recomp",
                      "Show the title's first achievement notification this many seconds after "
                      "the game starts, without unlocking it. For trying the popup and for "
                      "screenshots in scripted runs. 0 disables it.")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace recomp {

namespace {

constexpr const char* kSystemMenuBind = "bind_recomp_system_menu";
constexpr const char* kUnattendedInstallVariable = "RECOMP_INSTALL_ISO";
constexpr const char* kContentPackageVariable = "RECOMP_INSTALL_DLC";
constexpr const char* kTitleUpdateVariable = "RECOMP_INSTALL_TU";
constexpr const char* kPrimaryGpuPlugin = "xenos";

std::unique_ptr<rex::system::IInputSystem> CreateSharedControllerInput(bool tool_mode) {
  auto input = rex::input::CreateDefaultInputSystem(tool_mode);
  input->SetDeviceAssignment(std::make_unique<rex::input::SharedAssignment>());
  return input;
}

}  // namespace

GameRecompApp::GameRecompApp(rex::ui::WindowedAppContext& context, GameDescriptor descriptor,
                             rex::PPCImageInfo image_info)
    : rex::ReXApp(context, descriptor.app_name, image_info),
      descriptor_(std::move(descriptor)),
      image_info_(image_info) {}

void GameRecompApp::OnConfigurePaths(rex::PathConfig& paths) {
  paths_.Configure(paths);
}

void GameRecompApp::OnPostInitLogging() {
  UserSettingsStore(paths_.settings_file()).Load();
  gaming_runtime_.Begin();
}

void GameRecompApp::OnPreSetup(rex::RuntimeConfig& config) {
  if (config.gpu_plugin.empty()) {
    config.gpu_plugin = kPrimaryGpuPlugin;
  }
  if (REXCVAR_GET(recomp_shared_controllers)) {
    config.input_factory = CreateSharedControllerInput;
  }
  config.apply_xex_patches = descriptor_.title_update.has_value();
}

void GameRecompApp::OnConfigureFonts(ImFontAtlas* atlas) {
  ConfigureGuideFonts(atlas);
}

void GameRecompApp::OnConfigureStyle(ImGuiStyle& imgui_style, rex::ui::Style& overlay_style) {
  MonochromeTheme::Apply(imgui_style, overlay_style);
}

void GameRecompApp::OnCreateDialogs(rex::ui::ImGuiDrawer*) {
  if (auto* game_window = window()) {
    game_window->SetTitle(descriptor_.display_name);
    game_window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kAutoHidden);
  }
  rex::ui::RegisterBind(kSystemMenuBind, "Escape", "Open the system menu",
                        [this] { ToggleSystemMenu(); });
}

std::optional<rex::PathConfig> GameRecompApp::OnFinalizePaths(
    const rex::PathConfig& defaults, std::function<void(rex::PathConfig)> resume) {
  rex::PathConfig paths = defaults;
  paths.game_data_root = paths_.ResolveGameRoot(defaults.game_data_root, descriptor_);
  game_data_root_ = paths.game_data_root;

  if (DiscImageInstaller::IsGameInstalled(game_data_root_)) {
    return FinalizeTitleUpdatePaths(std::move(paths), std::move(resume));
  }
  REXLOG_INFO("Game files not found at {}", game_data_root_.string());
  if (rex::platform::env::get(kUnattendedInstallVariable)) {
    if (!InstallFromEnvironment(game_data_root_)) {
      app_context().QuitFromUIThread();
      return std::nullopt;
    }
    return FinalizeTitleUpdatePaths(std::move(paths), std::move(resume));
  }

  DiscInstallDialog::Show(
      imgui_drawer(), app_context(),
      DiscInstallRequest{
          .game_display_name = descriptor_.display_name,
          .install_folder = game_data_root_,
          .owner_window = window() ? window()->GetNativeWindowHandle() : nullptr,
          .on_installed =
              [this, paths, resume = std::move(resume)]() mutable {
                if (auto finalized = FinalizeTitleUpdatePaths(std::move(paths), resume)) {
                  resume(std::move(*finalized));
                }
              },
          .on_quit = [this] { app_context().QuitFromUIThread(); },
      });
  return std::nullopt;
}

std::optional<rex::PathConfig> GameRecompApp::FinalizeTitleUpdatePaths(
    rex::PathConfig paths, std::function<void(rex::PathConfig)> resume) {
  if (!descriptor_.title_update) {
    paths.update_data_root.clear();
    std::filesystem::path unexpected_patch;
    if (HasUnexpectedCodePatch(paths.game_data_root, unexpected_patch)) {
      const std::string message =
          "This build targets the original disc executable, but a title-update patch was found "
          "at:\n\n" +
          unexpected_patch.string() +
          "\n\nRemove the .xexp file or use a build made for that title update.";
      REXLOG_ERROR("Refusing disc build with title-update patch {}", unexpected_patch.string());
      rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, message);
      app_context().QuitFromUIThread();
      return std::nullopt;
    }
    return paths;
  }

  const auto& update = *descriptor_.title_update;
  paths.update_data_root = paths_.user_data_root() / "title_update";
  if (TitleUpdateInstaller::IsInstalled(paths.update_data_root, update)) {
    return paths;
  }

  if (const auto source = rex::platform::env::get(kTitleUpdateVariable)) {
    TitleUpdateInstaller installer;
    if (!installer.Install(rex::to_path(*source), paths.update_data_root, update)) {
      REXLOG_ERROR("Unattended title update installation failed: {}", installer.error());
      rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error,
                                "Title update installation failed:\n\n" + installer.error());
      app_context().QuitFromUIThread();
      return std::nullopt;
    }
    return paths;
  }

  // The console asked before it updated a game, and took no for an answer. This
  // build is recompiled from the update's code, so there is nothing to fall back
  // to inside it; a release that also ships the disc-compiled executable sets
  // can_play_without_update and the launcher starts that one instead.
  UpdateRequiredDialog::Show(
      imgui_drawer(), app_context(),
      UpdateRequiredRequest{
          .game_display_name = descriptor_.display_name,
          .descriptor = update,
          .install_folder = paths.update_data_root,
          .download_folder = paths_.user_data_root() / "downloads",
          .owner_window = window() ? window()->GetNativeWindowHandle() : nullptr,
          .can_play_without_update = false,
          // The guest image is built as the runtime starts, so an update that
          // arrives after that only takes effect next launch - as it did on the
          // console, which restarted. Where the host will not start us again,
          // carrying on in this process still applies the patch correctly.
          .on_installed =
              [this, paths, resume = std::move(resume)]() mutable {
                std::string error;
                if (RestartApplication(error)) {
                  app_context().QuitFromUIThread();
                  return;
                }
                REXLOG_WARN("Carrying on without restarting: {}", error);
                resume(std::move(paths));
              },
          .on_declined = [this] { app_context().QuitFromUIThread(); },
          .on_quit = [this] { app_context().QuitFromUIThread(); },
      });
  return std::nullopt;
}

bool GameRecompApp::HasUnexpectedCodePatch(const std::filesystem::path& game_root,
                                           std::filesystem::path& patch) const {
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator
           it(game_root, std::filesystem::directory_options::skip_permission_denied, error),
       end;
       !error && it != end; it.increment(error)) {
    auto extension = it->path().extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) { return std::tolower(character); });
    if (it->is_regular_file(error) && extension == ".xexp") {
      patch = it->path();
      return true;
    }
  }
  return false;
}

void GameRecompApp::OnPostLoadXexImage() {
  GuestImageDump::WriteAndExitIfRequested(*runtime(), image_info_);
  InstallContentPackages();
}

void GameRecompApp::OnPostSetup() {
  NativeRenderer::Install(imgui_drawer());
  NativeRenderProbe::InstallIfRequested();
  guide_.Install(imgui_drawer(),
                 XboxGuide::Actions{
                     .game_display_name = descriptor_.display_name,
                     .settings = SettingsContext{
                         .settings_file = paths_.settings_file(),
                         .game_data_root = game_data_root_,
                         .user_data_root = paths_.user_data_root(),
                         .dlc_folder = paths_.dlc_folder(),
                         .portable = paths_.portable(),
                         .apply_fullscreen = [this](bool fullscreen) {
                           if (window()) window()->SetFullscreen(fullscreen);
                         },
                     },
                     .exit_game =
                         [this] {
                           if (auto* game_window = window()) {
                             game_window->RequestClose();
                           }
                         },
                     .achievements = &achievements(),
                     .runtime = runtime(),
                     .dlc = descriptor_.dlc,
                     .on_ui_thread =
                         [this](std::function<void()> work) {
                           app_context().CallInUIThreadDeferred(std::move(work));
                         },
                 });
  menu_watcher_.Start(static_cast<rex::input::InputSystem*>(runtime()->input_system()),
                      &app_context(), [this] { guide_.Open("View + Menu"); });
}

std::unique_ptr<rex::ui::AchievementNotificationDialog>
GameRecompApp::CreateAchievementNotificationDialog() {
  if (!imgui_drawer() || !runtime()) {
    return nullptr;
  }
  auto popup = std::make_unique<AchievementPopup>(imgui_drawer(), runtime());

  const double seconds = REXCVAR_GET(recomp_achievement_popup_after_seconds);
  if (seconds > 0.0) {
    std::thread([this, seconds] {
      std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
      const auto list = achievements().ListAchievements();
      if (!list.empty()) {
        REXLOG_INFO("Achievements: showing {} (recomp_achievement_popup_after_seconds)",
                    list.front().label);
        achievements().ShowAchievementNotification(list.front().id);
      }
    }).detach();
  }
  return popup;
}

void GameRecompApp::OnShutdown() {
  NativeRenderer::Shutdown();
  guide_.Uninstall();
  menu_watcher_.Stop();
  gaming_runtime_.End();
  rex::ui::UnregisterBind(kSystemMenuBind);
}

bool GameRecompApp::InstallFromEnvironment(const std::filesystem::path& game_root) {
  InstallProgress progress;
  DiscImageInstaller installer;
  const auto disc_image = rex::platform::env::get(kUnattendedInstallVariable).value_or("");
  if (installer.Install(rex::to_path(disc_image), game_root, progress)) {
    return true;
  }
  REXLOG_ERROR("Unattended install failed: {}", installer.error());
  return false;
}

void GameRecompApp::InstallContentPackages() {
  auto* kernel_state = runtime()->kernel_state();
  if (!kernel_state || !kernel_state->content_manager()) {
    return;
  }
  ContentPackageInstaller installer(*kernel_state->content_manager(), kernel_state->title_id());
  const auto dlc_folder = paths_.dlc_folder();
  std::error_code error;
  std::filesystem::create_directories(dlc_folder, error);
  int installed = installer.InstallFrom(dlc_folder);
  if (const auto source = rex::platform::env::get(kContentPackageVariable)) {
    installed += installer.InstallFrom(rex::to_path(*source));
  }
  if (installed > 0) {
    REXLOG_INFO("DLC: installed {} package(s)", installed);
  }
}

void GameRecompApp::ToggleSystemMenu() {
  guide_.Toggle();
}

}  // namespace recomp
