#include "MatcherBase.h"
#include "diacritics.h"
#include "fuzzaldrin.h"
#include "score_match.h"

#include <algorithm>
#include <atomic>
#include <optional>
#include <queue>
#include <thread>

using namespace std;

typedef priority_queue<MatchResult> ResultHeap;

bool is_ascii(const string &text) {
  return all_of(text.begin(), text.end(), [](unsigned char c) { return c < 0x80; });
}

bool same_matching_options(const MatcherOptions &a, const MatcherOptions &b) {
  return a.algorithm == b.algorithm && a.case_sensitive == b.case_sensitive &&
         a.smart_case == b.smart_case && a.max_gap == b.max_gap &&
         a.use_path_scoring == b.use_path_scoring &&
         a.use_extension_bonus == b.use_extension_bonus &&
         a.max_results == b.max_results;
}

// Per-query fuzzaldrin state, prepared once per findMatches() call and shared
// read-only by all worker threads. `prepared` is non-null exactly when the
// fuzzaldrin algorithm runs (fuzzaldrin selected and the query non-empty).
struct FuzzaldrinJob {
  const fuzzaldrin::PreparedQuery *prepared = nullptr;
  const fuzzaldrin::UnicodePreparedQuery *unicode_prepared = nullptr;
  const UnicodeText *unicode_query = nullptr;
  bool query_non_ascii = false;
  bool requires_non_ascii = false;
  fuzzaldrin::ScorerOptions scorer_options;
  float ceiling = 1;
  float unicode_ceiling = 1;
};

template <typename Char>
inline uint64_t letter_bitmask(const std::basic_string<Char> &str) {
  uint64_t result = 0;
  for (auto c : str) {
    if (c >= 'a' && c <= 'z') {
      int index = c - 'a';
      uint64_t count_bit = (result >> (index * 2));
      // "Increment" the count_bit:
      // 00 -> 01
      // 01 -> 11
      // 11 -> 11
      count_bit = ((count_bit << 1) | 1) & 3;
      result |= count_bit << (index * 2);
    } else if (c == '-') {
      result |= (1ULL << 52);
    } else if (c >= '0' && c <= '9') {
      result |= (1ULL << (c - '0' + 54));
    }
  }
  return result;
}

inline string str_to_lower(const std::string &s) {
  string lower(s);
  for (auto &c : lower) {
    if (c >= 'A' && c <= 'Z') {
      c += 'a' - 'A';
    }
  }
  return lower;
}

bool is_slash(char c) { return c == '/' || c == '\\'; }

int num_dirs(const std::string &path) {
  int num = 0;
  for (size_t i = 0; i < path.length(); i++) {
    if (is_slash(path[i])) {
      num++;
    }
  }
  return num;
}

int score_based_root_path(const MatchOptions &options,
                          const MatcherBase::CandidateData &candidate) {
  const std::string &root = options.root_path;
  if (root.length() == 0) {
    return 0;
  }

  const std::string &value = candidate.value;

  size_t num_common_dirs = 0;
  size_t i = 0;

  // Count number of common directories
  for (; i < root.length() && i < value.length(); i++) {
    if (root[i] != value[i]) {
      break;
    }
    if (is_slash(root[i])) {
      num_common_dirs++;
    }
  }

  if (i == root.length() && i < value.length() && is_slash(value[i])) {
    num_common_dirs++;
  }

  return 1000 * num_common_dirs - candidate.num_dirs;
}

// Push a new entry on the heap while ensuring size <= max_results.
void push_heap(ResultHeap &heap, float score, int score_based_root_path,
               uint32_t id, const std::string *value, size_t max_results,
               const UnicodeText *unicode = nullptr) {
  MatchResult result(score, score_based_root_path, id, value, unicode);
  if (heap.size() < max_results || result < heap.top()) {
    heap.push(std::move(result));
    if (heap.size() > max_results) {
      heap.pop();
    }
  }
}

