"use strict";
(() => {
  // src/artifacts.ts
  var manifestCache = /* @__PURE__ */ new Map();
  function manifestKey(kind, n) {
    return `${kind}_n${n}`;
  }
  function joinDataPath(baseUrl, rel) {
    const prefix = baseUrl && !baseUrl.endsWith("/") ? baseUrl + "/" : baseUrl;
    return `${prefix}${rel}`;
  }
  async function fetchManifest(baseUrl, kind, n) {
    const key = manifestKey(kind, n);
    const hit = manifestCache.get(key);
    if (hit) return hit;
    const url = joinDataPath(baseUrl, `data/partition/${key}/manifest.json`);
    const res = await fetch(url);
    if (!res.ok) throw new Error(`manifest ${url}: ${res.status}`);
    const m = await res.json();
    manifestCache.set(key, m);
    return m;
  }
  async function fetchBucket(baseUrl, kind, n, feedbackCode) {
    const key = manifestKey(kind, n);
    const url = joinDataPath(baseUrl, `data/partition/${key}/b/${feedbackCode}.json`);
    const res = await fetch(url);
    if (!res.ok) throw new Error(`bucket ${url}: ${res.status}`);
    return await res.json();
  }
  async function fetchPoolFull(baseUrl, kind, n) {
    const key = manifestKey(kind, n);
    const url = joinDataPath(baseUrl, `data/partition/${key}/pool_full.json`);
    const res = await fetch(url);
    if (!res.ok) throw new Error(`pool_full ${url}: ${res.status}`);
    return await res.json();
  }

  // src/feedback.ts
  var POW3 = [1, 3, 9, 27, 81, 243, 729, 2187, 6561, 19683, 59049];
  function pow3Table(n) {
    return n >= 0 && n <= 10 ? POW3[n] : 0;
  }
  function allGreenPacked(n) {
    let c = 0;
    let m = 1;
    for (let i = 0; i < n; i++) {
      c += 2 * m;
      m *= 3;
    }
    return c;
  }
  function computeFeedbackPacked(guess, solution, n) {
    const remaining = new Array(256).fill(0);
    for (let i = 0; i < n; i++) remaining[solution.charCodeAt(i)]++;
    const trits = new Array(n);
    for (let i = 0; i < n; i++) {
      if (guess.charCodeAt(i) === solution.charCodeAt(i)) {
        trits[i] = 2;
        remaining[guess.charCodeAt(i)]--;
      } else {
        trits[i] = 0;
      }
    }
    for (let i = 0; i < n; i++) {
      if (trits[i] === 2) continue;
      const c = guess.charCodeAt(i);
      if (remaining[c] > 0) {
        trits[i] = 1;
        remaining[c]--;
      }
    }
    let code = 0;
    let mul = 1;
    for (let i = 0; i < n; i++) {
      code += trits[i] * mul;
      mul *= 3;
    }
    return code;
  }
  function feedbackStringToPacked(fb, n) {
    let code = 0;
    let mul = 1;
    for (let i = 0; i < n; i++) {
      const ch = fb[i];
      let t = 0;
      if (ch === "G" || ch === "g") t = 2;
      else if (ch === "P" || ch === "p") t = 1;
      code += t * mul;
      mul *= 3;
    }
    return code;
  }
  var PLACE_SQ = 1;
  var PLACE_CB = 2;
  function normalizeMaxi(s) {
    let out = "";
    for (let i = 0; i < s.length; i++) {
      const c0 = s.charCodeAt(i);
      if (c0 === 194 && i + 1 < s.length) {
        const c1 = s.charCodeAt(i + 1);
        if (c1 === 178) {
          out += String.fromCharCode(PLACE_SQ);
          i++;
          continue;
        }
        if (c1 === 179) {
          out += String.fromCharCode(PLACE_CB);
          i++;
          continue;
        }
      }
      out += s[i];
    }
    return out;
  }
  function maxiToDisplay(s) {
    let out = "";
    for (let i = 0; i < s.length; i++) {
      const c = s.charCodeAt(i);
      if (c === PLACE_SQ) out += "\xB2";
      else if (c === PLACE_CB) out += "\xB3";
      else out += s[i];
    }
    return out;
  }
  function normalizeGuessInput(s, isMaxi) {
    return isMaxi ? normalizeMaxi(s) : s;
  }
  function isConsistentFeedback(candidateEq, guess, feedbackGpb, n) {
    return computeFeedbackPacked(guess, candidateEq, n) === feedbackStringToPacked(feedbackGpb, n);
  }

  // src/partition_policy.ts
  function partitionSolveDistLess(a, b, horizon) {
    const eps = 1e-12;
    for (let t = 1; t <= horizon && t < a.solve_at.length; t++) {
      const da = a.solve_at[t] ?? 0;
      const db = b.solve_at[t] ?? 0;
      if (Math.abs(da - db) > eps) return da < db;
    }
    return false;
  }
  var PartitionGreedyEvaluator = class {
    constructor(getEq, N, maxTries, tieDepth) {
      this.getEq = getEq;
      this.N = N;
      this.maxTries = maxTries;
      this.tieDepth = tieDepth;
      this.stampId = 0;
      this.memo = /* @__PURE__ */ new Map();
      this.P = pow3Table(N);
      this.green = allGreenPacked(N);
      this.stamp = new Int32Array(this.P);
      this.stamp.fill(-1);
    }
    feedbackCode(g, s) {
      return computeFeedbackPacked(this.getEq(g), this.getEq(s), this.N);
    }
    /** Match C++ `make_key`: order-sensitive (not sorted). */
    makeKey(state, k) {
      return `${k}:${state.join(",")}`;
    }
    partitionCount(state, g) {
      this.stampId++;
      let count = 0;
      const sid = this.stampId;
      for (const s of state) {
        const code = this.feedbackCode(g, s);
        if (this.stamp[code] !== sid) {
          this.stamp[code] = sid;
          count++;
        }
      }
      return count;
    }
    bestPartitionGuesses(state, k) {
      if (k < 2) return [...state];
      let best = -1;
      const out = [];
      for (const g of state) {
        const pc = this.partitionCount(state, g);
        if (pc > best) {
          best = pc;
          out.length = 0;
          out.push(g);
        } else if (pc === best) {
          out.push(g);
        }
      }
      return out;
    }
    afterGuess(state, g, k) {
      const out = { solve_at: new Array(7).fill(0) };
      const inv = 1 / state.length;
      const buckets = Array.from({ length: this.P }, () => []);
      const usedCodes = [];
      for (const s of state) {
        const code = this.feedbackCode(g, s);
        if (code === this.green) {
          out.solve_at[1] += inv;
        } else {
          if (buckets[code].length === 0) usedCodes.push(code);
          buckets[code].push(s);
        }
      }
      for (const code of usedCodes) {
        const child = buckets[code];
        const weight = child.length * inv;
        const v = this.solve(child, k - 1);
        for (let t = 2; t <= k && t <= this.maxTries; t++) {
          out.solve_at[t] += weight * (v.solve_at[t - 1] ?? 0);
        }
      }
      return out;
    }
    solve(state, k) {
      if (state.length === 0 || k < 1) return { solve_at: [] };
      if (state.length === 1) {
        const o = { solve_at: new Array(7).fill(0) };
        o.solve_at[1] = 1;
        return o;
      }
      const key = this.makeKey(state, k);
      const hit = this.memo.get(key);
      if (hit) return hit;
      const bestG = this.chooseGuess(state, k);
      const best = this.afterGuess(state, bestG, k);
      this.memo.set(key, best);
      return best;
    }
    chooseGuess(state, k) {
      const guesses = this.bestPartitionGuesses(state, k);
      if (guesses.length === 0) return -1;
      if (guesses.length === 1 || this.tieDepth <= 0 || k < 3) return guesses[0];
      const horizon = Math.min(k, this.tieDepth + 2);
      let haveBest = false;
      let best = { solve_at: [] };
      let bestG = guesses[0];
      for (const g of guesses) {
        const val = this.afterGuess(state, g, k);
        if (!haveBest || partitionSolveDistLess(best, val, horizon)) {
          haveBest = true;
          best = val;
          bestG = g;
        }
      }
      return bestG;
    }
    bestGuessIndex(candidateIndices, k) {
      const st = [...candidateIndices];
      return this.chooseGuess(st, k);
    }
    bestGuessString(candidateIndices, k) {
      const g = this.bestGuessIndex(candidateIndices, k);
      return this.getEq(g);
    }
  };
  var PARTITION_INTERACTIVE_TIE_DEPTH = 6;
  function bestGuessPartitionPolicy(getEq, candidateIndices, N, triesRemaining, tieDepth = PARTITION_INTERACTIVE_TIE_DEPTH) {
    if (candidateIndices.length === 0) return "";
    if (candidateIndices.length === 1) return getEq(candidateIndices[0]);
    if (triesRemaining < 1) return getEq(candidateIndices[0]);
    const ev = new PartitionGreedyEvaluator(getEq, N, triesRemaining, tieDepth);
    return ev.bestGuessString(candidateIndices, triesRemaining);
  }

  // src/partition_multi.ts
  function sameState(a, b) {
    if (a.length !== b.length) return false;
    const set = new Set(a);
    for (const x of b) if (!set.has(x)) return false;
    return true;
  }
  function binerdlePartitionClassesExcluding(getEq, state, guessIdx, n, stamp, stampIdRef) {
    const P = pow3Table(n);
    if (P <= 0 || state.length === 0) return 0;
    stampIdRef.id++;
    const sid = stampIdRef.id;
    let count = 0;
    const guess = getEq(guessIdx);
    for (const s of state) {
      if (s === guessIdx) continue;
      const code = computeFeedbackPacked(guess, getEq(s), n);
      if (stamp[code] !== sid) {
        stamp[code] = sid;
        count++;
      }
    }
    return count;
  }
  function canonicalLess(a, b, rank) {
    return rank(a) < rank(b);
  }
  function bestGuessBinerdlePartition(getEq, rank, c1, c2, n, triesRemaining, solved1, solved2, partitionTieDepth = PARTITION_INTERACTIVE_TIE_DEPTH) {
    if (c1.length === 0 || c2.length === 0) return "";
    const canonicalBestSingleton = () => {
      if (c1.length === 0) return c2[0];
      if (c2.length === 0) return c1[0];
      if (solved1 && !solved2) return c2[0];
      if (solved2 && !solved1) return c1[0];
      return canonicalLess(c1[0], c2[0], rank) ? c1[0] : c2[0];
    };
    if (c1.length === 1 && c2.length === 1) return getEq(canonicalBestSingleton());
    if (!solved1 && c1.length === 1) return getEq(c1[0]);
    if (!solved2 && c2.length === 1) return getEq(c2[0]);
    if (solved1 && !solved2) {
      return bestGuessPartitionPolicy(getEq, c2, n, triesRemaining, partitionTieDepth);
    }
    if (solved2 && !solved1) {
      return bestGuessPartitionPolicy(getEq, c1, n, triesRemaining, partitionTieDepth);
    }
    if (sameState(c1, c2)) {
      return bestGuessPartitionPolicy(getEq, c1, n, triesRemaining, partitionTieDepth);
    }
    const in1 = new Set(c1);
    const in2 = new Set(c2);
    const pool = [...c1];
    for (const idx of c2) if (!in1.has(idx)) pool.push(idx);
    const P = pow3Table(n);
    const stamp = new Int32Array(P);
    const stampIdRef = { id: 0 };
    let haveBest = false;
    let bestIdx = 0;
    let bestClasses = -1;
    for (const idx of pool) {
      let classes = 0;
      if (in1.has(idx)) classes += binerdlePartitionClassesExcluding(getEq, c2, idx, n, stamp, stampIdRef);
      if (in2.has(idx)) classes += binerdlePartitionClassesExcluding(getEq, c1, idx, n, stamp, stampIdRef);
      if (!haveBest || classes > bestClasses || classes === bestClasses && canonicalLess(idx, bestIdx, rank)) {
        haveBest = true;
        bestClasses = classes;
        bestIdx = idx;
      }
    }
    return haveBest ? getEq(bestIdx) : "";
  }
  function quadActiveIndices(solved, boards) {
    const act = [];
    for (let b = 0; b < 4; b++) {
      if (solved[b] || boards[b].length === 0) continue;
      act.push(b);
    }
    return act;
  }
  function bestGuessQuadPartition(getEq, rank, boards, n, triesRemaining, solved, partitionTieDepth = PARTITION_INTERACTIVE_TIE_DEPTH) {
    const B = boards;
    for (let b = 0; b < 4; b++) {
      if (solved[b]) continue;
      if (B[b].length === 0) return "";
    }
    for (let b = 0; b < 4; b++) {
      if (solved[b]) continue;
      if (B[b].length === 1) return getEq(B[b][0]);
    }
    const act = quadActiveIndices(solved, B);
    if (act.length === 0) return "";
    if (act.length === 1) {
      const bi = act[0];
      return bestGuessPartitionPolicy(getEq, B[bi], n, Math.max(1, triesRemaining), partitionTieDepth);
    }
    if (act.length === 2) {
      const i0 = act[0];
      const i1 = act[1];
      return bestGuessBinerdlePartition(
        getEq,
        rank,
        B[i0],
        B[i1],
        n,
        Math.max(1, triesRemaining),
        solved[i0],
        solved[i1],
        partitionTieDepth
      );
    }
    const ca = B[act[0]];
    let allSame = true;
    for (let a = 1; a < act.length; a++) {
      if (!sameState(ca, B[act[a]])) {
        allSame = false;
        break;
      }
    }
    if (allSame) {
      return bestGuessPartitionPolicy(getEq, ca, n, Math.max(1, triesRemaining), partitionTieDepth);
    }
    const u = /* @__PURE__ */ new Set();
    for (const bi of act) for (const idx of B[bi]) u.add(idx);
    const pool = [...u].sort((a, b) => a - b);
    const P = pow3Table(n);
    const stamp = new Int32Array(P);
    const stampIdRef = { id: 0 };
    let bestS = -1;
    let bestIdx = pool[0];
    for (const g of pool) {
      let s = 0;
      for (const bi of act) {
        s += binerdlePartitionClassesExcluding(getEq, B[bi], g, n, stamp, stampIdRef);
      }
      if (s > bestS || s === bestS && canonicalLess(g, bestIdx, rank)) {
        bestS = s;
        bestIdx = g;
      }
    }
    return getEq(bestIdx);
  }

  // src/browser_engine.ts
  var MAX_TRIES_CLASSIC = 6;
  var MAX_TRIES_BINERDLE = 7;
  var MAX_TRIES_QUAD = 10;
  var PoolStore = class {
    constructor() {
      this.idToInternal = /* @__PURE__ */ new Map();
      this.idToRank = /* @__PURE__ */ new Map();
      this.idToDisplay = /* @__PURE__ */ new Map();
    }
    clear() {
      this.idToInternal.clear();
      this.idToRank.clear();
      this.idToDisplay.clear();
    }
    ingest(rows, n) {
      const isMaxi = n === 10;
      for (const [id, rank, disp] of rows) {
        this.idToInternal.set(id, normalizeGuessInput(disp, isMaxi));
        this.idToRank.set(id, rank);
        this.idToDisplay.set(id, disp);
      }
    }
    getEq(id) {
      const s = this.idToInternal.get(id);
      if (s === void 0) throw new Error(`missing equation id ${id} in pool store`);
      return s;
    }
    rank(id) {
      return this.idToRank.get(id) ?? 1e9;
    }
    toDisplay(internal, n) {
      return n === 10 ? maxiToDisplay(internal) : internal;
    }
    allIdsSorted() {
      return [...this.idToInternal.keys()].sort((a, b) => a - b);
    }
  };
  function openingInternal(manifestOpening, n) {
    return normalizeGuessInput(manifestOpening, n === 10);
  }
  function isAllGreenFeedback(fb, n) {
    if (fb.length !== n) return false;
    for (let i = 0; i < n; i++) if (fb[i] !== "G" && fb[i] !== "g") return false;
    return true;
  }
  function filterIndices(cands, guess, fb, n, store) {
    const g = normalizeGuessInput(guess, n === 10);
    return cands.filter((i) => isConsistentFeedback(store.getEq(i), g, fb, n));
  }
  function mergeRowsUnique(rowsList) {
    const byId = /* @__PURE__ */ new Map();
    for (const rows of rowsList) {
      for (const row of rows) {
        if (!byId.has(row[0])) byId.set(row[0], row);
      }
    }
    return [...byId.values()];
  }
  async function browserPartitionStep(baseUrl, kind, n, history) {
    try {
      const manifest = await fetchManifest(baseUrl, kind, n);
      if (manifest.n !== n || manifest.kind !== kind) {
        return { ok: false, error: "manifest mismatch" };
      }
      if (history.length === 0) {
        const ps = manifest.poolSize;
        if (kind === "classic") {
          return { ok: true, suggestion: manifest.opening, remaining: ps, engine: "browser_partition" };
        }
        if (kind === "binerdle") {
          return {
            ok: true,
            suggestion: manifest.opening,
            remaining: { boards: [ps, ps], product: ps * ps },
            engine: "browser_partition"
          };
        }
        return {
          ok: true,
          suggestion: manifest.opening,
          remaining: { boards: [ps, ps, ps, ps], product: ps ** 4 },
          engine: "browser_partition"
        };
      }
      const store = new PoolStore();
      const isMaxi = n === 10;
      const h0 = history[0];
      const g0 = normalizeGuessInput(h0.guess, isMaxi);
      const openInt = openingInternal(manifest.opening, n);
      const usedOpening = g0 === openInt;
      let cClassic = null;
      let cB1 = null;
      let cB2 = null;
      let cQuad = null;
      try {
        if (usedOpening) {
          if (manifest.hasOpeningBuckets === false) {
            return {
              ok: false,
              error: "Static deploy includes only the Maxi recommended first guess (" + manifest.opening + "). Post-opening Maxi buckets are omitted to keep deploy prep fast."
            };
          }
          if (kind === "classic") {
            const fb = h0.feedback;
            const code = feedbackStringToPacked(fb, n);
            const rows = await fetchBucket(baseUrl, kind, n, code);
            store.ingest(rows, n);
            cClassic = rows.map((r) => r[0]).sort((a, b) => a - b);
          } else if (kind === "binerdle") {
            const fbs = h0.feedback;
            const rows1 = await fetchBucket(baseUrl, kind, n, feedbackStringToPacked(fbs[0], n));
            const rows2 = await fetchBucket(baseUrl, kind, n, feedbackStringToPacked(fbs[1], n));
            store.ingest(mergeRowsUnique([rows1, rows2]), n);
            cB1 = rows1.map((r) => r[0]).sort((a, b) => a - b);
            cB2 = rows2.map((r) => r[0]).sort((a, b) => a - b);
          } else {
            const fbs = h0.feedback;
            const rowLists = [];
            for (let b = 0; b < 4; b++) {
              rowLists.push(await fetchBucket(baseUrl, kind, n, feedbackStringToPacked(fbs[b], n)));
            }
            store.ingest(mergeRowsUnique(rowLists), n);
            cQuad = rowLists.map((rows) => rows.map((r) => r[0]).sort((a, b) => a - b));
          }
        } else {
          if (!manifest.hasPoolFull && isMaxi) {
            return {
              ok: false,
              error: "Standalone partition for Maxi requires the recommended first guess (" + manifest.opening + "). Use that opening or run the local server with full data."
            };
          }
          const full = await fetchPoolFull(baseUrl, kind, n);
          store.ingest(full, n);
          const all = store.allIdsSorted();
          if (kind === "classic") cClassic = [...all];
          else if (kind === "binerdle") {
            cB1 = [...all];
            cB2 = [...all];
          } else {
            cQuad = [0, 1, 2, 3].map(() => [...all]);
          }
        }
      } catch (e) {
        return { ok: false, error: String(e) };
      }
      if (kind === "classic") {
        let cands = cClassic;
        for (let t = 0; t < history.length; t++) {
          const h = history[t];
          const fb = h.feedback;
          if (isAllGreenFeedback(fb, n)) {
            return {
              ok: true,
              solved: true,
              suggestion: openingInternal(manifest.opening, n) === normalizeGuessInput(h.guess, isMaxi) ? manifest.opening : store.toDisplay(normalizeGuessInput(h.guess, isMaxi), n),
              remaining: 1,
              engine: "browser_partition"
            };
          }
          cands = filterIndices(cands, h.guess, fb, n, store);
          if (cands.length === 0) return { ok: false, error: "no candidates remain \u2014 check guess and feedback" };
        }
        const turn2 = history.length;
        const triesLeft2 = Math.max(1, MAX_TRIES_CLASSIC - turn2);
        const guess2 = bestGuessPartitionPolicy((i) => store.getEq(i), cands, n, triesLeft2);
        const sugg2 = store.toDisplay(normalizeGuessInput(guess2, isMaxi), n);
        return { ok: true, suggestion: sugg2, remaining: cands.length, engine: "browser_partition" };
      }
      if (kind === "binerdle") {
        let c1 = cB1;
        let c2 = cB2;
        let solved1 = false;
        let solved2 = false;
        for (let t = 0; t < history.length; t++) {
          const h = history[t];
          const f1 = h.feedback[0];
          const f2 = h.feedback[1];
          const gNorm = normalizeGuessInput(h.guess, isMaxi);
          if (isAllGreenFeedback(f1, n)) {
            solved1 = true;
            const m = c1.filter((i) => store.getEq(i) === gNorm);
            if (m.length) c1 = [m[0]];
          } else c1 = filterIndices(c1, h.guess, f1, n, store);
          if (isAllGreenFeedback(f2, n)) {
            solved2 = true;
            const m = c2.filter((i) => store.getEq(i) === gNorm);
            if (m.length) c2 = [m[0]];
          } else c2 = filterIndices(c2, h.guess, f2, n, store);
          if (solved1 && solved2) {
            return {
              ok: true,
              solved: true,
              suggestion: store.toDisplay(gNorm, n),
              remaining: { boards: [1, 1], product: 1 },
              engine: "browser_partition"
            };
          }
          if (c1.length === 0 || c2.length === 0)
            return { ok: false, error: "no candidates remain on one board \u2014 check feedback" };
        }
        const turn2 = history.length;
        const triesLeft2 = Math.max(1, MAX_TRIES_BINERDLE - turn2);
        const guess2 = bestGuessBinerdlePartition(
          (i) => store.getEq(i),
          (i) => store.rank(i),
          c1,
          c2,
          n,
          triesLeft2,
          solved1,
          solved2
        );
        const sugg2 = store.toDisplay(normalizeGuessInput(guess2, isMaxi), n);
        return {
          ok: true,
          suggestion: sugg2,
          remaining: { boards: [c1.length, c2.length], product: c1.length * c2.length },
          engine: "browser_partition"
        };
      }
      let c = cQuad.map((x) => [...x]);
      const solved = [false, false, false, false];
      for (let t = 0; t < history.length; t++) {
        const h = history[t];
        const gNorm = normalizeGuessInput(h.guess, isMaxi);
        for (let b = 0; b < 4; b++) {
          const fb = h.feedback[b];
          if (isAllGreenFeedback(fb, n)) {
            solved[b] = true;
            const m = c[b].filter((i) => store.getEq(i) === gNorm);
            if (m.length) c[b] = [m[0]];
            continue;
          }
          c[b] = filterIndices(c[b], h.guess, fb, n, store);
          if (c[b].length === 0) return { ok: false, error: "no candidates remain on one board \u2014 check feedback" };
        }
        if (solved.every(Boolean)) {
          return {
            ok: true,
            solved: true,
            suggestion: store.toDisplay(normalizeGuessInput(h.guess, isMaxi), n),
            remaining: { boards: [1, 1, 1, 1], product: 1 },
            engine: "browser_partition"
          };
        }
      }
      const turn = history.length;
      const triesLeft = Math.max(1, MAX_TRIES_QUAD - turn);
      const guess = bestGuessQuadPartition(
        (i) => store.getEq(i),
        (i) => store.rank(i),
        c,
        n,
        triesLeft,
        solved
      );
      const sugg = store.toDisplay(normalizeGuessInput(guess, isMaxi), n);
      const prod = c[0].length * c[1].length * c[2].length * c[3].length;
      return {
        ok: true,
        suggestion: sugg,
        remaining: { boards: [c[0].length, c[1].length, c[2].length, c[3].length], product: prod },
        engine: "browser_partition"
      };
    } catch (e) {
      return { ok: false, error: String(e) };
    }
  }

  // src/micro_bellman.ts
  var POLICY_MAGIC = 1312116802;
  var POLICY_VER1 = 1;
  var POLICY_VER2 = 2;
  function maskFromCandidates(cands) {
    const w = [0n, 0n, 0n, 0n];
    for (const i of cands) {
      if (i >= 0 && i < 256) {
        const limb = i >> 6;
        w[limb] |= 1n << BigInt(i & 63);
      }
    }
    return w;
  }
  function maskKey(w) {
    const u8 = new Uint8Array(32);
    const dv = new DataView(u8.buffer);
    for (let i = 0; i < 4; i++) dv.setBigUint64(i * 8, w[i], true);
    return new TextDecoder("latin1").decode(u8);
  }
  var policyCache = null;
  var policyLoadError = null;
  async function loadMicroPolicyMap(baseUrl, expectedPoolSize) {
    if (policyCache) return policyCache;
    if (policyLoadError) throw new Error(policyLoadError);
    const prefix = baseUrl && !baseUrl.endsWith("/") ? baseUrl + "/" : baseUrl;
    const url = `${prefix}data/optimal_policy_5.bin`;
    let buf;
    try {
      const res = await fetch(url);
      if (!res.ok) throw new Error(`${url}: ${res.status}`);
      buf = await res.arrayBuffer();
    } catch (e) {
      policyLoadError = String(e);
      throw e;
    }
    const v = new DataView(buf);
    let o = 0;
    const magic = v.getUint32(o, true);
    o += 4;
    const ver = v.getUint32(o, true);
    o += 4;
    if (magic !== POLICY_MAGIC || ver !== POLICY_VER1 && ver !== POLICY_VER2) {
      throw new Error("optimal_policy_5.bin: bad header");
    }
    const neq = v.getUint8(o);
    o += 1;
    o += 3;
    const nent = v.getUint32(o, true);
    o += 4;
    if (expectedPoolSize !== void 0 && neq !== expectedPoolSize) {
      throw new Error(`optimal_policy_5.bin: neq ${neq} != pool ${expectedPoolSize}`);
    }
    const map = /* @__PURE__ */ new Map();
    for (let i = 0; i < nent; i++) {
      let w;
      if (ver === POLICY_VER1) {
        const lo = v.getBigUint64(o, true);
        o += 8;
        const hi = v.getBigUint64(o, true);
        o += 8;
        w = [lo, hi, 0n, 0n];
      } else {
        w = [];
        for (let j = 0; j < 4; j++) {
          w.push(v.getBigUint64(o, true));
          o += 8;
        }
      }
      const g = v.getUint8(o);
      o += 1;
      map.set(maskKey(w), g);
    }
    if (o !== buf.byteLength) {
      throw new Error("optimal_policy_5.bin: trailing bytes");
    }
    policyCache = map;
    return map;
  }
  function filterClassic(cands, guess, fb, n, getEq) {
    const g = normalizeGuessInput(guess, false);
    return cands.filter((i) => isConsistentFeedback(getEq(i), g, fb, n));
  }
  function isAllGreen(fb, n) {
    if (fb.length !== n) return false;
    for (let i = 0; i < n; i++) if (fb[i] !== "G" && fb[i] !== "g") return false;
    return true;
  }
  async function microBellmanClassicStep(baseUrl, history) {
    const n = 5;
    try {
      const full = await fetchPoolFull(baseUrl, "classic", n);
      const idToEq = /* @__PURE__ */ new Map();
      for (const [id, , disp] of full) {
        idToEq.set(id, disp);
      }
      const getEq = (i) => {
        const s = idToEq.get(i);
        if (s === void 0) throw new Error(`missing id ${i}`);
        return s;
      };
      let cands = full.map((r) => r[0]).sort((a, b) => a - b);
      const poolSize = cands.length;
      for (let t = 0; t < history.length; t++) {
        const h = history[t];
        if (isAllGreen(h.feedback, n)) {
          return {
            ok: true,
            solved: true,
            suggestion: h.guess,
            remaining: 1,
            engine: "browser_bellman"
          };
        }
        cands = filterClassic(cands, h.guess, h.feedback, n, getEq);
        if (cands.length === 0) return { ok: false, error: "no candidates remain \u2014 check guess and feedback" };
      }
      if (cands.length === 1) {
        return {
          ok: true,
          suggestion: getEq(cands[0]),
          remaining: 1,
          engine: "browser_bellman"
        };
      }
      let policy;
      try {
        policy = await loadMicroPolicyMap(baseUrl, poolSize);
      } catch {
        const triesLeft = Math.max(1, 6 - history.length);
        const guess = bestGuessPartitionPolicy((i) => getEq(i), cands, n, triesLeft);
        return {
          ok: true,
          suggestion: guess,
          remaining: cands.length,
          engine: "browser_bellman_fallback_partition"
        };
      }
      const m = maskFromCandidates(cands);
      const gi = policy.get(maskKey(m));
      if (gi === void 0 || gi >= poolSize) {
        const triesLeft = Math.max(1, 6 - history.length);
        const guess = bestGuessPartitionPolicy((i) => getEq(i), cands, n, triesLeft);
        return {
          ok: true,
          suggestion: guess,
          remaining: cands.length,
          engine: "browser_bellman_fallback_partition"
        };
      }
      return {
        ok: true,
        suggestion: getEq(gi),
        remaining: cands.length,
        engine: "browser_bellman"
      };
    } catch (e) {
      return { ok: false, error: String(e) };
    }
  }

  // src/entry.ts
  window.nerdleBrowserPartition = browserPartitionStep;
  window.nerdleMicroBellmanClassic = microBellmanClassicStep;
})();
