#include "recomp/platform/gaming_runtime_session.h"

#include <rex/logging.h>

#if defined(RECOMP_HAS_GDK)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <XGameRuntimeInit.h>
#endif

namespace recomp {

GamingRuntimeSession::~GamingRuntimeSession() {
  End();
}

#if defined(RECOMP_HAS_GDK)

namespace {

bool IsGamingRuntimeInstalled() {
  HMODULE module = LoadLibraryW(L"xgameruntime.dll");
  if (!module) {
    return false;
  }
  FreeLibrary(module);
  return true;
}

}

bool GamingRuntimeSession::Begin() {
  if (active_) {
    return true;
  }
  if (!IsGamingRuntimeInstalled()) {
    REXLOG_INFO("Gaming Runtime unavailable; running without Xbox app integration");
    return false;
  }
  const HRESULT result = XGameRuntimeInitialize();
  if (FAILED(result)) {
    REXLOG_WARN("XGameRuntimeInitialize failed: 0x{:08X}", static_cast<uint32_t>(result));
    return false;
  }
  active_ = true;
  REXLOG_INFO("Gaming Runtime initialized");
  return true;
}

void GamingRuntimeSession::End() {
  if (active_) {
    XGameRuntimeUninitialize();
    active_ = false;
  }
}

#else

bool GamingRuntimeSession::Begin() {
  return false;
}

void GamingRuntimeSession::End() {
  active_ = false;
}

#endif

}