vector<MatchResult> finalize(const string &query, const string &query_case,
                             const MatchOptions &options, bool ignore_diacritics,
                             bool record_match_indexes, const FuzzaldrinJob &fz,
                             ResultHeap &&heap) {
  vector<MatchResult> vec;
  while (heap.size()) {
    const MatchResult &result = heap.top();
    if (record_match_indexes) {
      if (result.unicode) {
        result.matchIndexes.reset(new vector<int>());
        if (fz.unicode_prepared) {
          auto indexes = fuzzaldrin::match_indexes(result.unicode->value,
              result.unicode->folded, *fz.unicode_prepared);
          result.matchIndexes->assign(indexes.begin(), indexes.end());
        } else {
          score_match(result.unicode->value.c_str(), result.unicode->folded.c_str(),
              fz.unicode_query->value.c_str(), fz.unicode_query->folded.c_str(),
              options, 0.0, result.matchIndexes.get());
        }
        unicode_match_indexes_to_utf16(*result.unicode, *result.matchIndexes);
      } else if (fz.prepared) {
        // Fuzzaldrin-ranked results take their highlight positions from the
        // same algorithm (trace-matrix alignment + basename merge). The
        // returned array can be shorter than the query (optional characters
        // may go unmatched) or longer (basename positions merge in).
        if (ignore_diacritics) {
          // Match against the folded value, then map each folded byte offset
          // back to a UTF-16 offset in the original value so callers can
          // highlight the accented display text correctly.
          vector<int> pos_map;
          string folded = fold_diacritics(*result.value, &pos_map);
          auto indexes = fuzzaldrin::match_indexes(folded, folded, *fz.prepared);
          result.matchIndexes.reset(
              new vector<int>(indexes.begin(), indexes.end()));
          match_indexes_to_utf16(folded, *result.matchIndexes, &pos_map);
        } else {
          string lower = str_to_lower(*result.value);
          auto indexes =
              fuzzaldrin::match_indexes(*result.value, lower, *fz.prepared);
          result.matchIndexes.reset(
              new vector<int>(indexes.begin(), indexes.end()));
          match_indexes_to_utf16(*result.value, *result.matchIndexes);
        }
      } else {
        result.matchIndexes.reset(new vector<int>(query.size()));
        if (ignore_diacritics) {
          // Score against the folded value to find match positions, then map
          // each folded byte offset back to a UTF-16 offset in the original
          // value so callers can highlight the accented display text correctly.
          vector<int> pos_map;
          string folded = fold_diacritics(*result.value, &pos_map);
          score_match(folded.c_str(), folded.c_str(), query.c_str(),
                      query_case.c_str(), options, 0.0,
                      result.matchIndexes.get());
          match_indexes_to_utf16(folded, *result.matchIndexes, &pos_map);
        } else {
          string lower = str_to_lower(*result.value);
          score_match(result.value->c_str(), lower.c_str(), query.c_str(),
                      query_case.c_str(), options, 0.0,
                      result.matchIndexes.get());
          match_indexes_to_utf16(*result.value, *result.matchIndexes);
        }
      }
    }
    vec.push_back(result);
    if (fz.prepared) {
      // Raw fuzzaldrin-plus scores are unbounded; normalize to the module's
      // (0, 1] contract against the query's flat self-match ceiling. Heap
      // ordering already happened in raw units, so top-N selection is exact
      // fuzzaldrin-plus order; better-than-exact matches (basename bonus)
      // saturate at 1 and fall to the shorter-string tie-break.
      float ceiling = result.unicode ? fz.unicode_ceiling : fz.ceiling;
      vec.back().score = std::min(1.0f, vec.back().score / ceiling);
    }
    heap.pop();
  }
  reverse(vec.begin(), vec.end());
  return vec;
}

