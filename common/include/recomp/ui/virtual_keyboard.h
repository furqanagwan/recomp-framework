#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace recomp {

// The on-screen keyboard a title opens with XamShowKeyboardUI, without any
// drawing: its keys, which one is highlighted, and the text being edited.
//
// The keys sit where the console's keyboard scene puts them (vkmedia's
// KeyboardBase.xur, which Dash.Search.xex and Title.Zune.xex carry): five rows of
// ten character keys, Backspace and Space under them, and a column either side
// for the cursor (LB, RB), the character pages (LT, RT), Caps (left stick) and
// Done (START). Which characters the console puts on each page is filled in by
// vk.xex's code, which no system update carries, so the pages are this
// framework's own.
class VirtualKeyboard {
 public:
  enum class KeyKind {
    kCharacter,
    kCaps,
    kSymbols,
    kAccents,
    kSpace,
    kBackspace,
    kDone,
    kCursorLeft,
    kCursorRight,
  };

  enum class Page { kLetters, kSymbols, kAccents };

  struct Key {
    KeyKind kind;
    int column;
    int row;
    int width;
    int height;
    // For kCharacter, the key's place in its page.
    int character = -1;
  };

  static constexpr int kColumns = 12;
  static constexpr int kRows = 6;
  static constexpr int kCharacterColumns = 10;
  static constexpr int kCharacterRows = 5;
  // The grid's fifty, then the two beside Space.
  static constexpr int kCharactersPerPage = kCharacterColumns * kCharacterRows + 2;
  // Those, Backspace and Space, and the six keys down the sides.
  static constexpr int kKeyCount = kCharactersPerPage + 2 + 6;

  VirtualKeyboard(std::u16string text, size_t max_length)
      : text_(std::move(text)), max_length_(max_length) {
    if (text_.size() > max_length_) {
      text_.resize(max_length_);
    }
    cursor_ = text_.size();
    for (int row = 0; row < kCharacterRows; ++row) {
      for (int column = 0; column < kCharacterColumns; ++column) {
        AddKey({KeyKind::kCharacter, column + 1, row, 1, 1, row * kCharacterColumns + column});
      }
    }
    AddKey({KeyKind::kBackspace, 1, 5, 4, 1});
    AddKey({KeyKind::kSpace, 5, 5, 4, 1});
    AddKey({KeyKind::kCharacter, 9, 5, 1, 1, kCharactersPerPage - 2});
    AddKey({KeyKind::kCharacter, 10, 5, 1, 1, kCharactersPerPage - 1});
    AddKey({KeyKind::kCursorLeft, 0, 0, 1, 2});
    AddKey({KeyKind::kSymbols, 0, 2, 1, 2});
    AddKey({KeyKind::kCaps, 0, 4, 1, 2});
    AddKey({KeyKind::kCursorRight, 11, 0, 1, 2});
    AddKey({KeyKind::kAccents, 11, 2, 1, 2});
    AddKey({KeyKind::kDone, 11, 4, 1, 2});
  }

  const std::u16string& text() const { return text_; }
  size_t cursor() const { return cursor_; }
  size_t max_length() const { return max_length_; }
  bool caps() const { return caps_; }
  Page page() const { return page_; }

  const std::array<Key, kKeyCount>& keys() const { return keys_; }
  const Key& selected() const { return keys_[static_cast<size_t>(KeyIndexAt(column_, row_))]; }
  bool IsSelected(const Key& key) const { return &key == &selected(); }

  // What a character key types on the current page.
  char16_t CharacterFor(const Key& key) const {
    const size_t index = static_cast<size_t>(key.character);
    switch (page_) {
      case Page::kSymbols:
        return kSymbolPage[index];
      case Page::kAccents:
        return caps_ ? LatinUpper(kAccentPage[index]) : kAccentPage[index];
      case Page::kLetters:
        break;
    }
    return caps_ ? LatinUpper(kLetterPage[index]) : kLetterPage[index];
  }

  // Moves the highlight one key over, wrapping at the edges. A key wider or
  // taller than one cell is stepped over in one move.
  void MoveSelection(int columns, int rows) {
    if (columns == 0 && rows == 0) {
      return;
    }
    const int start = KeyIndexAt(column_, row_);
    do {
      column_ = (column_ + columns + kColumns) % kColumns;
      row_ = (row_ + rows + kRows) % kRows;
    } while (KeyIndexAt(column_, row_) == start);
  }

  enum class Result {
    kChanged,  // the text, cursor or page changed
    kRefused,  // nothing to do: the text is full, or the cursor is at an end
    kDone,
  };

