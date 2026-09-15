#pragma once

struct ImGuiIO;

namespace recomp {

class ImGuiGamepadBridge {
 public:
  static void FeedPrimaryController(ImGuiIO& io);
};

}
