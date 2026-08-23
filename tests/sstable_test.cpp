#include "clomdb/sstable.h"
#include <filesystem>
#include "test_util.h"

using namespace clomdb;

CLOMDB_TEST(SSTableWriteReadGet) {
    std::string path = test::TempDir() + "/sstable_write_read_get.sst";
    std::filesystem::remove(path);

    std::vector<SSTableEntry> entries;
    for (int i = 0; i < 100; i++) {
        SSTableEntry e;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "key%03d", i);
        e.key = buf;
        e.type = (i % 10 == 0) ? EntryType::kDelete : EntryType::kPut;
        if (e.type == EntryType::kPut) e.value = "value" + std::to_string(i);
        entries.push_back(e);
    }
    CHECK(SSTableWriter::Write(path, entries, 10).ok());

    SSTableReader reader;
    CHECK(reader.Open(path).ok());
    CHECK_EQ(reader.entry_count(), 100u);
    CHECK_EQ(reader.min_key(), std::string("key000"));
    CHECK_EQ(reader.max_key(), std::string("key099"));

    for (int i = 0; i < 100; i++) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "key%03d", i);
        std::string value;
        bool deleted = false;
        bool found = reader.Get(Slice(buf), &value, &deleted, ReadOptions());
        CHECK(found);
        if (i % 10 == 0) {
            CHECK(deleted);
        } else {
            CHECK(!deleted);
            CHECK_EQ(value, std::string("value" + std::to_string(i)));
        }
    }

    std::string value;
    bool deleted;
    CHECK(!reader.Get(Slice("nonexistent"), &value, &deleted, ReadOptions()));
}

CLOMDB_TEST(SSTableReadAllPreservesOrder) {
    std::string path = test::TempDir() + "/sstable_read_all.sst";
    std::filesystem::remove(path);

    std::vector<SSTableEntry> entries;
    for (int i = 0; i < 50; i++) {
        SSTableEntry e;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "k%04d", i);
        e.key = buf;
        e.type = EntryType::kPut;
        e.value = "v" + std::to_string(i);
        entries.push_back(e);
    }
    CHECK(SSTableWriter::Write(path, entries, 10).ok());

    SSTableReader reader;
    CHECK(reader.Open(path).ok());
    std::vector<SSTableEntry> read_back;
    CHECK(reader.ReadAll(&read_back).ok());
    CHECK_EQ(read_back.size(), entries.size());
    for (size_t i = 0; i < entries.size(); i++) {
        CHECK_EQ(read_back[i].key, entries[i].key);
        CHECK_EQ(read_back[i].value, entries[i].value);
    }
}

CLOMDB_TEST(SSTableRejectsBadMagic) {
    std::string path = test::TempDir() + "/not_an_sstable.sst";
    std::filesystem::remove(path);
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        std::string junk(64, 'x');
        std::fwrite(junk.data(), 1, junk.size(), f);
        std::fclose(f);
    }
    SSTableReader reader;
    Status s = reader.Open(path);
    CHECK(!s.ok());
}

CLOMDB_TEST_MAIN()
