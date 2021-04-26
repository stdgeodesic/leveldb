//
// Created by Xiaofei ZHAO on 14/4/2021.
//

#include <chrono>
#include <ctime>

#include "gtest/gtest.h"
#include "db/memtable.h"
#include "db/write_batch_internal.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "util/logging.h"

namespace leveldb {

// TODO
static std::string PrintContents(WriteBatchMV* b) {
  InternalKeyComparator cmp(BytewiseComparator(), true);
  MemTable* mem = new MemTable(cmp);
  mem->Ref();
  std::string state;
  Status s = WriteBatchMVInternal::InsertInto(b, mem);
  int count = 0;
  Iterator* iter = mem->NewIterator();
  for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
    ParsedMVInternalKey ikey;
    EXPECT_TRUE(ParseMVInternalKey(iter->key(), &ikey));
    switch (ikey.type) {
      case kTypeValue:
        state.append("Put(");
        state.append(ikey.user_key.ToString());
        state.append(", ");
        PutFixed64(&state, ikey.valid_time);
        state.append(", ");
        state.append(iter->value().ToString());
        state.append(")");
        count++;
        break;
      case kTypeDeletion:
        state.append("Delete(");
        state.append(ikey.user_key.ToString());
        state.append(", ");
        PutFixed64(&state, ikey.valid_time);
        state.append(")");
        count++;
        break;
    }
    state.append("@");
    state.append(NumberToString(ikey.sequence));
  }
  delete iter;
  if (!s.ok()) {
    state.append("ParseError()");
  } else if (count != WriteBatchMVInternal::Count(b)) {
    state.append("CountMismatch()");
  }
  mem->Unref();
  return state;
}

TEST(WriteBatchMVTest, Empty) {
  WriteBatchMV batch;
  ASSERT_EQ("", PrintContents(&batch));
  ASSERT_EQ(0, WriteBatchMVInternal::Count(&batch));
}

TEST(WriteBatchMVTest, Multiple) {
  auto current = std::chrono::system_clock::now();
  std::time_t current_time = std::chrono::system_clock::to_time_t(current);
  std::string current_time_string;
  PutFixed64(&current_time_string, current_time);

  WriteBatchMV batch;
  batch.Put(Slice("foo"), current_time, Slice("bar"));
  batch.Delete(Slice("box"), current_time);
  batch.Put(Slice("baz"), current_time, Slice("boo"));
  WriteBatchMVInternal::SetSequence(&batch, 100);
  ASSERT_EQ(100, WriteBatchMVInternal::Sequence(&batch));
  ASSERT_EQ(3, WriteBatchMVInternal::Count(&batch));

  std::ostringstream content;
  content << "Put(baz, " << current_time_string << ", boo)@102";
  content << "Delete(box, " << current_time_string << ")@101";
  content << "Put(foo, " << current_time_string << ", bar)@100";
  ASSERT_EQ(
      content.str(),
      PrintContents(&batch));
}

TEST(WriteBatchMVTest, Corruption) {
  auto current = std::chrono::system_clock::now();
  std::time_t current_time = std::chrono::system_clock::to_time_t(current);
  std::string current_time_string;
  PutFixed64(&current_time_string, current_time);

  WriteBatchMV batch;
  batch.Put(Slice("foo"), current_time, Slice("bar"));
  batch.Delete(Slice("box"), current_time);
  WriteBatchMVInternal::SetSequence(&batch, 200);
  Slice contents = WriteBatchMVInternal::Contents(&batch);
  WriteBatchMVInternal::SetContents(&batch,
                                  Slice(contents.data(), contents.size() - 1));

  std::ostringstream content;
  content << "Put(foo, " << current_time_string << ", bar)@200";
  content << "ParseError()";
  ASSERT_EQ(
      content.str(),
      PrintContents(&batch));
}

TEST(WriteBatchMVTest, Append) {
  auto current = std::chrono::system_clock::now();
  std::time_t current_time = std::chrono::system_clock::to_time_t(current);
  std::string current_time_string;
  PutFixed64(&current_time_string, current_time);

  WriteBatchMV b1, b2;
  WriteBatchMVInternal::SetSequence(&b1, 200);
  WriteBatchMVInternal::SetSequence(&b2, 300);
  b1.Append(b2);
  ASSERT_EQ("", PrintContents(&b1));
  b2.Put("a", current_time, "va");
  b1.Append(b2);
  ASSERT_EQ("Put(a, " + current_time_string +", va)@200",
            PrintContents(&b1));
  b2.Clear();
  b2.Put("b", current_time, "vb");
  b1.Append(b2);
  ASSERT_EQ(
      "Put(a, " + current_time_string + ", va)@200"
      "Put(b, " + current_time_string + ", vb)@201",
      PrintContents(&b1));
  b2.Delete("foo", current_time);
  b1.Append(b2);
  ASSERT_EQ(
      "Put(a, " + current_time_string + ", va)@200"
      "Put(b, " + current_time_string + ", vb)@202"
      "Put(b, " + current_time_string + ", vb)@201"
      "Delete(foo, " + current_time_string + ")@203",
      PrintContents(&b1));
}

}  // namespace leveldb

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}