#pragma once
#include <cstddef>

namespace clomdb {

struct Options {
    bool create_if_missing = true;
    bool error_if_exists = false;

    size_t memtable_flush_bytes = 4 * 1024 * 1024;

    int l0_compaction_trigger = 4;

    int level_size_multiplier = 10;

    size_t level1_target_bytes = 16 * 1024 * 1024;

    int bloom_bits_per_key = 10;

    bool sync_writes = false;

    bool background_compaction = true;
};

struct ReadOptions {
    bool verify_checksums = true;
};

struct WriteOptions {
    bool sync = false;
};

}
