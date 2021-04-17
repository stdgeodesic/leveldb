Write Batch
===========

Changes of WriteBatch in MVLevelDB compared to LevelDB. We created a new class named WriteBatchMV and a set of corresponding helper classes and functions, used to build batches when called by upper level MVLevelDB functions (e.g., `db->Put(key, time, value)`). We left the original WriteBatch class unchanged.

Data format of WriteBatchMV in MVLevelDB:

    // WriteBatchMV::rep_ :=
    //    sequence: fixed64
    //    count: fixed32
    //    data: record[count]
    // record :=
    //    kTypeValue ValidTime varstring varstring         |
    //    kTypeDeletion ValidTime varstring
    // varstring :=
    //    len: varint32
    //    data: uint8[len]

Data format of WriteBatch in LevelDB:

    // WriteBatch::rep_ :=
    //    sequence: fixed64
    //    count: fixed32
    //    data: record[count]
    // record :=
    //    kTypeValue varstring varstring         |
    //    kTypeDeletion varstring
    // varstring :=
    //    len: varint32
    //    data: uint8[len]

Helper functions for WriteBatchMV are defined in:

- `include/leveldb/write_batch.h`
- `db/write_batch.cc`
- `db/write_batch_internal.h`

(New) Unit tests for MVLevelDB:

- `db/write_batch_mv_test.cc`
