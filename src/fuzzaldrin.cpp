// Ported from zadeh (https://github.com/atom-community/zadeh) — a C++
// implementation of the fuzzaldrin-plus scoring algorithm — from its
// src/scorer.h, src/path_scorer.h, src/query.h and src/matcher.h.
// zadeh is licensed under the Apache License, Version 2.0; see LICENSE-zadeh
// at the repository root. Deviations from zadeh are marked "PORT NOTE".
#include "fuzzaldrin.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>

#include "unicode.h"
#include <type_traits>

namespace fuzzaldrin {
namespace {
template <typename Char>
struct Scorer {
  using String = std::basic_string<Char>;
  using PreparedQuery = BasicPreparedQuery<Char>;
  static size_t findPathSeparator(const String &s, size_t from = 0) {
    for (size_t i = from; i < s.size(); i++) {
      if (s[i] == '/' || s[i] == '\\') return i;
    }
    return String::npos;
  }

  // Base point for a single character match. This balances making patterns
  // versus position and size penalty.
  static constexpr int wm = 150;

  // Fading function: characters from 0..pos_bonus receive a greater bonus for
  // being at the start of the string.
  static constexpr Score pos_bonus = 20;

  // Full path length at which the whole match score is halved.
  static constexpr Score tau_size = 150;

  // Max number of missed consecutive hits: ceil(miss_coeff * query.length) + 5.
  // Limiting hits bounds how many permutations are considered when searching
  // for the best alignment; this speeds up long paths and vowel-heavy queries.
  static constexpr float miss_coeff = 0.75;

  // Directory depth at which the full path influence is halved.
  static constexpr size_t tau_depth = 20;

  // The full path is also penalized for the length of the basename. This is a
  // scale factor for that penalty.
  static constexpr Score file_coeff = 2.5;

  // The char instantiation preserves ASCII byte scoring. The char32_t
  // instantiation folds aligned Unicode scalars, whose UTF-8 widths may differ.
  static Char fold_char(const Char c) noexcept {
    if constexpr (std::is_same_v<Char, char32_t>) return simple_case_fold(c);
    return (c >= 'A' && c <= 'Z') ? static_cast<Char>(c + ('a' - 'A')) : c;
  }

  static constexpr Char ascii_upper(const Char c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<Char>(c - ('a' - 'A')) : c;
  }

  static String to_lower(const String &s) {
    String out(s);
    for (auto &c : out) {
      c = fold_char(c);
    }
    return out;
  }

  static String to_upper(const String &s) {
    String out(s);
    for (auto &c : out) {
      c = ascii_upper(c);
    }
    return out;
  }

  // PORT NOTE: both slash kinds are path separators everywhere. zadeh threads a
  // single pathSeparator option through; this module matches the command-t
  // scorer, where Windows backslash haystacks and `/` queries are equivalent.
  static constexpr bool is_path_sep(const Char c) noexcept {
    return c == '/' || c == '\\';
  }

  // PORT NOTE: character equality up to the slash class — `/` in a query
  // matches `\` in a candidate and vice versa, in every comparison the scorer
  // makes (alignment, consecutive runs, exact substrings, isMatch). Without
  // this, Windows-native backslash paths would score below their
  // forward-slash spelling for the same query.
  static constexpr bool chars_eq(const Char a, const Char b) noexcept {
    return a == b || (is_path_sep(a) && is_path_sep(b));
  }

  // Class-aware substring search (String::find up to chars_eq).
  static size_t find_eq(const String &haystack, const String &needle,
                 const size_t from = 0) {
    const size_t n = haystack.size();
    const size_t m = needle.size();
    if (m == 0 || m > n) {
      return m == 0 ? std::min(from, n) : String::npos;
    }
    for (size_t i = from; i + m <= n; i++) {
      size_t j = 0;
      while (j < m && chars_eq(haystack[i + j], needle[j])) {
        j++;
      }
      if (j == m) {
        return i;
      }
    }
    return String::npos;
  }

  // JavaScript String.lastIndexOf(separator, from) over the path-separator
  // class: a negative `from` is clamped to 0, and the scan is inclusive of
  // `from`. Returns -1 when no separator is found.
  // PORT NOTE: zadeh maps this to String::rfind, which wraps `from - 1`
  // to npos when from == 0 and then scans the whole string — a mistranslation
  // of fuzzaldrin-plus for paths with a leading separator.
  static ptrdiff_t last_sep(const String &s, ptrdiff_t from) {
    if (from >= static_cast<ptrdiff_t>(s.size())) {
      from = static_cast<ptrdiff_t>(s.size()) - 1;
    }
    if (from < 0) {
      from = 0;
    }
    for (ptrdiff_t k = from; k >= 0; k--) {
      if (is_path_sep(s[k])) {
        return k;
      }
    }
    return -1;
  }

