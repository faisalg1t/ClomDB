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

    std::unique_ptr<Iterator> NewIterator(const ReadOptions& opts);

    Status Flush();

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

    Status ApplyBatchLocked(const WriteBatch& batch);
    Status MaybeFlushLocked();
    Status FlushMemTableLocked();
    bool NeedsCompactionLocked(int* level_to_compact) const;
    Status CompactLocked(int level);
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

    std::vector<std::vector<std::shared_ptr<class SSTableReader>>> open_readers_;

    std::thread bg_thread_;
    std::condition_variable bg_cv_;
    bool bg_work_requested_ = false;
    bool shutting_down_ = false;
    bool bg_idle_ = true;
    std::condition_variable bg_idle_cv_;
    bool closed_ = false;
};

}
