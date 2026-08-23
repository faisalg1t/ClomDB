#pragma once
#include <map>
#include <string>
#include "clomdb/slice.h"
#include "clomdb/status.h"

namespace clomdb {

class Iterator {
public:
    explicit Iterator(std::map<std::string, std::string> snapshot)
        : snapshot_(std::move(snapshot)), it_(snapshot_.begin()) {}

    void SeekToFirst() { it_ = snapshot_.begin(); }
    void Seek(const Slice& target) { it_ = snapshot_.lower_bound(target.ToString()); }
    bool Valid() const { return it_ != snapshot_.end(); }
    void Next() {
        if (Valid()) ++it_;
    }
    Slice key() const { return it_->first; }
    Slice value() const { return it_->second; }
    Status status() const { return Status::OK(); }

private:
    std::map<std::string, std::string> snapshot_;
    std::map<std::string, std::string>::const_iterator it_;
};

}
