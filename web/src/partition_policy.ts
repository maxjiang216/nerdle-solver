/**
 * Partition greedy policy (tie-depth 6) — mirrors src/partition_greedy.hpp for interactive use.
 */

import {
  allGreenPacked,
  computeFeedbackPacked,
  pow3Table,
} from "./feedback";

export type PartitionSolveDist = { solve_at: number[] };

function partitionSolveDistLess(a: PartitionSolveDist, b: PartitionSolveDist, horizon: number): boolean {
  const eps = 1e-12;
  for (let t = 1; t <= horizon && t < a.solve_at.length; t++) {
    const da = a.solve_at[t] ?? 0;
    const db = b.solve_at[t] ?? 0;
    if (Math.abs(da - db) > eps) return da < db;
  }
  return false;
}

export class PartitionGreedyEvaluator {
  private readonly P: number;
  private readonly green: number;
  private readonly stamp: Int32Array;
  private stampId = 0;
  private readonly memo = new Map<string, PartitionSolveDist>();

  constructor(
    private readonly getEq: (idx: number) => string,
    private readonly N: number,
    private readonly maxTries: number,
    private readonly tieDepth: number,
  ) {
    this.P = pow3Table(N);
    this.green = allGreenPacked(N);
    this.stamp = new Int32Array(this.P);
    this.stamp.fill(-1);
  }

  private feedbackCode(g: number, s: number): number {
    return computeFeedbackPacked(this.getEq(g), this.getEq(s), this.N);
  }

  /** Match C++ `make_key`: order-sensitive (not sorted). */
  private makeKey(state: number[], k: number): string {
    return `${k}:${state.join(",")}`;
  }

  private partitionCount(state: number[], g: number): number {
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

  private bestPartitionGuesses(state: number[], k: number): number[] {
    if (k < 2) return [...state];
    let best = -1;
    const out: number[] = [];
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

  private afterGuess(state: number[], g: number, k: number): PartitionSolveDist {
    const out: PartitionSolveDist = { solve_at: new Array(7).fill(0) };
    const inv = 1 / state.length;
    const buckets: number[][] = Array.from({ length: this.P }, () => []);
    const usedCodes: number[] = [];

    for (const s of state) {
      const code = this.feedbackCode(g, s);
      if (code === this.green) {
        out.solve_at[1]! += inv;
      } else {
        if (buckets[code]!.length === 0) usedCodes.push(code);
        buckets[code]!.push(s);
      }
    }

    for (const code of usedCodes) {
      const child = buckets[code]!;
      const weight = (child.length * inv);
      const v = this.solve(child, k - 1);
      for (let t = 2; t <= k && t <= this.maxTries; t++) {
        out.solve_at[t]! += weight * (v.solve_at[t - 1] ?? 0);
      }
    }
    return out;
  }

  solve(state: number[], k: number): PartitionSolveDist {
    if (state.length === 0 || k < 1) return { solve_at: [] };
    if (state.length === 1) {
      const o: PartitionSolveDist = { solve_at: new Array(7).fill(0) };
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

  private chooseGuess(state: number[], k: number): number {
    const guesses = this.bestPartitionGuesses(state, k);
    if (guesses.length === 0) return -1;
    if (guesses.length === 1 || this.tieDepth <= 0 || k < 3) return guesses[0]!;

    const horizon = Math.min(k, this.tieDepth + 2);
    let haveBest = false;
    let best: PartitionSolveDist = { solve_at: [] };
    let bestG = guesses[0]!;
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

  bestGuessIndex(candidateIndices: number[], k: number): number {
    const st = [...candidateIndices];
    return this.chooseGuess(st, k);
  }

  bestGuessString(candidateIndices: number[], k: number): string {
    const g = this.bestGuessIndex(candidateIndices, k);
    return this.getEq(g);
  }
}

export const PARTITION_INTERACTIVE_TIE_DEPTH = 6;

export function bestGuessPartitionPolicy(
  getEq: (idx: number) => string,
  candidateIndices: number[],
  N: number,
  triesRemaining: number,
  tieDepth: number = PARTITION_INTERACTIVE_TIE_DEPTH,
): string {
  if (candidateIndices.length === 0) return "";
  if (candidateIndices.length === 1) return getEq(candidateIndices[0]!);
  if (triesRemaining < 1) return getEq(candidateIndices[0]!);

  const ev = new PartitionGreedyEvaluator(getEq, N, triesRemaining, tieDepth);
  return ev.bestGuessString(candidateIndices, triesRemaining);
}
