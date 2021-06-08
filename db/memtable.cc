// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/memtable.h"

#include "db/dbformat.h"

#include "leveldb/comparator.h"
#include "leveldb/env.h"
#include "leveldb/iterator.h"

#include "util/coding.h"

namespace leveldb {

static Slice GetLengthPrefixedSlice(const char* data) {
  uint32_t len;
  const char* p = data;
  p = GetVarint32Ptr(p, p + 5, &len);  // +5: we assume "p" is not corrupted
  return Slice(p, len);
}

MemTable::MemTable(const InternalKeyComparator& comparator)
    : comparator_(comparator), refs_(0), table_(comparator_, &arena_) {}

MemTable::~MemTable() { assert(refs_ == 0); }

size_t MemTable::ApproximateMemoryUsage() { return arena_.MemoryUsage(); }

int MemTable::KeyComparator::operator()(const char* aptr,
                                        const char* bptr) const {
  // Internal keys are encoded as length-prefixed strings.
  Slice a = GetLengthPrefixedSlice(aptr);
  Slice b = GetLengthPrefixedSlice(bptr);
  return comparator.Compare(a, b);
}

// Encode a suitable internal key target for "target" and return it.
// Uses *scratch as scratch space, and the returned pointer will point
// into this scratch space.
static const char* EncodeKey(std::string* scratch, const Slice& target) {
  scratch->clear();
  PutVarint32(scratch, target.size());
  scratch->append(target.data(), target.size());
  return scratch->data();
}

class MemTableIterator : public Iterator {
 public:
  explicit MemTableIterator(MemTable::Table* table) : iter_(table) {}

  MemTableIterator(const MemTableIterator&) = delete;
  MemTableIterator& operator=(const MemTableIterator&) = delete;

  ~MemTableIterator() override = default;

  bool Valid() const override { return iter_.Valid(); }
  void Seek(const Slice& k) override { iter_.Seek(EncodeKey(&tmp_, k)); }
  void SeekToFirst() override { iter_.SeekToFirst(); }
  void SeekToLast() override { iter_.SeekToLast(); }
  void Next() override { iter_.Next(); }
  void Prev() override { iter_.Prev(); }
  Slice key() const override { return GetLengthPrefixedSlice(iter_.key()); }
  Slice value() const override {
    Slice key_slice = GetLengthPrefixedSlice(iter_.key());
    return GetLengthPrefixedSlice(key_slice.data() + key_slice.size());
  }

  Status status() const override { return Status::OK(); }

