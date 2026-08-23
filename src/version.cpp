#include "clomdb/version.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include "clomdb/coding.h"

namespace clomdb {

namespace {
constexpr uint64_t kManifestMagic = 0x436c6f6d444d4631ULL;  // "ClomDMF1"
}

Status Version::Save(const std::string& manifest_path) const {
    std::string tmp_path = manifest_path + ".tmp";
    std::FILE* f = std::fopen(tmp_path.c_str(), "wb");
    if (f == nullptr) return Status::IOError(tmp_path + ": " + std::strerror(errno));

    std::string buf;
    PutFixed64(&buf, kManifestMagic);
    PutFixed64(&buf, next_file_number);
    PutFixed32(&buf, static_cast<uint32_t>(kMaxLevels));
    for (int lvl = 0; lvl < kMaxLevels; lvl++) {
        const auto& files = levels_[lvl];
        PutFixed32(&buf, static_cast<uint32_t>(files.size()));
        for (const auto& fm : files) {
            PutFixed64(&buf, fm.number);
            PutFixed64(&buf, fm.file_size);
            PutLengthPrefixedSlice(&buf, fm.min_key);
            PutLengthPrefixedSlice(&buf, fm.max_key);
        }
    }
    uint32_t crc = CRC32C::Value(buf.data(), buf.size());
    std::string out;
    PutFixed32(&out, crc);
    out += buf;

    bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    ok = ok && std::fflush(f) == 0;
    std::fclose(f);
    if (!ok) {
        std::remove(tmp_path.c_str());
        return Status::IOError(manifest_path + ": failed writing manifest");
    }
    if (std::rename(tmp_path.c_str(), manifest_path.c_str()) != 0) {
        return Status::IOError(manifest_path + ": rename failed: " + std::strerror(errno));
    }
    return Status::OK();
}

Status Version::Load(const std::string& manifest_path) {
    std::FILE* f = std::fopen(manifest_path.c_str(), "rb");
    if (f == nullptr) {
        if (errno == ENOENT) return Status::NotFound(manifest_path);
        return Status::IOError(manifest_path + ": " + std::strerror(errno));
    }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::string buf(sz, '\0');
    size_t got = std::fread(buf.data(), 1, sz, f);
    std::fclose(f);
    if (static_cast<long>(got) != sz || sz < 4) {
        return Status::Corruption(manifest_path + ": truncated manifest");
    }

    uint32_t stored_crc = DecodeFixed32(buf.data());
    Slice body(buf.data() + 4, buf.size() - 4);
    uint32_t computed_crc = CRC32C::Value(body.data(), body.size());
    if (computed_crc != stored_crc) {
        return Status::Corruption(manifest_path + ": checksum mismatch");
    }

    const char* p = body.data();
    const char* limit = body.data() + body.size();
    auto need = [&](size_t n) { return static_cast<size_t>(limit - p) >= n; };

    if (!need(8) || DecodeFixed64(p) != kManifestMagic) {
        return Status::Corruption(manifest_path + ": bad magic");
    }
    p += 8;
    if (!need(8)) return Status::Corruption(manifest_path + ": truncated");
    next_file_number = DecodeFixed64(p);
    p += 8;
    if (!need(4)) return Status::Corruption(manifest_path + ": truncated");
    uint32_t num_levels = DecodeFixed32(p);
    p += 4;
    if (num_levels != static_cast<uint32_t>(kMaxLevels)) {
        return Status::Corruption(manifest_path + ": level count mismatch");
    }

    levels_.assign(kMaxLevels, {});
    for (int lvl = 0; lvl < kMaxLevels; lvl++) {
        if (!need(4)) return Status::Corruption(manifest_path + ": truncated");
        uint32_t count = DecodeFixed32(p);
        p += 4;
        for (uint32_t i = 0; i < count; i++) {
            if (!need(16)) return Status::Corruption(manifest_path + ": truncated file entry");
            FileMetaData fm;
            fm.number = DecodeFixed64(p);
            p += 8;
            fm.file_size = DecodeFixed64(p);
            p += 8;
            Slice input(p, limit - p);
            Slice min_k, max_k;
            if (!GetLengthPrefixedSlice(&input, &min_k) || !GetLengthPrefixedSlice(&input, &max_k)) {
                return Status::Corruption(manifest_path + ": malformed file entry");
            }
            fm.min_key = min_k.ToString();
            fm.max_key = max_k.ToString();
            p = input.data();
            levels_[lvl].push_back(std::move(fm));
        }
    }
    return Status::OK();
}

}  // namespace clomdb
