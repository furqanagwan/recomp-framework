#include "recomp/platform/native_file_picker.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <commdlg.h>

namespace recomp {

bool NativeFilePicker::IsAvailable() {
  return true;
}

void NativeFilePicker::PickDiscImage(const std::string& title, PickedHandler on_picked) const {
  wchar_t selected[4096] = L"";
  std::wstring wide_title(title.begin(), title.end());
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = static_cast<HWND>(owner_window_);
  dialog.lpstrFilter = L"Xbox 360 disc image (*.iso)\0*.iso\0All files (*.*)\0*.*\0";
  dialog.lpstrFile = selected;
  dialog.nMaxFile = static_cast<DWORD>(std::size(selected));
  dialog.lpstrTitle = wide_title.c_str();
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&dialog)) {
    on_picked(std::nullopt);
    return;
  }
  on_picked(std::filesystem::path(selected));
}

}
