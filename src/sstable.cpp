#include "clomdb/sstable.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include "clomdb/bloom.h"
#include "clomdb/coding.h"

namespace clomdb {

namespace {

void EncodeEntry(std::string* dst, const SSTableEntry& e) {
    dst->push_back(static_cast<char>(e.type));
    PutVarint32(dst, static_cast<uint32_t>(e.key.size()));
    dst->append(e.key);
    if (e.type == EntryType::kPut) {
        PutVarint32(dst, static_cast<uint32_t>(e.value.size()));
        dst->append(e.value);
    }
}

}  // namespace

Status SSTableWriter::Write(const std::string& path, const std::vector<SSTableEntry>& entries,
                             int bloom_bits_per_key) {
    if (entries.empty()) {
        return Status::InvalidArgument("refusing to write an empty SSTable");
    }
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return Status::IOError(path + ": " + std::strerror(errno));

    std::string data;
    std::vector<std::pair<std::string, uint64_t>> offsets;
    offsets.reserve(entries.size());
    uint64_t offset = 0;
    std::vector<std::string> keys_for_filter;
    keys_for_filter.reserve(entries.size());

    for (const auto& e : entries) {
        offsets.emplace_back(e.key, offset);
        std::string enc;
        EncodeEntry(&enc, e);
        data.append(enc);
        offset += enc.size();
        keys_for_filter.push_back(e.key);
    }

    if (std::fwrite(data.data(), 1, data.size(), f) != data.size()) {
        std::fclose(f);
        return Status::IOError(path + ": short write (data)");
    }

    std::string index;
    for (const auto& [key, off] : offsets) {
        PutVarint32(&index, static_cast<uint32_t>(key.size()));
        index.append(key);
        PutFixed64(&index, off);
    }
    uint64_t index_offset = offset;
    if (std::fwrite(index.data(), 1, index.size(), f) != index.size()) {
        std::fclose(f);
        return Status::IOError(path + ": short write (index)");
    }

    std::string filter = BloomFilter::Build(keys_for_filter, bloom_bits_per_key);
    uint64_t filter_offset = index_offset + index.size();
    if (std::fwrite(filter.data(), 1, filter.size(), f) != filter.size()) {
        std::fclose(f);
        return Status::IOError(path + ": short write (filter)");
    }

    std::string footer;
    PutFixed64(&footer, index_offset);
    PutFixed64(&footer, static_cast<uint64_t>(index.size()));
    PutFixed64(&footer, filter_offset);
    PutFixed64(&footer, static_cast<uint64_t>(filter.size()));
    PutFixed64(&footer, kSSTableMagic);
    if (std::fwrite(footer.data(), 1, footer.size(), f) != footer.size()) {
        std::fclose(f);
        return Status::IOError(path + ": short write (footer)");
    }

    if (std::fflush(f) != 0) {
        std::fclose(f);
        return Status::IOError(path + ": fflush failed");
    }
    std::fclose(f);
    return Status::OK();
}

SSTableReader::~SSTableReader() {
    if (file_handle_ != nullptr) {
        std::fclose(static_cast<std::FILE*>(file_handle_));
    }
}

Status SSTableReader::Open(const std::string& path) {
    path_ = path;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return Status::IOError(path + ": " + std::strerror(errno));

    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return Status::IOError(path + ": fseek failed");
    }
    long sz = std::ftell(f);
    if (sz < static_cast<long>(kSSTableFooterSize)) {
        std::fclose(f);
        return Status::Corruption(path + ": file too small to contain a valid footer");
    }
    file_size_ = static_cast<size_t>(sz);

    if (std::fseek(f, sz - static_cast<long>(kSSTableFooterSize), SEEK_SET) != 0) {
        std::fclose(f);
        return Status::IOError(path + ": fseek failed");
    }
    char footer[kSSTableFooterSize];
    if (std::fread(footer, 1, kSSTableFooterSize, f) != kSSTableFooterSize) {
        std::fclose(f);
        return Status::Corruption(path + ": failed to read footer");
    }
    uint64_t index_offset = DecodeFixed64(footer);
    uint64_t index_size = DecodeFixed64(footer + 8);
    uint64_t filter_offset = DecodeFixed64(footer + 16);
    uint64_t filter_size = DecodeFixed64(footer + 24);
    uint64_t magic = DecodeFixed64(footer + 32);
    if (magic != kSSTableMagic) {
        std::fclose(f);
        return Status::Corruption(path + ": bad magic number, not a ClomDB SSTable");
    }

    std::string index_buf(index_size, '\0');
    if (index_size > 0) {
        if (std::fseek(f, static_cast<long>(index_offset), SEEK_SET) != 0 ||
            std::fread(index_buf.data(), 1, index_size, f) != index_size) {
            std::fclose(f);
            return Status::Corruption(path + ": failed to read index block");
        }
    }
    Slice input(index_buf);
    while (!input.empty()) {
        uint32_t klen;
        const char* p = GetVarint32Ptr(input.data(), input.data() + input.size(), &klen);
        if (p == nullptr || static_cast<size_t>(p - input.data()) + klen + 8 > input.size()) {
            std::fclose(f);
            return Status::Corruption(path + ": malformed index entry");
        }
        std::string key(p, klen);
        uint64_t off = DecodeFixed64(p + klen);
        input = Slice(p + klen + 8, input.size() - (p + klen + 8 - input.data()));
        index_.push_back({key, off});
    }
    if (!index_.empty()) {
        min_key_ = index_.front().key;
        max_key_ = index_.back().key;
    }

