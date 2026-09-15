#pragma once

namespace recomp {

class GamingRuntimeSession {
 public:
  GamingRuntimeSession() = default;
  GamingRuntimeSession(const GamingRuntimeSession&) = delete;
  GamingRuntimeSession& operator=(const GamingRuntimeSession&) = delete;
  ~GamingRuntimeSession();

  bool Begin();
  void End();

 private:
  bool active_ = false;
};

}
