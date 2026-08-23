#include "clomdb/db.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include "clomdb/sstable.h"

namespace fs = std::filesystem;

namespace clomdb {

namespace {

struct MemTableHandler {
    MemTable* mt;
    void Put(const Slice& k, const Slice& v) { mt->Put(k, v); }
    void Delete(const Slice& k) { mt->Delete(k); }
};

std::string ZeroPad(uint64_t n) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%012llu", static_cast<unsigned long long>(n));
    return std::string(buf);
}

constexpr size_t kMinCompactionOutputFileBytes = 2 * 1024 * 1024;

}

std::string DB::SSTablePath(uint64_t number) const {
    return db_path_ + "/" + ZeroPad(number) + ".sst";
}

Status DB::Open(const Options& options, const std::string& db_path, DB** dbptr) {
    *dbptr = nullptr;
    std::error_code ec;
    bool existed = fs::exists(db_path, ec);
    if (!existed) {
        if (!options.create_if_missing) {
            return Status::InvalidArgument(db_path + ": does not exist and create_if_missing is false");
        }
        if (!fs::create_directories(db_path, ec) && ec) {
            return Status::IOError(db_path + ": failed to create directory: " + ec.message());
        }
    } else if (options.error_if_exists) {
        return Status::AlreadyExists(db_path);
    }

    std::unique_ptr<DB> db(new DB());
    db->db_path_ = db_path;
    db->options_ = options;
    db->open_readers_.resize(kMaxLevels);

    Status s = db->version_.Load(db->ManifestPath());
    if (!s.ok() && !s.IsNotFound()) {
        return s;
    }

    {
        std::set<std::string> live;
        for (int lvl = 0; lvl < db->version_.NumLevels(); lvl++) {
            for (const auto& fm : db->version_.Level(lvl)) {
                live.insert(db->SSTablePath(fm.number));
            }
        }
        std::error_code it_ec;
        if (fs::exists(db_path, it_ec)) {
            for (const auto& entry : fs::directory_iterator(db_path, it_ec)) {
                if (entry.path().extension() == ".sst" && live.find(entry.path().string()) == live.end()) {
                    std::error_code rm_ec;
                    fs::remove(entry.path(), rm_ec);
                }
            }
        }
    }

    for (int lvl = 0; lvl < db->version_.NumLevels(); lvl++) {
        for (const auto& fm : db->version_.Level(lvl)) {
            auto reader = std::make_shared<SSTableReader>();
            Status rs = reader->Open(db->SSTablePath(fm.number));
            if (!rs.ok()) return rs;
            db->open_readers_[lvl].push_back(reader);
        }
    }

    s = WriteAheadLog::ReplayAll(db->WalPath(), [&](const Slice& record) {
        WriteBatch batch;
        batch.SetData(record.ToString());
        MemTableHandler handler{&db->memtable_};
        batch.Iterate(&handler);
    });
    if (!s.ok()) return s;

    db->wal_ = std::make_unique<WriteAheadLog>();
    s = db->wal_->Open(db->WalPath());
    if (!s.ok()) return s;

    if (!existed) {
        s = db->version_.Save(db->ManifestPath());
        if (!s.ok()) return s;
    }

    if (options.background_compaction) {
        db->bg_thread_ = std::thread([raw = db.get()] { raw->BackgroundLoop(); });
    }

    *dbptr = db.release();
    return Status::OK();
}

DB::~DB() {
    if (!closed_) {
        Close();
    }
}

Status DB::Close() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (closed_) return Status::OK();
    shutting_down_ = true;
    bg_cv_.notify_all();
    lock.unlock();
    if (bg_thread_.joinable()) bg_thread_.join();
    lock.lock();

    Status s = FlushMemTableLocked();
    if (wal_) {
        Status ws = wal_->Close();
        if (s.ok()) s = ws;
    }
    Status vs = version_.Save(ManifestPath());
    if (s.ok()) s = vs;
    closed_ = true;
    return s;
}

Status DB::Put(const WriteOptions& opts, const Slice& key, const Slice& value) {
    WriteBatch b;
    b.Put(key, value);
    return Write(opts, b);
}

Status DB::Delete(const WriteOptions& opts, const Slice& key) {
    WriteBatch b;
    b.Delete(key);
    return Write(opts, b);
}

Status DB::Write(const WriteOptions& opts, const WriteBatch& batch) {
    if (batch.empty()) return Status::OK();
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) return Status::InvalidArgument("DB is closed");

    Status s = wal_->AddRecord(Slice(batch.Data()), opts.sync || options_.sync_writes);
    if (!s.ok()) return s;

    s = ApplyBatchLocked(batch);
    if (!s.ok()) return s;

    if (options_.background_compaction) {
        bg_work_requested_ = true;
        bg_idle_ = false;
        bg_cv_.notify_one();
    } else {
        s = MaybeFlushLocked();
    }
    return s;
}

Status DB::ApplyBatchLocked(const WriteBatch& batch) {
    MemTableHandler handler{&memtable_};
    return batch.Iterate(&handler);
}

