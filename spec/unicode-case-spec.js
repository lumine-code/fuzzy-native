"use strict";

const { Matcher } = require("../lib/main");

function createMatcher(candidates, options) {
  return new Matcher(
    candidates.map((_, id) => id),
    candidates,
    options,
  );
}

function values(results) {
  return results.map((result) => result.value).sort();
}

describe("Unicode case matching", () => {
  for (const algorithm of ["command-t", "fuzzaldrin"]) {
    describe(algorithm, () => {
      const options = { algorithm, recordMatchIndexes: true };

      for (const [label, upper, lower] of [
        ["Polish letters", "ŻÓŁĆ", "żółć"],
        ["accented Latin letters", "CAFÉ", "café"],
        ["Greek letters", "ΑΒΓ", "αβγ"],
        ["Cyrillic letters", "БОР", "бор"],
        ["Deseret letters", "\u{10400}\u{10401}", "\u{10428}\u{10429}"],
        ["capital sharp S", "ẞ", "ß"],
      ]) {
        it(`matches either case of ${label} and preserves the original values`, () => {
          const matcher = createMatcher([upper, lower]);
          for (const query of [upper, lower]) {
            const results = matcher.match(query, options);
            expect(values(results)).withContext(query).toEqual([upper, lower].sort());
            for (const result of results) {
              expect(result.matchIndexes)
                .withContext(`${result.value} / ${query}`)
                .toEqual(Array.from({ length: result.value.length }, (_, index) => index));
              expect(result.score).toBeGreaterThan(0);
              expect(result.score).toBeLessThanOrEqual(1);
            }
          }
        });
      }

      it("matches all Greek sigma forms without changing the returned text", () => {
        const candidates = ["Σ", "σ", "ς"];
        const matcher = createMatcher(candidates);
        for (const query of candidates) {
          const results = matcher.match(query, options);
          expect(values(results))
            .withContext(query)
            .toEqual([...candidates].sort());
          for (const result of results) expect(result.matchIndexes).toEqual([0]);
        }
      });

      for (const [value, query, indexes] of [
        ["🙂ŻÓŁĆ.txt", "żółć", [2, 3, 4, 5]],
        ["🙂Kb.txt", "kb", [2, 3]],
        ["🙂kb.txt", "Kb", [2, 3]],
        ["🙂ſb.txt", "sb", [2, 3]],
        ["🙂sb.txt", "ſb", [2, 3]],
        ["🙂\u{10400}b.txt", "\u{10428}b", [2, 3, 4]],
        ["🙂\u{10428}b.txt", "\u{10400}b", [2, 3, 4]],
      ]) {
        it(`maps ${query} back to UTF-16 indexes in ${value}`, () => {
          const matcher = createMatcher([value]);
          const results = matcher.match(query, options);
          expect(results.length).toBe(1);
          if (results.length === 0) return;
          expect(results[0].value).toBe(value);
          expect(results[0].matchIndexes).toEqual(indexes);
          expect(results[0].score).toBe(matcher.match(query, { algorithm })[0].score);
        });
      }

      it("keeps accented and accentless letters distinct by default", () => {
        const matcher = createMatcher(["ŻÓŁĆ", "żółć", "ZOLC", "zolc"]);
        expect(values(matcher.match("żółć", options))).toEqual(["ŻÓŁĆ", "żółć"].sort());
        expect(values(matcher.match("zolc", options))).toEqual(["ZOLC", "zolc"].sort());
      });

      it("does not apply full or Turkic case folding by default", () => {
        for (const [value, query] of [
          ["ß", "ss"],
          ["ss", "ß"],
          ["İ", "i"],
          ["i", "İ"],
          ["İ", "i\u0307"],
          ["i\u0307", "İ"],
        ]) {
          expect(createMatcher([value]).match(query, options))
            .withContext(`${value} / ${query}`)
            .toEqual([]);
        }
      });

      it("requires separate Unicode characters for repeated query letters", () => {
        for (const [candidates, query, expected] of [
          [["éabc", "Éabc", "ééabc"], "éé", "ééabc"],
          [["σabc", "Σabc", "σσabc"], "σσ", "σσabc"],
        ]) {
          const results = createMatcher(candidates).match(query, options);
          expect(values(results)).withContext(query).toEqual([expected]);
          if (results.length === 1) expect(results[0].matchIndexes).toEqual([0, 1]);
        }
      });

      it("does not combine unrelated UTF-8 bytes into a Unicode character match", () => {
        expect(createMatcher(["Ã©"]).match("é", options)).toEqual([]);
      });

      it("retains explicit diacritic folding and its original highlight mapping", () => {
        const matcher = createMatcher(["ŻÓŁĆ", "Straße"], { ignoreDiacritics: true });
        const polish = matcher.match("zolc", options);
        expect(values(polish)).toEqual(["ŻÓŁĆ"]);
        if (polish.length > 0) expect(polish[0].matchIndexes).toEqual([0, 1, 2, 3]);
        const german = matcher.match("strasse", options);
        expect(values(german)).toEqual(["Straße"]);
        if (german.length > 0) expect(german[0].matchIndexes).toEqual([0, 1, 2, 3, 4, 4, 5]);
      });

      it("maps a Unicode case fold after an expanding diacritic fold to the original value", () => {
        const results = createMatcher(["ßk"], { ignoreDiacritics: true }).match("K", options);
        expect(values(results)).toEqual(["ßk"]);
        if (results.length === 1) expect(results[0].matchIndexes).toEqual([1]);
      });

      it("extends queries and refreshes mixed ASCII and Unicode candidates", () => {
        const matcher = createMatcher(["Żaba", "żak", "żółw", "zebra", "prefix/Żagiel"]);
        expect(values(matcher.match("ż", options))).toEqual(
          ["Żaba", "żak", "żółw", "prefix/Żagiel"].sort(),
        );
        expect(values(matcher.match("ża", options))).toEqual(
          ["Żaba", "żak", "prefix/Żagiel"].sort(),
        );
        matcher.addCandidates([5], ["Żar"]);
        expect(values(matcher.match("żar", options))).toEqual(["Żar"]);
        matcher.removeCandidates([5]);
        expect(matcher.match("żar", options)).toEqual([]);
        matcher.setCandidates([10, 11], ["Γάμμα", "gamma"]);
        expect(values(matcher.match("γ", options))).toEqual(["Γάμμα"]);
        matcher.addCandidates([12], ["ΓΙΑ"]);
        expect(values(matcher.match("γι", options))).toEqual(["ΓΙΑ"]);
      });

      it("keeps Unicode and ASCII case equivalents reachable in a mixed candidate set", () => {
        const candidates = ["ascii", "żaba", "kb", "sb", "Kb", "ſb", "σabc", "ςabc"];
        candidates.push(...Array.from({ length: 100 }, (_, index) => `unrelated-${index}`));
        const matcher = createMatcher(candidates);
        expect(values(matcher.match("ż", options))).toEqual(["żaba"]);
        expect(values(matcher.match("Ż", options))).toEqual(["żaba"]);
        expect(values(matcher.match("Kb", options))).toEqual(["kb", "Kb"].sort());
        expect(values(matcher.match("ſb", options))).toEqual(["sb", "ſb"].sort());
        for (const query of ["σ", "ς"]) {
          expect(values(matcher.match(query, options)))
            .withContext(query)
            .toEqual(["σabc", "ςabc"].sort());
        }
      });

      it("preserves Unicode results across worker threads in a mixed candidate set", () => {
        // At least 10,000 candidates are needed to exercise the native worker path.
        const candidates = Array.from({ length: 10004 }, (_, index) => `unrelated-${index}`);
        const expectedIds = [0, 3000, 7000, 10003];
        for (const [offset, id] of expectedIds.entries()) {
          candidates[id] = ["ŻĄ-one", "żą-two", "Żą-three", "żĄ-four"][offset];
        }
        const matcher = createMatcher(candidates);
        const serial = matcher.match("żą", { ...options, numThreads: 1 });
        const parallel = matcher.match("żą", { ...options, numThreads: 4 });
        const byId = (results) => [...results].sort((left, right) => left.id - right.id);
        expect(serial.map((result) => result.id).sort((left, right) => left - right)).toEqual(
          expectedIds,
        );
        expect(byId(parallel)).toEqual(byId(serial));
        for (const result of parallel) {
          expect(result.value).toBe(candidates[result.id]);
          expect(result.matchIndexes).toEqual([0, 1]);
        }
      });
    });
  }

  describe("command-t case options", () => {
    it("keeps explicit case-sensitive matching sensitive to Unicode case", () => {
      const matcher = createMatcher(["Żaba", "żaba"]);
      const options = { algorithm: "command-t", caseSensitive: true };
      expect(values(matcher.match("Ża", options))).toEqual(["Żaba"]);
      expect(values(matcher.match("ża", options))).toEqual(["żaba"]);
    });

    it("keeps Unicode aliases distinct from ASCII in case-sensitive matching", () => {
      const matcher = createMatcher(["kb", "Kb", "sb", "ſb"]);
      for (const query of ["Kb", "kb", "ſb", "sb"]) {
        expect(values(matcher.match(query, { caseSensitive: true })))
          .withContext(query)
          .toEqual([query]);
      }
    });

    it("rescans rejected candidates when case sensitivity changes on an extended query", () => {
      const matcher = createMatcher(["Żaba", "żaba"]);
      expect(values(matcher.match("ża", { caseSensitive: true }))).toEqual(["żaba"]);
      expect(values(matcher.match("żab", { caseSensitive: false }))).toEqual(
        ["Żaba", "żaba"].sort(),
      );
      expect(values(matcher.match("żaba", { caseSensitive: true }))).toEqual(["żaba"]);
      expect(values(matcher.match("żaba", { caseSensitive: false }))).toEqual(
        ["Żaba", "żaba"].sort(),
      );
    });

    it("uses Unicode uppercase in smartCase while preserving its soft ranking semantics", () => {
      // The only uppercase query letter is non-ASCII, so ASCII-only detection
      // cannot accidentally activate smartCase for this regression.
      const matcher = createMatcher(["żab", "Żab"]);
      const results = matcher.match("Żab", { smartCase: true, recordMatchIndexes: true });
      expect(results.map((result) => result.value)).toEqual(["Żab", "żab"]);
      if (results.length !== 2) return;
      expect(results[0].score).toBeGreaterThan(results[1].score);
      expect(results[1].score).toBeGreaterThan(0);
      expect(results[0].matchIndexes).toEqual([0, 1, 2]);
      expect(results[1].matchIndexes).toEqual([0, 1, 2]);
    });

    it("does not activate smartCase for a lowercase Unicode query", () => {
      const matcher = createMatcher(["żab", "ŻAB"]);
      const results = matcher.match("żab", { smartCase: true });
      expect(values(results)).toEqual(["ŻAB", "żab"].sort());
      if (results.length === 2) expect(results[0].score).toBe(results[1].score);
    });

    it("uses original Cherokee case for smartCase despite its uppercase fold", () => {
      const matcher = createMatcher(["Ꭰx", "ꭰx"]);
      const upper = matcher.match("Ꭰx", { smartCase: true });
      expect(upper.map((result) => result.value)).toEqual(["Ꭰx", "ꭰx"]);
      if (upper.length === 2) expect(upper[0].score).toBeGreaterThan(upper[1].score);
      const lower = matcher.match("ꭰx", { smartCase: true });
      expect(values(lower)).toEqual(["Ꭰx", "ꭰx"].sort());
      if (lower.length === 2) expect(lower[0].score).toBe(lower[1].score);
    });

    it("keeps explicit caseSensitive stronger than smartCase", () => {
      const matcher = createMatcher(["żab", "ŻAB"]);
      expect(values(matcher.match("ŻAB", { smartCase: true, caseSensitive: true }))).toEqual([
        "ŻAB",
      ]);
    });
  });

  it("keeps fuzzaldrin case-insensitive when command-t case options are passed", () => {
    const matcher = createMatcher(["żab", "ŻAB"]);
    const results = matcher.match("żab", {
      algorithm: "fuzzaldrin",
      caseSensitive: true,
      smartCase: true,
    });
    expect(values(results)).toEqual(["ŻAB", "żab"].sort());
  });
});

