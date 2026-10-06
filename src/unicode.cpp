#include "unicode.h"
#include "unicode_case_table.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace {

template <size_t N>
bool in_ranges(char32_t value, const UnicodePropertyRange (&ranges)[N]) {
  const auto found = std::lower_bound(
      std::begin(ranges), std::end(ranges), value,
      [](const UnicodePropertyRange &range, char32_t cp) { return range.end < cp; });
  return found != std::end(ranges) && found->start <= value;
}

// Native callers supply UTF-8 from JavaScript strings. Invalid bytes are still
// consumed individually, matching the existing diacritic decoder's fallback.
size_t decode_codepoint(const std::string &input, size_t offset, char32_t &cp) {
  const auto first = static_cast<unsigned char>(input[offset]);
  cp = first;
  if (first < 0x80) return 1;
  size_t width;
  char32_t minimum;
  if ((first & 0xE0) == 0xC0) {
    cp = first & 0x1F;
    width = 2;
    minimum = 0x80;
  } else if ((first & 0xF0) == 0xE0) {
    cp = first & 0x0F;
    width = 3;
    minimum = 0x800;
  } else if ((first & 0xF8) == 0xF0) {
    cp = first & 0x07;
    width = 4;
    minimum = 0x10000;
  } else {
    return 1;
  }
  if (offset + width > input.size()) {
    cp = first;
    return 1;
  }
  for (size_t i = 1; i < width; i++) {
    const auto byte = static_cast<unsigned char>(input[offset + i]);
    if ((byte & 0xC0) != 0x80) {
      cp = first;
      return 1;
    }
    cp = (cp << 6) | (byte & 0x3F);
  }
  if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    cp = first;
    return 1;
  }
  return width;
}

} // namespace

char32_t simple_case_fold(char32_t value) {
  if (value < 0x80) return value >= U'A' && value <= U'Z' ? value + (U'a' - U'A') : value;
  const auto found = std::lower_bound(
      std::begin(kUnicodeCaseFolds), std::end(kUnicodeCaseFolds), value,
      [](const UnicodeCaseFold &entry, char32_t cp) { return entry.codepoint < cp; });
  return found != std::end(kUnicodeCaseFolds) && found->codepoint == value ? found->folded : value;
}

bool is_unicode_uppercase(char32_t value) {
  if (value < 0x80) return value >= U'A' && value <= U'Z';
  return in_ranges(value, kUnicodeUppercaseRanges);
}

bool is_unicode_lowercase(char32_t value) {
  if (value < 0x80) return value >= U'a' && value <= U'z';
  return in_ranges(value, kUnicodeLowercaseRanges);
}

bool is_unicode_titlecase(char32_t value) {
  if (value < 0x80) return false;
  return in_ranges(value, kUnicodeTitlecaseRanges);
}

UnicodeText decode_unicode(const std::string &input, const std::vector<int> *fold_map) {
  UnicodeText result;
  size_t codepoints = 0;
  for (size_t i = 0; i < input.size(); codepoints++) {
    char32_t cp;
    i += decode_codepoint(input, i, cp);
  }
  result.value.reserve(codepoints);
  result.folded.reserve(codepoints);
  result.utf16_offsets.reserve(codepoints);
  result.utf16_widths.reserve(codepoints);
  int utf16_offset = 0;
  for (size_t i = 0; i < input.size();) {
    char32_t cp;
    const size_t bytes = decode_codepoint(input, i, cp);
    const int width = cp >= 0x10000 ? 2 : 1;
    result.value.push_back(cp);
    result.folded.push_back(simple_case_fold(cp));
    result.utf16_offsets.push_back(fold_map ? (*fold_map)[i] : utf16_offset);
    result.utf16_widths.push_back(width);
    utf16_offset += width;
    i += bytes;
  }
  return result;
}

void unicode_match_indexes_to_utf16(const UnicodeText &text, std::vector<int> &indexes) {
  std::vector<int> mapped;
  mapped.reserve(indexes.size());
  int previous = -1;
  for (const int index : indexes) {
    if (index < 0 || static_cast<size_t>(index) >= text.value.size() || index == previous) continue;
    previous = index;
    const int offset = text.utf16_offsets[index];
    mapped.push_back(offset);
    if (text.utf16_widths[index] == 2) mapped.push_back(offset + 1);
  }
  indexes = std::move(mapped);
}
