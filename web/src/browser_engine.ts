import { fetchBucket, fetchManifest, fetchPoolFull, type BucketRow } from "./artifacts";
import {
  feedbackStringToPacked,
  isConsistentFeedback,
  maxiToDisplay,
  normalizeGuessInput,
} from "./feedback";
import { bestGuessBinerdlePartition, bestGuessQuadPartition } from "./partition_multi";
import { bestGuessPartitionPolicy } from "./partition_policy";
import type { ClassicHist, Hist, MultiHist, StepResult } from "./step_types";

export type { ClassicHist, Hist, MultiHist, RemainingClassic, RemainingMulti, StepErr, StepOk, StepResult } from "./step_types";

const MAX_TRIES_CLASSIC = 6;
const MAX_TRIES_BINERDLE = 7;
const MAX_TRIES_QUAD = 10;

class PoolStore {
  private readonly idToInternal = new Map<number, string>();
  private readonly idToRank = new Map<number, number>();
  private readonly idToDisplay = new Map<number, string>();

  clear(): void {
    this.idToInternal.clear();
    this.idToRank.clear();
    this.idToDisplay.clear();
  }

  ingest(rows: BucketRow[], n: number): void {
    const isMaxi = n === 10;
    for (const [id, rank, disp] of rows) {
      this.idToInternal.set(id, normalizeGuessInput(disp, isMaxi));
      this.idToRank.set(id, rank);
      this.idToDisplay.set(id, disp);
    }
  }

  getEq(id: number): string {
    const s = this.idToInternal.get(id);
    if (s === undefined) throw new Error(`missing equation id ${id} in pool store`);
    return s;
  }

  rank(id: number): number {
    return this.idToRank.get(id) ?? 1e9;
  }

  toDisplay(internal: string, n: number): string {
    return n === 10 ? maxiToDisplay(internal) : internal;
  }

  allIdsSorted(): number[] {
    return [...this.idToInternal.keys()].sort((a, b) => a - b);
  }
}

function openingInternal(manifestOpening: string, n: number): string {
  return normalizeGuessInput(manifestOpening, n === 10);
}

function isAllGreenFeedback(fb: string, n: number): boolean {
  if (fb.length !== n) return false;
  for (let i = 0; i < n; i++) if (fb[i] !== "G" && fb[i] !== "g") return false;
  return true;
}

function filterIndices(
  cands: number[],
  guess: string,
  fb: string,
  n: number,
  store: PoolStore,
): number[] {
  const g = normalizeGuessInput(guess, n === 10);
  return cands.filter((i) => isConsistentFeedback(store.getEq(i), g, fb, n));
}

function mergeRowsUnique(rowsList: BucketRow[][]): BucketRow[] {
  const byId = new Map<number, BucketRow>();
  for (const rows of rowsList) {
    for (const row of rows) {
      if (!byId.has(row[0])) byId.set(row[0], row);
    }
  }
  return [...byId.values()];
}

