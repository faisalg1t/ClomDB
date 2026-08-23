#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <numeric>
#include <random>
#include <vector>
#include "clomdb/clomdb.h"

using namespace clomdb;
using Clock = std::chrono::steady_clock;

int main(int argc, char** argv) {
    int n = argc > 1 ? std::atoi(argv[1]) : 100000;
    std::string dir = "/tmp/clomdb_bench";
    std::filesystem::remove_all(dir);

    Options opts;
    DB* db;
    Status s = DB::Open(opts, dir, &db);
    if (!s.ok()) {
        std::fprintf(stderr, "open failed: %s\n", s.ToString().c_str());
        return 1;
    }

    std::vector<std::string> keys(n);
    for (int i = 0; i < n; i++) keys[i] = "key" + std::to_string(i);

    auto t0 = Clock::now();
    for (int i = 0; i < n; i++) {
        db->Put(WriteOptions(), keys[i], "value-" + std::to_string(i) + "-padding-padding");
    }
    db->Flush();
    auto t1 = Clock::now();

    std::mt19937 rng(42);
    std::shuffle(keys.begin(), keys.end(), rng);
    std::string v;
    int found = 0;
    for (int i = 0; i < n; i++) {
        if (db->Get(ReadOptions(), keys[i], &v).ok()) found++;
    }
    auto t2 = Clock::now();

    double write_s = std::chrono::duration<double>(t1 - t0).count();
    double read_s = std::chrono::duration<double>(t2 - t1).count();
    std::printf("wrote %d keys in %.3fs (%.0f ops/s)\n", n, write_s, n / write_s);
    std::printf("read  %d keys in %.3fs (%.0f ops/s), found=%d\n", n, read_s, n / read_s, found);

    auto stats = db->GetStats();
    for (int lvl = 0; lvl < kMaxLevels; lvl++) {
        if (stats.files_per_level[lvl] == 0) continue;
        std::printf("L%d: %zu files, %zu bytes\n", lvl, stats.files_per_level[lvl], stats.bytes_per_level[lvl]);
    }

    delete db;
    return 0;
}
