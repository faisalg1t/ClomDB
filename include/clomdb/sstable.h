#pragma once
#include <optional>
#include <string>
#include <vector>
#include "clomdb/options.h"
#include "clomdb/sstable_format.h"
#include "clomdb/slice.h"
#include "clomdb/status.h"

namespace clomdb {

struct SSTableEntry {
    std::string key;
    EntryType type;
    std::string value;  // empty for kDelete
};

// Writes a single immutable, sorted SSTable file from an already
// key-sorted, already-deduplicated sequence of entries.
class SSTableWriter {
public:
    static Status Write(const std::string& path, const std::vector<SSTableEntry>& entries,
                         int bloom_bits_per_key);
};

// Opens and serves reads against one immutable SSTable file. Loads the
// full index and bloom filter into memory on Open(); data entries are
// read from disk on demand.
class SSTableReader {
public:
    SSTableReader() = default;
    ~SSTableReader();

    SSTableReader(const SSTableReader&) = delete;
    SSTableReader& operator=(const SSTableReader&) = delete;

    Status Open(const std::string& path);

    // Point lookup. Returns true if the key is present (live or as a
    // tombstone) in this table. If live, *value and *deleted=false; if a
    // tombstone, *deleted=true.
    bool Get(const Slice& key, std::string* value, bool* deleted, const ReadOptions& opts) const;

    // Decodes and returns every entry in the table, in sorted key order.
    // Used by compaction and full-table scans.
    Status ReadAll(std::vector<SSTableEntry>* out) const;

    const std::string& min_key() const { return min_key_; }
    const std::string& max_key() const { return max_key_; }
    size_t file_size() const { return file_size_; }
    size_t entry_count() const { return index_.size(); }
    const std::string& path() const { return path_; }

private:
    struct IndexEntry {
        std::string key;
        uint64_t offset;
    };

    std::string path_;
    std::vector<IndexEntry> index_;
    std::string filter_;
    std::string min_key_;
    std::string max_key_;
    size_t file_size_ = 0;
    void* file_handle_ = nullptr;  // FILE*, opaque here to keep <cstdio> out of the header
};

}  // namespace clomdb
