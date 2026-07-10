#pragma once

#include <string>
#include <vector>

/**
 * Folds a UTF-8 string to a lowercase, diacritic-free ASCII form suitable for
 * accent-insensitive fuzzy matching (e.g. "café" -> "cafe", "Straße" ->
 * "strasse").
 *
 * Characters with a known fold are replaced by their ASCII base (which may be
 * longer than one byte, e.g. ß -> "ss"). ASCII letters are lowercased.
 * Everything else (digits, punctuation, path separators, unmapped multi-byte
 * code points) is passed through unchanged so the matcher's word-boundary and
 * path scoring still works.
 *
 * If `pos_map` is non-null, it is resized to the length of the returned string
 * and filled so that `pos_map[i]` is the UTF-16 code-unit offset, in the
 * original `input`, of the source character that produced folded byte `i`.
 * This lets callers translate byte offsets in the folded string back to
 * offsets usable against the original JavaScript (UTF-16) string for
 * highlighting.
 */
std::string fold_diacritics(const std::string &input,
                            std::vector<int> *pos_map = nullptr);
