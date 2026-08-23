#include "clomdb/coding.h"
#include "test_util.h"

using namespace clomdb;

CLOMDB_TEST(FixedRoundTrip) {
    std::string s;
    PutFixed32(&s, 0x12345678);
    PutFixed64(&s, 0x1122334455667788ULL);
    CHECK_EQ(DecodeFixed32(s.data()), 0x12345678u);
    CHECK_EQ(DecodeFixed64(s.data() + 4), 0x1122334455667788ULL);
}

CLOMDB_TEST(VarintRoundTrip) {
    std::string s;
    std::vector<uint32_t> values = {0, 1, 127, 128, 300, 16384, 0xFFFFFFFFu};
    for (auto v : values) PutVarint32(&s, v);

    const char* p = s.data();
    const char* limit = s.data() + s.size();
    for (auto expected : values) {
        uint32_t got;
        p = GetVarint32Ptr(p, limit, &got);
        CHECK(p != nullptr);
        CHECK_EQ(got, expected);
    }
}

CLOMDB_TEST(LengthPrefixedSliceRoundTrip) {
    std::string s;
    PutLengthPrefixedSlice(&s, Slice("hello"));
    PutLengthPrefixedSlice(&s, Slice(""));
    PutLengthPrefixedSlice(&s, Slice("world!"));

    Slice input(s);
    Slice a, b, c;
    CHECK(GetLengthPrefixedSlice(&input, &a));
    CHECK(GetLengthPrefixedSlice(&input, &b));
    CHECK(GetLengthPrefixedSlice(&input, &c));
    CHECK_EQ(a.ToString(), std::string("hello"));
    CHECK_EQ(b.ToString(), std::string(""));
    CHECK_EQ(c.ToString(), std::string("world!"));
    CHECK(input.empty());
}

CLOMDB_TEST(Crc32cKnownValue) {
    // Standard CRC32C check value for the ASCII string "123456789".
    uint32_t crc = CRC32C::Value("123456789", 9);
    CHECK_EQ(crc, 0xE3069283u);
}

CLOMDB_TEST(Crc32cMaskRoundTrip) {
    uint32_t crc = CRC32C::Value("clomdb", 6);
    uint32_t masked = CRC32C::Mask(crc);
    CHECK(masked != crc);
    CHECK_EQ(CRC32C::Unmask(masked), crc);
}

CLOMDB_TEST_MAIN()
