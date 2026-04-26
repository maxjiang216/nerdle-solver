/** Packed feedback: trit i = B=0, P=1, G=2 at position i (LSB = position 0). */

const POW3 = [1, 3, 9, 27, 81, 243, 729, 2187, 6561, 19683, 59049];

export function pow3Table(n: number): number {
  return n >= 0 && n <= 10 ? POW3[n]! : 0;
}

export function allGreenPacked(n: number): number {
  let c = 0;
  let m = 1;
  for (let i = 0; i < n; i++) {
    c += 2 * m;
    m *= 3;
  }
  return c;
}

export function computeFeedbackPacked(guess: string, solution: string, n: number): number {
  const remaining = new Array<number>(256).fill(0);
  for (let i = 0; i < n; i++) remaining[solution.charCodeAt(i)!]++;

  const trits = new Array<number>(n);
  for (let i = 0; i < n; i++) {
    if (guess.charCodeAt(i) === solution.charCodeAt(i)) {
      trits[i] = 2;
      remaining[guess.charCodeAt(i)!]--;
    } else {
      trits[i] = 0;
    }
  }
  for (let i = 0; i < n; i++) {
    if (trits[i] === 2) continue;
    const c = guess.charCodeAt(i)!;
    if (remaining[c]! > 0) {
      trits[i] = 1;
      remaining[c]--;
    }
  }
  let code = 0;
  let mul = 1;
  for (let i = 0; i < n; i++) {
    code += trits[i]! * mul;
    mul *= 3;
  }
  return code;
}

export function feedbackStringToPacked(fb: string, n: number): number {
  let code = 0;
  let mul = 1;
  for (let i = 0; i < n; i++) {
    const ch = fb[i]!;
    let t = 0;
    if (ch === "G" || ch === "g") t = 2;
    else if (ch === "P" || ch === "p") t = 1;
    code += t * mul;
    mul *= 3;
  }
  return code;
}

export function feedbackPackedToString(code: number, n: number): string {
  const out = new Array<string>(n);
  let x = code;
  for (let i = 0; i < n; i++) {
    const t = x % 3;
    out[i] = t === 2 ? "G" : t === 1 ? "P" : "B";
    x = Math.floor(x / 3);
  }
  return out.join("");
}

const PLACE_SQ = 0x01;
const PLACE_CB = 0x02;

/** Maxi: UTF-8 ²/³ → internal bytes (matches C++ solver_json). */
export function normalizeMaxi(s: string): string {
  let out = "";
  for (let i = 0; i < s.length; i++) {
    const c0 = s.charCodeAt(i);
    if (c0 === 0xc2 && i + 1 < s.length) {
      const c1 = s.charCodeAt(i + 1);
      if (c1 === 0xb2) {
        out += String.fromCharCode(PLACE_SQ);
        i++;
        continue;
      }
      if (c1 === 0xb3) {
        out += String.fromCharCode(PLACE_CB);
        i++;
        continue;
      }
    }
    out += s[i]!;
  }
  return out;
}

export function maxiToDisplay(s: string): string {
  let out = "";
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    if (c === PLACE_SQ) out += "\u00b2";
    else if (c === PLACE_CB) out += "\u00b3";
    else out += s[i]!;
  }
  return out;
}

export function normalizeGuessInput(s: string, isMaxi: boolean): string {
  return isMaxi ? normalizeMaxi(s) : s;
}

export function isConsistentFeedback(
  candidateEq: string,
  guess: string,
  feedbackGpb: string,
  n: number,
): boolean {
  return computeFeedbackPacked(guess, candidateEq, n) === feedbackStringToPacked(feedbackGpb, n);
}
