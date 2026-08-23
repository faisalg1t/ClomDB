# ClomDB

[![CI](https://github.com/faisalg1t/ClomDB/actions/workflows/ci.yml/badge.svg)](https://github.com/faisalg1t/ClomDB/actions/workflows/ci.yml)
[![Latest Release](https://img.shields.io/github/v/release/faisalg1t/ClomDB)](https://github.com/faisalg1t/ClomDB/releases/latest)

ClomDB is an embedded, durable, LSM-tree-backed key-value store written in
C++17, with no dependencies beyond the standard library and pthreads.

## Download prebuilt binaries

No build step needed — grab the latest release for your platform:

* **Linux (x86_64):** [clomdb-linux-x86_64.tar.gz](https://github.com/faisalg1t/ClomDB/releases/latest/download/clomdb-linux-x86_64.tar.gz)
* **macOS (Apple Silicon):** [clomdb-macos-arm64.tar.gz](https://github.com/faisalg1t/ClomDB/releases/latest/download/clomdb-macos-arm64.tar.gz)

```sh
tar xzf clomdb-<platform>.tar.gz
cd clomdb-<platform>
./bin/clomdb_cli /tmp/mydb put hello world
./bin/clomdb_cli /tmp/mydb get hello
```

Each archive includes `bin/clomdb_cli`, `bin/clomdb_bench`, `lib/libclomdb.a`,
and the `include/clomdb/` headers needed to link the library into your own
C++ project. See [all releases](https://github.com/faisalg1t/ClomDB/releases).

## Using it as a library

```cpp
#include "clomdb/clomdb.h"
using namespace clomdb;

Options options;
DB* db;
DB::Open(options, "/path/to/db", &db);

db->Put(WriteOptions(), "key", "value");

std::string value;
Status s = db->Get(ReadOptions(), "key", &value);
if (s.ok()) { /* use value */ }

db->Delete(WriteOptions(), "key");

auto it = db->NewIterator(ReadOptions());
for (it->SeekToFirst(); it->Valid(); it->Next()) {
    // it->key(), it->value()
}

delete db;  // flushes and closes automatically
```

## Design

* **Write-ahead log (WAL)** — every write is framed with a checksum and
  appended to a log file before being applied to the in-memory table, so
  acknowledged writes survive a process crash. A torn/incomplete trailing
  record (from a crash mid-write) is detected and the log is truncated at
  the last valid record instead of being treated as fatal corruption.
* **Memtable** — an in-memory sorted table (`std::map`) holding recent
  writes and delete markers ("tombstones"). Guarded by a single mutex
  rather than a lock-free skiplist: less exotic, still correct, easy to
  reason about.
* **SSTables** — once the memtable crosses `Options::memtable_flush_bytes`,
  it's written out as an immutable, sorted SSTable file (data section +
  full key index + Bloom filter + checksummed footer) and the WAL is
  rotated.
* **Compaction** — a simplified full-level-merge strategy: when L0
  accumulates too many files, or a level exceeds its size target, that
  level is merged wholesale with the level below it into fresh, sorted,
  non-overlapping files, dropping obsolete tombstones once they reach the
  last level. Simpler to reason about than LevelDB-style range-picking
  compaction, at the cost of more work per compaction.
* **Bloom filters** — one per SSTable, so point lookups for missing keys
  usually skip the disk read entirely.
* **Manifest** — the current set of live SSTable files per level is
  persisted as a checksummed snapshot on every flush/compaction, using a
  write-temp-file-then-rename for atomicity. Any `.sst` file left on disk
  that the manifest doesn't reference (e.g. from a crash between "new
  manifest saved" and "old files deleted") is swept up on the next `Open`.

## Building

Requires CMake >= 3.16 and a C++17 compiler.

```sh
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j
ctest --output-on-failure
```

This produces:
* `libclomdb.a` — the library
* `clomdb_cli` — a small CLI/REPL (`put`, `get`, `delete`, `scan`, `flush`, `stats`)
* `clomdb_bench` — a rough throughput benchmark
* the test suite, runnable via `ctest`

## Using it in another CMake project

```cmake
add_subdirectory(ClomDB)
target_link_libraries(your_target PRIVATE clomdb)
```

## Configuration (`clomdb::Options`)

| Field                     | Default | Meaning                                            |
|---------------------------|---------|-----------------------------------------------------|
| `create_if_missing`       | true    | Create the DB directory if it doesn't exist         |
| `error_if_exists`         | false   | Fail `Open()` if the DB already exists              |
| `memtable_flush_bytes`    | 4 MiB   | Flush the memtable once it reaches this size         |
| `l0_compaction_trigger`   | 4       | L0 file count that triggers compaction into L1       |
| `level_size_multiplier`   | 10      | Fan-out factor between levels                        |
| `level1_target_bytes`     | 16 MiB  | Size target for L1 before it's compacted further      |
| `bloom_bits_per_key`      | 10      | ~1% false-positive rate per SSTable                  |
| `sync_writes`             | false   | fsync the WAL on every write (durable vs. power loss) |
| `background_compaction`   | true    | Run flush/compaction on a background thread          |

## Known limitations / where this would need more work for a heavy production workload

This is a real, working, tested LSM engine — not a toy that only handles
the happy path — but it makes some deliberate simplicity trade-offs worth
being upfront about:

* **Coarse locking.** A single mutex guards the memtable, version state,
  and all file I/O for flush/compaction. Reads and writes serialize
  against an in-progress flush/compaction. A production system under
  heavy concurrent load would want to copy-on-write the `Version` so
  readers/writers don't block on background work.
* **`NewIterator()` materializes the whole keyspace** into memory (merging
  every SSTable + the memtable, dropping tombstones) rather than doing a
  lazy streaming k-way merge. Simple and correct; not suitable for range
  scans over datasets much larger than RAM.
* **No snapshots/MVCC.** Reads always see the latest committed state;
  there's no isolation between a long-running scan and concurrent writes
  beyond what the single mutex incidentally provides.
* **No block-level compression** in SSTables.
* **Full-level-merge compaction** rewrites an entire level at once rather
  than picking overlapping key ranges to compact incrementally (as
  LevelDB/RocksDB do) — simpler, but higher write amplification on very
  large datasets.
* **CRC32C is a portable table-driven implementation**, not a
  hardware-accelerated (SSE4.2/ARM CRC) one.

## Layout

```
include/clomdb/   public headers
src/              implementation
examples/         CLI + benchmark
tests/            unit + end-to-end tests (custom header-only harness, no external deps)
```

## License

MIT — see [LICENSE](/LICENSE)