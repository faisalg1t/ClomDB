#pragma once
#include <cstring>
#include <string>
#include <string_view>

namespace clomdb {

// A Slice is a simple, non-owning view over a byte array. Cheap to copy;
// the caller must ensure the underlying storage outlives the Slice.
class Slice {
public:
    Slice() : data_(""), size_(0) {}
    Slice(const char* d, size_t n) : data_(d), size_(n) {}
    Slice(const std::string& s) : data_(s.data()), size_(s.size()) {}
    Slice(const char* s) : data_(s), size_(strlen(s)) {}

    const char* data() const { return data_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    char operator[](size_t n) const { return data_[n]; }

    std::string ToString() const { return std::string(data_, size_); }
    std::string_view ToStringView() const { return std::string_view(data_, size_); }

    int compare(const Slice& b) const {
        const size_t min_len = size_ < b.size_ ? size_ : b.size_;
        int r = memcmp(data_, b.data_, min_len);
        if (r == 0) {
            if (size_ < b.size_) r = -1;
            else if (size_ > b.size_) r = 1;
        }
        return r;
    }

    bool starts_with(const Slice& x) const {
        return size_ >= x.size_ && memcmp(data_, x.data_, x.size_) == 0;
    }

private:
    const char* data_;
    size_t size_;
};

inline bool operator==(const Slice& a, const Slice& b) {
    return a.size() == b.size() && memcmp(a.data(), b.data(), a.size()) == 0;
}
inline bool operator!=(const Slice& a, const Slice& b) { return !(a == b); }
inline bool operator<(const Slice& a, const Slice& b) { return a.compare(b) < 0; }

}  // namespace clomdb
