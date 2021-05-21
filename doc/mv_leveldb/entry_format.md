MVLevelDB Data Format
=====================

Helper classes (Internal Keys and Lookup Keys) in MVLevelDB are different from LevelDB.

Data Entry
----------

Internal Key in MVLevelDB:

    user_key       Slice
    sequence|type  uint64
    valid_time     uint64

Internal Key in LevelDB:

    user_key       Slice
    sequence|type  uint64

Corresponding helper functions used to handle Internal Keys are defined in `db/dbformat.[h/cc]`.
