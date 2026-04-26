# Browser partition artifacts

Static files under `web/data/partition/` power the browser-only partition solver.

They are **generated** by `make browser_partition_data_web` (CI/Vercel-friendly: uses tracked `data/equations_{5,6,7,8,10}.txt`). Generated output is normally **not** committed; run `./scripts/prepare_web_deploy.sh` locally or on the host that builds static assets.

## Layout

```
web/data/partition/
  {kind}_n{N}/
    manifest.json
    b/{feedbackCode}.json   # one file per nonempty opening-feedback bucket
    pool_full.json          # optional: full pool for fallback (small n only)
```

- `kind`: `classic`, `binerdle`, or `quad` (binerdle/quad share the same buckets as classic for the same `N` — one opening, per-board buckets).
- `feedbackCode`: decimal `uint32` packed feedback (same encoding as `compute_feedback_packed` in C++).

## `manifest.json`

```json
{
  "version": 1,
  "kind": "classic",
  "n": 8,
  "opening": "52-34=18",
  "poolSize": 17256,
  "bucketDir": "b",
  "hasPoolFull": true,
  "hasOpeningBuckets": true
}
```

- Maxi (`n=10`): `hasPoolFull` is false and `hasOpeningBuckets` is true. The static UI requires the recommended first guess (`56+4-21=39`), then loads the matching opening-feedback bucket for post-opening play.

## Bucket file `b/{code}.json`

JSON array of rows, each row:

```json
[id, rank, "equation"]
```

- `id`: 0-based index into `data/equations_{N}.txt` (after maxi normalization in the generator).
- `rank`: canonical tie-break rank (0 = best); matches `canonical_less` order on the full pool for n≤8. Maxi opening-bucket rows use the equation id here because browser Classic post-opening selection does not need canonical rank.
- `equation`: display string (UTF-8 `²`/`³` for maxi).

Rows are sorted by `id` ascending.

## `pool_full.json` (optional)

Same row format as buckets, all equations in one array, sorted by `id`. Generated for `n ∈ {5,6,7,8}` when the pool is small enough for static hosting.
