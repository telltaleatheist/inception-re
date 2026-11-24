# The font, and other data read from BTECH.EXE

## Where it lives

The game's 8x8 font is not a file: it is 1,024 bytes inside BTECH.EXE, at 2c6c:a661 in the
unpacked program (image offset 0x26d21; file offset 0x2a121 in the unpacked file, whose header is
0x3400 bytes). 128 characters, 8 bytes each, one byte per row, the top bit the leftmost pixel.
`273d:00d5` masks every character with 0x7f before looking it up. Characters 2 and 6 are
pictures (an arrow and a check mark) but inside printed text those bytes are colour codes
(docs/text.md).

The EGA driver draws a character with `287f:2251`; the CGA and Tandy drivers draw with
`287f:2209` and `287f:21a8` from a widened copy made by `287f:2cf7`. All of those are display
drivers, replaced by the platform layer.

## The rebuild's font file

The importer (docs/import.md) writes the font as `font/font.txt` in the data folder, a plain
text picture that can be read and edited by hand:

    chr-font 1
    size 8 8
    glyph 65
    ..##....
    .####...
    (8 rows of 8: # set, . clear; then the next glyph)

`size` can be anything up to 64x64, so a mod can bring a bigger font. Characters 0-127; a
missing glyph is blank. The asset provider answers `{"FONT", c}` from this file with character c
in white on transparent, and the renderer colours it. The text layer measures its cells from the
font's glyphs rather than assuming 8x8.

## Our own font

A copy with no font in it (the Amiga's, docs/formats/amiga.md) is shown with `data/font/font.txt`,
in the same form: 8x8 cells, so lines break and windows fit as with the original's. It was drawn
for this project (MIT, as the rest of the repository) and packaged beside the program; the asset
provider reads it when the data folder has no `font/font.txt`. It has the characters 32-126 and the
pictures the game prints: 1-8, the arrows from N clockwise (the path arrows and the overhead map's
pointer). No glyph's cell is the same as DOS's but the space; eleven plain shapes (such as the
brackets, the plus, I and l) are the same when moved within the cell.

## Frame pieces: BTBORDER.CMP

At start-up (`1527:0044`) the program unpacks BTBORDER.CMP and cuts 8 cells of 8x8 pixels from it
(`2656:0ae5`), left to right from the top-left corner (a row of cells is 40 across). Those are
the frame pieces 0-7 of docs/text.md. The provider answers `{"BTBORDER.CMP:8x8", n}` with cell n.
TINYLAND.CMP (0x42 cells) and MECHSHAP.CMP are cut the same way, for later.
