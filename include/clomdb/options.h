#pragma once
#include <cstddef>

namespace clomdb {

struct Options {
    // Create the database directory if it doesn't exist.
    bool create_if_missing = true;
    // Fail Open() if the database already exists.
    bool error_if_exists = false;

    // Flush the active memtable to an L0 SSTable once it reaches this size
    // (bytes, approximate, counts key+value bytes of live entries).
    size_t memtable_flush_bytes = 4 * 1024 * 1024;  // 4 MiB

    // Number of L0 files that triggers a compaction of L0 into L1.
    int l0_compaction_trigger = 4;

    // Fan-out factor between levels: level L+1 target size = level L * this.
    int level_size_multiplier = 10;

    // Target size in bytes for level 1 before it triggers further compaction.
    size_t level1_target_bytes = 16 * 1024 * 1024;  // 16 MiB

    // Bits per key used for the per-SSTable bloom filter. 10 gives ~1% FP rate.
    int bloom_bits_per_key = 10;

    // fsync the WAL after every write batch. Slower, but durable across
    // OS/process crashes as well as power loss. If false, only fsync'd on
    // graceful Close() and before flush/compaction, trading some durability
    // for throughput.
    bool sync_writes = false;

    // Run flush/compaction on a background thread. If false, they happen
    // synchronously inline with the write that triggers them.
    bool background_compaction = true;
};

struct ReadOptions {
    // Verify checksums on every block read from disk.
    bool verify_checksums = true;
};

struct WriteOptions {
    // Force an fsync of the WAL for this write, overriding Options::sync_writes.
    bool sync = false;
};

}  // namespace clomdb