  //
  // isMatch: are all (non-optional) characters of the query in the subject, in
  // proper order?
  //
  static bool isMatch(const String &subject, const String &query_lw,
               const String &query_up) {
    const size_t subject_size = subject.size();
    const size_t query_size = query_lw.size();

    if (subject_size == 0 || query_size > subject_size) {
      return false;
    }

    size_t i = 0;
    for (size_t j = 0; j < query_size; j++) {
      const Char qj_lw = query_lw[j];
      const Char qj_up = query_up[j];

      // Continue walking the subject from where the previous query character left
      // off, until a character matching either case of the query character is found.
      while (i < subject_size) {
        const Char si = subject[i];
        bool matches = chars_eq(si, qj_lw);
        if constexpr (std::is_same_v<Char, char>) matches |= chars_eq(si, qj_up);
        if (matches) {
          break;
        }
        ++i;
      }

      // Passed the last character: the query is not in the subject.
      if (i == subject_size) {
        return false;
      }
      // Each required character consumes a distinct subject position.
      ++i;
    }
    return true;
  }

  static constexpr bool isSeparator(const Char c) noexcept {
    return c == ' ' || c == '.' || c == '-' || c == '_' || c == '/' || c == '\\';
  }

  //
  // Boundaries: is the character at the start of a word, the end of a word, or
  // a separator?
  //
  static bool isWordStart(const size_t pos, const String &subject,
                   const String &subject_lw) noexcept {
    if (pos == 0) {
      return true;  // first character (virtual separator before the string)
    }
    const Char curr_s = subject[pos];
    const Char prev_s = subject[pos - 1];
    if constexpr (std::is_same_v<Char, char32_t>) {
      return isSeparator(prev_s) ||
             ((is_unicode_uppercase(curr_s) || is_unicode_titlecase(curr_s)) &&
              is_unicode_lowercase(prev_s));
    }
    return isSeparator(prev_s) ||  // match follows a separator
           ((curr_s != subject_lw[pos]) &&
            (prev_s == subject_lw[pos - 1]));  // camelCase capital
  }

  static bool isWordEnd(const size_t pos, const String &subject,
                 const String &subject_lw, const size_t len) noexcept {
    if (pos == len - 1) {
      return true;  // last character of string
    }
    const Char curr_s = subject[pos];
    const Char next_s = subject[pos + 1];
    if constexpr (std::is_same_v<Char, char32_t>) {
      return isSeparator(next_s) ||
             (is_unicode_lowercase(curr_s) &&
              (is_unicode_uppercase(next_s) || is_unicode_titlecase(next_s)));
    }
    return isSeparator(next_s) ||  // match is followed by a separator
           ((curr_s == subject_lw[pos]) &&
            (next_s != subject_lw[pos + 1]));  // lowercase before uppercase
  }

  //
  // Scoring helpers
  //
  static Score scorePosition(const Score pos) noexcept {
    if (pos < pos_bonus) {
      const Score sc = pos_bonus - pos;
      return 100 + sc * sc;
    }
    return std::max<Score>(100 + pos_bonus - pos, 0);
  }

  static Score scoreSize(const Score n, const Score m) noexcept {
    // Size penalty, based on the difference of size (m - n).
    return tau_size / (tau_size + std::fabs(m - n));
  }

  // PORT NOTE: `quality` is a Score, as in fuzzaldrin-plus — zadeh declares it
  // size_t, truncating the fractional 1.1 basename bonus of scoreExactMatch.
  static Score scoreExact(const size_t n, const size_t m, const Score quality,
                   const Score pos) noexcept {
    return 2 * static_cast<Score>(n) * (wm * quality + scorePosition(pos)) *
           scoreSize(static_cast<Score>(n), static_cast<Score>(m));
  }

