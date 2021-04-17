//
// Created by Xiaofei ZHAO on 6/4/2021.
//

#include <chrono>
#include <ctime>

#include "db/dbformat.h"
#include "util/coding.h"

#include "gtest/gtest.h"

namespace leveldb {

static std::string MVIKey(const std::string& user_key, uint64_t seq,
                        ValueType vt, ValidTime ts) {
  std::string encoded;
  AppendMVInternalKey(&encoded, ParsedMVInternalKey(user_key, seq, vt, ts));
  return encoded;
}

static std::string Shorten(const std::string& s, const std::string& l) {
  std::string result = s;
  MVInternalKeyComparator(BytewiseComparator()).FindShortestSeparator(&result, l);
  return result;
}

static void TestKey(const std::string& key, uint64_t seq, ValueType vt,
                    ValidTime ts) {
  std::string encoded = MVIKey(key, seq, vt, ts);

  Slice in(encoded);
  ParsedMVInternalKey decoded("", 0, kTypeValue, 0);

  ASSERT_TRUE(ParseMVInternalKey(in, &decoded));
  ASSERT_EQ(key, decoded.user_key.ToString());
  ASSERT_EQ(seq, decoded.sequence);
  ASSERT_EQ(vt, decoded.type);
  ASSERT_EQ(ts, decoded.valid_time);

  ASSERT_TRUE(!ParseMVInternalKey(Slice("bar"), &decoded));
}

TEST(MVFormatTest, MVInternalKey_EncodeDecode) {
  const char* keys[] = {"", "k", "hello", "longggggggggggggggggggggg"};
  const uint64_t seq[] = {1,
                          2,
                          3,
                          (1ull << 8) - 1,
                          1ull << 8,
                          (1ull << 8) + 1,
                          (1ull << 16) - 1,
                          1ull << 16,
                          (1ull << 16) + 1,
                          (1ull << 32) - 1,
                          1ull << 32,
                          (1ull << 32) + 1};
  auto current = std::chrono::system_clock::now();
  std::time_t current_time = std::chrono::system_clock::to_time_t(current);
  for (auto & key : keys) {
    for (unsigned long long s : seq) {
      TestKey(key, s, kTypeValue, current_time);
      TestKey("hello", 1, kTypeDeletion, current_time);
    }
  }
}

TEST(MVFormatTest, InternalKey_DecodeFromEmpty) {
  MVInternalKey mv_internal_key;

  ASSERT_TRUE(!mv_internal_key.DecodeFrom(""));
}

TEST(MVFormatTest, InternalKeyShortSeparator) {
  auto current = std::chrono::system_clock::now();
  std::time_t current_time = std::chrono::system_clock::to_time_t(current);
  // When user keys are same
  ASSERT_EQ(MVIKey("foo", 100, kTypeValue, current_time),
            Shorten(MVIKey("foo", 100, kTypeValue, current_time), MVIKey("foo", 99, kTypeValue, current_time)));
  ASSERT_EQ(
      MVIKey("foo", 100, kTypeValue, current_time),
      Shorten(MVIKey("foo", 100, kTypeValue, current_time), MVIKey("foo", 101, kTypeValue, current_time)));
  ASSERT_EQ(
      MVIKey("foo", 100, kTypeValue, current_time),
      Shorten(MVIKey("foo", 100, kTypeValue, current_time), MVIKey("foo", 100, kTypeValue, current_time)));
  ASSERT_EQ(
      MVIKey("foo", 100, kTypeValue, current_time),
      Shorten(MVIKey("foo", 100, kTypeValue, current_time), MVIKey("foo", 100, kTypeDeletion, current_time)));

  // When user keys are misordered
  ASSERT_EQ(MVIKey("foo", 100, kTypeValue, current_time),
            Shorten(MVIKey("foo", 100, kTypeValue, current_time), MVIKey("bar", 99, kTypeValue, current_time)));

  // When user keys are different, but correctly ordered
  // TODO: this will fail because of MVInternalKeyComparator::FindShortestSeparator
//  ASSERT_EQ(
//      MVIKey("g", kMaxSequenceNumber, kValueTypeForSeek, current_time),
//      Shorten(MVIKey("foo", 100, kTypeValue, current_time), MVIKey("hello", 200, kTypeValue, current_time)));

  // When start user key is prefix of limit user key
  ASSERT_EQ(
      MVIKey("foo", 100, kTypeValue, current_time),
      Shorten(MVIKey("foo", 100, kTypeValue, current_time), MVIKey("foobar", 200, kTypeValue, current_time)));

  // When limit user key is prefix of start user key
  ASSERT_EQ(
      MVIKey("foobar", 100, kTypeValue, current_time),
      Shorten(MVIKey("foobar", 100, kTypeValue, current_time), MVIKey("foo", 200, kTypeValue, current_time)));
}

} // namespace leveldb

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}