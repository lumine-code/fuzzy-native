#include "diacritics.h"
#include "diacritics_table.h"

#include <algorithm>

namespace {

// Returns the lowercase ASCII fold for `cp`, or nullptr if it has none.
const char *lookup_fold(uint32_t cp) {
  // Binary search over the sorted table.
  int lo = 0, hi = kDiacriticFoldCount - 1;
  while (lo <= hi) {
    int mid = lo + (hi - lo) / 2;
    uint32_t mc = kDiacriticFolds[mid].codepoint;
    if (mc == cp) {
      return kDiacriticFolds[mid].base;
    } else if (mc < cp) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return nullptr;
}

// Decodes one UTF-8 code point starting at `i` in `s`.
// Sets `cp` to the scalar value and returns the number of bytes consumed.
// On malformed input, treats the byte as Latin-1 (returns 1).
size_t decode_utf8(const std::string &s, size_t i, uint32_t &cp) {
  unsigned char c = static_cast<unsigned char>(s[i]);
  if (c < 0x80) {
    cp = c;
    return 1;
  }
  size_t n;
  uint32_t min;
  if ((c & 0xE0) == 0xC0) {
    cp = c & 0x1F;
    n = 2;
    min = 0x80;
  } else if ((c & 0xF0) == 0xE0) {
    cp = c & 0x0F;
    n = 3;
    min = 0x800;
  } else if ((c & 0xF8) == 0xF0) {
    cp = c & 0x07;
    n = 4;
    min = 0x10000;
  } else {
    cp = c;
    return 1;
  }
  if (i + n > s.size()) {
    cp = c;
    return 1;
  }
  for (size_t k = 1; k < n; k++) {
    unsigned char cc = static_cast<unsigned char>(s[i + k]);
    if ((cc & 0xC0) != 0x80) {
      cp = c;
      return 1;
    }
    cp = (cp << 6) | (cc & 0x3F);
  }
  if (cp < min || cp > 0x10FFFF) {
    cp = c;
    return 1;
  }
  return n;
}

} // namespace

std::string fold_diacritics(const std::string &input,
                            std::vector<int> *pos_map) {
  std::string out;
  out.reserve(input.size());
  if (pos_map) {
    pos_map->clear();
    pos_map->reserve(input.size());
  }

  int utf16_offset = 0;
  size_t i = 0;
  while (i < input.size()) {
    uint32_t cp;
    size_t len = decode_utf8(input, i, cp);
    // UTF-16 width of this source code point (astral planes take 2 units).
    int utf16_width = cp >= 0x10000 ? 2 : 1;

    const char *fold = lookup_fold(cp);
    if (fold != nullptr) {
      for (const char *p = fold; *p; ++p) {
        out.push_back(*p);
        if (pos_map) pos_map->push_back(utf16_offset);
      }
    } else if (cp < 0x80) {
      char ch = static_cast<char>(cp);
      if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
      out.push_back(ch);
      if (pos_map) pos_map->push_back(utf16_offset);
    } else {
      // Unmapped multi-byte code point: pass the raw bytes through so it can't
      // accidentally match ASCII, but preserves positions.
      for (size_t k = 0; k < len; k++) {
        out.push_back(input[i + k]);
        if (pos_map) pos_map->push_back(utf16_offset);
      }
    }

    utf16_offset += utf16_width;
    i += len;
  }

  return out;
}
