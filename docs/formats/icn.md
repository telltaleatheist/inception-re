# .ICN: tile sheets

The game ships five `.ICN` files: `BTTLTECH.ICN`, `DESTRUCT.ICN`,
`STARLEAG.ICN`, `MAP.ICN` and `ANIMATE.ICN`. Each is a [.CMP](cmp.md)
container (same 2-byte length, type byte and run-length code; all five are
type 1, row order). The program loads them with the same loader (273d:063b)
and unpacker (273d:049d) as the pictures. The difference is what the 32,000
unpacked bytes mean.

## Layout

The unpacked bytes are **250 tiles of 16x16 pixels, one after another**:

| Bytes | What |
|---|---|
| n × 128 … n × 128 + 127 | tile *n*: 16 rows of 8 bytes, two pixels to a byte, high nibble on the left |

The map drawer shows this (287f:1af4 and 287f:1b1a). It turns a cell's byte
into an offset by multiplying by 128 (in the code: the tile number in a
register's high byte, shifted right once), adds the sheet's base, and copies
16 rows of 8 bytes. A picture viewer that shows an `.ICN` as a 320x200
screen gets each tile smeared across 3.2 rows of the picture. That is not
how the program reads it.

Colour numbers go through the same palette as everything else (see cmp.md,
"Palette"). The map uses the standard table at 37e8:0000.

For EGA the program converts the whole sheet into planes in spare video memory
at A400 (287f:0260) and copies tiles from there with the EGA latches. That is
the same data in a different form.

## Which sheet is used where

| File | Loaded by | Used for |
|---|---|---|
| `BTTLTECH.ICN` | 1000:4621 (and 1000:320b) | the town and area maps: every map except 11 and 14 |
| `DESTRUCT.ICN` | 1000:2da8, only for map 11 | map 11 |
| `STARLEAG.ICN` | 1b5d:0004 (which also loads map 14), 1b5d:079c, 24d3:0004 | map 14 (the cache, docs/cache.md) and the ending's screens |
| `MAP.ICN` | 1b5d:055a (with `MAP15.MTP`) | the map 15 screen (not read yet) |
| `ANIMATE.ICN` | 1000:320b | The animated tiles' frames. 1000:320b copies 0x780 words of it (30 tiles, from tile 1) to a buffer of its own: three frames for each of the 10 animated tile numbers, tile 1 + 3i + frame (docs/town_loop.md). |

## Cross-checks

Elthial's CrescentHawksTools (MIT) also decodes `.ICN` as 16x16 tiles of 128
bytes in sequence and lists the same sheets as tile sets. We agree on the
layout. Its tile-set choice per map (map 11 DESTRUCT, map 14 STARLEAG, the
rest BTTLTECH) matches what the program's loaders show.
