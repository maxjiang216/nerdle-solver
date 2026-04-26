/**
 * Binerdle + Quad partition selectors — mirrors binerdle_partition.hpp / quad_partition.hpp.
 */

import { computeFeedbackPacked, pow3Table } from "./feedback";
import { bestGuessPartitionPolicy, PARTITION_INTERACTIVE_TIE_DEPTH } from "./partition_policy";

function sameState(a: number[], b: number[]): boolean {
  if (a.length !== b.length) return false;
  const set = new Set(a);
  for (const x of b) if (!set.has(x)) return false;
  return true;
}

function binerdlePartitionClassesExcluding(
  getEq: (i: number) => string,
  state: number[],
  guessIdx: number,
  n: number,
  stamp: Int32Array,
  stampIdRef: { id: number },
): number {
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

function canonicalLess(
  a: number,
  b: number,
  rank: (idx: number) => number,
): boolean {
  return rank(a) < rank(b);
}

export function bestGuessBinerdlePartition(
  getEq: (i: number) => string,
  rank: (idx: number) => number,
  c1: number[],
  c2: number[],
  n: number,
  triesRemaining: number,
  solved1: boolean,
  solved2: boolean,
  partitionTieDepth: number = PARTITION_INTERACTIVE_TIE_DEPTH,
): string {
  if (c1.length === 0 || c2.length === 0) return "";

  const canonicalBestSingleton = (): number => {
    if (c1.length === 0) return c2[0]!;
    if (c2.length === 0) return c1[0]!;
    if (solved1 && !solved2) return c2[0]!;
    if (solved2 && !solved1) return c1[0]!;
    return canonicalLess(c1[0]!, c2[0]!, rank) ? c1[0]! : c2[0]!;
  };

  if (c1.length === 1 && c2.length === 1) return getEq(canonicalBestSingleton());
  if (!solved1 && c1.length === 1) return getEq(c1[0]!);
  if (!solved2 && c2.length === 1) return getEq(c2[0]!);

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
  const pool: number[] = [...c1];
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

    if (!haveBest || classes > bestClasses || (classes === bestClasses && canonicalLess(idx, bestIdx, rank))) {
      haveBest = true;
      bestClasses = classes;
      bestIdx = idx;
    }
  }

  return haveBest ? getEq(bestIdx) : "";
}

function quadActiveIndices(solved: boolean[], boards: number[][]): number[] {
  const act: number[] = [];
  for (let b = 0; b < 4; b++) {
    if (solved[b] || boards[b]!.length === 0) continue;
    act.push(b);
  }
  return act;
}

export function bestGuessQuadPartition(
  getEq: (i: number) => string,
  rank: (idx: number) => number,
  boards: number[][],
  n: number,
  triesRemaining: number,
  solved: boolean[],
  partitionTieDepth: number = PARTITION_INTERACTIVE_TIE_DEPTH,
): string {
  const B = boards;
  for (let b = 0; b < 4; b++) {
    if (solved[b]) continue;
    if (B[b]!.length === 0) return "";
  }
  for (let b = 0; b < 4; b++) {
    if (solved[b]) continue;
    if (B[b]!.length === 1) return getEq(B[b]![0]!);
  }

  const act = quadActiveIndices(solved, B);
  if (act.length === 0) return "";

  if (act.length === 1) {
    const bi = act[0]!;
    return bestGuessPartitionPolicy(getEq, B[bi]!, n, Math.max(1, triesRemaining), partitionTieDepth);
  }

  if (act.length === 2) {
    const i0 = act[0]!;
    const i1 = act[1]!;
    return bestGuessBinerdlePartition(
      getEq,
      rank,
      B[i0]!,
      B[i1]!,
      n,
      Math.max(1, triesRemaining),
      solved[i0]!,
      solved[i1]!,
      partitionTieDepth,
    );
  }

  const ca = B[act[0]!]!;
  let allSame = true;
  for (let a = 1; a < act.length; a++) {
    if (!sameState(ca, B[act[a]!]!)) {
      allSame = false;
      break;
    }
  }
  if (allSame) {
    return bestGuessPartitionPolicy(getEq, ca, n, Math.max(1, triesRemaining), partitionTieDepth);
  }

  const u = new Set<number>();
  for (const bi of act) for (const idx of B[bi]!) u.add(idx);
  const pool = [...u].sort((a, b) => a - b);

  const P = pow3Table(n);
  const stamp = new Int32Array(P);
  const stampIdRef = { id: 0 };
  let bestS = -1;
  let bestIdx = pool[0]!;

  for (const g of pool) {
    let s = 0;
    for (const bi of act) {
      s += binerdlePartitionClassesExcluding(getEq, B[bi]!, g, n, stamp, stampIdRef);
    }
    if (s > bestS || (s === bestS && canonicalLess(g, bestIdx, rank))) {
      bestS = s;
      bestIdx = g;
    }
  }
  return getEq(bestIdx);
}
