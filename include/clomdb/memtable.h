#pragma once
#include <map>
#include <string>
#include "clomdb/slice.h"

namespace clomdb {

struct MemValue {
    bool deleted = false;
    std::string value;
};

class MemTable {
public:
    MemTable() = default;

    void Put(const Slice& key, const Slice& value) {
        MemValue& mv = table_[key.ToString()];
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

}
