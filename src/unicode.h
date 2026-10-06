#pragma once

#include <cstdint>
#include <string>
#include <vector>

char32_t simple_case_fold(char32_t value);
bool is_unicode_uppercase(char32_t value);
bool is_unicode_lowercase(char32_t value);
bool is_unicode_titlecase(char32_t value);

struct UnicodeText {
  std::u32string value;
  std::u32string folded;
  std::vector<int> utf16_offsets;
  std::vector<uint8_t> utf16_widths;
};

// fold_map maps bytes in diacritic-folded input back to the original UTF-16 text.
UnicodeText decode_unicode(const std::string &input,
                           const std::vector<int> *fold_map = nullptr);
void unicode_match_indexes_to_utf16(const UnicodeText &text,
                                    std::vector<int> &indexes);
