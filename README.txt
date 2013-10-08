jaguarfs
========

Versioning file system done as project work.
It is based on Elephant, On-demand snapshot, and block level versioning.

Current Status
--------------
o ls working
o mkdir, rmdir working
o touch working
o cp, cat (create/read files) working
o Multiple level block indirection in inode working
o Inode/Data bitmaps that are more than 1 block size handled
o Tested with big disk (1 GB) and big files
o Inodes and super block buffers are marked dirty, and later synced
  by VFS. No synchronous writes done (unless otherwise required).
o statfs done

TODO
----
o Testing with FS benchmarks
