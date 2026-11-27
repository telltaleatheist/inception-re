# .BLD: the places' scripts

Every place you can enter (the Citadel, the training center, the shops and so on) is a script in
a .BLD file: the place's text and menus, and what each choice does. This page is the file. The
script language is in [scripts.md](../scripts.md). `src/formats/bld.cpp` reads it, and
`tools/bld.py` lists one from the player's own copy (nothing it prints goes in the repo).

## The files, by place number

The entrance code (docs/scripts.md, "Entering a place") loads a place by number through a table
of far pointers to the names, BTECH.EXE DGROUP:4ec2:

| # | file | # | file | # | file | # | file |
|---|------|---|------|---|------|---|------|
| 00 | TRAINING | 07 | LOUNGE | 0e | MAYOR | 15 | HUT |
| 01 | CITADEL | 08 | GARAGE | 0f | WEAPON2 | 16 | ENDMECH |
| 02 | COMSTAR | 09 | HOSPITAL | 10 | THEATER | 17 | INSTRUCT |
| 03 | WEAPON | 0a | ARENA | 11 | FROB | 18 | FINDIT |
| 04 | ARMOR | 0b | PARTY | 12 | VIEWDISK | 19 | WINSCENE |
| 05 | REPAIR | 0c | CLOTHES | 13 | BARRACK2 | | |
| 06 | BARRACKS | 0d | JAIL | 14 | ENTRANCE | | |

## Layout

    +0   u16   length of the script, little-endian, NOT enciphered
    +2   length bytes of script, enciphered

In DOS 1.03 every file is exactly 2 + length bytes. The loader (BTECH.EXE 273d:063b) opens the
file, reads the length word, and reads that many bytes into the script buffer (2892:00a0 at run
time, 0x2328 = 9000 bytes). If the file can't be opened it asks for the game disk and tries again
(1000:2913); the rebuild just reports the missing file.

## The cipher

`17dc:1d30` deciphers the buffer in place: each byte becomes

    plain = ((enciphered + 0x29) mod 256) XOR 0xE9

It deciphers all 9000 bytes of the buffer, not only the script's, which is harmless. The largest
script (HUT, 8499 bytes) fits; a longer one would overflow the buffer. The rebuild deciphers just
the script and has no size limit.

## The script

Offsets in the script count from its first byte (the byte after the length word). Every jump in
the script is an absolute offset of that kind. Execution starts at offset 0. The bytecode is
opcodes E4–FF with their operands. The text is inline after the print opcode (FC), ends at a NUL,
and uses the printer's codes (docs/text.md: 0d new line, 06 c colour, 02 c background, 13 n tab,
and so on).

### Walking a script

Two opcodes end in a jump table with no count:
- The menu (F9 m) has one word per item of menu m, and the count is in the program's menu table
  (385b:0092, imported into `tables.json`: [tables.md](tables.md)).
- The flag switch (F3 f) has one word per value flag f can take, and nothing says how many. In
  every original script the table ends where the code for its first forward entry begins, and
  that is how `formats::decode` and `tools/bld.py` find the end. The interpreter never needs the
  size: it just indexes the table.

A walk of the control flow from offset 0 reaches every byte of every DOS 1.03 script except these
(checked by the `bld_walk_reaches_every_text` test):
- the two zero bytes at the end of every file;
- WEAPON 0160–021f: a block that `goto 0220` at 015d jumps over, so the shopkeeper's line in it
  (the print at 0161, likely his reply to a suit turned down) and the menu heading after it (the
  print at 0208) are never shown (an original quirk; the rebuild keeps it, see docs/scripts.md);
- TRAINING 1be4–1be5: two bytes after an `end`.

## Text IDs

The importer (docs/import.md) walks every script and writes each text it can print as
`bld.<file>.<offset>`, with the print opcode's offset in four hex digits. For example, the
Citadel's first text is `bld.citadel.0005`, and its address in `text/english.json` is
`CITADEL.BLD+0006`, the text's own offset. Texts that can never be reached (WEAPON's dead block)
are not imported. The interpreter prints a text by its ID, so an override file can replace any of
them; the script logic itself is still read from the player's .BLD at run time.

## Other descriptions

- **Elthial/CrescentHawksTools (MIT).** `BldDisassembler.cs` and
  `docs/formats/INCEPTION_FORMATS.md` agree on the length word, the cipher, the operand sizes and
  the tables without a count.
- **velteyn/UnBattletech (GPL-3).** `docs/formats/bld-bytecode.md` and `bld-opcode-coverage.md`
  agree on the cipher and on most opcodes. They describe an 8-byte header (a file type, a
  paragraph count and a signature EE C6 EB EA), plaintext metadata to 0x9F, and code from 0xA0.
  The program does not read the files that way. The 0xA0 is the buffer's offset in memory (the
  loader reads the script to 2892:00a0), and bytes 0–1 are the length word. Their "substitution
  cipher" on the text is the same add-and-XOR cipher seen from the enciphered side. Their
  operand counts for E8 and ED differ between their two pages; the program reads 3 and 4 bytes.