Status DB::MaybeFlushLocked() {
    Status s;
    if (memtable_.ApproximateBytes() >= options_.memtable_flush_bytes) {
        s = FlushMemTableLocked();
        if (!s.ok()) return s;
    }
    int level;
    while (NeedsCompactionLocked(&level)) {
        s = CompactLocked(level);
        if (!s.ok()) return s;
    }
    return Status::OK();
}

Status DB::FlushMemTableLocked() {
    if (memtable_.empty()) return Status::OK();

    std::vector<SSTableEntry> entries;
    entries.reserve(memtable_.EntryCount());
    for (const auto& [key, mv] : memtable_.entries()) {
        SSTableEntry e;
        e.key = key;
        if (mv.deleted) {
            e.type = EntryType::kDelete;
        } else {
            e.type = EntryType::kPut;
            e.value = mv.value;
        }
        entries.push_back(std::move(e));
    }

    uint64_t number = version_.next_file_number++;
    std::string path = SSTablePath(number);
    Status s = SSTableWriter::Write(path, entries, options_.bloom_bits_per_key);
    if (!s.ok()) return s;

    FileMetaData fm;
    fm.number = number;
    fm.min_key = entries.front().key;
    fm.max_key = entries.back().key;
    std::error_code ec;
    fm.file_size = fs::file_size(path, ec);

    auto reader = std::make_shared<SSTableReader>();
    s = reader->Open(path);
    if (!s.ok()) return s;

    version_.MutableLevel(0).push_back(fm);
    open_readers_[0].push_back(reader);

    s = version_.Save(ManifestPath());
    if (!s.ok()) return s;

    memtable_.Clear();

    s = wal_->Close();
    if (!s.ok()) return s;
    std::remove(WalPath().c_str());
    wal_ = std::make_unique<WriteAheadLog>();
    return wal_->Open(WalPath());
}

bool DB::NeedsCompactionLocked(int* level_to_compact) const {
    if (static_cast<int>(version_.Level(0).size()) >= options_.l0_compaction_trigger) {
        *level_to_compact = 0;
        return true;
    }
    size_t target = options_.level1_target_bytes;
    for (int lvl = 1; lvl < kMaxLevels - 1; lvl++) {
        size_t total = 0;
        for (const auto& fm : version_.Level(lvl)) total += fm.file_size;
        if (total > target) {
            *level_to_compact = lvl;
            return true;
        }
        target *= static_cast<size_t>(std::max(1, options_.level_size_multiplier));
    }
    return false;
}

Status DB::CompactLocked(int level) {
    int target_level = level + 1;
    if (target_level >= kMaxLevels) return Status::OK();

    std::vector<FileMetaData> level_files = version_.Level(level);
    std::vector<std::shared_ptr<SSTableReader>> level_readers = open_readers_[level];
    if (level == 0) {
        std::vector<size_t> idx(level_files.size());
        for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
            return level_files[a].number > level_files[b].number;
        });
        std::vector<FileMetaData> sorted_files;
        std::vector<std::shared_ptr<SSTableReader>> sorted_readers;
        for (size_t i : idx) {
            sorted_files.push_back(level_files[i]);
            sorted_readers.push_back(level_readers[i]);
        }
        level_files = std::move(sorted_files);
        level_readers = std::move(sorted_readers);
    }

    std::vector<FileMetaData> next_files = version_.Level(target_level);
    std::vector<std::shared_ptr<SSTableReader>> next_readers = open_readers_[target_level];

    std::map<std::string, SSTableEntry> merged;
    for (auto& reader : next_readers) {
        std::vector<SSTableEntry> entries;
        Status s = reader->ReadAll(&entries);
        if (!s.ok()) return s;
        for (auto& e : entries) merged[e.key] = std::move(e);
    }
    for (auto it = level_readers.rbegin(); it != level_readers.rend(); ++it) {
        std::vector<SSTableEntry> entries;
        Status s = (*it)->ReadAll(&entries);
        if (!s.ok()) return s;
        for (auto& e : entries) merged[e.key] = std::move(e);
    }

    bool drop_tombstones = (target_level == kMaxLevels - 1);

    std::vector<FileMetaData> new_files;
    std::vector<std::shared_ptr<SSTableReader>> new_readers;
    size_t target_file_bytes = std::max(options_.memtable_flush_bytes, kMinCompactionOutputFileBytes);

    std::vector<SSTableEntry> batch;
    size_t batch_bytes = 0;
    auto flush_batch = [&]() -> Status {
        if (batch.empty()) return Status::OK();
        uint64_t number = version_.next_file_number++;
        std::string path = SSTablePath(number);
        Status s = SSTableWriter::Write(path, batch, options_.bloom_bits_per_key);
        if (!s.ok()) return s;
        FileMetaData fm;
        fm.number = number;
        fm.min_key = batch.front().key;
        fm.max_key = batch.back().key;
        std::error_code ec;
        fm.file_size = fs::file_size(path, ec);
        auto reader = std::make_shared<SSTableReader>();
        s = reader->Open(path);
        if (!s.ok()) return s;
        new_files.push_back(fm);
        new_readers.push_back(reader);
        batch.clear();
        batch_bytes = 0;
        return Status::OK();
    };

    for (auto& [key, entry] : merged) {
        if (drop_tombstones && entry.type == EntryType::kDelete) continue;
        batch_bytes += entry.key.size() + entry.value.size();
        batch.push_back(std::move(entry));
        if (batch_bytes >= target_file_bytes) {
            Status s = flush_batch();
            if (!s.ok()) return s;
        }
    }
    Status s = flush_batch();
    if (!s.ok()) return s;

    std::vector<uint64_t> old_numbers;
    for (const auto& fm : version_.Level(level)) old_numbers.push_back(fm.number);
    for (const auto& fm : version_.Level(target_level)) old_numbers.push_back(fm.number);

    version_.MutableLevel(level).clear();
    version_.MutableLevel(target_level) = new_files;
    open_readers_[level].clear();
    open_readers_[target_level] = new_readers;

    s = version_.Save(ManifestPath());
    if (!s.ok()) return s;

    for (uint64_t number : old_numbers) {
        std::remove(SSTablePath(number).c_str());
    }
    return Status::OK();
}

