#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include "clomdb/slice.h"
#include "clomdb/status.h"

namespace clomdb {

// WriteAheadLog appends length+CRC framed records to a single append-only
// file so that writes acknowledged to the caller survive a crash. Record
// framing on disk:
//   [4-byte masked CRC32C of (length ++ payload)][4-byte length][payload]
// A short/corrupt trailing record (from a crash mid-write) is detected and
// the log is truncated at the last valid record boundary during recovery;
// it is not treated as a fatal error, matching how real WALs handle torn
// writes at the tail.
class WriteAheadLog {
public:
    WriteAheadLog() = default;
    ~WriteAheadLog();

    WriteAheadLog(const WriteAheadLog&) = delete;
    WriteAheadLog& operator=(const WriteAheadLog&) = delete;

    // Opens (creating if necessary) the log file for appending.
    Status Open(const std::string& path);

    // Appends one record (typically a serialized WriteBatch). Durable on
    // disk once this returns OK if `sync` is true; otherwise durable once
    // Sync() or Close() is subsequently called (or the OS flushes on its
    // own -- i.e. survives process crash but not OS/power crash).
    Status AddRecord(const Slice& data, bool sync);

    Status Sync();
    Status Close();

    // Reads every valid record from `path` in order, invoking
    // callback(record) for each. Stops cleanly (without error) at the
    // first incomplete/corrupt trailing record, since that indicates a
    // torn write from a crash mid-append rather than genuine corruption
    // of already-fsynced data.
    static Status ReplayAll(const std::string& path,
                             const std::function<void(const Slice&)>& callback);

private:
    std::FILE* file_ = nullptr;
    std::string path_;
};

}  // namespace clomdb
