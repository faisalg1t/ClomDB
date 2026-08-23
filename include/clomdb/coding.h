#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include "clomdb/slice.h"

namespace clomdb {


inline void PutFixed32(std::string* dst, uint32_t v) {
    char buf[4];
    memcpy(buf, &v, sizeof(v));
    dst->append(buf, sizeof(buf));
}

inline void PutFixed64(std::string* dst, uint64_t v) {
    char buf[8];
    memcpy(buf, &v, sizeof(v));
    dst->append(buf, sizeof(buf));
}

inline uint32_t DecodeFixed32(const char* p) {
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

inline uint64_t DecodeFixed64(const char* p) {
    uint64_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}


inline void PutVarint32(std::string* dst, uint32_t v) {
    unsigned char buf[5];
    int n = 0;
    while (v >= 0x80) {
        buf[n++] = static_cast<unsigned char>(v) | 0x80;
        v >>= 7;
    }
    buf[n++] = static_cast<unsigned char>(v);
    dst->append(reinterpret_cast<char*>(buf), n);
}

inline void PutVarint64(std::string* dst, uint64_t v) {
    unsigned char buf[10];
    int n = 0;
    while (v >= 0x80) {
        buf[n++] = static_cast<unsigned char>(v) | 0x80;
        v >>= 7;
    }
    buf[n++] = static_cast<unsigned char>(v);
    dst->append(reinterpret_cast<char*>(buf), n);
}

inline void PutLengthPrefixedSlice(std::string* dst, const Slice& value) {
    PutVarint32(dst, static_cast<uint32_t>(value.size()));
    dst->append(value.data(), value.size());
}

inline const char* GetVarint32Ptr(const char* p, const char* limit, uint32_t* value) {
    uint32_t result = 0;
    for (int shift = 0; shift <= 28 && p < limit; shift += 7) {
        uint32_t byte = static_cast<unsigned char>(*p++);
        if (byte & 0x80) {
            result |= ((byte & 0x7f) << shift);
        } else {
            result |= (byte << shift);
            *value = result;
            return p;
        }
    }
    return nullptr;
}

inline const char* GetVarint64Ptr(const char* p, const char* limit, uint64_t* value) {
    uint64_t result = 0;
    for (int shift = 0; shift <= 63 && p < limit; shift += 7) {
        uint64_t byte = static_cast<unsigned char>(*p++);
        if (byte & 0x80) {
            result |= ((byte & 0x7f) << shift);
        } else {
            result |= (byte << shift);
            *value = result;
            return p;
        }
    }
    return nullptr;
}

inline bool GetLengthPrefixedSlice(Slice* input, Slice* result) {
    uint32_t len;
    const char* p = GetVarint32Ptr(input->data(), input->data() + input->size(), &len);
    if (p == nullptr || static_cast<size_t>(p - input->data()) + len > input->size()) {
        return false;
    }
    *result = Slice(p, len);
    *input = Slice(p + len, input->size() - (p - input->data()) - len);
    return true;
}

class CRC32C {
public:
    static uint32_t Extend(uint32_t crc, const char* data, size_t n) {
        static const uint32_t* table = Table();
        uint32_t c = crc ^ 0xFFFFFFFFu;
        const unsigned char* p = reinterpret_cast<const unsigned char*>(data);
        for (size_t i = 0; i < n; i++) {
            c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
        }
        return c ^ 0xFFFFFFFFu;
    }
    static uint32_t Value(const char* data, size_t n) { return Extend(0, data, n); }

    static uint32_t Mask(uint32_t crc) {
        return ((crc >> 15) | (crc << 17)) + 0xa282ead8u;
    }
    static uint32_t Unmask(uint32_t masked) {
        uint32_t rot = masked - 0xa282ead8u;
        return (rot >> 17) | (rot << 15);
    }

private:
    static const uint32_t* Table() {
        static uint32_t table[256];
        static bool init = [] {
            for (uint32_t n = 0; n < 256; n++) {
                uint32_t c = n;
                for (int k = 0; k < 8; k++) {
                    c = (c & 1) ? (0x82F63B78u ^ (c >> 1)) : (c >> 1);
                }
                table[n] = c;
            }
            return true;
        }();
        (void)init;
        return table;
    }
};

}
