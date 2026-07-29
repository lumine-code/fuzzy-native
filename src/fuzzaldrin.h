// Ported from zadeh (https://github.com/atom-community/zadeh) — a C++
// implementation of the fuzzaldrin-plus scoring algorithm — from its
// src/scorer.h, src/path_scorer.h, src/query.h and src/matcher.h.
// zadeh is licensed under the Apache License, Version 2.0; see LICENSE-zadeh
// at the repository root. Deviations from zadeh are marked "PORT NOTE".
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace fuzzaldrin {

using Score = float;

// Query preprocessed once per findMatches() call and shared read-only across
// worker threads. `core` is the query minus the optional characters
// " _-:/\" — those improve the score when present but never block a match.
struct PreparedQuery {
  std::string query;
  std::string query_lw;
  std::string core;
  std::string core_lw;
  std::string core_up;
  int depth = 0;
  std::string ext;
  // PORT NOTE: zadeh uses std::set<char>; a byte-presence table is faster and
  // trivially thread-safe for concurrent reads.
  std::array<bool, 256> char_codes{};

  explicit PreparedQuery(const std::string &q);
};

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

}  // namespace fuzzaldrin
