#include "recomp/ui/virtual_keyboard.h"

#include <cstdio>

using recomp::VirtualKeyboard;
using KeyKind = VirtualKeyboard::KeyKind;
using Result = VirtualKeyboard::Result;

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAILED: %s\n", what);
    ++failures;
  }
}

}  // namespace

int main() {
  {
    VirtualKeyboard keyboard(u"", 20);
    int cells = 0;
    bool all_sized = true;
    for (const auto& key : keyboard.keys()) {
      cells += key.width * key.height;
      all_sized = all_sized && key.width > 0 && key.height > 0;
    }
    Check(all_sized, "every key has a size");
    Check(cells == VirtualKeyboard::kColumns * VirtualKeyboard::kRows,
          "the keys cover the grid exactly once");
  }
  {
    VirtualKeyboard keyboard(u"", 20);
    Check(keyboard.CharacterFor(keyboard.selected()) == u'q', "the highlight starts on q");
    keyboard.MoveSelection(-1, 0);
    Check(keyboard.selected().kind == KeyKind::kCursorLeft, "left of q is the LB cursor key");
    keyboard.MoveSelection(-1, 0);
    Check(keyboard.selected().kind == KeyKind::kCursorRight, "the row wraps to the RB key");
    keyboard.MoveSelection(0, 1);
    Check(keyboard.selected().kind == KeyKind::kAccents, "one step down a two-row key");
    keyboard.MoveSelection(0, 1);
    Check(keyboard.selected().kind == KeyKind::kDone, "Done is at the bottom right");
  }
  {
    // Along the bottom row: Backspace and Space are four keys wide each.
    VirtualKeyboard keyboard(u"", 20);
    keyboard.MoveSelection(0, 4);
    Check(keyboard.selected().kind == KeyKind::kBackspace, "Backspace is under q's column");
    keyboard.MoveSelection(1, 0);
    Check(keyboard.selected().kind == KeyKind::kSpace, "Space follows Backspace");
    keyboard.MoveSelection(1, 0);
    Check(keyboard.CharacterFor(keyboard.selected()) == u',', "then the comma key");
  }
  {
    VirtualKeyboard keyboard(u"Team", 6);
    Check(keyboard.cursor() == 4, "the cursor starts after the default text");
    keyboard.ToggleCaps();
    Check(keyboard.Activate() == Result::kChanged && keyboard.text() == u"TeamQ",
          "caps types upper case");
    keyboard.TogglePage(VirtualKeyboard::Page::kAccents);
    Check(keyboard.Activate() == Result::kChanged && keyboard.text() == u"TeamQ\u00CA",
          "the accents page follows caps");
    Check(keyboard.Activate() == Result::kRefused, "the title's length is a hard limit");
    keyboard.TogglePage(VirtualKeyboard::Page::kAccents);
    Check(keyboard.page() == VirtualKeyboard::Page::kLetters, "a page's key toggles back");
    keyboard.MoveCursor(-1);
    keyboard.MoveCursor(-1);
    Check(keyboard.Backspace() && keyboard.text() == u"TeaQ\u00CA",
          "backspace deletes before the cursor");
    Check(keyboard.Delete() && keyboard.text() == u"Tea\u00CA", "delete removes after the cursor");
  }
  {
    VirtualKeyboard keyboard(u"much too long", 4);
    Check(keyboard.text() == u"much", "default text is cut to the limit");
  }
  return failures == 0 ? 0 : 1;
}
