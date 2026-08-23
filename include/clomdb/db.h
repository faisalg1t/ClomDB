#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "clomdb/iterator.h"
#include "clomdb/memtable.h"
#include "clomdb/options.h"
#include "clomdb/slice.h"
#include "clomdb/status.h"
#include "clomdb/version.h"
#include "clomdb/wal.h"
#include "clomdb/write_batch.h"

namespace clomdb {

// ClomDB: an embedded, durable, LSM-tree-backed key-value store.
//
//   Options options;
//   DB* db;
//   Status s = DB::Open(options, "/path/to/db", &db);
//   db->Put(WriteOptions(), "key", "value");
//   std::string value;
//   db->Get(ReadOptions(), "key", &value);
//   delete db;
//
// Thread-safety: a single DB instance may be shared across threads; all
// public methods are internally synchronized.
class DB {
public:
    static Status Open(const Options& options, const std::string& db_path, DB** dbptr);

    ~DB();
    DB(const DB&) = delete;
    DB& operator=(const DB&) = delete;

    Status Put(const WriteOptions& opts, const Slice& key, const Slice& value);
    Status Delete(const WriteOptions& opts, const Slice& key);
    Status Write(const WriteOptions& opts, const WriteBatch& batch);
    Status Get(const ReadOptions& opts, const Slice& key, std::string* value);

    // Materializes a point-in-time snapshot iterator over the whole
    // keyspace. See Iterator's class comment for its cost model.
    std::unique_ptr<Iterator> NewIterator(const ReadOptions& opts);

    // Flushes the active memtable (if non-empty) to an SSTable and waits
    // for any in-flight background compaction to settle. Safe to call
    // repeatedly; a no-op if there's nothing to flush.
    Status Flush();

    // Blocks until the background thread has finished, then closes the
    // WAL and persists the manifest. Called automatically by the
    // destructor if not called explicitly; safe to call more than once.
    Status Close();

    struct Stats {
        size_t memtable_entries = 0;
        size_t memtable_bytes = 0;
        size_t files_per_level[kMaxLevels] = {0};
        size_t bytes_per_level[kMaxLevels] = {0};
    };
    Stats GetStats();

private:
    DB() = default;

    // --- helpers, all assume mutex_ is held unless noted otherwise ---
    Status ApplyBatchLocked(const WriteBatch& batch);
    Status MaybeFlushLocked();  // may trigger FlushMemTableLocked()
    Status FlushMemTableLocked();
    bool NeedsCompactionLocked(int* level_to_compact) const;
    Status CompactLocked(int level);  // merges `level` and `level+1` into new `level+1` files
    std::string SSTablePath(uint64_t number) const;
    std::string WalPath() const { return db_path_ + "/CURRENT.wal"; }
    std::string ManifestPath() const { return db_path_ + "/MANIFEST"; }

    void BackgroundLoop();
    void MaybeWakeBackgroundThread();

    std::string db_path_;
    Options options_;

    std::mutex mutex_;
    MemTable memtable_;
    std::unique_ptr<WriteAheadLog> wal_;
    Version version_;

    // level -> list of open readers, index-aligned with version_.Level(level)
    std::vector<std::vector<std::shared_ptr<class SSTableReader>>> open_readers_;

    std::thread bg_thread_;
    std::condition_variable bg_cv_;
    bool bg_work_requested_ = false;
    bool shutting_down_ = false;
    bool bg_idle_ = true;
    std::condition_variable bg_idle_cv_;
    bool closed_ = false;
};

}  // namespace clomdb
