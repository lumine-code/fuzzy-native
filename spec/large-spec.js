"use strict";

var fuzzyNative = require("../lib/main");

// Stress test on a large set of strings.
var N = 100000;
var STRING_LEN = 10;

function randomString() {
  var str = "";
  for (var i = 0; i < STRING_LEN; i++) {
    str += String.fromCharCode(Math.floor(Math.random() * 26) + 97);
  }
  return str;
}

describe("fuzzy-native", function () {
  it("works on large inputs", () => {
    var ids = [];
    var candidates = [];
    var usedCandidates = new Set();
    for (var i = 0; i < N; i++) {
      ids[i] = i;
      while (true) {
        var candidate = randomString();
        if (!usedCandidates.has(candidate)) {
          usedCandidates.add(candidate);
          candidates.push(candidate);
          break;
        }
      }
    }
    var matcher = new fuzzyNative.Matcher(ids, candidates);

    // Match indexes should be 0..STRING_LEN - 1.
    var indexes = [];
    for (var k = 0; k < STRING_LEN; k++) {
      indexes.push(k);
    }

    var chunks = 4;
    var chunkSize = Math.floor(N / chunks);
    for (let i = 0; i < chunks; i++) {
      var start = i * chunkSize;
      // 1. Check that candidate strings are (still) matchable
      for (let j = 0; j < 10; j++) {
        const idx = Math.floor(Math.random() * (N - start));
        const results = matcher.match(candidates[idx + start], {
          maxResults: 10,
          numThreads: 4,
          recordMatchIndexes: true,
        });
        expect(results).toEqual([
          {
            score: 1,
            id: idx + start,
            value: candidates[idx + start],
            matchIndexes: indexes,
          },
        ]);
      }

      // 2. Delete a large chunk of strings
      var deletedIds = ids.slice(start, start + chunkSize);
      var deletedValues = candidates.slice(start, start + chunkSize);
      matcher.removeCandidates(deletedIds);

      // 3. Make sure deleted strings no longer match.
      for (let j = 0; j < 10; j++) {
        const idx = Math.floor(Math.random() * deletedIds.length);
        const results = matcher.match(deletedValues[idx], {
          maxResults: 10,
          numThreads: 4,
        });
        expect(results.length).toBe(0);
      }
    }
  });
});
