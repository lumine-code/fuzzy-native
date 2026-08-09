# fuzzy-native

Provides fast native fuzzy string matching with multithreading and diacritic-aware scoring.

## Features

- **Native performance**: scores candidate sets in C++ with multithreaded matching.
- **Two algorithms**: command-t scoring for paths and a full fuzzaldrin-plus port for general ranking.
- **Path-aware ranking**: prioritizes word boundaries, consecutive matches, and file path segments.
- **Diacritic handling**: optionally folds accents while preserving indexes into the original text.

## Installation

```sh
npm install @lumine-code/fuzzy-native
```

The default scoring algorithm is heavily tuned for file paths, but should work for general strings. The package also ships a faithful native port of the fuzzaldrin-plus algorithm, which Lumine's command palette, select lists, and other fuzzy finders use.

## API

Read `lib/main.d.ts` for the API of the `Matcher` class.

See also the [spec](spec/fuzzy-native-spec.js) for basic usage.

### Accent-insensitive matching

Pass `{ ignoreDiacritics: true }` as the third constructor argument to fold
candidates and queries to a lowercase, diacritic-free ASCII form before
matching, so e.g. `"cafe"` matches `"café"` and `"strasse"` matches `"Straße"`:

```js
const matcher = new Matcher([0, 1], ["café", "naïve"], { ignoreDiacritics: true });
matcher.match("cafe"); // => matches 'café'
```

The reported `value` and `matchIndexes` always refer to the **original**
(accented) string — indexes are mapped back through the fold, including
expanding folds such as `ß → ss`, so highlighting lines up with the displayed
text. Because folded forms are precomputed per candidate, this must be set at
construction time (it is not a per-`match()` option). The fold table lives in
`src/diacritics_table.h` and is regenerated after `npm install` via
`node tools/gen-diacritics-table.js`.

## Scoring algorithm

### Default

The _default scoring_ algorithm is mostly borrowed from @wincent's excellent [command-t](https://github.com/wincent/command-t) vim plugin; most of the code is from [his implementation in match.c](https://github.com/wincent/command-t/blob/master/ruby/command-t/match.c).

Read [the source code](src/score_match.cpp) for a quick overview of how it works (the function `recursive_match`).

NB: [score_match.cpp](src/score_match.cpp) and [score_match.h](src/score_match.h) have no dependencies besides the C/C++ stdlib and can easily be reused for other purposes.

There are a few notable additional optimizations:

- Before running the recursive matcher, we first do a backwards scan through the haystack to see if the needle exists at all. At the same time, we compute the right-most match for each character in the needle to prune the search space.
- For each candidate string, we pre-compute and store a bitmask of its letters in `MatcherBase`. We then compare this the "letter bitmask" of the query to quickly prune out non-matches.

### Fuzzaldrin

Selected with `{ algorithm: "fuzzaldrin" }`. A faithful C++ port of the
[fuzzaldrin-plus](https://github.com/jeancroy/fuzz-aldrin-plus) scoring
algorithm, translated from [zadeh](https://github.com/atom-community/zadeh):

- An optimal-alignment scorer (Smith–Waterman over two rolling rows) with
  bonuses for acronyms (`fb` → `FooBar`, `foo-bar`), consecutive runs, word
  boundaries, same-case matches, and matches near the start of the string,
  plus a miss budget that bounds worst-case work.
- `" _-:/\"` are _optional_ query characters: they improve the score when
  present but never block a match, so `foo-bar` still matches `foo/bar` and
  `foobar`.
- Path scoring (`usePathScoring`, default `true`): the final score
  interpolates between the basename score and the full-path score by
  directory depth, so shallow paths and basename hits rank first. An optional
  extension bonus (`useExtensionBonus`, default `false`) prefers `myFile.h`
  over `myFile.html` for the query `mf.h`.
- Both slash kinds are equivalent in every character comparison, so
  Windows-native `src\main\app.js` scores identically to `src/main/app.js`.
- `matchIndexes` come from the same algorithm (a trace-matrix alignment with
  basename merging), so highlights always agree with the ranking. The array
  can be shorter than the query (unmatched optional characters) or longer
  (basename positions merge in).
- Raw fuzzaldrin-plus scores are unbounded; they are normalized to `(0, 1]`
  against the query's self-match score, so `1` means an exact (or better,
  e.g. exact-basename) match.

The `caseSensitive`, `smartCase`, and `maxGap` options apply only to the
default algorithm; `usePathScoring` and `useExtensionBonus` apply only to
fuzzaldrin.

## Contributing

Got ideas to make this package better, found a bug, or want to help add new features? Just drop your thoughts on GitHub. Any feedback is welcome!
