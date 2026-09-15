#pragma once

#include <rex/image_info.h>

namespace rex {
class Runtime;
}

namespace recomp {

// Writes a loaded guest image to disk for the analysis scripts, then exits.
//   RECOMP_DUMP_IMAGE=<file>          the executable, right after it loads
//   RECOMP_DUMP_MODULE=<Name.xex>     with RECOMP_DUMP_IMAGE: that DLL module instead,
//                                     once the game has loaded it
class GuestImageDump {
 public:
  static constexpr const char* kEnvironmentVariable = "RECOMP_DUMP_IMAGE";
  static constexpr const char* kModuleEnvironmentVariable = "RECOMP_DUMP_MODULE";

  static void WriteAndExitIfRequested(rex::Runtime& runtime, const rex::PPCImageInfo& image);
};

}  // namespace recomp
