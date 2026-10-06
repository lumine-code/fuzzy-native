/**
 * The options that can be passed to {@link Matcher#match}.
 */
export type MatcherOptions = {
  /**
   * Whether matching is case-sensitive (`"command-t"` only; the
   * `"fuzzaldrin"` algorithm is always case-insensitive with a same-case
   * bonus). Defaults to `false`.
   */
  caseSensitive?: boolean;

  /**
   * When `true` and the query contains an uppercase letter, matching becomes
   * case-sensitive (`"command-t"` only). Defaults to `false`.
   */
  smartCase?: boolean;

  /** How many results to return at the maximum. Defaults to no limit. */
  maxResults?: number;

  /**
   * Maximum “gap” to allow between consecutive letters for a match candidate
   * (`"command-t"` only). Provide a smaller value to speed up query results.
   * Defaults to no limit.
   */
  maxGap?: number;

  /**
   * How many threads to use while searching. Defaults to `1`.
   */
  numThreads?: number;

  /**
   * Whether to return metadata about the indices of the characters that
   * matched in each returned max. Defaults to `false`.
   */
  recordMatchIndexes?: boolean;

  /**
   * A path used to break ties between equally-scored candidates: candidates
   * sharing more leading directories with `rootPath` (and having fewer
   * directories overall) win the tie.
   */
  rootPath?: string;

  /**
   * The algorithm to use for fuzzy-matching. `"fuzzaldrin"` is a native port
   * of the fuzzaldrin-plus scoring algorithm (acronym and consecutive-run
   * bonuses, basename-aware path scoring). Any other value, including the
   * default of `undefined`, selects the command-t algorithm.
   */
  algorithm?: "fuzzaldrin" | "command-t";

  /**
   * Whether the `"fuzzaldrin"` algorithm blends the basename score with the
   * full-path score by directory depth. A no-op for candidates without path
   * separators. Defaults to `true`.
   */
  usePathScoring?: boolean;

  /**
   * Whether the `"fuzzaldrin"` algorithm awards a bonus for matching the file
   * extension (e.g. query `mf.h` prefers `myFile.h` over `myFile.html`).
   * Defaults to `false`.
   */
  useExtensionBonus?: boolean;
};

/**
 * A single result returned by {@link Matcher#match}.
 */
export type MatchResult = {
  /** A unique identifier for the match. */
  id: number;

  /** The string value of the match. */
  value: string;

  /**
   * A number in the range (0, 1] — i.e., the maximum value is `1` and the
   * minimum value is the smallest possible positive value. Higher scores mean
   * more relevant matches. `0` means “no match” and will never be returned.
   */
  score: number;

  /**
   * Matching UTF-16 code-unit indexes in `value`, for highlight rendering.
   * A matched astral character includes both surrogate indexes. Expanding
   * diacritic folds can map several aligned characters to the same index.
   * With `"command-t"`, indexes follow the query alignment; with
   * `"fuzzaldrin"` the array can be shorter (optional characters such as
   * `-`/`_`/`/` may go unmatched) or longer (full-path and basename
   * alignments are merged). This can be costly, so this information is
   * returned only when {@link MatcherOptions.recordMatchIndexes} is `true`.
   */
  matchIndexes?: number[];
};

/**
 * Matcher-level options fixed at construction time.
 */
export type MatcherConstructorOptions = {
  /**
   * When `true`, candidates and queries are folded to a lowercase,
   * diacritic-free ASCII form before matching, so e.g. `"cafe"` matches
   * `"café"`. Reported `matchIndexes` are mapped back to offsets in the
   * original (accented) `value`. Must be set at construction because each
   * candidate's folded form is precomputed when it is added. Defaults to
   * `false`.
   */
  ignoreDiacritics?: boolean;
};

export class Matcher {
  /**
   * Construct a new {@link Matcher} object.
   *
   * You may specify candidates at instantiation time (with the same arguments
   * used by {@link addCandidates} and {@link setCandidates}) or you may wait
   * and add candidates later.
   *
   * @param ids A list of numeric IDs. Must correspond to the candidates
   *  themselves.
   * @param candidates A list of candidates against which we will be matching.
   * @param options Matcher-level {@link MatcherConstructorOptions}.
   */
  constructor();
  constructor(ids: number[], candidates: string[], options?: MatcherConstructorOptions);

  /**
   * Find all candidates that match the given query.
   *
   * @param query The input against which candidates will be searched.
   * @param options Any {@link MatcherOptions}.
   */
  match(query: string, options?: MatcherOptions): MatchResult[];

  /**
   * Add candidates to the list.
   *
   * You are responsible for ensuring that the IDs you use do not match the IDs
   * of any candidates that are already present in the `Matcher`. Any
   * candidates whose IDs already exist in the `Matcher` are silently ignored.
   *
   * @param ids A list of numeric IDs. Must correspond to the candidates
   *  themselves.
   * @param candidates A list of candidates against which we will be matching.
   */
  addCandidates(ids: number[], candidates: string[]): void;

  /**
   * Remove candidates from the list.
   *
   * @param ids The unique identifiers for each of the candidates you want to
   *  remove. Must be an array; if you want to remove only one candidate, wrap
   *  the value in an array first.
   */
  removeCandidates(ids: number[]): void;

  /**
   * Set a complete list of candidates, removing any candidate that may already
   * be defined.
   *
   * If you want to add candidates instead without removing any that may
   * already exist, use {@link addCandidates}.
   *
   * @param ids A list of numeric IDs. Must correspond to the candidates
   *  themselves.
   * @param candidates A list of candidates against which we will be matching.
   */
  setCandidates(ids: number[], candidates: string[]): void;
}
