#pragma once
#include <map>
#include <string>
#include "clomdb/slice.h"
#include "clomdb/status.h"

namespace clomdb {

// A snapshot-style iterator over the merged view of the database at the
// moment NewIterator() was called. Implemented as a materialized sorted
// view (memtable + every SSTable merged, tombstones dropped) rather than a
// streaming k-way merge -- simpler and fully correct, at the cost of O(DB
// size) memory/time per NewIterator() call. Fine for the workloads an
// embedded single-node store targets; a lazy streaming merge iterator is
// the natural next optimization if scans need to run over datasets much
// larger than available RAM.
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

}  // namespace clomdb
