#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include "clomdb/slice.h"
#include "clomdb/status.h"

namespace clomdb {

class WriteAheadLog {
public:
    WriteAheadLog() = default;
    ~WriteAheadLog();

    WriteAheadLog(const WriteAheadLog&) = delete;
    WriteAheadLog& operator=(const WriteAheadLog&) = delete;

    Status Open(const std::string& path);

    Status AddRecord(const Slice& data, bool sync);

    Status Sync();
    Status Close();

    static Status ReplayAll(const std::string& path,
                             const std::function<void(const Slice&)>& callback);

private:
    std::FILE* file_ = nullptr;
    std::string path_;
};

}
