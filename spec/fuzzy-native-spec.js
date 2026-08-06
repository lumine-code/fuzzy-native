"use strict";

var fuzzyNative = require("../lib/main");

function values(results) {
  return results.map(function (x) {
    return x.value;
  });
}

/** @returns {[number[], string[]]} */
function genIds(values) {
  return [Array.from({ length: values.length }, (_, i) => i), values];
}

describe("fuzzy-native", function () {
  /** @type {import('../lib/main').Matcher} */
  let matcher;
  beforeEach(function () {
    const paths = [
      "",
      "a",
      "ab",
      "abC",
      "abcd",
      "alphabetacappa",
      "AlphaBetaCappa",
      "thisisatestdir",
      "/////ThisIsATestDir",
      "/this/is/a/test/dir",
      "/test/tiatd",
      "/zzz/path2/path3/path4",
      "/path1/zzz/path3/path4",
      "/path1/path2/zzz/path4",
      "/path1/path2/path3/zzz",
    ];

    matcher = new fuzzyNative.Matcher(...genIds(paths));
  });

  it("can match strings", function () {
    var result = matcher.match("abc");
    expect(values(result)).toEqual(["abC", "abcd", "AlphaBetaCappa", "alphabetacappa"]);

    result = matcher.match("t/i/a/t/d");
    expect(values(result)).toEqual(["/this/is/a/test/dir"]);

    // Make sure we prefer exact matches > abbreviations > others.
    result = matcher.match("tiatd");
    expect(values(result)).toEqual([
      "/test/tiatd",
      "/this/is/a/test/dir",
      "/////ThisIsATestDir",
      "thisisatestdir",
    ]);

    // Defaults to case-insensitive matching (favouring the right case).
    result = matcher.match("ABC", { smartCase: true });
    expect(values(result)).toEqual(["AlphaBetaCappa", "abC", "abcd", "alphabetacappa"]);

    // Spaces should be ignored.
    result = matcher.match("a b\tcappa");
    expect(values(result)).toEqual(["AlphaBetaCappa", "alphabetacappa"]);

    // There was a bug where this would result in a match.
    result = matcher.match("abcc");
    expect(result).toEqual([]);
  });

  it("can match strings for alternate scoring", function () {
    let results = matcher.match("tiatd", { algorithm: "fuzzaldrin" });

    // Exact basename match first, then the acronym matches (the path
    // segments and the camelCase word starts), then scattered characters.
    expect(values(results)).toEqual([
      "/test/tiatd",
      "/this/is/a/test/dir",
      "/////ThisIsATestDir",
      "thisisatestdir",
    ]);
    // Normalized to (0, 1] and strictly descending.
    for (const result of results) {
      expect(result.score).toBeGreaterThan(0);
      expect(result.score).toBeLessThanOrEqual(1);
    }
    expect(results[0].score).toBeCloseTo(0.9823, 4);
    expect(results[1].score).toBeCloseTo(0.4585, 4);
    expect(results[2].score).toBeCloseTo(0.1221, 4);
    expect(results[3].score).toBeCloseTo(0.0592, 4);

    // Default (file-based) algorithm
    results = matcher.match("tiatd");
    const resultsWithScores = () =>
      results.map((result) => {
        return {
          value: result.value,
          score: Math.round(result.score * 10000) / 10000,
        };
      });
    expect(resultsWithScores()).toEqual([
      { value: "/test/tiatd", score: 0.75 },
      { value: "/this/is/a/test/dir", score: 0.1554 },
      { value: "/////ThisIsATestDir", score: 0.1536 },
      { value: "thisisatestdir", score: 0.0536 },
    ]);
  });

  it("can match empty strings for alternate scoring", function () {
    matcher.setCandidates([1, 2, 3], ["hello", "is", "it"]);
    // An empty query scores every candidate as 1, and 'is'/'it' are also
    // tied on length, so only 'hello' is guaranteed to sort last.
    let results = matcher.match("", { algorithm: "fuzzaldrin" });
    const emptyQueryResults = values(results);
    expect(emptyQueryResults.slice(0, 2).sort()).toEqual(["is", "it"]);
    expect(emptyQueryResults[2]).toEqual("hello");

    results = matcher.match("it", { algorithm: "fuzzaldrin" });
    expect(values(results)).toEqual(["it"]);
  });

  it("can do a case-sensitive search", function () {
    var result = matcher.match("abc", { caseSensitive: true });
    expect(values(result)).toEqual(["abcd", "alphabetacappa"]);

    result = matcher.match("C", { caseSensitive: true });
    expect(values(result)).toEqual(["abC", "AlphaBetaCappa"]);
  });

  it("uses smart case", function () {
    var result = matcher.match("ThisIsATestDir", { smartCase: true });
    expect(values(result)).toEqual([
      "/////ThisIsATestDir",
      "thisisatestdir",
      "/this/is/a/test/dir",
    ]);
  });

  it("respects maxGap", function () {
    var result = matcher.match("abc", { maxGap: 1 });
    expect(values(result)).toEqual(["abC", "abcd"]);
  });

  it("favours shallow matches", function () {
    var result = matcher.match("zzz", { caseSensitive: true });
    expect(values(result)).toEqual([
      "/path1/path2/path3/zzz",
      "/path1/path2/zzz/path4",
      "/path1/zzz/path3/path4",
      "/zzz/path2/path3/path4",
    ]);
  });

  it("prefers whole words", function () {
    matcher.setCandidates(...genIds(["testa", "testA", "tes/A"]));
    var result = matcher.match("a");
    expect(values(result)).toEqual(["tes/A", "testA", "testa"]);
  });

  it("breaks ties by length if root folder is not passed", function () {
    matcher.setCandidates(...genIds(["123/a", "12/a", "1/a"]));
    var result = matcher.match("a");
    expect(values(result)).toEqual(["1/a", "12/a", "123/a"]);
  });

  it("breaks ties by distance to root folder", function () {
    matcher.setCandidates(
      ...genIds([
        "/A/B/C/file.js",
        "/A/B/file.js",
        "/A/C/D/file.js",
        "/A/REALLY_BIG_NAME/file.js",
        "/A/file.js",
      ]),
    );
    var result = matcher.match("file", { rootPath: "/A/B/C/" });
    expect(values(result)).toEqual([
      "/A/B/C/file.js",
      "/A/B/file.js",
      "/A/file.js",
      "/A/REALLY_BIG_NAME/file.js",
      "/A/C/D/file.js",
    ]);
  });

  it("can limit to maxResults", function () {
    var result = matcher.match("abc", { maxResults: 1 });
    expect(values(result)).toEqual(["abC"]);

    result = matcher.match("ABC", { maxResults: 2, smartCase: true });
    expect(values(result)).toEqual(["AlphaBetaCappa", "abC"]);
  });

  it("can return matchIndexes", function () {
    var result = matcher.match("abc", { recordMatchIndexes: true });
    expect(result[0].matchIndexes).toEqual([0, 1, 2]);
    expect(result[1].matchIndexes).toEqual([0, 1, 2]);
    // alphabetacappa
    // _    _   _
    expect(result[2].matchIndexes).toEqual([0, 5, 9]);
    expect(result[3].matchIndexes).toEqual([4, 5, 9]);

    result = matcher.match("t/i/a/t/d", { recordMatchIndexes: true });
    // /this/is/a/test/dir',
    //  _   __ ____   __
    expect(result[0].matchIndexes).toEqual([1, 5, 6, 8, 9, 10, 11, 15, 16]);
  });

  it("supports modification", function () {
    var result = matcher.match("abc", { maxResults: 1 });
    expect(values(result)).toEqual(["abC"]);

    matcher.setCandidates([], []);
    result = matcher.match("abc");
    expect(values(result)).toEqual([]);

    matcher.addCandidates([0, 1], ["abc", "def"]);
    result = matcher.match("abc");
    expect(values(result)).toEqual(["abc"]);

    matcher.removeCandidates([0]);
    result = matcher.match("");
    expect(values(result)).toEqual(["def"]);
  });

  it("applies a variable gap penalty", function () {
    const CANDIDATE = "abcdefghijk/test";
    matcher.setCandidates([0], [CANDIDATE]);
    // Gap penalty caps out at 0.2.
    let query = "a/test";
    expect(matcher.match(query)[0].score).toBeCloseTo((0.2 * query.length) / CANDIDATE.length);
    query = "ab/test";
    expect(matcher.match(query)[0].score).toBeCloseTo((0.2 * query.length) / CANDIDATE.length);
    // And then decreases by 0.05.
    query = "abc/test";
    expect(matcher.match(query)[0].score).toBeCloseTo((0.25 * query.length) / CANDIDATE.length);
    query = "abcdefghij/test";
    expect(matcher.match(query)[0].score).toBeCloseTo((0.6 * query.length) / CANDIDATE.length);
    query = "abcdefghijk/test";
    expect(matcher.match(query)[0].score).toBe(1);
  });

  it("supports large strings", function () {
    var longString = "";
    var indexes = [];
    for (var i = 0; i < 500; i++) {
      longString += "ab";
      indexes.push(i * 2, i * 2 + 1);
    }
    matcher.setCandidates([0], [longString]);
    expect(matcher.match(longString, { recordMatchIndexes: true })).toEqual([
      {
        score: 1,
        id: 0,
        value: longString,
        matchIndexes: indexes,
      },
    ]);

    // Despite exceeding the scoring threshold, make sure penalties are applied.
    const matches = matcher.match("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    expect(matches.length).toBe(1);
    expect(matches[0].score).toBeLessThan(1e-6);
  });

  it("works with non-alpha characters", function () {
    matcher.setCandidates([0, 1], ["-_01234", "01234-"]);
    expect(values(matcher.match("-034"))).toEqual(["-_01234"]);
  });

  it("ensures that the ids array and the values array are the same length when adding candidates", () => {
    const expectedMessage = "Expected ids array and values array to have the same length";
    expect(() => matcher.addCandidates([1, 2, 3], ["a", "b"])).toThrowError(expectedMessage);
    expect(() => matcher.addCandidates([1, 2], ["a", "b", "c"])).toThrowError(expectedMessage);
  });

  it("throws (rather than crashing) on non-string candidates or non-number ids", () => {
    // With C++ exceptions disabled, converting the wrong type used to abort the
    // whole process; these must raise catchable JS errors instead.
    expect(() => matcher.addCandidates([0], [{}])).toThrowError(
      "Expected second array to only contain strings",
    );
    expect(() => matcher.addCandidates([0], [123])).toThrowError(
      "Expected second array to only contain strings",
    );
    expect(() => matcher.addCandidates(["x"], ["a"])).toThrowError(
      "Expected first array to only contain unsigned 32-bit integer ids",
    );
    expect(() => matcher.removeCandidates([{}])).toThrowError(
      "Expected array to only contain unsigned 32-bit integer ids",
    );
    // The matcher is still usable afterwards.
    matcher.setCandidates([0, 1], ["abc", "abd"]);
    expect(values(matcher.match("abc"))).toEqual(["abc"]);
  });

  it("returns match results for duplicate values with different ids", () => {
    matcher.setCandidates([0, 1], ["abc", "abc"]);
    expect(matcher.match("ac").length).toBe(2);
    matcher.setCandidates([0, 0], ["abc", "abc"]);
    expect(matcher.match("ac").length).toBe(1);
  });

  describe("ignoreDiacritics", () => {
    it("matches accented candidates with an unaccented query", () => {
      const m = new fuzzyNative.Matcher(...genIds(["café", "cafeteria", "Málaga", "random"]), {
        ignoreDiacritics: true,
      });
      expect(values(m.match("cafe")).sort()).toEqual(["cafeteria", "café"]);
      expect(values(m.match("malaga"))).toEqual(["Málaga"]);
    });

    it("folds expanding characters like ß -> ss", () => {
      const m = new fuzzyNative.Matcher(...genIds(["Straße", "strasse-other"]), {
        ignoreDiacritics: true,
      });
      expect(values(m.match("strasse")).sort()).toEqual(["Straße", "strasse-other"]);
    });

    it("reports match indexes against the original accented value", () => {
      // "café" — folded query "cafe" matches c(0) a(1) f(2) é(3).
      const m = new fuzzyNative.Matcher(...genIds(["café"]), {
        ignoreDiacritics: true,
      });
      const result = m.match("cafe", { recordMatchIndexes: true });
      expect(result.length).toBe(1);
      expect(result[0].value).toBe("café");
      expect(result[0].matchIndexes).toEqual([0, 1, 2, 3]);
    });

    it("maps indexes correctly when a fold expands (ß -> ss)", () => {
      // "Straße": S(0) t(1) r(2) a(3) ß(4) e(5). Folded: "strasse".
      // Query "strasse" matches folded s,t,r,a,s,s,e -> original offsets
      // 0,1,2,3,4,4,5 (both folded 's's come from the single ß at offset 4).
      const m = new fuzzyNative.Matcher(...genIds(["Straße"]), {
        ignoreDiacritics: true,
      });
      const result = m.match("strasse", { recordMatchIndexes: true });
      expect(result[0].matchIndexes).toEqual([0, 1, 2, 3, 4, 4, 5]);
    });

    it("leaves matching unchanged when disabled (default)", () => {
      const m = new fuzzyNative.Matcher(...genIds(["café", "cafe"]));
      // Without folding, the ASCII query only matches the ASCII candidate.
      expect(values(m.match("cafe"))).toEqual(["cafe"]);
    });
  });

  it("returns matches when using different path separators", () => {
    expect(values(matcher.match("path1_path2_path3_zzz", { caseSensitive: true }))).toEqual([
      "/path1/path2/path3/zzz",
    ]);
    expect(values(matcher.match("path1_path2_path3_zzz", { caseSensitive: false }))).toEqual([
      "/path1/path2/path3/zzz",
    ]);

    expect(values(matcher.match("\\path1\\path2\\path3\\zzz", { caseSensitive: true }))).toEqual([
      "/path1/path2/path3/zzz",
    ]);
    expect(values(matcher.match("\\path1\\path2\\path3\\zzz", { caseSensitive: false }))).toEqual([
      "/path1/path2/path3/zzz",
    ]);
  });

  it("returns exact matches than normalized path separator matches", () => {
    matcher.setCandidates(
      [0, 1, 2],
      ["/path1/path2/path3/zzz", "/path1/path2/path3/zzz_ooo", "/path1/path2/path3/zzz/ooo"],
    );

    // The exact match should rank first; the other two candidates are an
    // exact score tie for this query (same matched prefix, same length),
    // so their relative order isn't guaranteed.
    const exactMatches = values(matcher.match("path1/path2/path3/zzz", { caseSensitive: true }));
    expect(exactMatches[0]).toEqual("/path1/path2/path3/zzz");
    expect(exactMatches.slice(1).sort()).toEqual(
      ["/path1/path2/path3/zzz/ooo", "/path1/path2/path3/zzz_ooo"].sort(),
    );
    expect(values(matcher.match("zzz_ooo", { caseSensitive: true }))).toEqual([
      "/path1/path2/path3/zzz_ooo",
      "/path1/path2/path3/zzz/ooo",
    ]);
    expect(values(matcher.match("path1/path2/path3/zzz_ooo", { caseSensitive: true }))).toEqual([
      "/path1/path2/path3/zzz_ooo",
      "/path1/path2/path3/zzz/ooo",
    ]);
    expect(values(matcher.match("path1/path2/path3/zzz_ooo", { caseSensitive: true }))).toEqual([
      "/path1/path2/path3/zzz_ooo",
      "/path1/path2/path3/zzz/ooo",
    ]);
    expect(values(matcher.match("path1_path2_path3_zzz_ooo", { caseSensitive: true }))).toEqual([
      "/path1/path2/path3/zzz_ooo",
      "/path1/path2/path3/zzz/ooo",
    ]);
    expect(values(matcher.match("path1/path2_path3/zzz_ooo", { caseSensitive: false }))).toEqual([
      "/path1/path2/path3/zzz_ooo",
      "/path1/path2/path3/zzz/ooo",
    ]);
    expect(values(matcher.match("zzz/ooo", { caseSensitive: false }))).toEqual([
      "/path1/path2/path3/zzz/ooo",
    ]);
  });

  it("matches a path separator even when a literal _ or \\ occurs earlier", () => {
    // A literal occurrence before the separator must not cap the search
    // range for the separator-like needle character.
    matcher.setCandidates(...genIds(["lib/foo_utils/bar.js"]));
    expect(values(matcher.match("utils_bar"))).toEqual(["lib/foo_utils/bar.js"]);

    matcher.setCandidates(...genIds(["\\ab/cd"]));
    expect(values(matcher.match("b\\cd"))).toEqual(["\\ab/cd"]);
  });

  it("matches backslash-separated paths with /, _ and \\ in the query", () => {
    matcher.setCandidates(...genIds(["src\\main\\app.js"]));
    expect(values(matcher.match("src/app"))).toEqual(["src\\main\\app.js"]);
    expect(values(matcher.match("src_app"))).toEqual(["src\\main\\app.js"]);
    expect(values(matcher.match("src\\app"))).toEqual(["src\\main\\app.js"]);
  });

  it("ranks exact separators above substituted ones for both slash kinds", () => {
    matcher.setCandidates(...genIds(["a/b", "a\\b"]));
    expect(values(matcher.match("a/b"))).toEqual(["a/b", "a\\b"]);
    expect(values(matcher.match("a\\b"))).toEqual(["a\\b", "a/b"]);
  });

  it("does not match / against a literal underscore", () => {
    matcher.setCandidates(...genIds(["ab_cd"]));
    expect(values(matcher.match("ab/cd"))).toEqual([]);
  });

  describe("algorithm: fuzzaldrin (fuzzaldrin-plus port)", () => {
    const fz = { algorithm: "fuzzaldrin" };

    it("scores an exact match 1.0 and ranks it above partial matches", () => {
      matcher.setCandidates(...genIds(["select", "selection", "select-list", "so-elect"]));
      const results = matcher.match("select", fz);
      expect(values(results)).toEqual(["select", "select-list", "selection", "so-elect"]);
      expect(results[0].score).toBe(1);
      for (const result of results) {
        expect(result.score).toBeGreaterThan(0);
        expect(result.score).toBeLessThanOrEqual(1);
      }
    });

    it("ranks acronym matches far above scattered matches", () => {
      matcher.setCandidates(
        ...genIds(["FooBar", "foo-bar", "foo_bar", "fxoxbxr", "foobar", "flatbush"]),
      );
      const results = matcher.match("fb", fz);
      // The two separator acronyms are exact ties; their mutual order is
      // insertion-dependent.
      expect(values(results).slice(0, 2).sort()).toEqual(["foo-bar", "foo_bar"]);
      expect(results[0].score).toEqual(results[1].score);
      expect(results[2].value).toBe("FooBar"); // camelCase acronym
      expect(results[2].score).toBeGreaterThan(0.5);
      // Scattered matches trail far behind.
      for (const result of results.slice(3)) {
        expect(result.score).toBeLessThan(0.1);
      }
    });

    it("lets consecutive runs dominate scattered characters", () => {
      matcher.setCandidates(...genIds(["core.js", "controller.js", "coverage_report.js"]));
      expect(values(matcher.match("core", fz))).toEqual([
        "core.js",
        "coverage_report.js",
        "controller.js",
      ]);
    });

    it("interpolates basename and full-path scores by directory depth", () => {
      matcher.setCandidates(...genIds(["main.js", "lib/main.js", "a/b/c/d/main.js", "domain.js"]));
      const results = matcher.match("main", fz);
      expect(values(results)).toEqual(["main.js", "lib/main.js", "a/b/c/d/main.js", "domain.js"]);
      expect(results[0].score).toBeCloseTo(0.9804, 4);
      expect(results[1].score).toBeCloseTo(0.9551, 4);
      expect(results[2].score).toBeCloseTo(0.9357, 4);
    });

    it("drops the depth preference with usePathScoring: false", () => {
      matcher.setCandidates(...genIds(["main.js", "lib/main.js", "a/b/c/d/main.js", "domain.js"]));
      const results = matcher.match("main", { ...fz, usePathScoring: false });
      // Without path interpolation the exact-basename-after-slash bonus
      // saturates both path candidates at the ceiling, depth notwithstanding.
      expect(values(results)).toEqual(["lib/main.js", "a/b/c/d/main.js", "main.js", "domain.js"]);
      expect(results[0].score).toBe(1);
      expect(results[1].score).toBe(1);
    });

    it("awards the extension bonus only when enabled", () => {
      matcher.setCandidates(...genIds(["myFile.h", "myFile.html", "myFile.hpp"]));
      const plain = matcher.match("mf.h", fz);
      expect(values(plain)).toEqual(["myFile.h", "myFile.hpp", "myFile.html"]);

      const bonus = matcher.match("mf.h", { ...fz, useExtensionBonus: true });
      expect(values(bonus)).toEqual(["myFile.h", "myFile.hpp", "myFile.html"]);
      // The exact-extension match is amplified; the partial ones much less.
      expect(bonus[0].score).toBeGreaterThan(plain[0].score * 1.9);
      expect(bonus[0].score - bonus[1].score).toBeGreaterThan(plain[0].score - plain[1].score);
    });

    it('treats " _-:/\\" as optional query characters', () => {
      // command-t requires a literal dash; fuzzaldrin only uses it for
      // bonuses (this is also the prefilter-bitmask regression spec).
      matcher.setCandidates(...genIds(["foo/bar", "foo_bar", "foobar", "cat"]));
      expect(values(matcher.match("foo-bar", fz))).toEqual(["foo_bar", "foobar", "foo/bar"]);
      expect(matcher.match("foo-bar", { algorithm: "command-t" })).toEqual([]);

      matcher.setCandidates(...genIds(["abcd"]));
      expect(values(matcher.match("ab/cd", fz))).toEqual(["abcd"]);
      expect(matcher.match("ab/cd", { algorithm: "command-t" })).toEqual([]);
    });

    it("scores both slash kinds identically", () => {
      matcher.setCandidates(...genIds(["src\\main\\app.js", "src/main/app.js", "unrelated.txt"]));
      const results = matcher.match("src/app", fz);
      expect(values(results).sort()).toEqual(["src/main/app.js", "src\\main\\app.js"]);
      expect(results[0].score).toEqual(results[1].score);
    });

    it("does not blind the skip cache on optional-only queries", () => {
      // "-" scores 0 for "cat", but its extension "-a" must still match it.
      matcher.setCandidates(...genIds(["cat", "dog-house"]));
      expect(values(matcher.match("-", fz))).toEqual(["dog-house"]);
      expect(values(matcher.match("-a", fz))).toEqual(["cat"]);
    });

    it("rescans when the algorithm changes between queries", () => {
      // command-t rejects "a-b" for "abc" (literal dash); the fuzzaldrin
      // query "a-bc" extends it and must not inherit the rejection.
      matcher.setCandidates(...genIds(["abc"]));
      expect(matcher.match("a-b", { algorithm: "command-t" })).toEqual([]);
      expect(values(matcher.match("a-bc", fz))).toEqual(["abc"]);
    });

    it("computes match indexes with the fuzzaldrin alignment", () => {
      const opts = { ...fz, recordMatchIndexes: true };

      matcher.setCandidates(...genIds(["abc"]));
      expect(matcher.match("abc", opts)[0].matchIndexes).toEqual([0, 1, 2]);

      // Full-path and basename alignments merge.
      matcher.setCandidates(...genIds(["app/lib/app.js"]));
      expect(matcher.match("app", opts)[0].matchIndexes).toEqual([0, 1, 2, 8, 9, 10]);

      // An unmatched optional character yields no index (6 entries for a
      // 7-character query).
      matcher.setCandidates(...genIds(["foobar"]));
      expect(matcher.match("foo-bar", opts)[0].matchIndexes).toEqual([0, 1, 2, 3, 4, 5]);
    });

    it("maps fuzzaldrin match indexes through diacritic folding", () => {
      const opts = { ...fz, recordMatchIndexes: true };

      let accented = new fuzzyNative.Matcher(...genIds(["café"]), { ignoreDiacritics: true });
      expect(accented.match("cafe", opts)[0].matchIndexes).toEqual([0, 1, 2, 3]);

      // ß expands to "ss": both folded offsets map back to the ß.
      accented = new fuzzyNative.Matcher(...genIds(["Straße"]), { ignoreDiacritics: true });
      expect(accented.match("strasse", opts)[0].matchIndexes).toEqual([0, 1, 2, 3, 4, 4, 5]);
    });

    it("normalizes scores to (0, 1]", () => {
      matcher.setCandidates(...genIds(["abc", "x/abc", "abcdef"]));
      const results = matcher.match("abc", fz);
      // The exact-basename-after-slash bonus scores above the flat self-match
      // ceiling and saturates at 1 — genuine fuzzaldrin-plus ordering.
      expect(values(results)).toEqual(["x/abc", "abc", "abcdef"]);
      expect(results[0].score).toBe(1);
      expect(results[1].score).toBe(1);
      expect(results[2].score).toBeCloseTo(0.8786, 4);
    });
  });
});
