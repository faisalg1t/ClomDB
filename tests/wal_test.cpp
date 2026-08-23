#include "clomdb/wal.h"
#include <filesystem>
#include <vector>
#include "test_util.h"

using namespace clomdb;

CLOMDB_TEST(WalWriteAndReplay) {
    std::string path = test::TempDir() + "/wal_write_and_replay.log";
    std::filesystem::remove(path);

    {
        WriteAheadLog wal;
        CHECK(wal.Open(path).ok());
        CHECK(wal.AddRecord(Slice("record one"), false).ok());
        CHECK(wal.AddRecord(Slice("record two"), false).ok());
        CHECK(wal.AddRecord(Slice(""), false).ok());
        CHECK(wal.Close().ok());
    }

    std::vector<std::string> got;
    Status s = WriteAheadLog::ReplayAll(path, [&](const Slice& rec) { got.push_back(rec.ToString()); });
    CHECK(s.ok());
    CHECK_EQ(got.size(), 3u);
    CHECK_EQ(got[0], std::string("record one"));
    CHECK_EQ(got[1], std::string("record two"));
    CHECK_EQ(got[2], std::string(""));
}

CLOMDB_TEST(WalReplayMissingFileIsOk) {
    std::string path = test::TempDir() + "/does_not_exist.log";
    std::filesystem::remove(path);
    int count = 0;
    Status s = WriteAheadLog::ReplayAll(path, [&](const Slice&) { count++; });
    CHECK(s.ok());
    CHECK_EQ(count, 0);
}

CLOMDB_TEST(WalTornTailRecordIsSkippedNotFatal) {
    std::string path = test::TempDir() + "/wal_torn_tail.log";
    std::filesystem::remove(path);
    {
        WriteAheadLog wal;
        CHECK(wal.Open(path).ok());
        CHECK(wal.AddRecord(Slice("good record"), true).ok());
        CHECK(wal.Close().ok());
    }
    // Simulate a crash mid-write: append a truncated, bogus trailing record.
    {
        std::FILE* f = std::fopen(path.c_str(), "ab");
        const char garbage[] = {0x01, 0x02, 0x03};
        std::fwrite(garbage, 1, sizeof(garbage), f);
        std::fclose(f);
    }
    std::vector<std::string> got;
    Status s = WriteAheadLog::ReplayAll(path, [&](const Slice& rec) { got.push_back(rec.ToString()); });
    CHECK(s.ok());
    CHECK_EQ(got.size(), 1u);
    CHECK_EQ(got[0], std::string("good record"));
}

CLOMDB_TEST_MAIN()
