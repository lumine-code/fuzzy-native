#include "score_match.h"
#include "unicode.h"
#include <type_traits>

/**
 * This is mostly based on Greg Hurrell's implementation in
 * https://github.com/wincent/command-t/blob/master/ruby/command-t/match.c
 * with a few modifications and extra optimizations.
 */

#include <algorithm>
#include <string>
#include <cstring>

// memrchr is a non-standard extension only available in glibc.
#if defined(__APPLE__) || defined(_WIN32) || defined(_WIN64)
#include "memrchr.h"
#endif

using namespace std;

// Initial multiplier when a gap is used.
const float BASE_DISTANCE_PENALTY = 0.6;

// penalty = BASE_DISTANCE_PENALTY - (dist - 1) * ADDITIONAL_DISTANCE_PENALTY.
const float ADDITIONAL_DISTANCE_PENALTY = 0.05;

// The lowest the distance penalty can go. Add epsilon for precision errors.
const float MIN_DISTANCE_PENALTY = 0.2;

// Bail if the state space exceeds this limit.
const size_t MAX_MEMO_SIZE = 10000;

// Convenience structure for passing around during recursion.
template <typename Char>
struct MatchInfo {
  const Char *haystack;
  const Char *haystack_case;
  size_t haystack_len;
  const Char *needle;
  const Char *needle_case;
  size_t needle_len;
  int* last_match;
  float *memo;
  size_t *best_match;
  bool smart_case;
  size_t max_gap;
  float min_score;
};

template <typename Char>
bool is_camel_boundary(Char last, Char curr) {
  if constexpr (std::is_same_v<Char, char32_t>) {
    return is_unicode_lowercase(last) &&
           (is_unicode_uppercase(curr) || is_unicode_titlecase(curr));
  } else {
    return last >= 'a' && last <= 'z' && curr >= 'A' && curr <= 'Z';
  }
}

template <typename Char>
const Char *find_last(const Char *input, Char c, size_t length) {
  if constexpr (std::is_same_v<Char, char>) return static_cast<const char *>(memrchr(input, c, length));
  else {
    while (length > 0) {
      if (input[--length] == c) return input + length;
    }
    return nullptr;
  }
}

/**
 * This algorithm essentially looks for an optimal matching
 * from needle characters to matching haystack characters. We assign a multiplier
 * to each character in the needle, and multiply the scores together in the end.
 *
 * The key insight is that we wish to reduce the distance between adjacent
 * matched characters in the haystack. Exact substring matches will receive a score
 * of 1, while gaps incur significant multiplicative penalties.
 *
 * We reduce the penalty for word boundaries. This includes:
 * - paths (a in /x/abc)
 * - hyphens/underscores (a in x-a or x_a)
 * - upper camelcase names (A in XyzAbc)
 *
 * See below for the exact cases and weights used.
 *
 * Computing the optimal matching is a relatively straight-forward
 * dynamic-programming problem, similar to the classic Levenshtein distance.
 * We use a memoized-recursive implementation, since the state space tends to
 * be relatively sparse in most practical use cases.
 */
template <typename Char>
float recursive_match(const MatchInfo<Char> &m,
                      const size_t haystack_idx,
                      const size_t needle_idx,
                      const float cur_score) {
  if (needle_idx == m.needle_len) {
    return 1;
  }

  float &memoized = m.memo[needle_idx * m.haystack_len + haystack_idx];
  if (memoized >= 0) {
    return memoized;
  }

  float score = 0;
  size_t best_match = 0;
  Char c = m.needle_case[needle_idx];

  size_t lim = m.last_match[needle_idx];
  if (needle_idx > 0 && m.max_gap && haystack_idx + m.max_gap < lim) {
    lim = haystack_idx + m.max_gap;
  }

  // This is only used when needle_idx == haystack_idx == 0.
  // It won't be accurate for any other run.
  size_t last_slash = 0;
  for (size_t j = haystack_idx; j <= lim; j++) {
    Char d = m.haystack_case[j];
    bool is_path_sep = d == '/' || d == '\\';

    if (needle_idx == 0 && is_path_sep) {
      last_slash = j;
    }
    if (c == d || (is_path_sep && (c == '_' || c == '/' || c == '\\'))) {
      // calculate score
      float char_score = 1.0;
      if (j > haystack_idx) {
        Char last = m.haystack[j - 1];
        Char curr = m.haystack[j]; // case matters, so get again
        if (last == '/' || last == '\\') {
          char_score = 0.9;
        } else if (last == '-' || last == '_' || last == ' ' ||
                   (last >= '0' && last <= '9')) {
          char_score = 0.8;
        } else if (is_camel_boundary(last, curr)) {
          char_score = 0.8;
        } else if (last == '.') {
          char_score = 0.7;
        } else if (needle_idx == 0) {
          char_score = BASE_DISTANCE_PENALTY;
        } else {
          char_score = max(
            MIN_DISTANCE_PENALTY,
            BASE_DISTANCE_PENALTY -
              (j - haystack_idx - 1) * ADDITIONAL_DISTANCE_PENALTY
          );
        }
      }

      // Apply a severe penalty if the case doesn't match.
      // This will make the exact matches have higher score than the case
      // insensitive and the path insensitive matches.
      if (
        (m.smart_case || m.haystack[j] == '/' || m.haystack[j] == '\\') &&
        m.needle[needle_idx] != m.haystack[j]
      ) {
        char_score *= 0.001;
      }

      float multiplier = char_score;
      // Scale the score based on how much of the path was actually used.
      // (We measure this via # of characters since the last slash.)
      if (needle_idx == 0) {
        multiplier /= float(m.haystack_len - last_slash);
      }
      float next_score = 1.0;
      if (m.min_score > 0) {
        next_score = cur_score * multiplier;
        // Scores only decrease. If we can't pass the previous best, bail
        if (next_score < m.min_score) {
          // Ensure that score is non-zero:
          // MatcherBase shouldn't exclude this from future searches.
          if (score == 0) {
            score = 1e-18;
          }
          continue;
        }
      }
      float new_score =
        multiplier * recursive_match(m, j + 1, needle_idx + 1, next_score);
      if (new_score > score) {
        score = new_score;
        best_match = j;
        // Optimization: can't score better than 1.
        if (new_score == 1) {
          break;
        }
      }
    }
  }

  if (m.best_match != nullptr) {
    m.best_match[needle_idx * m.haystack_len + haystack_idx] = best_match;
  }
  return memoized = score;
}

