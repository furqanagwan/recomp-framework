#include "recomp/platform/native_file_picker.h"

namespace recomp {

bool NativeFilePicker::IsAvailable() { return false; }

void NativeFilePicker::PickDiscImage(const std::string&, PickedHandler on_picked) const {
  on_picked(std::nullopt);
}

void NativeFilePicker::PickContentPackage(const std::string&, PickedHandler on_picked) const {
  on_picked(std::nullopt);
}

}  // namespace recomp
