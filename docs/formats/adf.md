# .adf: the Amiga floppy

The Amiga version comes on an Amiga DOS floppy, saved as an .adf: the disk's blocks one
after another, 512 bytes each, 1,760 of them (901,120 bytes; a high-density disk has twice
as many). `src/formats/adf.cpp` lists its files (`read_adf`) and reads its volume name
(`adf_volume`). docs/versions.md says which files the game keeps there.

## The layout

All numbers are big-endian.

- **Block 0, bytes 0–3**: `DOS` and a flags byte, 0–7. Bit 0 set means the Fast File
  System (FFS); clear, the original one (OFS, which the game's disk uses).
- **The root block** is in the middle of the disk: block 880 (1,760 on a high-density disk).
  Its type (bytes 0–3) is 2, a header; its secondary type (the block's last 4 bytes) is 1.
- **A folder** (the root or any other) has a hash table of 72 block numbers at byte 24. Each
  names the first entry whose name hashes there; the entries with the same hash are chained
  through byte 496 of each entry.
- **An entry** is a header block, type 2. Its secondary type says what it is: 2 a folder,
  −3 a file. Its name is at byte 432 (a length byte, then up to 30 characters).
- **A file's header** holds its size at byte 324.
  - OFS: the first data block's number at byte 16. Each data block starts with a 24-byte
    header: its byte count at 12, the next data block's number at 16; then up to 488 bytes of
    data.
  - FFS: the header's table at byte 24 lists the data blocks, from its last entry backwards;
    each data block is 512 bytes of data. A longer file goes on in extension blocks (type 16),
    named at byte 504, with tables of their own.

## What the reader checks

The size, the `DOS` mark and the root block's types; every block number is inside the disk;
no chain (a folder's, a data chain, an extension chain) comes back to a block it has
visited; folders nest at most 16 deep and a disk holds at most 4,096 files; a file's chain
holds at least its size in bytes. Anything else stops the reader with an error, never a
read outside the image. It does not check the blocks' checksums.