template <typename Char>
float score_match_impl(const Char *haystack,
                  const Char *haystack_lower,
                  const Char *needle,
                  const Char *needle_lower,
                  const MatchOptions &options,
                  const float min_score,
                  vector<int> *match_indexes) {
  if (!*needle) {
    return 1.0;
  }

  MatchInfo<Char> m;
  m.haystack_len = std::char_traits<Char>::length(haystack);
  m.needle_len = std::char_traits<Char>::length(needle);
  // A needle longer than the haystack can never match; bail before
  // stack-allocating needle-sized buffers.
  if (m.needle_len > m.haystack_len) {
    return 0;
  }
  m.haystack_case = options.case_sensitive ? haystack : haystack_lower;
  m.needle_case = options.case_sensitive ? needle : needle_lower;
  m.smart_case = options.smart_case;
  m.max_gap = options.max_gap;
  m.min_score = min_score;

#ifdef _WIN32
  int *last_match = (int*)_alloca(m.needle_len * sizeof(int));
#else
  int last_match[m.needle_len];
#endif
  m.last_match = last_match;

  // Check if the needle exists in the haystack at all.
  // Simultaneously, we can figure out the last possible match for each needle
  // character (which prunes the search space by a ton)
  int hindex = m.haystack_len;
  for (int i = m.needle_len - 1; i >= 0; i--) {
    Char c = m.needle_case[i];
    const Char* ptr = find_last(m.haystack_case, c, hindex);
    // _, / and \ in the needle also match either path separator, so the last
    // possible match is the rightmost among all the alternatives — not the
    // literal occurrence with the separators as a mere fallback.
    if (c == '_' || c == '/' || c == '\\') {
      const Char seps[] = {'/', '\\'};
      for (Char sep : seps) {
        if (sep == c) {
          continue;
        }
        const Char* sep_ptr = find_last(m.haystack_case, sep, hindex);
        if (sep_ptr != nullptr && (ptr == nullptr || sep_ptr > ptr)) {
          ptr = sep_ptr;
        }
      }
    }
    if (ptr == nullptr) {
      return 0;
    }
    hindex = ptr - m.haystack_case;
    last_match[i] = hindex;
  }

  m.haystack = haystack;
  m.needle = needle;

  size_t memo_size = m.haystack_len * m.needle_len;
  if (memo_size >= MAX_MEMO_SIZE) {
    // Just return the initial match.
    float penalty = 1.0;
    for (size_t i = 1; i < m.needle_len; i++) {
      int gap = last_match[i] - last_match[i - 1];
      if (gap > 1) {
        penalty *= max(
          MIN_DISTANCE_PENALTY,
          BASE_DISTANCE_PENALTY - (gap - 1) * ADDITIONAL_DISTANCE_PENALTY
        );
      }
    }
    if (match_indexes != nullptr) {
      *match_indexes = vector<int>(last_match, last_match + m.needle_len);
    }
    return penalty * m.needle_len / m.haystack_len;
  }

  if (match_indexes != nullptr) {
    m.best_match = new size_t[memo_size];
  } else {
    m.best_match = nullptr;
  }

#ifdef _WIN32
  float *memo = (float*)_alloca(memo_size * sizeof(float));
#else
  float memo[memo_size];
#endif
  // This doesn't set the values to -2, but some negative number.
  memset(memo, -2, sizeof(float) * memo_size);
  m.memo = memo;

  // Since we scaled by the length of haystack used,
  // scale it back up by the needle length.
  float score = m.needle_len * recursive_match(m, 0, 0, m.needle_len);
  if (score <= 0) {
    return 0.0;
  }

  if (match_indexes != nullptr) {
    match_indexes->resize(m.needle_len);
    size_t curr_start = 0;
    for (size_t i = 0; i < m.needle_len; i++) {
      match_indexes->at(i) = m.best_match[i * m.haystack_len + curr_start];
      curr_start = match_indexes->at(i) + 1;
    }
    delete[] m.best_match;
  }

  return score;
}

float score_match(const char *haystack, const char *haystack_lower,
                  const char *needle, const char *needle_lower,
                  const MatchOptions &options, float min_score, vector<int> *indexes) {
  return score_match_impl(haystack, haystack_lower, needle, needle_lower, options, min_score, indexes);
}

float score_match(const char32_t *haystack, const char32_t *haystack_lower,
                  const char32_t *needle, const char32_t *needle_lower,
                  const MatchOptions &options, float min_score, vector<int> *indexes) {
  return score_match_impl(haystack, haystack_lower, needle, needle_lower, options, min_score, indexes);
}
