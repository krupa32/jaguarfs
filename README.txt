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

TODO
----
o Whenever inode information changes, it is synchronously updated on disk.
  This has to be changed so that mark_inode_dirty() is used, which results
  in VFS asynchronously calling superblock->write_inode to update the inode
  information. It is observed that VFS calls write_inode() at some later time.
  But it definitely calls it on sync command.

o Indirect blocks are not handled. Only direct block mapping works as of now.
