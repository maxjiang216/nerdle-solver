export type Manifest = {
  version: number;
  kind: string;
  n: number;
  opening: string;
  poolSize: number;
  bucketDir: string;
  hasPoolFull: boolean;
  hasOpeningBuckets?: boolean;
};

export type BucketRow = [number, number, string];

let manifestCache = new Map<string, Manifest>();

export function manifestKey(kind: string, n: number): string {
  return `${kind}_n${n}`;
}

function joinDataPath(baseUrl: string, rel: string): string {
  const prefix = baseUrl && !baseUrl.endsWith("/") ? baseUrl + "/" : baseUrl;
  return `${prefix}${rel}`;
}

export async function fetchManifest(baseUrl: string, kind: string, n: number): Promise<Manifest> {
  const key = manifestKey(kind, n);
  const hit = manifestCache.get(key);
  if (hit) return hit;
  const url = joinDataPath(baseUrl, `data/partition/${key}/manifest.json`);
  const res = await fetch(url);
  if (!res.ok) throw new Error(`manifest ${url}: ${res.status}`);
  const m = (await res.json()) as Manifest;
  manifestCache.set(key, m);
  return m;
}

export async function fetchBucket(baseUrl: string, kind: string, n: number, feedbackCode: number): Promise<BucketRow[]> {
  const key = manifestKey(kind, n);
  const url = joinDataPath(baseUrl, `data/partition/${key}/b/${feedbackCode}.json`);
  const res = await fetch(url);
  if (!res.ok) throw new Error(`bucket ${url}: ${res.status}`);
  return (await res.json()) as BucketRow[];
}

export async function fetchPoolFull(baseUrl: string, kind: string, n: number): Promise<BucketRow[]> {
  const key = manifestKey(kind, n);
  const url = joinDataPath(baseUrl, `data/partition/${key}/pool_full.json`);
  const res = await fetch(url);
  if (!res.ok) throw new Error(`pool_full ${url}: ${res.status}`);
  return (await res.json()) as BucketRow[];
}

export function clearManifestCache(): void {
  manifestCache = new Map();
}
