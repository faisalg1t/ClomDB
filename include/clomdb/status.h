#pragma once
#include <cstdint>
#include <string>
#include <utility>

namespace clomdb {

class Status {
public:
    enum class Code : uint8_t {
        kOk = 0,
        kNotFound,
        kCorruption,
        kIOError,
        kInvalidArgument,
        kNotSupported,
        kAlreadyExists,
    };

    Status() noexcept : code_(Code::kOk) {}

    static Status OK() { return Status(); }
    static Status NotFound(std::string msg) { return Status(Code::kNotFound, std::move(msg)); }
    static Status Corruption(std::string msg) { return Status(Code::kCorruption, std::move(msg)); }
    static Status IOError(std::string msg) { return Status(Code::kIOError, std::move(msg)); }
    static Status InvalidArgument(std::string msg) { return Status(Code::kInvalidArgument, std::move(msg)); }
    static Status NotSupported(std::string msg) { return Status(Code::kNotSupported, std::move(msg)); }
    static Status AlreadyExists(std::string msg) { return Status(Code::kAlreadyExists, std::move(msg)); }

    bool ok() const { return code_ == Code::kOk; }
    bool IsNotFound() const { return code_ == Code::kNotFound; }
    bool IsCorruption() const { return code_ == Code::kCorruption; }
    bool IsIOError() const { return code_ == Code::kIOError; }

    Code code() const { return code_; }

    std::string ToString() const {
        if (ok()) return "OK";
        static const char* names[] = {"OK", "NotFound", "Corruption", "IOError",
                                       "InvalidArgument", "NotSupported", "AlreadyExists"};
        return std::string(names[static_cast<int>(code_)]) + ": " + msg_;
    }

private:
    Status(Code c, std::string msg) : code_(c), msg_(std::move(msg)) {}
    Code code_;
    std::string msg_;
};

}
