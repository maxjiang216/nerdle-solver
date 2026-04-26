/**
 * Micro (5-tile) Bellman-optimal play using the same binary policy as C++ (`optimal_policy_5.bin`).
 */

import { fetchPoolFull } from "./artifacts";
import type { ClassicHist, StepResult } from "./step_types";
import { isConsistentFeedback, normalizeGuessInput } from "./feedback";
import { bestGuessPartitionPolicy } from "./partition_policy";

const POLICY_MAGIC = 0x4e355042;
const POLICY_VER1 = 1;
const POLICY_VER2 = 2;

function maskFromCandidates(cands: number[]): bigint[] {
  const w = [0n, 0n, 0n, 0n];
  for (const i of cands) {
    if (i >= 0 && i < 256) {
      const limb = i >> 6;
      w[limb]! |= 1n << BigInt(i & 63);
    }
  }
  return w;
}

function maskKey(w: bigint[]): string {
  const u8 = new Uint8Array(32);
  const dv = new DataView(u8.buffer);
  for (let i = 0; i < 4; i++) dv.setBigUint64(i * 8, w[i]!, true);
  return new TextDecoder("latin1").decode(u8);
}

let policyCache: Map<string, number> | null = null;
let policyLoadError: string | null = null;

export function clearMicroPolicyCache(): void {
  policyCache = null;
  policyLoadError = null;
}

export async function loadMicroPolicyMap(baseUrl: string, expectedPoolSize?: number): Promise<Map<string, number>> {
  if (policyCache) return policyCache;
  if (policyLoadError) throw new Error(policyLoadError);

  const prefix = baseUrl && !baseUrl.endsWith("/") ? baseUrl + "/" : baseUrl;
  const url = `${prefix}data/optimal_policy_5.bin`;
  let buf: ArrayBuffer;
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
  if (magic !== POLICY_MAGIC || (ver !== POLICY_VER1 && ver !== POLICY_VER2)) {
    throw new Error("optimal_policy_5.bin: bad header");
  }
  const neq = v.getUint8(o);
  o += 1;
  o += 3; // pad
  const nent = v.getUint32(o, true);
  o += 4;

  if (expectedPoolSize !== undefined && neq !== expectedPoolSize) {
    throw new Error(`optimal_policy_5.bin: neq ${neq} != pool ${expectedPoolSize}`);
  }

  const map = new Map<string, number>();
  for (let i = 0; i < nent; i++) {
    let w: bigint[];
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

function filterClassic(
  cands: number[],
  guess: string,
  fb: string,
  n: number,
  getEq: (i: number) => string,
): number[] {
  const g = normalizeGuessInput(guess, false);
  return cands.filter((i) => isConsistentFeedback(getEq(i), g, fb, n));
}

function isAllGreen(fb: string, n: number): boolean {
  if (fb.length !== n) return false;
  for (let i = 0; i < n; i++) if (fb[i] !== "G" && fb[i] !== "g") return false;
  return true;
}

/** Classic Micro (n=5) only. */
export async function microBellmanClassicStep(baseUrl: string, history: ClassicHist[]): Promise<StepResult> {
  const n = 5;
  try {
    const full = await fetchPoolFull(baseUrl, "classic", n);
    const idToEq = new Map<number, string>();
    for (const [id, , disp] of full) {
      idToEq.set(id, disp);
    }
    const getEq = (i: number) => {
      const s = idToEq.get(i);
      if (s === undefined) throw new Error(`missing id ${i}`);
      return s;
    };

    let cands = full.map((r) => r[0]).sort((a, b) => a - b);
    const poolSize = cands.length;

    for (let t = 0; t < history.length; t++) {
      const h = history[t]!;
      if (isAllGreen(h.feedback, n)) {
        return {
          ok: true,
          solved: true,
          suggestion: h.guess,
          remaining: 1,
          engine: "browser_bellman",
        };
      }
      cands = filterClassic(cands, h.guess, h.feedback, n, getEq);
      if (cands.length === 0) return { ok: false, error: "no candidates remain — check guess and feedback" };
    }

    if (cands.length === 1) {
      return {
        ok: true,
        suggestion: getEq(cands[0]!),
        remaining: 1,
        engine: "browser_bellman",
      };
    }

    let policy: Map<string, number>;
    try {
      policy = await loadMicroPolicyMap(baseUrl, poolSize);
    } catch {
      const triesLeft = Math.max(1, 6 - history.length);
      const guess = bestGuessPartitionPolicy((i) => getEq(i), cands, n, triesLeft);
      return {
        ok: true,
        suggestion: guess,
        remaining: cands.length,
        engine: "browser_bellman_fallback_partition",
      };
    }

    const m = maskFromCandidates(cands);
    const gi = policy.get(maskKey(m));
    if (gi === undefined || gi >= poolSize) {
      const triesLeft = Math.max(1, 6 - history.length);
      const guess = bestGuessPartitionPolicy((i) => getEq(i), cands, n, triesLeft);
      return {
        ok: true,
        suggestion: guess,
        remaining: cands.length,
        engine: "browser_bellman_fallback_partition",
      };
    }

    return {
      ok: true,
      suggestion: getEq(gi),
      remaining: cands.length,
      engine: "browser_bellman",
    };
  } catch (e) {
    return { ok: false, error: String(e) };
  }
}
