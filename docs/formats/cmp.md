# .CMP: compressed full-screen pictures

The game ships seven `.CMP` files. Each holds one 320x200 picture in 16
colours, run-length compressed:

| File | Order | Where the program uses it |
|---|---|---|
| `INFOCOM.CMP` | columns | the publisher's logo, shown first at start-up |
| `BTTITLE.CMP` | columns | the title screen |
| `BTBORDER.CMP` | rows | the screen frame, cut into pieces |
| `TINYLAND.CMP` | rows | a sheet of map tiles, cut into pieces |
| `MECHSHAP.CMP` | columns | a sheet of 'Mech shapes, cut into pieces |
| `BTSTATS.CMP` | columns | the statistics screen |
| `ENDMECH.CMP` | columns | the ending |

The five `.ICN` tile sheets use the same container. Only the meaning of the
unpacked bytes differs (see [icn.md](icn.md)).

(The uses come from the code that names each file: the start-up routine
1527:0044 and the title routine 1000:46a7. The sheets are cut by 273d:070a,
which is not read yet.)

## Layout

| Offset | Size | What |
|---|---|---|
| 0 | 2 | length of the rest of the file, little-endian (always file size − 2) |
| 2 | 1 | type: 1 = row order, anything else = column order (the shipped files use 1 and 2) |
| 3 | length − 1 | the run-length stream |

The loader (273d:063b) reads the length, then exactly that many bytes into one
buffer. The unpacker (273d:049d) looks at the buffer's first byte to choose
between two routines (287f:22f8 for rows, 287f:2368 for columns).

## The run-length code

The stream is a series of codes. Each code byte is read as a signed number:

- **1 to 127**: that many bytes follow and are copied as they are.
- **−1 to −128**: the next byte is repeated that many times (the absolute value).
- **0**: a 16-bit little-endian count follows, then the byte to repeat. A count
  of 0 behaves as 65,536, so it fills the rest of the picture.

Unpacking always produces 32,000 bytes and stops the moment the last one is
written, even mid-run. Nothing marks the end of the stream. In all seven
shipped files the stream ends exactly where the 32,000th byte is written.

## Where the bytes go

The 32,000 bytes are the picture in the program's own working form: 200 rows
of 160 bytes, two pixels to a byte. **The high nibble is the left pixel.** Each
nibble is a colour number from 0 to 15.

- **Row order** (type 1): bytes fill the picture left to right, top to bottom.
- **Column order** (other types): bytes fill one byte-column (two pixels wide)
  from row 0 to row 199, then move to the next byte-column on the right.

The same file serves every display. Only what happens after unpacking differs.

## Palette

A `.CMP` holds colour numbers only. There is no palette in the file. Before
showing a picture the program sets the palette from a table of its own
(273d:0525). The caller passes the table's address. The Infocom and title
screens use the table at 37e8:0000 (MCGA: 37e8:0010).

Special 45 (24d3:15dd) shows ENDMECH.CMP with the table at 3858:0000 (MCGA: 3858:0010), which
nothing else uses; it differs from the standard one in colours 5 and 13. It is imported as
`endmech_palette`, and the original-files provider colours ENDMECH.CMP with it.

The rebuild shows every other picture with the standard table for now. (The colours are imported as
the copy shows them; a copy whose pictures have other palettes says so by the picture's name,
`GameSource::picture_palette`.) Still to do:

- find the screens that set a palette of their own (`15ab:1afe` puts the standard one back after
  them, so there are some), and the tables they use;
- import those tables and key each by its picture's file name in the original-files provider,
  as ENDMECH.CMP's is. The game names only the picture; which colours the original showed it in
  is the provider's business, so a mod provider supplies its own art with no palette at all.

## The four displays

The start-up menu (1527:0044) sets the display variable at DG:4fba from the
key pressed: 1 → 0, 2 → 1, 3 → 2, 4 → 3.

| Choice | Value | Display | What the program does with the unpacked picture |
|---|---|---|---|
| 1 | 0 | CGA or 64K EGA, 320x200 in 4 colours | 287f:0163 packs every two bytes (four pixels) into one CGA byte through 256-entry lookup tables. It switches between two tables on alternate rows, which gives a dither. Then 287f:200e copies the result into the two interleaved CGA banks at B800. The palette is not set. |
| 2 | 1 | Tandy 1000, 320x200 in 16 colours | Tandy's 16-colour mode is already two pixels to a byte with the high nibble on the left. 287f:200e copies the rows as they are into the four interleaved banks at B800. The palette comes from the same table as EGA, through 287f:2b45 (not read yet). |
| 3 | 2 | 256K EGA/VGA, 320x200 in 16 colours (mode 0Dh) | 287f:0260 turns every four packed bytes (eight pixels) into one byte in each of the four bit planes, written to the off-screen page at A800. 287f:245c then copies that page to the screen at A000. The palette is set below. |
| 4 | 3 | MCGA, 320x200 in 256 colours (mode 13h) | 287f:200e writes each packed byte as two screen bytes: the high nibble times 16 for the left pixel and the low nibble for the right. 287f:0fee therefore sets each palette entry *n* at both DAC registers *n* and 16·*n*. |

### EGA/VGA (the rebuild's reference)

For EGA the palette table has 16 bytes, one per colour number. Each byte is the
standard 16-colour number to show (0 black, 1 blue, … 6 brown, … 15 white).
273d:0525 moves 8–15 up by 8 before writing each palette register (287f:022a),
because in the 200-line modes an EGA card takes bit 4, not bit 3, as the
bright bit. The table the title screens use is the standard order with three
changes: colour number 1 shows black, and 5 and 9 swap (5 is light blue and 9
is dark blue).

### MCGA

The MCGA table has 16 two-byte entries. The first byte's high nibble is green
and its low three bits are blue. The second byte is red. Each value from 0 to 7
goes through an eight-step table (P:a459) to a 6-bit DAC level
between 0 and 63. MCGA therefore has its own colours, not the EGA ones. The
rebuild does not use them yet.

## In the rebuild

`src/formats/cmp.cpp` reads the file into `CmpPicture` (the 32,000 packed bytes
and their order) and gives one colour number per pixel. `src/formats/palette.cpp`
turns the stored EGA numbers into colours, at import. The default asset provider
(`src/platform/original_assets.cpp`) combines them into an RGBA image for the
renderer, using the standard palette unless the source or `main.cpp` (ENDMECH.CMP) gives one for that
file name. CGA's 4-colour reduction and the Tandy and MCGA colours are not
built, because the rebuild always shows the 16-colour EGA picture.
