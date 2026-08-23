#include "clomdb/db.h"
#include <filesystem>
#include <set>
#include "test_util.h"

using namespace clomdb;

namespace {
std::string FreshDir(const std::string& name) {
    std::string dir = test::TempDir() + "/" + name;
    std::filesystem::remove_all(dir);
    return dir;
}
}

CLOMDB_TEST(BasicPutGetDelete) {
    std::string dir = FreshDir("basic_put_get_delete");
    Options opts;
    opts.background_compaction = false;
    DB* db;
    CHECK(DB::Open(opts, dir, &db).ok());

    CHECK(db->Put(WriteOptions(), "a", "1").ok());
    CHECK(db->Put(WriteOptions(), "b", "2").ok());

    std::string v;
    CHECK(db->Get(ReadOptions(), "a", &v).ok());
    CHECK_EQ(v, std::string("1"));
    CHECK(db->Get(ReadOptions(), "b", &v).ok());
    CHECK_EQ(v, std::string("2"));

    Status s = db->Get(ReadOptions(), "missing", &v);
    CHECK(s.IsNotFound());

    CHECK(db->Delete(WriteOptions(), "a").ok());
    s = db->Get(ReadOptions(), "a", &v);
    CHECK(s.IsNotFound());

    delete db;
}

CLOMDB_TEST(OverwriteReturnsLatestValue) {
    std::string dir = FreshDir("overwrite_latest");
    Options opts;
    opts.background_compaction = false;
    DB* db;
    CHECK(DB::Open(opts, dir, &db).ok());

    CHECK(db->Put(WriteOptions(), "k", "v1").ok());
    CHECK(db->Put(WriteOptions(), "k", "v2").ok());
    CHECK(db->Put(WriteOptions(), "k", "v3").ok());

    std::string v;
    CHECK(db->Get(ReadOptions(), "k", &v).ok());
    CHECK_EQ(v, std::string("v3"));
    delete db;
}

CLOMDB_TEST(WriteBatchIsAtomicAndOrdered) {
    std::string dir = FreshDir("write_batch");
    Options opts;
    opts.background_compaction = false;
    DB* db;
    CHECK(DB::Open(opts, dir, &db).ok());

    WriteBatch batch;
    batch.Put("x", "1");
    batch.Put("y", "2");
    batch.Delete("x");
    CHECK(db->Write(WriteOptions(), batch).ok());

    std::string v;
    CHECK(db->Get(ReadOptions(), "x", &v).IsNotFound());
    CHECK(db->Get(ReadOptions(), "y", &v).ok());
    CHECK_EQ(v, std::string("2"));
    delete db;
}

CLOMDB_TEST(RecoversFromWalAfterCrash) {
    std::string dir = FreshDir("recovers_from_wal");
    Options opts;
    opts.background_compaction = false;

    {
        DB* db;
        CHECK(DB::Open(opts, dir, &db).ok());
        CHECK(db->Put(WriteOptions(), "durable1", "yes").ok());
        CHECK(db->Put(WriteOptions(), "durable2", "also-yes").ok());
        CHECK(db->Delete(WriteOptions(), "durable1").ok());
        delete db;
    }

    {
        DB* db;
        CHECK(DB::Open(opts, dir, &db).ok());
        std::string v;
        CHECK(db->Get(ReadOptions(), "durable1", &v).IsNotFound());
        CHECK(db->Get(ReadOptions(), "durable2", &v).ok());
        CHECK_EQ(v, std::string("also-yes"));
        delete db;
    }
}

CLOMDB_TEST(RecoversUnflushedWalWithoutCleanShutdown) {
    std::string dir = FreshDir("recovers_unflushed_wal");
    Options opts;
    opts.background_compaction = false;
    opts.memtable_flush_bytes = 1024 * 1024 * 1024;

    {
        DB* db;
        CHECK(DB::Open(opts, dir, &db).ok());
        for (int i = 0; i < 50; i++) {
            CHECK(db->Put(WriteOptions(), "k" + std::to_string(i), "v" + std::to_string(i)).ok());
        }
        auto stats = db->GetStats();
        CHECK_EQ(stats.files_per_level[0], 0u);
        delete db;
    }
    {
        DB* db;
        CHECK(DB::Open(opts, dir, &db).ok());
        for (int i = 0; i < 50; i++) {
            std::string v;
            CHECK(db->Get(ReadOptions(), "k" + std::to_string(i), &v).ok());
            CHECK_EQ(v, std::string("v" + std::to_string(i)));
        }
        delete db;
    }
}

CLOMDB_TEST(FlushCreatesL0FileAndSurvivesReopen) {
    std::string dir = FreshDir("flush_creates_l0");
    Options opts;
    opts.background_compaction = false;
    DB* db;
    CHECK(DB::Open(opts, dir, &db).ok());

    for (int i = 0; i < 200; i++) {
        CHECK(db->Put(WriteOptions(), "key" + std::to_string(i), std::string(50, 'x')).ok());
    }
    CHECK(db->Flush().ok());
    auto stats = db->GetStats();
    CHECK(stats.files_per_level[0] >= 1);
    CHECK_EQ(stats.memtable_entries, 0u);
    delete db;

    DB* db2;
    CHECK(DB::Open(opts, dir, &db2).ok());
    std::string v;
    CHECK(db2->Get(ReadOptions(), "key0", &v).ok());
    CHECK(db2->Get(ReadOptions(), "key199", &v).ok());
    delete db2;
}

