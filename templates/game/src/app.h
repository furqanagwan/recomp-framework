#pragma once

#include <memory>

#include "recomp/app/game_recomp_app.h"

class @APP_CLASS@ final : public recomp::GameRecompApp {
 public:
  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& context) {
    return std::unique_ptr<@APP_CLASS@>(new @APP_CLASS@(context));
  }

 private:
  explicit @APP_CLASS@(rex::ui::WindowedAppContext& context)
      : GameRecompApp(context, Descriptor(), PPCImageConfig) {}

  static recomp::GameDescriptor Descriptor() {
    recomp::GameDescriptor descriptor;
    descriptor.app_name = "@PROJECT_NAME@";
    descriptor.display_name = "@DISPLAY_NAME@";
#ifdef RECOMP_DEVELOPMENT_GAME_ROOT
    descriptor.development_game_root = RECOMP_DEVELOPMENT_GAME_ROOT;
#endif
    return descriptor;
  }
};
