#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "clomdb/status.h"

namespace clomdb {

constexpr int kMaxLevels = 6;

struct FileMetaData {
    uint64_t number = 0;
    std::string min_key;
    std::string max_key;
    uint64_t file_size = 0;
};

class Version {
public:
    Version() : levels_(kMaxLevels) {}

    std::vector<FileMetaData>& MutableLevel(int level) { return levels_[level]; }
    const std::vector<FileMetaData>& Level(int level) const { return levels_[level]; }
    int NumLevels() const { return kMaxLevels; }

    uint64_t next_file_number = 1;

    Status Save(const std::string& manifest_path) const;
    Status Load(const std::string& manifest_path);

private:
    std::vector<std::vector<FileMetaData>> levels_;
};

}
