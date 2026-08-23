#pragma once
#include <string>
#include "clomdb/coding.h"
#include "clomdb/slice.h"

namespace clomdb {

enum class RecordType : uint8_t { kPut = 1, kDelete = 2 };

class WriteBatch {
public:
    WriteBatch() = default;

    void Put(const Slice& key, const Slice& value) {
        rep_.push_back(static_cast<char>(RecordType::kPut));
        PutLengthPrefixedSlice(&rep_, key);
        PutLengthPrefixedSlice(&rep_, value);
        count_++;
    }

    void Delete(const Slice& key) {
        rep_.push_back(static_cast<char>(RecordType::kDelete));
        PutLengthPrefixedSlice(&rep_, key);
        count_++;
    }

    void Clear() {
        rep_.clear();
        count_ = 0;
    }

    size_t Count() const { return count_; }
    bool empty() const { return count_ == 0; }
    const std::string& Data() const { return rep_; }
    void SetData(std::string d) { rep_ = std::move(d); }

    template <typename Handler>
    Status Iterate(Handler* handler) const {
        Slice input(rep_);
        size_t seen = 0;
        while (!input.empty()) {
            char tag = input[0];
            input = Slice(input.data() + 1, input.size() - 1);
            Slice key, value;
            if (tag == static_cast<char>(RecordType::kPut)) {
                if (!GetLengthPrefixedSlice(&input, &key) ||
                    !GetLengthPrefixedSlice(&input, &value)) {
                    return Status::Corruption("malformed WriteBatch (put)");
                }
                handler->Put(key, value);
            } else if (tag == static_cast<char>(RecordType::kDelete)) {
                if (!GetLengthPrefixedSlice(&input, &key)) {
                    return Status::Corruption("malformed WriteBatch (delete)");
                }
                handler->Delete(key);
            } else {
                return Status::Corruption("unknown WriteBatch tag");
            }
            seen++;
        }
        if (seen != count_) return Status::Corruption("WriteBatch count mismatch");
        return Status::OK();
    }

private:
    std::string rep_;
    size_t count_ = 0;
};

}
