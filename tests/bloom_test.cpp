#include "clomdb/bloom.h"
#include "test_util.h"

using namespace clomdb;

CLOMDB_TEST(BloomNoFalseNegatives) {
    std::vector<std::string> keys;
    for (int i = 0; i < 1000; i++) keys.push_back("key-" + std::to_string(i));
    std::string filter = BloomFilter::Build(keys, 10);
    for (const auto& k : keys) {
        CHECK(BloomFilter::MayContain(filter, Slice(k)));
    }
}

CLOMDB_TEST(BloomReasonableFalsePositiveRate) {
    std::vector<std::string> keys;
    for (int i = 0; i < 1000; i++) keys.push_back("present-" + std::to_string(i));
    std::string filter = BloomFilter::Build(keys, 10);

    int false_positives = 0;
    int trials = 2000;
    for (int i = 0; i < trials; i++) {
        std::string absent = "absent-" + std::to_string(i);
        if (BloomFilter::MayContain(filter, Slice(absent))) false_positives++;
    }
    // At 10 bits/key we expect roughly ~1% FP rate; allow generous slack
    // since this is a statistical property, not an exact one.
    CHECK(false_positives < trials / 10);
}

CLOMDB_TEST_MAIN()
