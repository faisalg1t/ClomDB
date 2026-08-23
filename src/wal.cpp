#include "clomdb/wal.h"
#include <cerrno>
#include <cstring>
#include <vector>
#include "clomdb/coding.h"
#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace clomdb {

WriteAheadLog::~WriteAheadLog() {
    if (file_ != nullptr) {
        std::fclose(file_);
    }
}

Status WriteAheadLog::Open(const std::string& path) {
    path_ = path;
    file_ = std::fopen(path.c_str(), "ab");
    if (file_ == nullptr) {
        return Status::IOError(path + ": " + std::strerror(errno));
    }
    return Status::OK();
}

Status WriteAheadLog::AddRecord(const Slice& data, bool sync) {
    if (file_ == nullptr) return Status::IOError("WAL not open");

    std::string framed;
    framed.reserve(8 + data.size());
    PutFixed32(&framed, static_cast<uint32_t>(data.size()));
    framed.append(data.data(), data.size());
    uint32_t crc = CRC32C::Mask(CRC32C::Value(framed.data(), framed.size()));

    std::string record;
    record.reserve(4 + framed.size());
    PutFixed32(&record, crc);
    record.append(framed);

    size_t written = std::fwrite(record.data(), 1, record.size(), file_);
    if (written != record.size()) {
        return Status::IOError(path_ + ": short write to WAL");
    }
    if (sync) {
        return Sync();
    }
    if (std::fflush(file_) != 0) {
        return Status::IOError(path_ + ": fflush failed: " + std::strerror(errno));
    }
    return Status::OK();
}

Status WriteAheadLog::Sync() {
    if (file_ == nullptr) return Status::IOError("WAL not open");
    if (std::fflush(file_) != 0) {
        return Status::IOError(path_ + ": fflush failed: " + std::strerror(errno));
    }
#if defined(_WIN32)
#else
    if (fsync(fileno(file_)) != 0) {
        return Status::IOError(path_ + ": fsync failed: " + std::strerror(errno));
    }
#endif
    return Status::OK();
}

Status WriteAheadLog::Close() {
    if (file_ == nullptr) return Status::OK();
    Status s = Sync();
    if (std::fclose(file_) != 0 && s.ok()) {
        s = Status::IOError(path_ + ": fclose failed: " + std::strerror(errno));
    }
    file_ = nullptr;
    return s;
}

Status WriteAheadLog::ReplayAll(const std::string& path,
                                 const std::function<void(const Slice&)>& callback) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        if (errno == ENOENT) return Status::OK();
        return Status::IOError(path + ": " + std::strerror(errno));
    }

    std::vector<char> buf;
    for (;;) {
        char header[8];
        size_t got = std::fread(header, 1, 8, f);
        if (got < 8) break;

        uint32_t stored_crc = CRC32C::Unmask(DecodeFixed32(header));
        uint32_t length = DecodeFixed32(header + 4);

        buf.resize(length);
        if (length > 0) {
            size_t payload_got = std::fread(buf.data(), 1, length, f);
            if (payload_got < length) break;
        }

        std::string framed;
        framed.reserve(4 + length);
        PutFixed32(&framed, length);
        framed.append(buf.data(), length);
        uint32_t computed_crc = CRC32C::Value(framed.data(), framed.size());
        if (computed_crc != stored_crc) {
            break;
        }

        callback(Slice(buf.data(), length));
    }
    std::fclose(f);
    return Status::OK();
}

}
