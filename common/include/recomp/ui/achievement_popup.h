#pragma once

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

#include <rex/ui/overlay/achievement_notification.h>

namespace rex {
class Runtime;
namespace ui {
class AchievementIconCache;
}  // namespace ui
}  // namespace rex

namespace recomp {

class GuideResources;

// "Achievement unlocked", as an Xbox 360 shows it over a game: the Xbox logo
// blooms in a green burst, a dark bar stretches out from it with the
// achievement's gamerscore and name, the ring of light glints while it holds,
// then everything folds back into the badge and pops away. NotifyPopup.xma
// plays as it opens.
//
// Timing, shapes and colours come from huduiskin.xex's scr_Notification. The
// logo, trophy and chime come from the guide's resources when supplied; drawn
// stand-ins take their place otherwise. Unlocks arriving together queue.
class AchievementPopup final : public rex::ui::AchievementNotificationDialog {
 public:
  AchievementPopup(rex::ui::ImGuiDrawer* drawer, rex::Runtime* runtime);
  ~AchievementPopup() override;

  // Safe from any thread, the guest's included.
  void Push(const rex::system::AchievementEvent& event) override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  struct Showing {
    rex::system::AchievementEvent event;
    std::chrono::steady_clock::time_point started;
  };

  rex::Runtime* runtime_ = nullptr;
  std::unique_ptr<GuideResources> resources_;
  std::unique_ptr<rex::ui::AchievementIconCache> icons_;

  std::mutex mutex_;
  std::deque<rex::system::AchievementEvent> queue_;
  std::optional<Showing> showing_;
};

}  // namespace recomp