  // A on the highlighted key.
  Result Activate() {
    const Key& key = selected();
    switch (key.kind) {
      case KeyKind::kCharacter:
        return Outcome(Insert(CharacterFor(key)));
      case KeyKind::kCaps:
        ToggleCaps();
        return Result::kChanged;
      case KeyKind::kSymbols:
        TogglePage(Page::kSymbols);
        return Result::kChanged;
      case KeyKind::kAccents:
        TogglePage(Page::kAccents);
        return Result::kChanged;
      case KeyKind::kSpace:
        return Outcome(Insert(u' '));
      case KeyKind::kBackspace:
        return Outcome(Backspace());
      case KeyKind::kDone:
        return Result::kDone;
      case KeyKind::kCursorLeft:
        return Outcome(MoveCursor(-1));
      case KeyKind::kCursorRight:
        return Outcome(MoveCursor(1));
    }
    return Result::kRefused;
  }

  bool Insert(char16_t character) {
    if (text_.size() >= max_length_) {
      return false;
    }
    text_.insert(cursor_, 1, character);
    ++cursor_;
    return true;
  }

  bool Backspace() {
    if (cursor_ == 0) {
      return false;
    }
    text_.erase(cursor_ - 1, 1);
    --cursor_;
    return true;
  }

  bool Delete() {
    if (cursor_ >= text_.size()) {
      return false;
    }
    text_.erase(cursor_, 1);
    return true;
  }

  bool MoveCursor(int direction) {
    const size_t before = cursor_;
    if (direction < 0 && cursor_ > 0) {
      --cursor_;
    } else if (direction > 0 && cursor_ < text_.size()) {
      ++cursor_;
    }
    return cursor_ != before;
  }

  void MoveCursorToEnd(bool end) { cursor_ = end ? text_.size() : 0; }
  void ToggleCaps() { caps_ = !caps_; }
  // A page's key goes to it, or back to the letters when it is already showing.
  void TogglePage(Page page) { page_ = page_ == page ? Page::kLetters : page; }

 private:
  // Latin-1 only, so every key draws with the guide's font atlas.
  static constexpr std::u16string_view kLetterPage =
      u"1234567890qwertyuiopasdfghjkl'zxcvbnm-_@!?&#()/:;\",.";
  static constexpr std::u16string_view kSymbolPage =
      u"!@#$%^&*()`~-_=+[]{}\\|;:'\"<>/?\u00A3\u00A5\u00A2\u00A9\u00AE\u00B0\u00B1\u00D7\u00F7\u00A7"
      u"\u00BF\u00A1\u00AB\u00BB\u00B5\u00B6\u00B7\u00B9\u00B2\u00B3,.";
  static constexpr std::u16string_view kAccentPage =
      u"\u00E0\u00E1\u00E2\u00E3\u00E4\u00E5\u00E6\u00E7\u00E8\u00E9"
      u"\u00EA\u00EB\u00EC\u00ED\u00EE\u00EF\u00F0\u00F1\u00F2\u00F3"
      u"\u00F4\u00F5\u00F6\u00F8\u00F9\u00FA\u00FB\u00FC\u00FD\u00FE"
      u"\u00FF\u00DF\u00AA\u00BA\u00BC\u00BD\u00BE\u00AC\u00A6\u00A4"
      u"\u00A8\u00B4\u00B8\u00AF\u00BF\u00A1\u00AB\u00BB\u00B5\u00B7,.";

  static_assert(kLetterPage.size() == kCharactersPerPage &&
                    kSymbolPage.size() == kCharactersPerPage &&
                    kAccentPage.size() == kCharactersPerPage,
                "each page has a character for every key");

  // Upper case within ASCII and Latin-1; everything else stays as it is.
  static constexpr char16_t LatinUpper(char16_t c) {
    if (c >= u'a' && c <= u'z') {
      return static_cast<char16_t>(c - 0x20);
    }
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) {
      return static_cast<char16_t>(c - 0x20);
    }
    return c;
  }

  static Result Outcome(bool changed) { return changed ? Result::kChanged : Result::kRefused; }

  void AddKey(Key key) {
    const int index = key_count_++;
    keys_[static_cast<size_t>(index)] = key;
    for (int row = key.row; row < key.row + key.height; ++row) {
      for (int column = key.column; column < key.column + key.width; ++column) {
        grid_[static_cast<size_t>(row * kColumns + column)] = index;
      }
    }
  }

  int KeyIndexAt(int column, int row) const {
    return grid_[static_cast<size_t>(row * kColumns + column)];
  }

  std::u16string text_;
  size_t max_length_ = 0;
  size_t cursor_ = 0;
  bool caps_ = false;
  Page page_ = Page::kLetters;
  int column_ = 1;
  int row_ = 1;  // on q
  std::array<Key, kKeyCount> keys_{};
  std::array<int, kColumns * kRows> grid_{};
  int key_count_ = 0;
};

}  // namespace recomp