void thread_worker(const string &query, const string &query_case,
                   const MatchOptions &options, const FuzzaldrinJob &fz,
                   bool ignore_diacritics, bool use_last_match,
                   std::atomic<float> *min_score, size_t max_results,
                   vector<MatcherBase::CandidateData> &candidates, size_t start,
                   size_t end, ResultHeap &result) {
  // Fuzzaldrin treats " _-:/\" as optional query characters, so its prefilter
  // must only require the query's core characters — a full-query bitmask
  // would wrongly demand a literal `-` (the one optional character the
  // bitmask tracks).
  uint64_t bitmask = fz.unicode_query
      ? letter_bitmask(fz.unicode_prepared ? fz.unicode_prepared->core_lw
          : (options.case_sensitive ? fz.unicode_query->value : fz.unicode_query->folded))
      : letter_bitmask(fz.prepared ? fz.prepared->core_lw : query_case);
  for (size_t i = start; i < end; i++) {
    auto &candidate = candidates[i];
    if (use_last_match && !candidate.last_match) {
      continue;
    }
    // An ASCII matching string cannot contain a required non-ASCII scalar.
    // Reject it before allocating a Unicode view, while keeping aliases such
    // as Kelvin sign and long s eligible when their simple fold is ASCII.
    if (fz.requires_non_ascii && !candidate.non_ascii) {
      candidate.last_match = false;
      continue;
    }
    if ((bitmask & candidate.bitmask) == bitmask) {
      // In diacritic-insensitive mode the folded form is the string we score
      // against (both the "original" and the case-folded haystack), and the
      // query is already folded.
      const string &haystack =
          ignore_diacritics ? candidate.folded : candidate.value;
      const string &haystack_case =
          ignore_diacritics ? candidate.folded : candidate.lowercase;
      float score;
      const UnicodeText *unicode = nullptr;
      if (candidate.non_ascii || fz.query_non_ascii) {
        if (!candidate.unicode) {
          if (ignore_diacritics && !is_ascii(candidate.value)) {
            vector<int> pos_map;
            fold_diacritics(candidate.value, &pos_map);
            candidate.unicode = make_unique<UnicodeText>(decode_unicode(haystack, &pos_map));
          } else {
            candidate.unicode = make_unique<UnicodeText>(decode_unicode(haystack));
          }
        }
        unicode = candidate.unicode.get();
      }
      if (query == "") {
        score = 1;
      } else if (unicode && fz.unicode_prepared) {
        score = fuzzaldrin::score(unicode->value, unicode->folded,
                                 *fz.unicode_prepared, fz.scorer_options);
      } else if (unicode) {
        score = score_match(unicode->value.c_str(), unicode->folded.c_str(),
            fz.unicode_query->value.c_str(), fz.unicode_query->folded.c_str(),
            options, min_score->load());
      } else if (fz.prepared) {
        // Raw fuzzaldrin-plus score; normalized to (0, 1] in finalize().
        // min_score is deliberately not consulted here: the scorer's own
        // miss-budget bail is its pruning mechanism, and a useful pre-score
        // upper bound does not exist for this scoring model.
        score = fuzzaldrin::score(haystack, haystack_case, *fz.prepared,
                                  fz.scorer_options);
      } else {
        score = score_match(haystack.c_str(), haystack_case.c_str(),
                            query.c_str(), query_case.c_str(), options,
                            min_score->load());
      }
      if (score > 0) {
        push_heap(result, score, score_based_root_path(options, candidate),
                  candidate.id, &candidate.value, max_results, unicode);
        if (result.size() == max_results) {
          float current_max = result.top().score;
          float min_score_value = min_score->load();
          // Unfortunately there's no thread-safe "max"...
          // When running compare_exchange_weak it's possible that another
          // thread wrote to it in the meantime, in which case we have to check
          // again. Since it's always increasing this is guaranteed to converge.
          while (current_max > min_score_value) {
            min_score->compare_exchange_weak(min_score_value, current_max);
          }
        }
        candidate.last_match = true;
      } else {
        // A fuzzaldrin query consisting only of optional characters scores 0
        // everywhere, yet an extension of it can still match — such a query
        // must not blind the last_match skip cache.
        candidate.last_match = fz.prepared && fz.prepared->core_lw.empty();
      }
    }
  }
}

