#pragma once

#include <rex/image_info.h>

namespace rex {
class Runtime;
}

namespace recomp {

class GuestImageDump {
 public:
  static constexpr const char* kEnvironmentVariable = "RECOMP_DUMP_IMAGE";

  static void WriteAndExitIfRequested(rex::Runtime& runtime, const rex::PPCImageInfo& image);
};

}
