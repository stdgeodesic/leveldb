MemTable API
============

This file logs the changes on MemTable and Skip List (the underlying core data structure of MemTable) of MVLevelDB, compared to LevelDB.

Skip List
---------

No changes yet. Since the Skip List, as an underlying data structure, has no knowledge of the high-level data entry structure (data stored in Skip List are just byte strings. The structured information, e.g., user key, value, etc., are parsed AFTER they are retrieved from the Skip List), we did not make any changes to Skip List.

MemTable
--------

We did NOT create a separated MemTable class to host multi-version data entries.

Two main interfaces of MemTable in LevelDB:

- `Add(SequenceNumber seq, ValueType type, const Slice& key, const Slice& value);`
- `bool Get(const LookupKey& key, std::string* value, Status* s);`

NEW interfaces added in MVLevelDB:

- `void AddMV(SequenceNumber seq, ValueType type, const Slice& key, ValidTime vt, const Slice& value);`
- `bool GetMV(const MVLookupKey& key, ValidTime vt, std::string* value, Status* s);`
