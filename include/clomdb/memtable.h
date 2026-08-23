#pragma once
#include <map>
#include <string>
#include "clomdb/slice.h"

namespace clomdb {

// Value stored per key in the memtable: either a live value or a tombstone
// recording that the key was deleted (tombstones must survive until they've
// been compacted past the last level, so deletes correctly shadow older
// values already flushed to SSTables).
struct MemValue {
    bool deleted = false;
    std::string value;
};

// A MemTable is a simple in-memory sorted map. It is intentionally not a
// lock-free skiplist (as LevelDB uses) -- correctness and readability were
// prioritized over lock-free concurrent access; the DB guards all memtable
// access with a mutex instead. This is a legitimate, if less exotic, design
// point for an embedded KV store.
class MemTable {
public:
    MemTable() = default;

    void Put(const Slice& key, const Slice& value) {
        MemValue& mv = table_[key.ToString()];
        // Approximate size tracking only (used to decide when to flush) --
        // deliberately does not subtract bytes on overwrite, so it's a
        // monotonic upper-ish bound rather than an exact live-byte count.
        approx_bytes_ += key.size() + value.size();
        mv.deleted = false;
        mv.value = value.ToString();
    }

    void Delete(const Slice& key) {
        MemValue& mv = table_[key.ToString()];
        approx_bytes_ += key.size();
        mv.deleted = true;
        mv.value.clear();
    }

    // Returns true if the key was found (live or tombstoned) in this
    // memtable. If found and live, *value is set and *deleted is false.
    // If found and tombstoned, *deleted is set true.
    bool Get(const Slice& key, std::string* value, bool* deleted) const {
        auto it = table_.find(key.ToString());
        if (it == table_.end()) return false;
        *deleted = it->second.deleted;
        if (!*deleted) *value = it->second.value;
        return true;
    }

    size_t ApproximateBytes() const { return approx_bytes_; }
    size_t EntryCount() const { return table_.size(); }
    bool empty() const { return table_.empty(); }

    const std::map<std::string, MemValue>& entries() const { return table_; }

    void Clear() {
        table_.clear();
        approx_bytes_ = 0;
    }

private:
    std::map<std::string, MemValue> table_;
    size_t approx_bytes_ = 0;
};

}  // namespace clomdb
