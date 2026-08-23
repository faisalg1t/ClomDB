#pragma once
#include <cstdint>

namespace clomdb {

constexpr uint64_t kSSTableMagic = 0x436c6f6d44420001ULL;
constexpr size_t kSSTableFooterSize = 5 * sizeof(uint64_t);

enum class EntryType : uint8_t { kPut = 1, kDelete = 2 };

}