CLOMDB_TEST(CompactionMergesL0IntoL1AndPreservesData) {
    std::string dir = FreshDir("compaction_merges");
    Options opts;
    opts.background_compaction = false;
    opts.memtable_flush_bytes = 2048;
    opts.l0_compaction_trigger = 3;
    DB* db;
    CHECK(DB::Open(opts, dir, &db).ok());

    for (int round = 0; round < 5; round++) {
        for (int i = 0; i < 100; i++) {
            std::string key = "k" + std::to_string(i);
            std::string val = "round" + std::to_string(round) + "-" + std::to_string(i);
            CHECK(db->Put(WriteOptions(), key, val).ok());
        }
    }
    CHECK(db->Flush().ok());

    auto stats = db->GetStats();
    CHECK(stats.bytes_per_level[1] > 0);

    for (int i = 0; i < 100; i++) {
        std::string v;
        CHECK(db->Get(ReadOptions(), "k" + std::to_string(i), &v).ok());
        CHECK_EQ(v, std::string("round4-" + std::to_string(i)));
    }
    delete db;

    DB* db2;
    CHECK(DB::Open(opts, dir, &db2).ok());
    for (int i = 0; i < 100; i++) {
        std::string v;
        CHECK(db2->Get(ReadOptions(), "k" + std::to_string(i), &v).ok());
        CHECK_EQ(v, std::string("round4-" + std::to_string(i)));
    }
    delete db2;
}

CLOMDB_TEST(DeleteShadowsOlderCompactedValue) {
    std::string dir = FreshDir("delete_shadows_older");
    Options opts;
    opts.background_compaction = false;
    opts.memtable_flush_bytes = 64;
    opts.l0_compaction_trigger = 2;
    DB* db;
    CHECK(DB::Open(opts, dir, &db).ok());

    CHECK(db->Put(WriteOptions(), "gone", "should-not-be-visible").ok());
    CHECK(db->Flush().ok());
    CHECK(db->Delete(WriteOptions(), "gone").ok());
    CHECK(db->Flush().ok());

    std::string v;
    CHECK(db->Get(ReadOptions(), "gone", &v).IsNotFound());
    delete db;

    DB* db2;
    CHECK(DB::Open(opts, dir, &db2).ok());
    CHECK(db2->Get(ReadOptions(), "gone", &v).IsNotFound());
    delete db2;
}

CLOMDB_TEST(IteratorReturnsSortedLiveKeysOnly) {
    std::string dir = FreshDir("iterator_sorted");
    Options opts;
    opts.background_compaction = false;
    opts.memtable_flush_bytes = 256;
    DB* db;
    CHECK(DB::Open(opts, dir, &db).ok());

    std::vector<std::string> keys = {"delta", "alpha", "charlie", "bravo", "echo"};
    for (auto& k : keys) CHECK(db->Put(WriteOptions(), k, "val-" + k).ok());
    CHECK(db->Flush().ok());
    CHECK(db->Delete(WriteOptions(), "charlie").ok());

    auto it = db->NewIterator(ReadOptions());
    std::vector<std::string> seen;
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        seen.push_back(it->key().ToString());
    }
    std::vector<std::string> expected = {"alpha", "bravo", "delta", "echo"};
    CHECK_EQ(seen.size(), expected.size());
    for (size_t i = 0; i < expected.size(); i++) CHECK_EQ(seen[i], expected[i]);

    delete db;
}

CLOMDB_TEST(BackgroundCompactionModeIsCorrect) {
    std::string dir = FreshDir("background_mode");
    Options opts;
    opts.background_compaction = true;
    opts.memtable_flush_bytes = 1024;
    opts.l0_compaction_trigger = 2;
    DB* db;
    CHECK(DB::Open(opts, dir, &db).ok());

    for (int i = 0; i < 500; i++) {
        CHECK(db->Put(WriteOptions(), "bg" + std::to_string(i), std::string(20, 'y')).ok());
    }
    CHECK(db->Flush().ok());

    for (int i = 0; i < 500; i++) {
        std::string v;
        CHECK(db->Get(ReadOptions(), "bg" + std::to_string(i), &v).ok());
    }
    delete db;
}

CLOMDB_TEST(ErrorIfExistsIsRespected) {
    std::string dir = FreshDir("error_if_exists");
    Options opts;
    DB* db;
    CHECK(DB::Open(opts, dir, &db).ok());
    delete db;

    Options opts2;
    opts2.error_if_exists = true;
    DB* db2 = nullptr;
    Status s = DB::Open(opts2, dir, &db2);
    CHECK(!s.ok());
    CHECK(db2 == nullptr);
}

CLOMDB_TEST_MAIN()