  //
  // Shared scoring logic between exact match, consecutive & acronym.
  // Ensures pattern length dominates the score, then refines with
  // case-sensitivity and the structural quality of the pattern (word boundary).
  //
  static constexpr Score scorePattern(const size_t count, const size_t len,
                               const size_t sameCase, const bool start,
                               const bool end) noexcept {
    size_t sz = count;

    // To ensure consecutive length dominates the score, this should be as
    // large as the other bonuses combined.
    size_t bonus = 6;
    if (sameCase == count) {
      bonus += 2;
    }
    if (start) {
      bonus += 3;
    }
    if (end) {
      bonus += 1;
    }

    if (count == len) {
      // Matching 100% of the query may break the size ordering; this helps
      // exact matches bubble up against size and depth penalties.
      if (start) {
        if (sameCase == len) {
          sz += 2;
        } else {
          sz += 1;
        }
      }
      if (end) {
        bonus += 1;
      }
    }

    return static_cast<Score>(sameCase + sz * (sz + bonus));
  }

  //
  // Bonus for two characters confirmed to match case-insensitively.
  //
  static Score scoreCharacter(const size_t i, const bool start, const Score acro_score,
                       const Score csc_score) noexcept {
    // Start-of-string / position-of-match bonus.
    const Score posBonus = scorePosition(static_cast<Score>(i));

    // The match is a word boundary: choose between being part of a consecutive
    // run or a consecutive acronym.
    if (start) {
      return posBonus + wm * (std::max(acro_score, csc_score) + 10);
    }

    // Normal match.
    return posBonus + wm * csc_score;
  }

  //
  // Forward search for a sequence of consecutive characters.
  //
  static Score scoreConsecutives(const String &subject,
                          const String &subject_lw,
                          const String &query, const String &query_lw,
                          size_t i, size_t j, const bool startOfWord) {
    const size_t subject_size = subject.size();
    const size_t query_size = query.size();

    size_t sameCase = 0;
    // query_lw[j] ~ subject_lw[i] was checked before entering; now do the
    // case-sensitive check.
    if (chars_eq(query[j], subject[i])) {
      sameCase++;
    }

    size_t sz = 1;  // sz ends one more than the last matching offset
    const size_t limit = std::min(subject_size - i, query_size - j);

    // Continue while the lowercase chars agree, recording case-sensitive hits.
    while (sz < limit && chars_eq(query_lw[++j], subject_lw[++i])) {
      if (chars_eq(query[j], subject[i])) {
        sameCase++;
      }
      ++sz;
    }

    // If the loop stopped on a mismatch, reposition the cursor to the last
    // matching character.
    if (sz < limit) {
      i--;
    }

    // Fast path for a single match: isolated character matches occur often and
    // are not interesting enough to pay for the pattern score. Acronyms are
    // handled by the acronym context bonus instead.
    if (sz == 1) {
      return static_cast<Score>(1 + 2 * sameCase);
    }

    return scorePattern(sz, query_size, sameCase, startOfWord,
                        isWordEnd(i, subject, subject_lw, subject_size));
  }

  //
  // Score of an exact (substring) match at position pos.
  //
  static Score scoreExactMatch(const String &subject,
                        const String &subject_lw, const String &query,
                        const String &query_lw, size_t pos, const size_t n,
                        const size_t m) {
    bool start = isWordStart(pos, subject, subject_lw);

    // Heuristic: if not at a word start, try the next occurrence. For exact
    // matches the word start has the biggest impact on the score, and testing
    // two instances sits between testing one and testing all of them.
    if (!start) {
      const size_t pos2 = find_eq(subject_lw, query_lw, pos + 1);
      if (pos2 != String::npos) {
        start = isWordStart(pos2, subject, subject_lw);
        if (start) {
          pos = pos2;
        }
      }
    }

    // Exact-case bonus.
    size_t sameCase = 0;
    for (size_t i = 0; i < n; i++) {
      if (chars_eq(query[i], subject[pos + i])) {
        sameCase++;
      }
    }

    const bool end = isWordEnd(pos + n - 1, subject, subject_lw, m);

    Score baseNameStart = 1.0;
    if (start && pos > 0 && is_path_sep(subject[pos - 1])) {
      baseNameStart = static_cast<Score>(1.1);
    }

    return scoreExact(n, m, baseNameStart * scorePattern(n, n, sameCase, start, end),
                      static_cast<Score>(pos));
  }

  //
  // Acronym prefix
  //
  struct AcronymResult {
    Score score;
    Score pos;
    size_t count;
  };