describe("core subsequence guard", () => {
  for (const algorithm of ["command-t", "fuzzaldrin"]) {
    it(`consumes repeated ASCII query letters once with ${algorithm}`, () => {
      const results = createMatcher(["aab", "aaa", "aaab", "baaa"]).match("aaa", {
        algorithm,
        recordMatchIndexes: true,
      });
      expect(values(results)).toEqual(["aaa", "aaab", "baaa"]);
      for (const result of results) {
        expect(result.matchIndexes).toEqual(result.value === "baaa" ? [1, 2, 3] : [0, 1, 2]);
      }
    });

    it(`preserves query order between repeated letters with ${algorithm}`, () => {
      const results = createMatcher(["aabb", "abab"]).match("abab", {
        algorithm,
        recordMatchIndexes: true,
      });
      expect(values(results)).toEqual(["abab"]);
      if (results.length === 1) expect(results[0].matchIndexes).toEqual([0, 1, 2, 3]);
    });
  }

  it("keeps fuzzaldrin query separators optional without reusing core letters", () => {
    const results = createMatcher(["aab", "aaa", "aaab", "baaa"]).match("a-a-a", {
      algorithm: "fuzzaldrin",
      recordMatchIndexes: true,
    });
    expect(values(results)).toEqual(["aaa", "aaab", "baaa"]);
    for (const result of results) {
      expect(result.matchIndexes).toEqual(result.value === "baaa" ? [1, 2, 3] : [0, 1, 2]);
    }
  });
});
