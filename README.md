# @lumine-code/fuzzy-native

Provides fast native fuzzy string matching with multithreading and diacritic-aware scoring.

## Features

- **Native performance**: scores candidate sets in C++ with multithreaded matching.
- **Path-aware ranking**: prioritizes word boundaries, consecutive matches, and file path segments.
- **Diacritic handling**: optionally folds accents while preserving indexes into the original text.

## Installation

```sh
npm install @lumine-code/fuzzy-native
```

The scoring algorithm is heavily tuned for file paths, but should work for general strings. It also supports the Fuzzaldrin algorithm used by Lumine's command palette and other fuzzy finders.

## API

Read `lib/main.d.ts` for the API of the `Matcher` class.

See also the [spec](spec/fuzzy-native-spec.js) for basic usage.

### Accent-insensitive matching

Pass `{ ignoreDiacritics: true }` as the third constructor argument to fold
candidates and queries to a lowercase, diacritic-free ASCII form before
matching, so e.g. `"cafe"` matches `"café"` and `"strasse"` matches `"Straße"`:

```js
const matcher = new Matcher([0, 1], ['café', 'naïve'], { ignoreDiacritics: true });
matcher.match('cafe'); // => matches 'café'
```

The reported `value` and `matchIndexes` always refer to the **original**
(accented) string — indexes are mapped back through the fold, including
expanding folds such as `ß → ss`, so highlighting lines up with the displayed
text. Because folded forms are precomputed per candidate, this must be set at
construction time (it is not a per-`match()` option). The fold table lives in
`src/diacritics_table.h` and is regenerated via `tools/gen-diacritics-table.js`.

## Scoring algorithm

### Default
The _default scoring_ algorithm is mostly borrowed from @wincent's excellent [command-t](https://github.com/wincent/command-t) vim plugin; most of the code is from [his implementation in  match.c](https://github.com/wincent/command-t/blob/master/ruby/command-t/match.c).

Read [the source code](src/score_match.cpp) for a quick overview of how it works (the function `recursive_match`).

NB: [score_match.cpp](src/score_match.cpp) and [score_match.h](src/score_match.h) have no dependencies besides the C/C++ stdlib and can easily be reused for other purposes.

There are a few notable additional optimizations:

- Before running the recursive matcher, we first do a backwards scan through the haystack to see if the needle exists at all. At the same time, we compute the right-most match for each character in the needle to prune the search space.
- For each candidate string, we pre-compute and store a bitmask of its letters in `MatcherBase`. We then compare this the "letter bitmask" of the query to quickly prune out non-matches.

### Fuzzaldrin

Ported from the original Fuzzaldrin implementation. Its [scorer](https://github.com/atom/fuzzaldrin/blob/master/src/scorer.coffee) is easier to follow than the equivalent optimized C++ code.

## Contributing

Got ideas to make this package better, found a bug, or want to help add new features? Just drop your thoughts on GitHub. Any feedback is welcome!
