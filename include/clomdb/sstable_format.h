#pragma once
#include <cstdint>

namespace clomdb {

// ClomDB SSTable file layout (all multi-byte integers little-endian):
//
//   [Data section]
//     repeated Entry:
//       1 byte     type          (1 = Put, 2 = Delete)
//       varint32   key_len
//       key_len B  key
//       varint32   value_len     (present only if type == Put; else 0 and omitted)
//       value_len B value
//
//   [Index section]
//     repeated IndexEntry, one per data Entry, in the same sorted order:
//       varint32   key_len
//       key_len B  key
//       fixed64    data_offset   (byte offset of this entry within the file)
//
//   [Filter section]
//     raw bytes of a BloomFilter::Build() blob covering every key in the table
//
//   [Footer] (fixed 40 bytes, at the very end of the file)
//       fixed64    index_offset
//       fixed64    index_size
//       fixed64    filter_offset
//       fixed64    filter_size
//       fixed64    magic         (kSSTableMagic)
//
// Keeping a full (non-sparse) index in memory per open table trades some
// memory for a much simpler, obviously-correct implementation; it's a
// reasonable trade at the scale a single-node embedded store operates at,
// and is called out explicitly rather than left as an accident.

constexpr uint64_t kSSTableMagic = 0x436c6f6d44420001ULL;  // "ClomDB" + version
constexpr size_t kSSTableFooterSize = 5 * sizeof(uint64_t);

enum class EntryType : uint8_t { kPut = 1, kDelete = 2 };

}  // namespace clomdb
