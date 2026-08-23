#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "clomdb/slice.h"

namespace clomdb {

inline uint32_t BloomHash(const Slice& key) {
    // Murmur2-style hash (public domain algorithm), matches the classic
    // LevelDB bloom filter hash so the double-hashing trick below spreads
    // bits well in practice.
    const uint32_t seed = 0xbc9f1d34;
    const uint32_t m = 0xc6a4a793;
    uint32_t h = seed ^ static_cast<uint32_t>(key.size() * m);
    const char* data = key.data();
    size_t n = key.size();
    while (n >= 4) {
        uint32_t w = static_cast<uint8_t>(data[0]) | (static_cast<uint8_t>(data[1]) << 8) |
                     (static_cast<uint8_t>(data[2]) << 16) | (static_cast<uint8_t>(data[3]) << 24);
        h += w;
        h *= m;
        h ^= (h >> 16);
        data += 4;
        n -= 4;
    }
    switch (n) {
        case 3: h += static_cast<uint8_t>(data[2]) << 16; [[fallthrough]];
        case 2: h += static_cast<uint8_t>(data[1]) << 8; [[fallthrough]];
        case 1:
            h += static_cast<uint8_t>(data[0]);
            h *= m;
            h ^= (h >> 24);
            break;
    }
    return h;
}

// A classic Bloom filter, built once from a batch of keys (used per
// SSTable to skip disk reads for keys that definitely aren't present).
class BloomFilter {
public:
    static std::string Build(const std::vector<std::string>& keys, int bits_per_key) {
        int k = static_cast<int>(bits_per_key * 0.69);  // ln(2)
        if (k < 1) k = 1;
        if (k > 30) k = 30;

        size_t bits = keys.size() * static_cast<size_t>(bits_per_key);
        if (bits < 64) bits = 64;
        size_t bytes = (bits + 7) / 8;
        bits = bytes * 8;

        std::string filter(bytes, static_cast<char>(0));
        filter.push_back(static_cast<char>(k));  // store k as trailing byte

        for (const auto& key : keys) {
            uint32_t h = BloomHash(key);
            const uint32_t delta = (h >> 17) | (h << 15);  // rotate for 2nd hash
            for (int j = 0; j < k; j++) {
                uint32_t bitpos = h % bits;
                filter[bitpos / 8] |= static_cast<char>(1 << (bitpos % 8));
                h += delta;
            }
        }
        return filter;
    }

    static bool MayContain(const Slice& filter, const Slice& key) {
        size_t len = filter.size();
        if (len < 1) return true;  // malformed filter: fail open
        size_t bytes = len - 1;
        size_t bits = bytes * 8;
        if (bits == 0) return true;
        int k = static_cast<uint8_t>(filter[len - 1]);
        if (k > 30) return true;  // treat as "no filter" for forward-compat

        uint32_t h = BloomHash(key);
        const uint32_t delta = (h >> 17) | (h << 15);
        for (int j = 0; j < k; j++) {
            uint32_t bitpos = h % bits;
            if ((static_cast<uint8_t>(filter[bitpos / 8]) & (1 << (bitpos % 8))) == 0) {
                return false;
            }
            h += delta;
        }
        return true;
    }
};

}  // namespace clomdb
