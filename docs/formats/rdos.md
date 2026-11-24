# RDOS .dsk: the Apple II floppy

The Apple II version comes on two 140K disks, saved as .dsk images: 35 tracks of 16 sectors
of 256 bytes (143,360 bytes), in DOS 3.3's sector order. The disks don't use DOS 3.3's file
system but RDOS 3.3, a small one of their own. `src/formats/rdos.cpp` lists their files
(`read_rdos`). docs/versions.md says which files the game keeps there.

## The layout

Sectors are numbered across the disk, 0 to 559 (track × 16 + sector).

- **Track 1** is the catalog. It starts with a 32-byte header whose first characters,
  high-bit ASCII, are `RDOS`.
- **Each 32-byte entry** after it names one file. An entry whose first byte is below 0xA0 is
  unused.

  | Bytes | What |
  |---|---|
  | 0–23 | the name, high-bit ASCII, padded with spaces |
  | 24 | the type, a letter (high-bit ASCII) |
  | 25 | the length in sectors |
  | 26, 27 | the load address, little-endian |
  | 28, 29 | the length in bytes, little-endian |
  | 30, 31 | the first sector, little-endian |

- **A file** is its sectors one after another from the first, so its bytes are a single
  run of the image.

## What the reader checks

The size and the `RDOS` mark; every entry's sectors are inside the disk and its byte length
fits its sector count. Names keep their letters; `/` and control characters become `_`.
