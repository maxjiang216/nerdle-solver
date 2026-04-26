/**
 * Patches global fetch to read web/ from disk (avoids flaky mini HTTP server under load).
 * Usage from repo root: node web/scripts/validate_partition_engine.mjs
 * Requires: make solver_json browser_partition_artifacts, generated pools + web/data/partition/*.
 */
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import { spawnSync, execSync } from "child_process";
import { createRequire } from "module";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const webRoot = path.resolve(__dirname, "..");
const repoRoot = path.resolve(webRoot, "..");
const require = createRequire(import.meta.url);

execSync(
  `npx esbuild "${path.join(webRoot, "src/browser_engine.ts")}" --bundle --platform=node --format=cjs --outfile="${path.join(webRoot, "partition_node.cjs")}"`,
  { stdio: "inherit", cwd: webRoot },
);

/** @param {string} base file:// or http://unused/ — only pathname is used for disk reads */
function installFileFetch(base) {
  const root = webRoot;
  globalThis.fetch = async (input) => {
    const url = typeof input === "string" ? input : input.url;
    const u = new URL(url, base);
    let rel = u.pathname.replace(/^\//, "");
    if (rel.startsWith("web/")) rel = rel.slice(4);
    const fsPath = path.join(root, rel);
    if (!fsPath.startsWith(root)) return new Response("forbidden", { status: 403 });
    try {
      const data = fs.readFileSync(fsPath);
      return new Response(data, { status: 200 });
    } catch {
      return new Response("not found", { status: 404 });
    }
  };
}

const { browserPartitionStep } = require(path.join(webRoot, "partition_node.cjs"));

function solverJson(body) {
  const bin = path.join(repoRoot, "solver_json");
  const r = spawnSync(bin, { input: JSON.stringify(body), encoding: "utf-8", cwd: repoRoot });
  if (r.status !== 0) throw new Error(r.stderr || "solver_json failed");
  return JSON.parse(r.stdout.trim());
}

function feedbackFromStrings(guess, solution, n) {
  const remaining = new Array(256).fill(0);
  for (let i = 0; i < n; i++) remaining[solution.charCodeAt(i)]++;

  const trits = new Array(n);
  for (let i = 0; i < n; i++) {
    if (guess[i] === solution[i]) {
      trits[i] = 2;
      remaining[guess.charCodeAt(i)]--;
    } else trits[i] = 0;
  }
  for (let i = 0; i < n; i++) {
    if (trits[i] === 2) continue;
    const c = guess.charCodeAt(i);
    if (remaining[c] > 0) {
      trits[i] = 1;
      remaining[c]--;
    }
  }
  return trits.map((t) => (t === 2 ? "G" : t === 1 ? "P" : "B")).join("");
}

function loadPool(n) {
  const p = path.join(repoRoot, `data/equations_${n}.txt`);
  return fs.readFileSync(p, "utf-8").trim().split("\n").filter(Boolean);
}

async function compareClassic(base, n) {
  const pool = loadPool(n);
  const emptyHist = [];
  const b0 = await browserPartitionStep(base, "classic", n, emptyHist);
  const j0 = solverJson({ kind: "classic", n, strategy: "partition", history: [] });
  if (!b0.ok || !j0.ok) throw new Error(`classic n=${n} open ${JSON.stringify(b0)} ${JSON.stringify(j0)}`);
  if (b0.suggestion !== j0.suggestion) throw new Error(`classic n=${n} opening ${b0.suggestion} vs ${j0.suggestion}`);

  const guess = j0.suggestion;
  const secret = pool[Math.floor(Math.random() * pool.length)];
  const fb = feedbackFromStrings(guess, secret, n);
  const hist = [{ guess, feedback: fb }];
  const b1 = await browserPartitionStep(base, "classic", n, hist);
  const j1 = solverJson({ kind: "classic", n, strategy: "partition", history: hist });
  if (!b1.ok || !j1.ok) throw new Error(`classic n=${n} step1 ${JSON.stringify(b1)} ${JSON.stringify(j1)}`);
  if (b1.suggestion !== j1.suggestion) throw new Error(`classic n=${n} step1 ${b1.suggestion} vs ${j1.suggestion}`);
  if (b1.remaining !== j1.remaining) throw new Error(`classic n=${n} remaining ${b1.remaining} vs ${j1.remaining}`);
}

async function compareBinerdle(base, n) {
  const pool = loadPool(n);
  let s1 = pool[Math.floor(Math.random() * pool.length)];
  let s2 = pool[Math.floor(Math.random() * pool.length)];
  if (s2 === s1) s2 = pool[(pool.indexOf(s1) + 1) % pool.length];

  const emptyHist = [];
  const b0 = await browserPartitionStep(base, "binerdle", n, emptyHist);
  const j0 = solverJson({ kind: "binerdle", n, strategy: "partition", history: [] });
  if (!b0.ok || !j0.ok) throw new Error(`binerdle n=${n} open ${JSON.stringify(b0)} ${JSON.stringify(j0)}`);
  if (b0.suggestion !== j0.suggestion) throw new Error(`binerdle n=${n} opening ${b0.suggestion} vs ${j0.suggestion}`);

  const guess = j0.suggestion;
  const f1 = feedbackFromStrings(guess, s1, n);
  const f2 = feedbackFromStrings(guess, s2, n);
  const hist = [{ guess, feedback: [f1, f2] }];
  const b1 = await browserPartitionStep(base, "binerdle", n, hist);
  const j1 = solverJson({ kind: "binerdle", n, strategy: "partition", history: hist });
  if (!b1.ok || !j1.ok) throw new Error(`binerdle n=${n} step1 ${JSON.stringify(b1)} ${JSON.stringify(j1)}`);
  if (b1.suggestion !== j1.suggestion) throw new Error(`binerdle n=${n} step1 ${b1.suggestion} vs ${j1.suggestion}`);
  const brb = b1.remaining.boards.join(",");
  const jrb = j1.remaining.boards.join(",");
  if (brb !== jrb) throw new Error(`binerdle n=${n} boards ${brb} vs ${jrb}`);
}

async function compareQuad(base) {
  const n = 8;
  const pool = loadPool(n);
  const secrets = [];
  while (secrets.length < 4) {
    const x = pool[Math.floor(Math.random() * pool.length)];
    if (!secrets.includes(x)) secrets.push(x);
  }

  const b0 = await browserPartitionStep(base, "quad", n, []);
  const j0 = solverJson({ kind: "quad", n, strategy: "partition", history: [] });
  if (!b0.ok || !j0.ok) throw new Error(`quad open ${JSON.stringify(b0)} ${JSON.stringify(j0)}`);
  if (b0.suggestion !== j0.suggestion) throw new Error(`quad opening ${b0.suggestion} vs ${j0.suggestion}`);

  const guess = j0.suggestion;
  const feedbacks = secrets.map((sec) => feedbackFromStrings(guess, sec, n));
  const hist = [{ guess, feedback: feedbacks }];
  const b1 = await browserPartitionStep(base, "quad", n, hist);
  const j1 = solverJson({ kind: "quad", n, strategy: "partition", history: hist });
  if (!b1.ok || !j1.ok) throw new Error(`quad step1 ${JSON.stringify(b1)} ${JSON.stringify(j1)}`);
  if (b1.suggestion !== j1.suggestion) throw new Error(`quad step1 ${b1.suggestion} vs ${j1.suggestion}`);
}

async function main() {
  const base = `http://validate.local/`;
  const origFetch = globalThis.fetch;
  installFileFetch(base);
  try {
    const tests = [];
    for (const n of [5, 6, 7, 8]) {
      const poolPath = path.join(repoRoot, `data/equations_${n}.txt`);
      const manPath = path.join(webRoot, `data/partition/classic_n${n}/manifest.json`);
      if (fs.existsSync(poolPath) && fs.existsSync(manPath)) tests.push(() => compareClassic(base, n));
    }
    for (const n of [6, 8]) {
      const poolPath = path.join(repoRoot, `data/equations_${n}.txt`);
      const manPath = path.join(webRoot, `data/partition/binerdle_n${n}/manifest.json`);
      if (fs.existsSync(poolPath) && fs.existsSync(manPath)) tests.push(() => compareBinerdle(base, n));
    }
    {
      const manPath = path.join(webRoot, "data/partition/quad_n8/manifest.json");
      if (fs.existsSync(manPath)) tests.push(() => compareQuad(base));
    }

    if (tests.length === 0) throw new Error("no tests (missing data/equations_*.txt or web/data/partition/*)");

    for (const t of tests) await t();

    console.log(`validate_partition_engine: OK (${tests.length} cases)`);
  } finally {
    globalThis.fetch = origFetch;
  }
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