vector<MatchResult> MatcherBase::findMatches(const std::string &query,
                                             const MatcherOptions &options) {
  size_t max_results = options.max_results;
  size_t num_threads = options.num_threads;
  if (max_results == 0) {
    max_results = numeric_limits<size_t>::max();
  }
  MatchOptions matchOptions;
  matchOptions.case_sensitive = options.case_sensitive;
  matchOptions.smart_case = false;
  matchOptions.max_gap = options.max_gap;
  matchOptions.root_path = options.root_path;

  string new_query;
  // Ignore all whitespace in the query.
  for (unsigned char c : query) {
    if (!(c == ' ' || (c >= '\t' && c <= '\r'))) {
      new_query += c;
    }
    if (options.smart_case && c >= 'A' && c <= 'Z' && !matchOptions.case_sensitive) {
      matchOptions.smart_case = true;
    }
  }
  if (options.smart_case && !matchOptions.case_sensitive && !is_ascii(query)) {
    for (auto cp : decode_unicode(query).value) {
      if (is_unicode_uppercase(cp) || is_unicode_titlecase(cp)) matchOptions.smart_case = true;
    }
  }

  string query_case;
  if (ignore_diacritics_) {
    // Fold the query the same way candidates were folded. The folded query is
    // used as both the needle and its case form, since the folded haystack is
    // already lowercase ASCII.
    new_query = fold_diacritics(new_query);
    query_case = new_query;
  } else if (!options.case_sensitive) {
    query_case = str_to_lower(new_query);
  } else {
    query_case = query;
  }

  // The prepared query and normalization ceiling are computed once and
  // shared read-only across the worker threads.
  std::optional<fuzzaldrin::PreparedQuery> prepared;
  std::optional<fuzzaldrin::UnicodePreparedQuery> unicode_prepared;
  std::optional<UnicodeText> unicode_query;
  FuzzaldrinJob fz;
  fz.query_non_ascii = !is_ascii(new_query);
  if (fz.query_non_ascii || unicode_candidate_count_ > 0) {
    unicode_query.emplace(decode_unicode(new_query));
    fz.unicode_query = &*unicode_query;
  }
  if (options.algorithm == ScoringAlgorithm::Fuzzaldrin && !new_query.empty()) {
    prepared.emplace(new_query);
    fz.prepared = &*prepared;
    fz.scorer_options = {options.use_path_scoring, options.use_extension_bonus};
    fz.ceiling = fuzzaldrin::score_ceiling(*prepared);
    if (unicode_query) {
      unicode_prepared.emplace(unicode_query->value);
      fz.unicode_prepared = &*unicode_prepared;
      fz.unicode_ceiling = fuzzaldrin::score_ceiling(*unicode_prepared);
    }
  }
  if (unicode_query) {
    const auto &required = unicode_prepared ? unicode_prepared->core_lw
        : (options.case_sensitive ? unicode_query->value : unicode_query->folded);
    fz.requires_non_ascii = any_of(required.begin(), required.end(),
                                  [](char32_t cp) { return cp >= 0x80; });
  }

  // If our current query is just an extension of the last query,
  // quickly ignore all previous non-matches as an optimization.
  bool use_last_match = lastOptions_ && same_matching_options(options, *lastOptions_) &&
                        query_case.substr(0, lastQuery_.size()) == lastQuery_;
  lastQuery_ = query_case;
  lastOptions_ = options;

  ResultHeap combined;
  std::atomic<float> min_score(0);
  if (num_threads == 0 || candidates_.size() < 10000) {
    thread_worker(new_query, query_case, matchOptions, fz, ignore_diacritics_,
                  use_last_match, &min_score, max_results, candidates_, 0,
                  candidates_.size(), combined);
  } else {
    vector<ResultHeap> thread_results(num_threads);
    vector<thread> threads;
    size_t cur_start = 0;
    for (size_t i = 0; i < num_threads; i++) {
      size_t chunk_size = candidates_.size() / num_threads;
      // Distribute remainder among the chunks.
      if (i < candidates_.size() % num_threads) {
        chunk_size++;
      }
      threads.emplace_back(thread_worker, ref(new_query), ref(query_case),
                           ref(matchOptions), cref(fz), ignore_diacritics_,
                           use_last_match, &min_score, max_results,
                           ref(candidates_), cur_start, cur_start + chunk_size,
                           ref(thread_results[i]));
      cur_start += chunk_size;
    }

    for (size_t i = 0; i < num_threads; i++) {
      threads[i].join();
      while (thread_results[i].size()) {
        auto &top = thread_results[i].top();
        push_heap(combined, top.score, top.score_based_root_path, top.id,
                  top.value, max_results, top.unicode);
        thread_results[i].pop();
      }
    }
  }

  return finalize(new_query, query_case, matchOptions, ignore_diacritics_,
                  options.record_match_indexes, fz, std::move(combined));
}

void MatcherBase::addCandidate(uint32_t id, const string &candidate) {
  auto it = lookup_.find(id);
  if (it == lookup_.end()) {
    lookup_[id] = candidates_.size();
    CandidateData data;
    data.id = id;
    data.value = candidate;
    data.last_match = true;
    data.num_dirs = num_dirs(candidate);
    if (ignore_diacritics_) {
      // Match against the folded form; the bitmask is derived from it too.
      data.folded = fold_diacritics(candidate);
      data.non_ascii = !is_ascii(data.folded);
      if (data.non_ascii) {
        vector<int> pos_map;
        fold_diacritics(candidate, &pos_map);
        data.unicode = make_unique<UnicodeText>(decode_unicode(data.folded, &pos_map));
      }
      data.bitmask = letter_bitmask(data.folded);
    } else {
      data.non_ascii = !is_ascii(candidate);
      if (data.non_ascii) {
        data.unicode = make_unique<UnicodeText>(decode_unicode(candidate));
      } else {
        string lowercase = str_to_lower(candidate);
        data.bitmask = letter_bitmask(lowercase);
        data.lowercase = std::move(lowercase);
      }
    }
    if (data.non_ascii) {
      data.bitmask = letter_bitmask(data.unicode->folded);
      unicode_candidate_count_++;
    }
    candidates_.emplace_back(std::move(data));
  }
}

void MatcherBase::removeCandidate(uint32_t id) {
  auto it = lookup_.find(id);
  if (it != lookup_.end()) {
    if (candidates_[it->second].non_ascii) unicode_candidate_count_--;
    if (it->second + 1 != candidates_.size()) {
      swap(candidates_[it->second], candidates_.back());
      lookup_[candidates_[it->second].id] = it->second;
    }
    candidates_.pop_back();
    lookup_.erase(id);
  }
}

void MatcherBase::clear() {
  candidates_.clear();
  lookup_.clear();
  unicode_candidate_count_ = 0;
}

void MatcherBase::reserve(size_t n) {
  candidates_.reserve(n);
  lookup_.reserve(n);
}

size_t MatcherBase::size() const { return candidates_.size(); }