export async function browserPartitionStep(
  baseUrl: string,
  kind: "classic" | "binerdle" | "quad",
  n: number,
  history: Hist[],
): Promise<StepResult> {
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
          engine: "browser_partition",
        };
      }
      return {
        ok: true,
        suggestion: manifest.opening,
        remaining: { boards: [ps, ps, ps, ps], product: ps ** 4 },
        engine: "browser_partition",
      };
    }

    const store = new PoolStore();
    const isMaxi = n === 10;
    const h0 = history[0]!;
    const g0 = normalizeGuessInput(h0.guess, isMaxi);
    const openInt = openingInternal(manifest.opening, n);
    const usedOpening = g0 === openInt;

    let cClassic: number[] | null = null;
    let cB1: number[] | null = null;
    let cB2: number[] | null = null;
    let cQuad: number[][] | null = null;

    try {
      if (usedOpening) {
        if (manifest.hasOpeningBuckets === false) {
          return {
            ok: false,
            error:
              "Static deploy includes only the Maxi recommended first guess (" +
              manifest.opening +
              "). Post-opening Maxi buckets are omitted to keep deploy prep fast.",
          };
        }
        if (kind === "classic") {
          const fb = (h0 as ClassicHist).feedback;
          const code = feedbackStringToPacked(fb, n);
          const rows = await fetchBucket(baseUrl, kind, n, code);
          store.ingest(rows, n);
          cClassic = rows.map((r) => r[0]).sort((a, b) => a - b);
        } else if (kind === "binerdle") {
          const fbs = (h0 as MultiHist).feedback;
          const rows1 = await fetchBucket(baseUrl, kind, n, feedbackStringToPacked(fbs[0]!, n));
          const rows2 = await fetchBucket(baseUrl, kind, n, feedbackStringToPacked(fbs[1]!, n));
          store.ingest(mergeRowsUnique([rows1, rows2]), n);
          cB1 = rows1.map((r) => r[0]).sort((a, b) => a - b);
          cB2 = rows2.map((r) => r[0]).sort((a, b) => a - b);
        } else {
          const fbs = (h0 as MultiHist).feedback;
          const rowLists: BucketRow[][] = [];
          for (let b = 0; b < 4; b++) {
            rowLists.push(await fetchBucket(baseUrl, kind, n, feedbackStringToPacked(fbs[b]!, n)));
          }
          store.ingest(mergeRowsUnique(rowLists), n);
          cQuad = rowLists.map((rows) => rows.map((r) => r[0]).sort((a, b) => a - b));
        }
      } else {
        if (!manifest.hasPoolFull && isMaxi) {
          return {
            ok: false,
            error:
              "Standalone partition for Maxi requires the recommended first guess (" +
              manifest.opening +
              "). Use that opening or run the local server with full data.",
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
      let cands = cClassic!;
      for (let t = 0; t < history.length; t++) {
        const h = history[t]! as ClassicHist;
        const fb = h.feedback;
        if (isAllGreenFeedback(fb, n)) {
          return {
            ok: true,
            solved: true,
            suggestion: openingInternal(manifest.opening, n) === normalizeGuessInput(h.guess, isMaxi)
              ? manifest.opening
              : store.toDisplay(normalizeGuessInput(h.guess, isMaxi), n),
            remaining: 1,
            engine: "browser_partition",
          };
        }
        cands = filterIndices(cands, h.guess, fb, n, store);
        if (cands.length === 0) return { ok: false, error: "no candidates remain — check guess and feedback" };
      }

      const turn = history.length;
      const triesLeft = Math.max(1, MAX_TRIES_CLASSIC - turn);
      const guess = bestGuessPartitionPolicy((i) => store.getEq(i), cands, n, triesLeft);
      const sugg = store.toDisplay(normalizeGuessInput(guess, isMaxi), n);
      return { ok: true, suggestion: sugg, remaining: cands.length, engine: "browser_partition" };
    }

    if (kind === "binerdle") {
      let c1 = cB1!;
      let c2 = cB2!;
      let solved1 = false;
      let solved2 = false;

      for (let t = 0; t < history.length; t++) {
        const h = history[t]! as MultiHist;
        const f1 = h.feedback[0]!;
        const f2 = h.feedback[1]!;
        const gNorm = normalizeGuessInput(h.guess, isMaxi);
        if (isAllGreenFeedback(f1, n)) {
          solved1 = true;
          const m = c1.filter((i) => store.getEq(i) === gNorm);
          if (m.length) c1 = [m[0]!];
        } else c1 = filterIndices(c1, h.guess, f1, n, store);
        if (isAllGreenFeedback(f2, n)) {
          solved2 = true;
          const m = c2.filter((i) => store.getEq(i) === gNorm);
          if (m.length) c2 = [m[0]!];
        } else c2 = filterIndices(c2, h.guess, f2, n, store);
        if (solved1 && solved2) {
          return {
            ok: true,
            solved: true,
            suggestion: store.toDisplay(gNorm, n),
            remaining: { boards: [1, 1], product: 1 },
            engine: "browser_partition",
          };
        }
        if (c1.length === 0 || c2.length === 0)
          return { ok: false, error: "no candidates remain on one board — check feedback" };
      }

      const turn = history.length;
      const triesLeft = Math.max(1, MAX_TRIES_BINERDLE - turn);
      const guess = bestGuessBinerdlePartition(
        (i) => store.getEq(i),
        (i) => store.rank(i),
        c1,
        c2,
        n,
        triesLeft,
        solved1,
        solved2,
      );
      const sugg = store.toDisplay(normalizeGuessInput(guess, isMaxi), n);
      return {
        ok: true,
        suggestion: sugg,
        remaining: { boards: [c1.length, c2.length], product: c1.length * c2.length },
        engine: "browser_partition",
      };
    }

    /* quad */
    let c = cQuad!.map((x) => [...x]);
    const solved = [false, false, false, false];

    for (let t = 0; t < history.length; t++) {
      const h = history[t]! as MultiHist;
      const gNorm = normalizeGuessInput(h.guess, isMaxi);
      for (let b = 0; b < 4; b++) {
        const fb = h.feedback[b]!;
        if (isAllGreenFeedback(fb, n)) {
          solved[b] = true;
          const m = c[b]!.filter((i) => store.getEq(i) === gNorm);
          if (m.length) c[b] = [m[0]!];
          continue;
        }
        c[b] = filterIndices(c[b]!, h.guess, fb, n, store);
        if (c[b]!.length === 0) return { ok: false, error: "no candidates remain on one board — check feedback" };
      }
      if (solved.every(Boolean)) {
        return {
          ok: true,
          solved: true,
          suggestion: store.toDisplay(normalizeGuessInput(h.guess, isMaxi), n),
          remaining: { boards: [1, 1, 1, 1], product: 1 },
          engine: "browser_partition",
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
      solved,
    );
    const sugg = store.toDisplay(normalizeGuessInput(guess, isMaxi), n);
    const prod = c[0]!.length * c[1]!.length * c[2]!.length * c[3]!.length;
    return {
      ok: true,
      suggestion: sugg,
      remaining: { boards: [c[0]!.length, c[1]!.length, c[2]!.length, c[3]!.length], product: prod },
      engine: "browser_partition",
    };
  } catch (e) {
    return { ok: false, error: String(e) };
  }
}