  //
  // Is there a 1:1 relationship between the query and the acronym of the
  // candidate? (a) every query character matches an acronym of the candidate — the
  // caller checked that — and (b) every acronym of the candidate matches a
  // query character, which this checks.
  //
  static bool isAcronymFullWord(const String &subject,
                         const String &subject_lw,
                         const size_t nbAcronymInQuery) noexcept {
    const size_t subject_size = subject.size();
    size_t count = 0;

    // Heuristic: assume at most one acronym every 12 characters on average.
    // This filters out long paths, which can still match on the filename.
    if (subject_size > 12 * nbAcronymInQuery) {
      return false;
    }

    for (size_t i = 0; i < subject_size; i++) {
      if (isWordStart(i, subject, subject_lw) && (++count > nbAcronymInQuery)) {
        return false;
      }
    }
    return true;
  }

  static constexpr AcronymResult emptyAcronymResult{static_cast<Score>(0),
                                             static_cast<Score>(0.1), 0};

  static AcronymResult scoreAcronyms(const String &subject,
                              const String &subject_lw,
                              const String &query,
                              const String &query_lw) {
    const size_t subject_size = subject.size();
    const size_t query_size = query.size();

    // A single character is not an acronym.
    if (subject_size <= 1 || query_size <= 1) {
      return emptyAcronymResult;
    }

    size_t count = 0;
    size_t sepCount = 0;
    size_t sumPos = 0;
    size_t sameCase = 0;

    size_t i = String::npos;  // incrementing wraps to 0

    // For each character of the query...
    for (size_t j = 0; j < query_size; j++) {
      const Char qj_lw = query_lw[j];

      // A separator scores no points but continues the prefix when present in
      // the candidate; the cursor advances to its position. A missing
      // separator breaks the prefix.
      if (isSeparator(qj_lw)) {
        // PORT NOTE: a slash in the query continues the prefix on either slash
        // kind (both-slash class); other separators stay literal.
        i = is_path_sep(qj_lw) ? findPathSeparator(subject_lw, i + 1)
                               : subject_lw.find(qj_lw, i + 1);
        if (i != String::npos) {
          sepCount++;
          continue;
        }
        break;
      }

      // For other characters, search for the first match that is also a
      // start-of-word.
      while (++i < subject_size) {
        if (qj_lw == subject_lw[i] && isWordStart(i, subject, subject_lw)) {
          if (query[j] == subject[i]) {
            sameCase++;
          }
          sumPos += i;
          count++;
          break;
        }
      }

      // All of the subject is consumed: stop processing the query.
      if (i == subject_size) {
        break;
      }
    }

    // A single character is not an acronym (this also prevents division by 0).
    if (count < 2) {
      return emptyAcronymResult;
    }

    // Acronyms are scored as start-of-word; a 1:1 match with the candidate is
    // upgraded to full-word.
    const bool fullWord =
        count == query_size ? isAcronymFullWord(subject, subject_lw, count)
                            : false;
    const Score score = scorePattern(count, query_size, sameCase, true, fullWord);

    return AcronymResult{score, static_cast<Score>(sumPos) / count,
                         count + sepCount};
  }

