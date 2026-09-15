#include "recomp/debug/guest_image_dump.h"

#include <cstdio>
#include <cstdlib>

#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

namespace recomp {

void GuestImageDump::WriteAndExitIfRequested(rex::Runtime& runtime, const rex::PPCImageInfo& image) {
  const char* output_path = std::getenv(kEnvironmentVariable);
  if (!output_path || !*output_path) {
    return;
  }
  const uint8_t* image_bytes = runtime.memory()->TranslateVirtual(image.image_base);
  if (std::FILE* file = std::fopen(output_path, "wb")) {
    std::fwrite(image_bytes, 1, image.image_size, file);
    std::fclose(file);
    REXLOG_INFO("Wrote guest image {:08X}+{:X} to {}", uint32_t(image.image_base),
                uint32_t(image.image_size), output_path);
  }
  std::_Exit(0);
}

}
