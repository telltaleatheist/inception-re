# Importing from the player's BTECH.EXE

Some of what the game shows is not in a data file but inside the program: the font and much of
the text. The rebuild never ships any of it (docs/rules.md, rule 1). Instead, an importer reads
the player's own BTECH.EXE once and writes that data as plain files in the player's data folder.
After that the game reads only those files; BTECH.EXE is needed only to import.

## When it runs

- By itself, the first time `chr` starts, and again whenever the data folder's import is missing
  or comes from another import version (`kImportVersion` in `src/formats/import.h`).
- On demand with `chr --import`, which imports again and quits.

If an import is needed and BTECH.EXE is missing or is not DOS 1.03, `chr` stops with a message
that says so and how to run the importer.

The data folder is `--data-dir DIR`, else the `CHR_DATA_DIR` environment variable, else the
player's folder (on macOS `~/Library/Application Support/BattleTech/Inception Rebuild`). A
folder under an earlier name is moved to it once (docs/setup.md, "The player's folder").

## What it writes

    import.json          {"import_version": 29, "source": "BTECH.EXE DOS 1.03", "exe_sha256": ...,
                          "table_sha256": ...}
    text/english.json    {"texts": [{"id": ..., "address": "ssss:oooo", "text": ...}, ...]}
    font/font.txt        the font (docs/formats/font.md)
    rules/mechs.json     the 'Mech templates   } the rule tables built into the program,
    rules/weapons.json   the weapon table      } with named fields
    rules/combat.json    the combat tables     } (docs/formats/mech.md)
    rules/quiz.json      the training center quiz's ten parts: x, y and answer (docs/combat.md 7)
    rules/shops.json     the shops' prices: armour, armour points, weapons (docs/scripts.md)
    tables.json          the program's small tables: layouts, offsets, masks, scripts
                         (docs/formats/tables.md)

The places' texts come from the player's .BLD files (docs/formats/bld.md): every text a script
can print, as `bld.<file>.<offset>` with the address `FILE.BLD+offset`. All 26 .BLD files must
be there. The names of the 'Mech templates and the weapons are texts too (`mech.<id>`,
`weapon.<n>`), read from inside the rule tables.

`import.json` is written last, so an interrupted import counts as missing.

chr imports again when `import.json` has another `import_version` (the files changed shape) or
another `table_sha256`: a hash of the import table itself, every piece's kind, ID, address, size
and expected hash (`import_table_hash`). A text ID added to the table is therefore imported into
an older data folder without anyone remembering to bump the version.

The versions: 2 the direction names; 3 the panel's money label; 4 the .BLD texts; 5 the class and talk texts;
6 the start-up's; 7 the rule tables, the 'Mech and weapon names, the combat texts; 8 the table
hash in `import.json`; 9 `rules/quiz.json`; 10 `tables.json`; 11 the round's tables in it; 12 the shot's poses and scripts;
13 the orders screen's path arrows; 18 the area-to-map table; 19 the random encounter's tables; 20 the plural "s", the full stop and the game-over texts; 21 the healing texts and dice table; 22 the repair phrases by Tech level; 28 `rules/shops.json` and the shops' and ComStar's texts; 29 the Arena's tables and texts.

## How it reads the program

`src/formats/exepack.cpp` unpacks BTECH.EXE in memory, with the same steps as
`tools/unexepack.py`. The importer then accepts the copy in two steps:

1. **The layout.** The unpacked program must have DOS 1.03's size, relocation count, header size,
   entry point and stack, and its header (with the relocation table) must hash to DOS 1.03's. Then
   every address in the import table means what the table says.
2. **The pieces.** Each piece in the import table (the font, every text, every rule table) carries
   the first 16 hex digits of its own SHA-256, and each must match. A copy that fails is refused
   with a message naming the pieces that differ.

Bytes the rebuild does not import are not checked, so a copy of DOS 1.03 that differs from ours
only elsewhere imports cleanly. The whole-file hash in docs/original.md describes the copy this
project was studied on; it is not used to accept a copy. `import.json` records the player's own
copy's SHA-256.
`src/formats/exe_data.cpp` holds the import table: each piece's name (for text, its text ID),
segment:offset and length. The repo ships that table, never the text or the tables' values. A text is read the way the
printer reads it (docs/text.md): it ends at a NUL that is not a code's argument, or after a byte
with the top bit set.

## Through the source

The importer takes the copy as a source (`GameSource`, docs/versions.md "One source by ID"): the
program's pieces by the import table's IDs (`GameSource::program()`, src/formats/program_data.h)
and the places' scripts (`GameSource::script`). A DOS copy's program is BTECH.EXE, read as above.
Another version's reader hands out the same IDs in DOS's forms, so the files written are the
same shape whichever copy the player has; `import.json`'s `source` and `exe_sha256` name the
program it came from. The Amiga's is `AmigaProgramData` (docs/formats/amiga.md, "The texts"):
so far it gives the texts only.

## Overrides

A text file of the same form layered on top (`chr --text FILE`) replaces any ID, for a translation
or a rewrite: `{"texts": [{"id": "walk.too_deep", "text": "..."}]}`. Other members, such as
"address", are ignored. Control codes are written as `\u00NN` and new lines as `\r`, as in the
imported file. The names in the map files have IDs too (`map1.place.1`, `map1.person.0`), so
they can be replaced the same way. A mod's font is another font file.