 private:
  MemTable::Table::Iterator iter_;
  std::string tmp_;  // For passing to EncodeKey
};

Iterator* MemTable::NewIterator() { return new MemTableIterator(&table_); }

void MemTable::Add(SequenceNumber s, ValueType type, const Slice& key,
                   const Slice& value) {
  // Format of an entry is concatenation of:
  //  key_size     : varint32 of internal_key.size()
  //  key bytes    : char[internal_key.size()]
  //  value_size   : varint32 of value.size()
  //  value bytes  : char[value.size()]
  size_t key_size = key.size();
  size_t val_size = value.size();
  size_t internal_key_size = key_size + 8;
  const size_t encoded_len = VarintLength(internal_key_size) +
                             internal_key_size + VarintLength(val_size) +
                             val_size;
  char* buf = arena_.Allocate(encoded_len);
  char* p = EncodeVarint32(buf, internal_key_size);
  std::memcpy(p, key.data(), key_size);
  p += key_size;
  EncodeFixed64(p, (s << 8) | type);
  p += 8;
  p = EncodeVarint32(p, val_size);
  std::memcpy(p, value.data(), val_size);
  assert(p + val_size == buf + encoded_len);
  table_.Insert(buf);
}

bool MemTable::Get(const LookupKey& key, std::string* value, Status* s) {
  Slice memkey = key.memtable_key();
  Slice* a = new Slice();
  Table::Iterator iter(&table_);
  iter.Seek(memkey.data());
  if (iter.Valid()) {
    // entry format is:
    //    klength  varint32
    //    userkey  char[klength]
    //    tag      uint64
    //    vlength  varint32
    //    value    char[vlength]
    // Check that it belongs to same user key.  We do not check the
    // sequence number since the Seek() call above should have skipped
    // all entries with overly large sequence numbers.
    const char* entry = iter.key();
    uint32_t key_length;
    const char* key_ptr = GetVarint32Ptr(entry, entry + 5, &key_length);
    if (comparator_.comparator.user_comparator()->Compare(
            Slice(key_ptr, key_length - 8), key.user_key()) == 0) {
      // Correct user key
      const uint64_t tag = DecodeFixed64(key_ptr + key_length - 8);
      switch (static_cast<ValueType>(tag & 0xff)) {
        case kTypeValue: {
          Slice v = GetLengthPrefixedSlice(key_ptr + key_length);
          value->assign(v.data(), v.size());
          return true;
        }
        case kTypeDeletion:
          *s = Status::NotFound(Slice());
          return true;
      }
    }
  }
  return false;
}

// Add multi-version entries to MemTable
void MemTable::AddMV(SequenceNumber s, ValueType type, const Slice& key,
                     ValidTime vt, const Slice& value) {
  size_t key_size = key.size();
  size_t val_size = value.size();
  size_t internal_key_size = key_size + 16;
  const size_t encoded_len = VarintLength(internal_key_size) +
                             internal_key_size + VarintLength(val_size) +
                             val_size;
  char* buf = arena_.Allocate(encoded_len);
  char* p = EncodeVarint32(buf, internal_key_size);
  std::memcpy(p, key.data(), key_size);
  p += key_size;
  EncodeFixed64(p, (s << 8) | type);
  p += 8;
  EncodeFixed64(p, vt);
  p += 8;
  p = EncodeVarint32(p, val_size);
  std::memcpy(p, value.data(), val_size);
  assert(p + val_size == buf + encoded_len);
  table_.Insert(buf);
}

// TODO
bool MemTable::GetMV(const MVLookupKey& key, std::string* value,
                     ValidTimePeriod* period, Status* s) {
  Slice memkey = key.memtable_key();
  Table::Iterator iter(&table_);
  // Advances to the latest data version of the required key
  // Physically the first version
  iter.Seek(memkey.data());
  if (iter.Valid()) {
    const char* entry = iter.key();
    uint32_t key_length;
    const char* key_ptr = GetVarint32Ptr(entry, entry + 5, &key_length);
    if (comparator_.comparator.user_comparator()->Compare(
            Slice(key_ptr, key_length - 16), key.user_key()) == 0) {
      // Correct user key
      // TODO: check upper valid time of the first-hit version
      ValidTime hi_ = std::min(kMaxValidTime, valid_time_hi_);
      ValidTime lo_ = DecodeFixed64(key_ptr + key_length - 8);
      while (key.valid_time() <
             lo_) {  // retrieved key's valid time not overlaps lookup key
        hi_ = lo_;
        iter.Next();
        if (!iter.Valid()) return false;  // Not found in MemTable
        entry = iter.key();
        key_ptr = GetVarint32Ptr(entry, entry + 5, &key_length);
        lo_ = DecodeFixed64(key_ptr + key_length - 8);
      }
      const uint64_t tag = DecodeFixed64(key_ptr + key_length - 16);
      switch (static_cast<ValueType>(tag & 0xff)) {
        case kTypeValue: {
          Slice v = GetLengthPrefixedSlice(key_ptr + key_length);
          value->assign(v.data(), v.size());
          period->lo = lo_;
          period->hi = hi_;
          return true;
        }
        case kTypeDeletion:
          *s = Status::NotFound(Slice());
          period->lo = lo_;
          period->hi = hi_;
          return true;
      }
    }
  }
  return false;
}

bool MemTable::GetMVRange(const KeyList& key_list, const TimeRange& time_range,
                          SequenceNumber snapshot, ResultSet* result_set,
                          Status* s) {
  ValidTime vt_lo = time_range.lo;
  ValidTime vt_hi = time_range.hi;
  size_t init_result_set_size = result_set->size();
  // Iterate over keys
  for (auto k : key_list) {
    MVLookupKey key(k, snapshot, vt_hi);
    Slice memkey = key.memtable_key();
    Table::Iterator iter(&table_);

    iter.Seek(memkey.data());
    if (iter.Valid()) {
      const char* entry = iter.key();
      uint32_t key_length;
      const char* key_ptr = GetVarint32Ptr(entry, entry + 5, &key_length);
      if (comparator_.comparator.user_comparator()->Compare(
              Slice(key_ptr, key_length - 16), key.user_key()) == 0) {
        // Correct user key
        ValidTime hi_ = std::min(kMaxValidTime, valid_time_hi_);  // imm_
        ValidTime lo_ = DecodeFixed64(key_ptr + key_length - 8);
        while (hi_ > vt_lo) {  // not inclusive
          // Parse current key and append to result set
          const uint64_t tag = DecodeFixed64(key_ptr + key_length - 16);
          switch (static_cast<ValueType>(tag & 0xff)) {
            case kTypeValue: {
              Slice v = GetLengthPrefixedSlice(key_ptr + key_length);
              result_set->push_back(ResultVersion(key.user_key(), v, lo_, hi_));
              break;
            }
            case kTypeDeletion:
              result_set->push_back(
                  ResultVersion(key.user_key(), Slice(), lo_, hi_));
              break;
          }

          // Advance to next (earlier) version
          hi_ = lo_;
          iter.Next();
          if (!iter.Valid()) {
            // Check next key in key range
            break;
          }
          entry = iter.key();
          key_ptr = GetVarint32Ptr(entry, entry + 5, &key_length);
          lo_ = DecodeFixed64(key_ptr + key_length - 8);
          if (comparator_.comparator.user_comparator()->Compare(
                  Slice(key_ptr, key_length - 16), key.user_key()) != 0) {
            // Next key
            break;
          }
        }
      }
    }
  }
  return result_set->size() > init_result_set_size;
}

}  // namespace leveldb
