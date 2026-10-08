// Ported from zadeh (https://github.com/atom-community/zadeh) — a C++
// implementation of the fuzzaldrin-plus scoring algorithm — from its
// src/scorer.h, src/path_scorer.h, src/query.h and src/matcher.h.
// zadeh is licensed under the Apache License, Version 2.0; see LICENSE
// at the repository root. Deviations from zadeh are marked "PORT NOTE".
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>
#include <type_traits>
#include <unordered_set>

namespace fuzzaldrin {

using Score = float;

// Query preprocessed once per findMatches() call and shared read-only across
// worker threads. `core` is the query minus the optional characters
// " _-:/\" — those improve the score when present but never block a match.
template <typename Char>
struct BasicPreparedQuery {
  std::basic_string<Char> query;
  std::basic_string<Char> query_lw;
  std::basic_string<Char> core;
  std::basic_string<Char> core_lw;
  std::basic_string<Char> core_up;
  int depth = 0;
  std::basic_string<Char> ext;
  // The ASCII scorer retains its byte-presence table; Unicode queries use a
  // sparse set so arbitrary scalar values never index a byte-sized array.
  std::conditional_t<std::is_same_v<Char, char>, std::array<bool, 256>,
                     std::unordered_set<Char>> char_codes{};

  explicit BasicPreparedQuery(const std::basic_string<Char> &q);
  bool containsChar(Char c) const {
    if constexpr (std::is_same_v<Char, char>) return char_codes[static_cast<unsigned char>(c)];
    else return char_codes.find(c) != char_codes.end();
  }
};

using PreparedQuery = BasicPreparedQuery<char>;
using UnicodePreparedQuery = BasicPreparedQuery<char32_t>;

struct ScorerOptions {
  bool use_path_scoring = true;
  bool use_extension_bonus = false;
};

// Raw fuzzaldrin-plus score; >= 0, 0 = no match. Not normalized — divide by
// score_ceiling() (and clamp to 1) for the module's (0, 1] score contract.
Score score(const std::string &subject, const std::string &subject_lw,
            const PreparedQuery &query, const ScorerOptions &options);

// Flat self-match score of the query (no path scoring, no extension bonus),
// used as the normalization ceiling. Always >= 1.
Score score_ceiling(const PreparedQuery &query);

// Byte offsets of the subject characters matched by the query, for highlight
// rendering (computeMatch + basename merge). May contain fewer entries than
// the query has characters (optional characters can go unmatched) or more
// (full-path and basename alignments are merged).
std::vector<size_t> match_indexes(const std::string &subject,
                                  const std::string &subject_lw,
                                  const PreparedQuery &query);

// Unicode overloads return code-point positions; callers map them to UTF-16.
Score score(const std::u32string &subject, const std::u32string &subject_lw,
            const UnicodePreparedQuery &query, const ScorerOptions &options);
Score score_ceiling(const UnicodePreparedQuery &query);
std::vector<size_t> match_indexes(const std::u32string &subject,
                                  const std::u32string &subject_lw,
                                  const UnicodePreparedQuery &query);

}  // namespace fuzzaldrin