  //----------------------------------------------------------------------
  //
  // Main scoring algorithm (modified Smith-Waterman over two 1-D rows)
  //
  static Score computeScore(const String &subject, const String &subject_lw,
                     const PreparedQuery &preparedQuery) {
    const String &query = preparedQuery.query;
    const String &query_lw = preparedQuery.query_lw;

    const size_t subject_size = subject.size();
    const size_t query_size = query.size();

    //----------------------------
    // Abbreviation sequence: if the whole query is an abbreviation, use that
    // as the score.
    const AcronymResult acro = scoreAcronyms(subject, subject_lw, query, query_lw);
    const Score acro_score = acro.score;

    if (acro.count == query_size) {
      return scoreExact(query_size, subject_size, acro_score, acro.pos);
    }

    //----------------------------
    // Exact (substring) match: use that as the score.
    const size_t pos = find_eq(subject_lw, query_lw);
    if (pos != String::npos) {
      return scoreExactMatch(subject, subject_lw, query, query_lw, pos,
                             query_size, subject_size);
    }

    //----------------------------
    // Individual characters (Smith-Waterman)
    std::vector<Score> score_row(query_size, 0);
    std::vector<Score> csc_row(query_size, 0);
    const Score sz = scoreSize(static_cast<Score>(query_size),
                               static_cast<Score>(subject_size));

    const float miss_budget =
        static_cast<float>(std::ceil(miss_coeff * query_size)) + 5;
    float miss_left = miss_budget;
    bool csc_should_rebuild = true;

    for (size_t i = 0; i < subject_size; i++) {
      const Char si_lw = subject_lw[i];

      // If si_lw is not in the query, reset csc_row and move on — unless it
      // was just cleaned, in which case the cleaned version is kept.
      if (!preparedQuery.containsChar(si_lw)) {
        if (csc_should_rebuild) {
          std::fill(csc_row.begin(), csc_row.end(), 0.0f);
          csc_should_rebuild = false;
        }
        continue;
      }

      Score score = 0;
      Score score_diag = 0;
      Score csc_diag = 0;
      bool record_miss = true;
      csc_should_rebuild = true;

      for (size_t j = 0; j < query_size; j++) {
        // What is the best gap? score_up is the score of a gap in the subject;
        // score (from the previous j) is a gap in the query.
        const Score score_up = score_row[j];
        if (score_up > score) {
          score = score_up;
        }

        Score csc_score = 0;

        // Compute a tentative match.
        if (chars_eq(query_lw[j], si_lw)) {
          const bool start = isWordStart(i, subject, subject_lw);

          // Forward search for a sequence of consecutive chars.
          csc_score = csc_diag > 0
                          ? csc_diag
                          : scoreConsecutives(subject, subject_lw, query,
                                              query_lw, i, j, start);

          // Bonus for aligning subject[i] with query[j].
          const Score align =
              score_diag + scoreCharacter(i, start, acro_score, csc_score);

          // Better to use this match, or to take the best gap?
          if (align > score) {
            score = align;
            // Reset the consecutive-miss count.
            miss_left = miss_budget;
          } else {
            // The match was rejected: record a miss, and exit when the budget
            // is exhausted. Each query character's score history is in score_row;
            // the full query score is its last item.
            if (record_miss && --miss_left <= 0) {
              return std::max(score, score_row[query_size - 1]) * sz;
            }
            record_miss = false;
          }
        }

        // Prepare the next sequence & match score.
        score_diag = score_up;
        csc_diag = csc_row[j];
        csc_row[j] = csc_score;
        score_row[j] = score;
      }
    }

    return score_row[query_size - 1] * sz;
  }

  //
  // Path scoring
  //

  static String getExtension(const String &str) {
    const size_t pos = str.rfind('.');
    return pos == String::npos ? String{} : str.substr(pos + 1);
  }

  // Fraction of the query extension matched by the candidate extension. For
  // example `mf.h` prefers `myFile.h` to `myFile.html`. This needs special
  // handling because it awards points for *not* having characters (the `tml`).
  static Score getExtensionScore(const String &candidate, const String &ext,
                          const ptrdiff_t startPos, const ptrdiff_t endPos,
                          const int maxDepth) {
    // startPos is the position of the last slash of the candidate, -1 if none.
    if (ext.empty() || endPos < 0) {
      return 0;
    }

    // Check that (a) an extension exists and (b) it is after the start of the
    // basename.
    // PORT NOTE: explicit npos handling; zadeh funnels rfind's npos through an
    // int cast and a tautological unsigned assert.
    const size_t dot = candidate.rfind('.', static_cast<size_t>(endPos));
    if (dot == String::npos ||
        static_cast<ptrdiff_t>(dot) <= startPos) {
      return 0;  // (note that startPos >= -1)
    }

    ptrdiff_t ext_size = static_cast<ptrdiff_t>(ext.size());
    ptrdiff_t m = endPos - static_cast<ptrdiff_t>(dot);

    // n holds the smaller of both extension lengths, m the larger.
    if (m < ext_size) {
      ext_size = m;
      m = static_cast<ptrdiff_t>(ext.size());
    }

    // Place the cursor after the dot & count matching extension characters.
    const size_t pos = dot + 1;
    ptrdiff_t matched = 0;
    while (matched < ext_size) {
      if (candidate[pos + matched] != ext[matched]) {
        break;
      }
      ++matched;
    }

    // If nothing matched, try a deeper extension (e.g. `.tar.gz`), with a
    // penalty per level.
    if (matched == 0 && maxDepth > 0) {
      return 0.9f * getExtensionScore(candidate, ext, startPos,
                                      static_cast<ptrdiff_t>(dot) - 1,
                                      maxDepth - 1);
    }

    // m is the largest extension length and both are non-zero here.
    return static_cast<Score>(matched) / static_cast<Score>(m);
  }

