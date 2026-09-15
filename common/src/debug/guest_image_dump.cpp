#include "recomp/debug/guest_image_dump.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/platform/env.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xmemory.h>

namespace recomp {

namespace {

// Tight: a DLL can crash its first thread within a millisecond of registering.
constexpr auto kModulePollInterval = std::chrono::milliseconds(1);
constexpr auto kModuleWaitLimit = std::chrono::minutes(3);

void WriteImage(rex::Runtime& runtime, uint32_t base, uint32_t size, const char* output_path) {
  const uint8_t* image_bytes = runtime.memory()->TranslateVirtual(base);
  if (std::FILE* file = rex::filesystem::OpenFile(rex::to_path(output_path), "wb")) {
    std::fwrite(image_bytes, 1, size, file);
    std::fclose(file);
    REXLOG_INFO("Wrote guest image {:08X}+{:X} to {}", base, size, output_path);
  } else {
    REXLOG_ERROR("Could not write guest image to {}", output_path);
  }
}

// DLL modules load while the game runs, so wait for the module on a host thread.
void DumpModuleWhenLoaded(rex::Runtime& runtime, std::string module_name, std::string output_path) {
  std::thread([&runtime, module_name = std::move(module_name), output_path = std::move(output_path)] {
    const auto deadline = std::chrono::steady_clock::now() + kModuleWaitLimit;
    while (std::chrono::steady_clock::now() < deadline) {
      if (auto* kernel_state = runtime.kernel_state()) {
        auto module = kernel_state->GetModule(module_name, true);
        auto* user_module = static_cast<rex::system::UserModule*>(module.get());
        if (user_module && user_module->xex_module() && user_module->xex_module()->base_address()) {
          WriteImage(runtime, user_module->xex_module()->base_address(),
                     user_module->xex_module()->image_size(), output_path.c_str());
          std::_Exit(0);
        }
      }
      std::this_thread::sleep_for(kModulePollInterval);
    }
    REXLOG_ERROR("Module {} was not loaded within the dump wait limit", module_name);
    std::_Exit(1);
  }).detach();
}

}  // namespace

void GuestImageDump::WriteAndExitIfRequested(rex::Runtime& runtime, const rex::PPCImageInfo& image) {
  const auto output_path = rex::platform::env::get(kEnvironmentVariable);
  if (!output_path || output_path->empty()) {
    return;
  }
  if (const auto module_name = rex::platform::env::get(kModuleEnvironmentVariable); module_name && !module_name->empty()) {
    REXLOG_INFO("Waiting for module {} to load before dumping it", *module_name);
    DumpModuleWhenLoaded(runtime, *module_name, *output_path);
    return;
  }
  WriteImage(runtime, uint32_t(image.image_base), uint32_t(image.image_size), output_path->c_str());
  std::_Exit(0);
}

}  // namespace recomp
