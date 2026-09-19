#include "recomp/platform/application_restart.h"

namespace recomp {

bool RestartApplication(std::string& error) {
  error = "Application restart must use the Xbox console lifecycle API.";
  return false;
}

}  // namespace recomp
