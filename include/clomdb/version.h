#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "clomdb/status.h"

namespace clomdb {

// Maximum number of levels. L0 holds recently-flushed, possibly
// key-overlapping tables; L1..kMaxLevels-1 are sorted and non-overlapping
// within each level, sized geometrically (see Options::level_size_multiplier).
constexpr int kMaxLevels = 6;

struct FileMetaData {
    uint64_t number = 0;   // used to derive the on-disk filename
    std::string min_key;
    std::string max_key;
    uint64_t file_size = 0;
};

// The current set of live SSTable files, grouped by level. Persisted to a
// MANIFEST file as a full snapshot (rather than an incremental edit log)
// every time it changes -- simpler to reason about and to recover, at the
// cost of a full rewrite per flush/compaction, which is cheap since the
// manifest is just file metadata, not the data itself.
class Version {
public:
    Version() : levels_(kMaxLevels) {}

    std::vector<FileMetaData>& MutableLevel(int level) { return levels_[level]; }
    const std::vector<FileMetaData>& Level(int level) const { return levels_[level]; }
    int NumLevels() const { return kMaxLevels; }

    uint64_t next_file_number = 1;

    Status Save(const std::string& manifest_path) const;
    Status Load(const std::string& manifest_path);

private:
    std::vector<std::vector<FileMetaData>> levels_;
};

}  // namespace clomdb