Status DB::Get(const ReadOptions& opts, const Slice& key, std::string* value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) return Status::InvalidArgument("DB is closed");

    bool deleted = false;
    if (memtable_.Get(key, value, &deleted)) {
        return deleted ? Status::NotFound(key.ToString()) : Status::OK();
    }

    std::vector<std::pair<uint64_t, std::shared_ptr<SSTableReader>>> l0;
    const auto& l0_meta = version_.Level(0);
    for (size_t i = 0; i < l0_meta.size(); i++) {
        l0.emplace_back(l0_meta[i].number, open_readers_[0][i]);
    }
    std::sort(l0.begin(), l0.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (auto& [num, reader] : l0) {
        if (reader->Get(key, value, &deleted, opts)) {
            return deleted ? Status::NotFound(key.ToString()) : Status::OK();
        }
    }

    for (int lvl = 1; lvl < kMaxLevels; lvl++) {
        const auto& metas = version_.Level(lvl);
        for (size_t i = 0; i < metas.size(); i++) {
            if (Slice(key) < Slice(metas[i].min_key) || Slice(metas[i].max_key) < Slice(key)) continue;
            if (open_readers_[lvl][i]->Get(key, value, &deleted, opts)) {
                return deleted ? Status::NotFound(key.ToString()) : Status::OK();
            }
        }
    }

    return Status::NotFound(key.ToString());
}

std::unique_ptr<Iterator> DB::NewIterator(const ReadOptions& /*opts*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<std::string, std::string> snapshot;

    auto apply_entries = [&](const std::vector<SSTableEntry>& entries) {
        for (const auto& e : entries) {
            if (e.type == EntryType::kDelete) {
                snapshot.erase(e.key);
            } else {
                snapshot[e.key] = e.value;
            }
        }
    };

    for (int lvl = kMaxLevels - 1; lvl >= 1; lvl--) {
        for (auto& reader : open_readers_[lvl]) {
            std::vector<SSTableEntry> entries;
            if (reader->ReadAll(&entries).ok()) apply_entries(entries);
        }
    }
    {
        std::vector<std::pair<uint64_t, std::shared_ptr<SSTableReader>>> l0;
        const auto& l0_meta = version_.Level(0);
        for (size_t i = 0; i < l0_meta.size(); i++) l0.emplace_back(l0_meta[i].number, open_readers_[0][i]);
        std::sort(l0.begin(), l0.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& [num, reader] : l0) {
            std::vector<SSTableEntry> entries;
            if (reader->ReadAll(&entries).ok()) apply_entries(entries);
        }
    }
    for (const auto& [key, mv] : memtable_.entries()) {
        if (mv.deleted) {
            snapshot.erase(key);
        } else {
            snapshot[key] = mv.value;
        }
    }

    return std::make_unique<Iterator>(std::move(snapshot));
}

Status DB::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    Status s = FlushMemTableLocked();
    if (!s.ok()) return s;
    int level;
    while (NeedsCompactionLocked(&level)) {
        s = CompactLocked(level);
        if (!s.ok()) return s;
    }
    return Status::OK();
}

DB::Stats DB::GetStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats stats;
    stats.memtable_entries = memtable_.EntryCount();
    stats.memtable_bytes = memtable_.ApproximateBytes();
    for (int lvl = 0; lvl < kMaxLevels; lvl++) {
        stats.files_per_level[lvl] = version_.Level(lvl).size();
        size_t bytes = 0;
        for (const auto& fm : version_.Level(lvl)) bytes += fm.file_size;
        stats.bytes_per_level[lvl] = bytes;
    }
    return stats;
}

void DB::BackgroundLoop() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        bg_cv_.wait(lock, [&] { return shutting_down_ || bg_work_requested_; });
        if (shutting_down_) return;
        bg_work_requested_ = false;

        Status s = FlushMemTableLocked();
        int level;
        while (s.ok() && NeedsCompactionLocked(&level)) {
            s = CompactLocked(level);
        }

        bg_idle_ = true;
        bg_idle_cv_.notify_all();
    }
}

}
