#include "recomp/platform/application_restart.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <vector>

#include <rex/logging.h>

namespace recomp {

bool RestartApplication(std::string& error) {
  wchar_t executable[MAX_PATH] = {};
  if (GetModuleFileNameW(nullptr, executable, MAX_PATH) == 0) {
    error = "The program's own path could not be read.";
    return false;
  }

  // CreateProcessW may write to the command line it is given, so it gets a copy.
  std::wstring command_line = GetCommandLineW();
  std::vector<wchar_t> mutable_command_line(command_line.begin(), command_line.end());
  mutable_command_line.push_back(L'\0');

  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  if (!CreateProcessW(executable, mutable_command_line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      nullptr, &startup, &process)) {
    error = "The program could not be started again (error " + std::to_string(GetLastError()) + ").";
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  REXLOG_INFO("Restarting to pick up the title update");
  return true;
}

}  // namespace recomp
