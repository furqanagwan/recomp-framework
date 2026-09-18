#include "recomp/platform/application_restart.h"

#include <filesystem>

#include <unistd.h>

#include <rex/filesystem.h>
#include <rex/logging.h>

namespace recomp {

namespace {

// The running program's own path. Linux keeps it in /proc; a host that does not
// is left to carry on in this process rather than guess.
std::filesystem::path ExecutablePath() {
  std::error_code ec;
  const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
  return ec ? std::filesystem::path() : self;
}

}  // namespace

bool RestartApplication(std::string& error) {
  const std::filesystem::path executable = ExecutablePath();
  if (executable.empty()) {
    error = "This system does not say where the program is running from.";
    return false;
  }

  const pid_t child = fork();
  if (child < 0) {
    error = "The program could not be started again.";
    return false;
  }
  if (child == 0) {
    const std::string path = rex::path_to_utf8(executable);
    char* const argv[] = {const_cast<char*>(path.c_str()), nullptr};
    execv(path.c_str(), argv);
    _exit(127);
  }
  REXLOG_INFO("Restarting to pick up the title update");
  return true;
}

}  // namespace recomp
