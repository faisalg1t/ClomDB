# ClomDB — Design Notes

This file consolidates the "why" behind non-obvious decisions in the
codebase. Inline comments were intentionally removed from the source for
readability; this document exists so that reasoning isn't lost.

## Status (`status.h`)
- Allocation-free on success (`Status::OK()` carries no string), since the
  common path shouldn't pay for an error message it doesn't have.

## Write-ahead log (`wal.h` / `wal.cpp`)
- Every record is framed as `[4-byte masked CRC32C][4-byte length][payload]`.
- The CRC is **masked** before storing (`CRC32C::Mask`), not stored raw.
  This avoids an all-zero region of a partially-written file looking like
  a valid checksum of an all-zero record.
- On replay, a **short or corrupt trailing record is not treated as fatal
  corruption** — it's assumed to be a torn write from a crash mid-append,
  and replay simply stops there. A checksum mismatch anywhere else in the
  file is treated the same way (stop, don't skip-and-continue), because
  later records may depend on earlier ones and skipping silently could
  produce a valid-looking but wrong database state.
- `AddRecord` always `fflush()`s even when `sync=false`, so a **process**
  crash never loses data — only an OS/power crash can, and only if
  `sync_writes`/`WriteOptions::sync` wasn't requested.
- After every successful memtable flush, the WAL is **rotated** (closed,
  deleted, reopened empty) rather than left to grow forever, since
  everything in it is now durable in an SSTable.

## MemTable (`memtable.h`)
- Backed by `std::map`, not a lock-free skiplist. This is a deliberate
  simplicity-over-throughput trade-off — a single mutex in `DB` guards all
  access instead. Documented explicitly so it doesn't read as an
  oversight.
- `ApproximateBytes()` is **monotonic and approximate** — it does not
  subtract bytes on key overwrite. It exists only to decide *when* to
  flush, not to report exact live-data size.
- A tombstone (`MemValue::deleted = true`) is a first-class value, not a
  missing key — it must survive until compacted into the last level so it
  correctly shadows older values already flushed to SSTables.

## SSTable format (`sstable_format.h`, `sstable.h/.cpp`)
- Layout: data section → **full** index (one entry per key, not sparse) →
  Bloom filter → fixed 40-byte footer with a magic number.
- The index is intentionally **not sparse** (unlike LevelDB's
  restart-interval blocks). Trades memory for a much simpler, obviously-
  correct implementation. Reasonable at the scale a single-node embedded
  store targets; sparsifying it is the natural first optimization if
  memory becomes a problem.
- `ReadAll()` decodes the entire data section into memory. Used by
  compaction and by `NewIterator()` — see the iterator note below for the
  cost implications.
- Bloom filter: `MayContain` **fails open** (returns `true`) on a
  malformed/empty filter or an out-of-range `k`, rather than risking a
  false negative that would hide a real key.

## Manifest / Version (`version.h/.cpp`)
- Persisted as a **full snapshot** (not an incremental edit log), written
  to a `.tmp` file and atomically `rename()`d into place. Simpler to
  reason about and recover than an edit log, at the cost of rewriting all
  file metadata (not the data itself) on every flush/compaction — cheap.
- On `Open()`, any `.sst` file on disk **not referenced by the loaded
  manifest** is deleted. This cleans up orphans left by a crash that
  happened between "new manifest saved" and "old compaction inputs
  deleted" (see Compaction below) — safe because the manifest is the sole
  source of truth for liveness.

## DB / Compaction (`db.h`, `db.cpp`)
- **Locking model**: one `std::mutex` guards the memtable, `Version`, and
  all flush/compaction file I/O. Reads and writes block during an
  in-progress flush/compaction. This is the single biggest scalability
  simplification in the codebase — flagged here and in the README rather
  than left implicit.
- **Compaction strategy**: full-level-merge, not LevelDB-style overlapping-
  range picking. When level `L` needs compacting, *all* of `L` and *all*
  of `L+1` are merged into fresh, sorted, non-overlapping files replacing
  both. Simpler to implement correctly; costs more write amplification on
  large datasets than incremental range compaction.
- **Recency ordering during merge**: level `L` always wins over `L+1` on a
  duplicate key. Within `L0` specifically (the only level whose files can
  have overlapping key ranges), files are processed **newest-file-number
  first** so the newest write wins among L0 files themselves.
- **Tombstone dropping**: a delete marker is only dropped when the
  compaction's *target* level is the last level (`kMaxLevels - 1`). Since
  every compaction step fully consumes its two input levels, any older
  data for that key living in a deeper, not-yet-touched level still needs
  the tombstone to shadow it correctly once *that* level eventually gets
  merged up. Dropping earlier would let a deleted key "resurrect."
- **Crash safety of compaction**: new SSTables are written and opened
  first, then the manifest is saved (atomic rename) pointing only at the
  new files, and **only then** are the old input files unlinked from disk.
  If a crash happens between the manifest save and the unlink, the old
  files become harmless orphans, swept up by the `Version::Load` orphan
  sweep on the next `Open()`.
- **Background thread**: a single condition-variable-driven loop that
  drains "flush if oversized, then compact while needed" under the same
  global mutex used by foreground writes — so it never races with them,
  but also never overlaps with them for extra parallelism.

## Iterator (`iterator.h`)
- `NewIterator()` **materializes the entire keyspace** into a
  `std::map<string,string>` (deepest level first, L0 oldest-to-newest,
  memtable last — so later writers correctly overwrite earlier ones, and
  tombstones remove rather than shadow-with-a-marker). This is an
  explicit simplicity trade-off: O(DB size) time/memory per
  `NewIterator()` call, in exchange for a trivially-correct implementation
  with no streaming k-way-merge logic to get wrong. Not suitable for scans
  over datasets much larger than RAM — see README "Known limitations."

## Testing
- The custom test harness (`tests/test_util.h`) exists purely to avoid an
  external dependency (e.g. GoogleTest) for a from-scratch project; it
  intentionally does not try to replicate matcher/mocking features.
- `db_test.cpp`'s WAL-recovery tests call `delete db` (which runs a clean
  destructor, including a flush) between "crash" scenarios rather than
  actually killing the process — because a graceful destructor still
  exercises the WAL-replay-then-continue code path on the next `Open()`
  as long as the flush threshold hasn't been crossed. This is noted
  in-test where it matters (see `RecoversUnflushedWalWithoutCleanShutdown`).