  // Number of folders in a path (consecutive slashes count as one directory,
  // and leading slashes are skipped so `foo/bar` and `/foo/bar` agree).
  static int countDir(const String &path, const size_t end) {
    if (end < 1) {
      return 0;
    }

    int count = 0;
    size_t i = 0;

    while (i < end && is_path_sep(path[i])) {
      ++i;
    }

    ++i;
    while (i < end) {
      if (is_path_sep(path[i])) {
        count++;
        ++i;
        while (i < end && is_path_sep(path[i])) {
          ++i;
        }
      }
      ++i;
    }

    return count;
  }

  //
  // Score adjustment for paths: linear interpolation between the basename
  // score and the full-path score. Low directory depth favours the basename,
  // and more of the full path is included as depth increases. The full-path
  // score is also penalized by the basename length so that a focused basename
  // match can overcome a longer directory path.
  //
  static Score scorePath(const String &subject, const String &subject_lw,
                  Score fullPathScore, const PreparedQuery &preparedQuery,
                  const ScorerOptions &options) {
    if (fullPathScore == 0) {
      return 0;
    }

    // Skip trailing slashes.
    // PORT NOTE: signed guard — zadeh underflows on an all-slash subject.
    ptrdiff_t end = static_cast<ptrdiff_t>(subject.size()) - 1;
    while (end >= 0 && is_path_sep(subject[end])) {
      end--;
    }
    if (end < 0) {
      return fullPathScore;
    }

    // Position of the subject's basename.
    ptrdiff_t basePos = last_sep(subject, end);
    const ptrdiff_t fileLength = end - basePos;

    // Bonus for matching the extension.
    Score extAdjust = 1.0;
    if (options.use_extension_bonus) {
      extAdjust += getExtensionScore(subject_lw, preparedQuery.ext, basePos, end, 2);
      fullPathScore *= extAdjust;
    }

    // No base path: nothing else to compute.
    if (basePos == -1) {
      return fullPathScore;
    }

    // Extend the basename with as many folders as the query contains.
    int depth = preparedQuery.depth;
    while (basePos > -1 && depth-- > 0) {
      basePos = last_sep(subject, basePos - 1);
    }

    // Get the base path score; if the basename is the whole string, reuse the
    // full-path score (the folder-depth and filename penalties still apply).
    // PORT NOTE: the basename substring is [basePos + 1, end] — count
    // end - basePos. zadeh passes `end + 1` as the substr *count* (a JS
    // slice-end mistranslation), which drags trailing separators back in.
    const Score basePathScore =
        basePos == -1
            ? fullPathScore
            : extAdjust *
                  computeScore(
                      subject.substr(basePos + 1, static_cast<size_t>(end - basePos)),
                      subject_lw.substr(basePos + 1, static_cast<size_t>(end - basePos)),
                      preparedQuery);

    const Score alpha =
        (0.5f * tau_depth) /
        (tau_depth + countDir(subject, static_cast<size_t>(end) + 1));
    return alpha * basePathScore +
           (1 - alpha) * fullPathScore *
               scoreSize(0, file_coeff * static_cast<Score>(fileLength));
  }

  //----------------------------------------------------------------------
  //
  // Match positions (used for highlight indexes)
  //
  // Follows computeScore closely, except the best move at each position is
  // recorded in a matrix and a traceback collects the matched positions.
  // Differences: consecutive sequences reset when a match is not taken, and
  // there is no miss budget.
  //

  // PORT NOTE: the trace matrix is capped (score_match.cpp caps its memo the
  // same way); above the cap a greedy in-order walk provides the indexes.
  static constexpr size_t kMaxTraceSize = size_t{1} << 22;

  static std::vector<size_t> greedyMatch(const String &subject_lw,
                                  const String &query_lw,
                                  const size_t offset) {
    std::vector<size_t> matches;
    size_t i = 0;
    const size_t subject_size = subject_lw.size();
    for (const Char qj_lw : query_lw) {
      while (i < subject_size && !chars_eq(subject_lw[i], qj_lw)) {
        ++i;
      }
      if (i == subject_size) {
        break;
      }
      matches.push_back(i + offset);
      ++i;
    }
    return matches;
  }