    filter_.assign(filter_size, '\0');
    if (filter_size > 0) {
        if (std::fseek(f, static_cast<long>(filter_offset), SEEK_SET) != 0 ||
            std::fread(filter_.data(), 1, filter_size, f) != filter_size) {
            std::fclose(f);
            return Status::Corruption(path + ": failed to read filter block");
        }
    }

    file_handle_ = f;
    return Status::OK();
}

bool SSTableReader::Get(const Slice& key, std::string* value, bool* deleted,
                         const ReadOptions& /*opts*/) const {
    if (!filter_.empty() && !BloomFilter::MayContain(filter_, key)) {
        return false;
    }
    auto it = std::lower_bound(index_.begin(), index_.end(), key,
                                [](const IndexEntry& e, const Slice& k) {
                                    return Slice(e.key) < k;
                                });
    if (it == index_.end() || Slice(it->key) != key) return false;

    std::FILE* f = static_cast<std::FILE*>(file_handle_);
    if (std::fseek(f, static_cast<long>(it->offset), SEEK_SET) != 0) return false;

    unsigned char type_byte;
    if (std::fread(&type_byte, 1, 1, f) != 1) return false;

    char lenbuf[5];
    // Read up to 5 bytes for the varint key length, but we already know
    // the key length equals key.size() worth of bytes following, so just
    // decode the varint properly by reading incrementally.
    uint32_t klen = 0;
    {
        std::string tmp;
        for (int i = 0; i < 5; i++) {
            unsigned char b;
            if (std::fread(&b, 1, 1, f) != 1) return false;
            tmp.push_back(static_cast<char>(b));
            if (!(b & 0x80)) break;
        }
        const char* p = GetVarint32Ptr(tmp.data(), tmp.data() + tmp.size(), &klen);
        if (p == nullptr) return false;
    }
    (void)lenbuf;
    std::string k(klen, '\0');
    if (klen > 0 && std::fread(k.data(), 1, klen, f) != klen) return false;

    if (static_cast<EntryType>(type_byte) == EntryType::kDelete) {
        *deleted = true;
        return true;
    }

    uint32_t vlen = 0;
    {
        std::string tmp;
        for (int i = 0; i < 5; i++) {
            unsigned char b;
            if (std::fread(&b, 1, 1, f) != 1) return false;
            tmp.push_back(static_cast<char>(b));
            if (!(b & 0x80)) break;
        }
        const char* p = GetVarint32Ptr(tmp.data(), tmp.data() + tmp.size(), &vlen);
        if (p == nullptr) return false;
    }
    value->assign(vlen, '\0');
    if (vlen > 0 && std::fread(value->data(), 1, vlen, f) != vlen) return false;

    *deleted = false;
    return true;
}

Status SSTableReader::ReadAll(std::vector<SSTableEntry>* out) const {
    out->clear();
    out->reserve(index_.size());
    std::FILE* f = static_cast<std::FILE*>(file_handle_);
    if (std::fseek(f, 0, SEEK_SET) != 0) return Status::IOError(path_ + ": fseek failed");

    for (size_t i = 0; i < index_.size(); i++) {
        unsigned char type_byte;
        if (std::fread(&type_byte, 1, 1, f) != 1) return Status::Corruption(path_ + ": truncated data section");

        uint32_t klen = 0;
        {
            std::string tmp;
            for (int j = 0; j < 5; j++) {
                unsigned char b;
                if (std::fread(&b, 1, 1, f) != 1) return Status::Corruption(path_ + ": truncated key length");
                tmp.push_back(static_cast<char>(b));
                if (!(b & 0x80)) break;
            }
            const char* p = GetVarint32Ptr(tmp.data(), tmp.data() + tmp.size(), &klen);
            if (p == nullptr) return Status::Corruption(path_ + ": bad varint (key len)");
        }
        std::string key(klen, '\0');
        if (klen > 0 && std::fread(key.data(), 1, klen, f) != klen) {
            return Status::Corruption(path_ + ": truncated key");
        }

        SSTableEntry entry;
        entry.key = key;
        entry.type = static_cast<EntryType>(type_byte);
        if (entry.type == EntryType::kPut) {
            uint32_t vlen = 0;
            std::string tmp;
            for (int j = 0; j < 5; j++) {
                unsigned char b;
                if (std::fread(&b, 1, 1, f) != 1) return Status::Corruption(path_ + ": truncated value length");
                tmp.push_back(static_cast<char>(b));
                if (!(b & 0x80)) break;
            }
            const char* p = GetVarint32Ptr(tmp.data(), tmp.data() + tmp.size(), &vlen);
            if (p == nullptr) return Status::Corruption(path_ + ": bad varint (value len)");
            entry.value.assign(vlen, '\0');
            if (vlen > 0 && std::fread(entry.value.data(), 1, vlen, f) != vlen) {
                return Status::Corruption(path_ + ": truncated value");
            }
        }
        out->push_back(std::move(entry));
    }
    return Status::OK();
}

}  // namespace clomdb
