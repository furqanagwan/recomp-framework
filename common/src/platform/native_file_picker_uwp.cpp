#include "recomp/platform/native_file_picker.h"

#include <string_view>
#include <utility>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.AccessCache.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.h>

namespace recomp {

namespace {

using winrt::Windows::Storage::AccessCache::StorageApplicationPermissions;
using winrt::Windows::Storage::Pickers::FileOpenPicker;
using winrt::Windows::Storage::Pickers::PickerLocationId;
using winrt::Windows::Storage::Pickers::PickerViewMode;

winrt::fire_and_forget ShowDiscImagePicker(NativeFilePicker::PickedHandler on_picked) {
  std::optional<std::filesystem::path> disc_image;
  try {
    FileOpenPicker picker;
    picker.ViewMode(PickerViewMode::List);
    picker.SuggestedStartLocation(PickerLocationId::ComputerFolder);
    picker.FileTypeFilter().Append(L".iso");
    auto file = co_await picker.PickSingleFileAsync();
    if (file) {
      StorageApplicationPermissions::FutureAccessList().Add(file);
      disc_image = std::filesystem::path(std::wstring_view(file.Path()));
    }
  } catch (const winrt::hresult_error&) {
    disc_image.reset();
  }
  on_picked(std::move(disc_image));
}

}

bool NativeFilePicker::IsAvailable() {
  return true;
}

void NativeFilePicker::PickDiscImage(const std::string& title, PickedHandler on_picked) const {
  (void)title;
  (void)owner_window_;
  ShowDiscImagePicker(std::move(on_picked));
}

}