  static std::vector<size_t> computeMatch(const String &subject,
                                   const String &subject_lw,
                                   const PreparedQuery &preparedQuery,
                                   const size_t offset = 0) {
    const String &query = preparedQuery.query;
    const String &query_lw = preparedQuery.query_lw;

    const size_t subject_size = subject.size();
    const size_t query_size = query.size();

    if (subject_size == 0 || query_size == 0) {
      return {};
    }
    if (subject_size * query_size > kMaxTraceSize) {
      return greedyMatch(subject_lw, query_lw, offset);
    }

    // Acronym context bonus (camelCase / snake_case initials).
    const AcronymResult acro = scoreAcronyms(subject, subject_lw, query, query_lw);
    const Score acro_score = acro.score;

    std::vector<Score> score_row(query_size, 0);
    std::vector<Score> csc_row(query_size, 0);

    enum class Direction : uint8_t { STOP, UP, LEFT, DIAGONAL };

    std::vector<Direction> trace(subject_size * query_size, Direction::STOP);
    size_t pos = 0;

    for (size_t i = 0; i < subject_size; i++) {
      Score score = 0;
      Score score_up = 0;
      Score csc_diag = 0;
      const Char si_lw = subject_lw[i];

      for (size_t j = 0; j < query_size; j++) {
        Score csc_score = 0;
        Score align = 0;
        const Score score_diag = score_up;

        // Compute a tentative match.
        if (chars_eq(query_lw[j], si_lw)) {
          const bool start = isWordStart(i, subject, subject_lw);

          csc_score = csc_diag > 0
                          ? csc_diag
                          : scoreConsecutives(subject, subject_lw, query,
                                              query_lw, i, j, start);

          align = score_diag + scoreCharacter(i, start, acro_score, csc_score);
        }

        // Prepare the next sequence & match score.
        score_up = score_row[j];  // current score_up is the next run's diag
        csc_diag = csc_row[j];

        Direction move;
        if (score > score_up) {
          move = Direction::LEFT;
        } else {
          // On equality, moving UP gets closer to the start of the candidate.
          score = score_up;
          move = Direction::UP;
        }

        // Only take the alignment if it is the absolute best option.
        if (align > score) {
          score = align;
          move = Direction::DIAGONAL;
        } else {
          // If this character is not taken, break the consecutive sequence
          // (when it is 0, it gets recomputed).
          csc_score = 0;
        }

        score_row[j] = score;
        csc_row[j] = csc_score;
        trace[pos] = score > 0 ? move : Direction::STOP;
        ++pos;
      }
    }

    // Traceback: walk the matrix backwards and collect matches (diagonals).
    ptrdiff_t ii = static_cast<ptrdiff_t>(subject_size) - 1;
    ptrdiff_t jj = static_cast<ptrdiff_t>(query_size) - 1;
    std::vector<size_t> matches;

    while (ii >= 0 && jj >= 0) {
      const size_t at = static_cast<size_t>(ii) * query_size + static_cast<size_t>(jj);
      const Direction dir = trace[at];
      if (dir == Direction::UP) {
        ii--;
      } else if (dir == Direction::LEFT) {
        jj--;
      } else if (dir == Direction::DIAGONAL) {
        matches.push_back(static_cast<size_t>(ii) + offset);
        jj--;
        ii--;
      } else {
        break;
      }
    }

    std::reverse(matches.begin(), matches.end());
    return matches;
  }

  static std::vector<size_t> basenameMatch(const String &subject,
                                    const String &subject_lw,
                                    const PreparedQuery &preparedQuery) {
    // Skip trailing slashes.
    // PORT NOTE: signed guard — zadeh wraps a size_t below 0 on an all-slash
    // subject.
    ptrdiff_t end = static_cast<ptrdiff_t>(subject.size()) - 1;
    while (end >= 0 && is_path_sep(subject[end])) {
      end--;
    }
    if (end < 0) {
      return {};
    }

    // Position of the subject's basename; no separator means no base path.
    ptrdiff_t basePos = last_sep(subject, end);
    if (basePos == -1) {
      return {};
    }

    // Extend the basename with as many folders as the query contains.
    int depth = preparedQuery.depth;
    while (depth-- > 0) {
      basePos = last_sep(subject, basePos - 1);
      if (basePos == -1) {  // consumed the whole subject?
        return {};
      }
    }

    basePos++;
    const size_t count = static_cast<size_t>(end) + 1 - static_cast<size_t>(basePos);
    return computeMatch(subject.substr(basePos, count),
                        subject_lw.substr(basePos, count), preparedQuery,
                        static_cast<size_t>(basePos));
  }

