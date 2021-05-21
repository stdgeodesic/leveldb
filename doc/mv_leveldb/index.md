# MVLevelDB manual

## Original LevelDB interfaces and Usage

The documentation of original LevelDB is hosted here: [https://github.com/google/leveldb/blob/master/doc/index.md](https://github.com/google/leveldb/blob/master/doc/index.md).

### Preamble and Terminologies

- User: the user of LevelDB, usually refers to the _developer_ of an application which uses LevelDB as a storage backend. This does _NOT_ refer to the end users (a.k.a. the sensors of an IoT network) of those applications in this doc.
- Slice: a string-like class. By default, LevelDB stores key and value as Slices.
- Comparator: a C++ class, which provide a `Compare(a, b)` method to determine the order of `a` and `b`, where `a` and `b` are two user keys, normally can be arbitrary string. LevelDB has a default comparator which is a Bytewise comparator. The users can write their own comparator to replace the default one.
- Put/Insert: insert a key-value pair to the database.
- Tag: is a enum Value, whether is `0x1` (indicates that this is an insert with an value) all `0x0` (indicates that this is a deletion operation).
- Delete: in LevelDB, the user is _NOT_ to purge an entry manually. Thus the Delete operation is actually Put/Insert an dummy version (with deletion mark tag=`0x0`) with no value into the database.
- Sequence Number: is an incremental 56-bit integer. Each write operation (i.e., Insert or Deletion) is associated with a sequence number.
- LevelDB has been implemented in maybe several languages, we are doing our modification based on the actively maintained C++ version.
- We are changing the internal implementation of LevelDB to:
  - preserve the original LevelDB interfaces (backward compatibility)
  - add some special interfaces to serve multi-version functionalities.

### Usage

To use LevelDB, the _user_ should:

1. Install the LevelDB library (or build from source);
2. In developing their own C++ application, include the LevelDB library, and create an instance (object) of `leveldb::DB` class to use the LevelDB library. We assume the name of the `leveldb::DB` instance is `db`.
3. There are some options that the user can change. User can create an `leveldb::Options` instance (hereinafter referred to as `options`), and set the values, e.g., `options.write_buffer_size = 400 * 1024 * 1024`, which means the maximum size of MemTable, or `options.comparator = UserComparator`, which means that the user can define their own comparator, etc. Finally, when Open (or Create) a new database, the user gives the `options` as a parameter of the `Open()` method.

### Interfaces

In summary, the original LevelDB provides these interfaces:

- `db->Open(options, dbname, &db)`, Open or Create a new database, where `dbname` is the path of the database.
- `db->Put(key, value)`, to insert a key-value pair into the database.
- `db->Delete(key)`, actually a wrapper of `Put(key, no_value)`.
- `db->Get(ReadOptions, key, &value)`, to search in the database for key `key` and if found, store the corresponding value in `value`. In ReadOptions, the user can define the maximum sequence number to be searched. All the entries later than the specified sequence number will be ignored. The default option is to search all the data.
- `db->NewIterator(ReadOptions)`, returns an `leveldb::Iterator` instance that provides methods like `SeekToFirst()`, `Next()`, `Prev()`, etc.
- `leveldb::WriteBatch` provides atomic updates. The user can create a batch, put multiple operations in the batch, and then bulk apply the batch to the database. LevelDB guarantee that the batch operation is _atomic_ (all or none).
  - In fact, the `Put()` and `Delete()` interfaces are creating internal WriteBatches which contains only one single entry and applying the batch to the database.

## MVLevelDB: additional interfaces

In MVLevelDB, we have additional interfaces to handle Multi-Version operations, including

- `options.multi_version`, when set to `true`, enable multi-version support. default is `false`.
- `db->PutMV(key, vt, value)`, where `vt` is the `start valid time` of the data entry.
- `db->GetMV(key, vt, &period, &value)`, which period is the valid time period of the found value. And value found will be stored in `value` variable.
- `db->GetMV(key_range, vt_range, &period, &value)`, *Not stable and To be optimized*. This is the variant of `GetMV()` to serve range query and return a set of data entries.

To implement these interfaces, we have created a set of internal classes to handle data entries that associated with timestamp values. We will not discuss the internal changes in details here.

## Building and Testing MVLevelDB

We use the same procedure as LevelDB to build MVLevelDB from source.

```bash
git clone -b lsmv2 --recurse-submodules git@github.com:stdgeodesic/leveldb.git mvleveldb
cd mvleveldb

mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build .
```

We use GoogleTest scripts to validate the correctness of our method. In the test scripts, we write assertions to validate the result of operations.

For example, if we put a key-value pair into the database and set the start valid time as `100` : `Put("k1", 100, "v1");`, Then we query for that data entry: `GetMV("k1", 100, period, value);`, the result in `value` should be **equal** to `"v1"`. That is, the assertion `ASSERT_EQ("v1", GetMV("k1", 100))` should not raise any exception. Here a _GoogleTest script_ is a C++ source file that contains a set of designed Test Suites with multiple assertions.

The commands above will compile a set of executables with file name end with `_test`.
We put the multi-version interface test in `db_mv_test`.

### Test Suite Examples

- `Empty, EmptyKey, EmptyValue`: test edge cases, e.g., the key, or value, or both, are empty strings.
- `ReadWriteOldVersion`: test basic multi-version functionalities, e.g., put some (k1, v1) at time 100, then put (k1, v2) at time 200; When query for (k1, 150) we should get v1 and query for (k1, 200) we should get v2, etc.
- `GetFromImmutableLayer`: after we insert some data into the database, we force a minor compaction (which means mem -> imm). In this case, the MemTable will be converted to a Immutable MemTable, waiting to be flushed to disk. There are some latest data versions with undetermined upper valid time. They will be duplicated in the newly created empty MemTable. This test suite is to validate the query result after the minor compaction.
- `GetFromVersions`: after we insert some data into the database, we force a compaction that flush the Immutable MemTable to on-disk files, and then validate the correctness of get data entries from on-disk files.

The output of a `_test` executable should looks like:

```txt
[==========] Running 7 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 7 tests from DBTest
[ RUN      ] DBTest.Empty
[       OK ] DBTest.Empty (225 ms)
[ RUN      ] DBTest.EmptyKey
[       OK ] DBTest.EmptyKey (210 ms)
[ RUN      ] DBTest.EmptyValue
[       OK ] DBTest.EmptyValue (208 ms)
[ RUN      ] DBTest.ReadWriteOldVersion
[       OK ] DBTest.ReadWriteOldVersion (204 ms)
[ RUN      ] DBTest.PutDeleteGetOldVersion
[       OK ] DBTest.PutDeleteGetOldVersion (207 ms)
[ RUN      ] DBTest.GetFromImmutableLayer
[       OK ] DBTest.GetFromImmutableLayer (211 ms)
[ RUN      ] DBTest.GetFromVersions
[       OK ] DBTest.GetFromVersions (278 ms)
[----------] 7 tests from DBTest (1543 ms total)

[----------] Global test environment tear-down
[==========] 7 tests from 1 test suite ran. (1543 ms total)
[  PASSED  ] 7 tests.
```

## Performance

We use Google Benchmark script to evaluate the performance of MVLevelDB.

The source code of the benchmark script is `benchmarks/db_mv_bench.cc`, and the compiled executable is `db_mv_bench`.

The script contains the followings:

- static flags, e.g., number of key/values to place in database, block size, write buffer size, etc.
- wrappers and helper functions, e.g., wrapper of the comparator, or random key generator, etc.
- The `Benchmark` class. This class has a set of member methods like WriteSeq, WriteRandom, ReadSequential, ReadReverse, etc.
- The `Benchmark` class also has a `Run()` method, which is the main entrance of benchmark. Given a benchmark name (e.g., fillseq), The `Run()` method create a new fresh database and call the corresponding method (e.g., WriteSeq()), record the total time of the run, and then destroy the database after running.
- The main function. By default run all the benchmark items one by one.

### Benchmark setup

- Firstly we test the performance when `options.multi_version` is set to true, compared to that set to false. Both benchmarks run original LevelDB interfaces, i.e., Put, Get without timestamp version.
- Then we use multi-version interfaces to fill the database.

Non-MultiVersion operations with multi-version switch disabled:

```txt
LevelDB:    version 1.23
Keys:       16 bytes each
Values:     100 bytes each (50 bytes after compression)
Entries:    1000000
RawSize:    110.6 MB (estimated)
FileSize:   62.9 MB (estimated)
WARNING: Snappy compression is not enabled
------------------------------------------------
fillseq      :       5.530 micros/op;   20.0 MB/s     
fillsync     :   22260.182 micros/op;    0.0 MB/s (1000 ops)
fillrandom   :       7.826 micros/op;   14.1 MB/s     
overwrite    :       9.456 micros/op;   11.7 MB/s     
readrandom   :       4.024 micros/op; (864322 of 1000000 found)
readrandom   :       3.933 micros/op; (864083 of 1000000 found)
readseq      :       0.626 micros/op;  176.8 MB/s    
readreverse  :       2.368 micros/op;   46.7 MB/s    
```

Non-MultiVersion operations with multi-version switch enabled:

```txt
LevelDB:    version 1.23
Keys:       16 bytes each
Values:     100 bytes each (50 bytes after compression)
Entries:    1000000
RawSize:    110.6 MB (estimated)
FileSize:   62.9 MB (estimated)
WARNING: Snappy compression is not enabled
------------------------------------------------
fillseq      :       6.703 micros/op;   16.5 MB/s     
fillsync     :   22468.925 micros/op;    0.0 MB/s (1000 ops)
fillrandom   :       6.833 micros/op;   16.2 MB/s     
overwrite    :       8.034 micros/op;   13.8 MB/s     
readrandom   :       3.642 micros/op; (864322 of 1000000 found)
readrandom   :       3.603 micros/op; (864083 of 1000000 found)
readseq      :   85295.333 micros/op;    0.0 MB/s
readreverse  :       9.147 micros/op;   12.1 MB/s    
```

Fill the database with multi-version data entries (with timestamp info):

```txt
LevelDB:    version 1.23
Keys:       16 bytes each
Values:     100 bytes each (50 bytes after compression)
Entries:    1000000
RawSize:    110.6 MB (estimated)
FileSize:   62.9 MB (estimated)
WARNING: Snappy compression is not enabled
------------------------------------------------
fillseq      :       7.237 micros/op;   15.3 MB/s     
fillseq(mv)  :       3.975 micros/op;   29.7 MB/s     
fillsync     :   22704.868 micros/op;    0.0 MB/s (1000 ops)
fillrandom   :       7.092 micros/op;   15.6 MB/s     
fillrandom(mv) :       3.739 micros/op;   31.6 MB/s   
overwrite    :       7.479 micros/op;   14.8 MB/s     
readrandom   :       3.405 micros/op; (632136 of 1000000 found)
readrandom   :       3.351 micros/op; (631252 of 1000000 found)
readseq      :  216543.000 micros/op;    0.0 MB/s
readreverse  :       5.816 micros/op;   19.0 MB/s   
```