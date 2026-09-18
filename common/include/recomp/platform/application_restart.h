#pragma once

#include <string>

namespace recomp {

// Starts this program again with the arguments it was given and asks the caller
// to quit, which is how a title update takes effect: the guest image is built
// while the runtime starts, so a freshly installed update only applies to the
// next launch, as it did on the console.
//
// Returns false with a reason when the host cannot do it, leaving the caller to
// carry on in this process instead.
bool RestartApplication(std::string& error);

}  // namespace recomp