  //
  // Combine two sorted match sequences and remove duplicates.
  // PORT NOTE: rewritten as a standard sorted-merge — the zadeh/fuzzaldrin-plus
  // loop drops the final basename position when it sorts before the last
  // full-path position.
  //
  static std::vector<size_t> mergeMatches(const std::vector<size_t> &a,
                                   const std::vector<size_t> &b) {
    if (b.empty()) {
      return a;
    }
    if (a.empty()) {
      return b;
    }

    std::vector<size_t> out;
    out.reserve(a.size() + b.size());
    size_t i = 0;
    size_t j = 0;
    while (i < a.size() && j < b.size()) {
      if (a[i] == b[j]) {
        out.push_back(a[i]);
        i++;
        j++;
      } else if (a[i] < b[j]) {
        out.push_back(a[i++]);
      } else {
        out.push_back(b[j++]);
      }
    }
    while (i < a.size()) {
      out.push_back(a[i++]);
    }
    while (j < b.size()) {
      out.push_back(b[j++]);
    }
    return out;
  }

  static String coreChars(String query) {
    for (const Char ch : {' ', '_', '-', ':', '/', '\\'}) {
      query.erase(std::remove(query.begin(), query.end(), ch), query.end());
    }
    return query;
  }

};
}  // namespace

template <typename Char>
BasicPreparedQuery<Char>::BasicPreparedQuery(const std::basic_string<Char> &q)
    : query(q),
      query_lw(Scorer<Char>::to_lower(q)),
      core(Scorer<Char>::coreChars(q)),
      core_lw(Scorer<Char>::to_lower(core)),
      core_up(Scorer<Char>::to_upper(core)) {
  depth = Scorer<Char>::countDir(query, query.size());
  ext = Scorer<Char>::getExtension(query_lw);
  const auto add = [this](Char c) {
    if constexpr (std::is_same_v<Char, char>) char_codes[static_cast<unsigned char>(c)] = true;
    else char_codes.insert(c);
  };
  for (const Char c : query_lw) {
    add(c);
    if (Scorer<Char>::is_path_sep(c)) {
      add('/');
      add('\\');
    }
  }
}

template struct BasicPreparedQuery<char>;
template struct BasicPreparedQuery<char32_t>;

template <typename Char>
Score score_impl(const std::basic_string<Char> &subject,
                 const std::basic_string<Char> &subject_lw,
                 const BasicPreparedQuery<Char> &query, const ScorerOptions &options) {
  if constexpr (std::is_same_v<Char, char32_t>) {
    if (!Scorer<Char>::isMatch(subject_lw, query.core_lw, query.core_up)) return 0;
  } else {
    if (!Scorer<Char>::isMatch(subject, query.core_lw, query.core_up)) return 0;
  }
  Score sc = Scorer<Char>::computeScore(subject, subject_lw, query);
  if (options.use_path_scoring) sc = Scorer<Char>::scorePath(subject, subject_lw, sc, query, options);
  return std::ceil(sc);
}

template <typename Char>
Score ceiling_impl(const BasicPreparedQuery<Char> &query) {
  const Score sc = std::ceil(Scorer<Char>::computeScore(query.query, query.query_lw, query));
  return sc < 1 ? 1 : sc;
}

template <typename Char>
std::vector<size_t> indexes_impl(const std::basic_string<Char> &subject,
                                 const std::basic_string<Char> &subject_lw,
                                 const BasicPreparedQuery<Char> &query) {
  auto matches = Scorer<Char>::computeMatch(subject, subject_lw, query);
  if (Scorer<Char>::findPathSeparator(subject) != std::basic_string<Char>::npos) {
    return Scorer<Char>::mergeMatches(matches, Scorer<Char>::basenameMatch(subject, subject_lw, query));
  }
  return matches;
}

Score score(const std::string &subject, const std::string &subject_lw,
            const PreparedQuery &query, const ScorerOptions &options) {
  return score_impl(subject, subject_lw, query, options);
}

Score score_ceiling(const PreparedQuery &query) { return ceiling_impl(query); }

std::vector<size_t> match_indexes(const std::string &subject, const std::string &subject_lw,
                                  const PreparedQuery &query) {
  return indexes_impl(subject, subject_lw, query);
}

Score score(const std::u32string &subject, const std::u32string &subject_lw,
            const UnicodePreparedQuery &query, const ScorerOptions &options) {
  return score_impl(subject, subject_lw, query, options);
}

Score score_ceiling(const UnicodePreparedQuery &query) { return ceiling_impl(query); }

std::vector<size_t> match_indexes(const std::u32string &subject, const std::u32string &subject_lw,
                                  const UnicodePreparedQuery &query) {
  return indexes_impl(subject, subject_lw, query);
}

}  // namespace fuzzaldrin
