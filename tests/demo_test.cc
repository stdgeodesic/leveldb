//
// Created by Xiaofei ZHAO on 29/3/2021.
//

#include <string>

#include "leveldb/db.h"
#include "leveldb/comparator.h"
#include "leveldb/write_batch.h"

#include "gtest/gtest.h"

namespace leveldb {

TEST(Demo, SimpleKV) {
  DB *db;
  Options options;
  options.create_if_missing = true;
  options.error_if_exists = true;
  // options.comparator = IntegerComparator();
  Status status = DB::Open(options, "test_db", &db);
  ASSERT_TRUE(status.ok());

  // Put
  WriteOptions writeOptions;

  db->Put(writeOptions, "k1", "v1");

  // Get
  std::string value;

  Status s = db->Get(ReadOptions(), "k1", &value);

  ASSERT_TRUE(s.ok());
  ASSERT_EQ(value, "v1");

  delete db;

  DestroyDB("test_db", options);
}

}  // namespace leveldb

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
