#include "recomp/platform/native_file_picker.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace recomp {

namespace {

bool CommandExists(std::string_view command) {
  const std::string probe = "command -v " + std::string(command) + " >/dev/null 2>&1";
  return std::system(probe.c_str()) == 0;
}

std::string QuoteForShell(const std::string& text) {
  std::string quoted = "'";
  for (char character : text) {
    quoted += character == '\'' ? std::string("'\\''") : std::string(1, character);
  }
  return quoted + "'";
}

std::vector<std::string> PickerCommands(const std::string& title) {
#if defined(__APPLE__)
  return {"osascript -e " +
          QuoteForShell("POSIX path of (choose file with prompt \"" + title + "\")")};
#else
  std::vector<std::string> commands;
  if (CommandExists("zenity")) {
    commands.push_back("zenity --file-selection --title=" + QuoteForShell(title) +
                       " --file-filter='Xbox 360 disc image | *.iso *.ISO'");
  }
  if (CommandExists("kdialog")) {
    commands.push_back("kdialog --title " + QuoteForShell(title) +
                       " --getopenfilename ~ '*.iso *.ISO'");
  }
  return commands;
#endif
}

std::optional<std::string> RunAndReadFirstLine(const std::string& command) {
  std::FILE* pipe = popen(command.c_str(), "r");
  if (!pipe) {
    return std::nullopt;
  }
  std::string output;
  std::array<char, 1024> chunk{};
  while (std::fgets(chunk.data(), static_cast<int>(chunk.size()), pipe)) {
    output += chunk.data();
  }
  if (pclose(pipe) != 0) {
    return std::nullopt;
  }
  while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
    output.pop_back();
  }
  return output.empty() ? std::nullopt : std::optional<std::string>(output);
}

}

bool NativeFilePicker::IsAvailable() {
#if defined(__APPLE__)
  return true;
#else
  return CommandExists("zenity") || CommandExists("kdialog");
#endif
}

void NativeFilePicker::PickDiscImage(const std::string& title, PickedHandler on_picked) const {
  for (const auto& command : PickerCommands(title)) {
    if (auto selected = RunAndReadFirstLine(command)) {
      on_picked(std::filesystem::path(*selected));
      return;
    }
  }
  on_picked(std::nullopt);
}

}
