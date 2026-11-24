// The Amiga readers (formats/amiga_program.h, formats/amiga_bld.h) on data made here, byte by
// byte: a load file, a scripts bundle, letter-pair tables, a script, pictures, tile sheets, maps
// and place pictures.
// No game files.
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "formats/amiga_anm.h"
#include "formats/amiga_bld.h"
#include "formats/amiga_colours.h"
#include "formats/amiga_map.h"
#include "formats/amiga_picture.h"
#include "formats/amiga_program.h"
#include "formats/exe_data.h"
#include "formats/amiga_program_data.h"
#include "formats/amiga_source.h"
#include "formats/cmp.h"
#include "test.h"

using namespace chr::formats;

namespace {

using Bytes = std::vector<std::uint8_t>;

void put32(Bytes& b, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(std::uint8_t(v >> s));
}

// A load file: a code hunk of 2 longs with one relocation, a data hunk of 1 long stored (3 in
// memory), a BSS hunk of 4 longs.
Bytes load_file() {
    Bytes b;
    put32(b, 0x3F3), put32(b, 0), put32(b, 3), put32(b, 0), put32(b, 2);
    put32(b, 2), put32(b, 3), put32(b, 4);
    put32(b, 0x3E9), put32(b, 2), put32(b, 0x4E714E75), put32(b, 0x00000004);
    put32(b, 0x3EC), put32(b, 1), put32(b, 1), put32(b, 4), put32(b, 0);
    put32(b, 0x3F2);
    put32(b, 0x3EA), put32(b, 1), put32(b, 0x11223344);
    put32(b, 0x3F2);
    put32(b, 0x3EB), put32(b, 4);
    put32(b, 0x3F2);
    return b;
}

std::uint8_t encipher(std::uint8_t plain) { return std::uint8_t((plain ^ 0xE9) - 0x29); }

}  // namespace

TEST(amiga_hunks_are_read_as_stored) {
    const auto hunks = read_amiga_hunks(load_file());
    CHECK(hunks.size() == 3);
    CHECK(hunks[0].kind == AmigaHunk::Kind::Code && hunks[0].size == 8 && hunks[0].bytes.size() == 8);
    CHECK(hunks[0].bytes[7] == 4);  // the relocation is not applied
    CHECK(hunks[1].kind == AmigaHunk::Kind::Data && hunks[1].size == 12 && hunks[1].bytes == Bytes({0x11, 0x22, 0x33, 0x44}));
    CHECK(hunks[2].kind == AmigaHunk::Kind::Bss && hunks[2].size == 16 && hunks[2].bytes.empty());
}

TEST(amiga_hunks_refuse_what_is_not_a_load_file) {
    auto throws = [](const Bytes& b) {
        try {
            read_amiga_hunks(b);
        } catch (const FormatError&) {
            return true;
        }
        return false;
    };
    Bytes b = load_file();
    b[3] = 0xF4;
    CHECK(throws(b));
    Bytes cut = load_file();
    cut.resize(40);
    CHECK(throws(cut));
    Bytes big = load_file();
    big[39] = 9;  // the code hunk claims 9 longs stored, more than its size
    CHECK(throws(big));
    // A load file, but not version 2.3's program.
    bool refused = false;
    try {
        AmigaProgram p(load_file());
    } catch (const FormatError&) {
        refused = true;
    }
    CHECK(refused);
}

TEST(amiga_place_script_is_found_and_deciphered) {
    Bytes builds{0x55};  // something before the first script
    Bytes offsets;
    const Bytes one{0xFD, 0xFF}, two{0xFB, 0xFF};
    for (int place = 0; place < 26; ++place) {
        const Bytes& s = place == 1 ? two : one;
        put32(offsets, std::uint32_t(builds.size()));
        builds.push_back(std::uint8_t(s.size() >> 8)), builds.push_back(std::uint8_t(s.size()));
        for (std::uint8_t c : s) builds.push_back(encipher(c));
    }
    CHECK(amiga_place_script(builds, offsets, 0) == one);
    CHECK(amiga_place_script(builds, offsets, 1) == two);
    bool refused = false;
    try {
        amiga_place_script(Bytes(builds.begin(), builds.begin() + 3), offsets, 25);
    } catch (const FormatError&) {
        refused = true;
    }
    CHECK(refused);
}

namespace {

// Made-up letter tables: first letters A-P, second letters a-z then digits, repeating.
struct Pairs {
    std::array<std::uint8_t, 16> first{};
    std::array<std::uint8_t, 128> second{};
    Pairs() {
        for (int i = 0; i < 16; ++i) first[std::size_t(i)] = std::uint8_t('A' + i);
        for (int i = 0; i < 128; ++i) second[std::size_t(i)] = std::uint8_t(i % 36 < 26 ? 'a' + i % 36 : '0' + i % 36 - 26);
    }
    AmigaTextPairs tables() const { return {first, second}; }
};

}  // namespace

TEST(amiga_text_pairs_expand_and_control_arguments_stay) {
    const Pairs p;
    // 0x80: first[0], second[0]; 0x9B: first[3] ('D'), second[0x1B] ('1'); 0x09 takes 0x85 raw.
    const std::string packed{char(0x80), 'x', char(0x9B), 0x09, char(0x85), char(0x13), char(0xC0)};
    CHECK(expand_amiga_text(packed, p.tables()) == std::string("Aax") + "D1" + "\x09" + char(0x85) + "\x13" + char(0xC0));
}

TEST(amiga_script_is_laid_out_in_dos_form) {
    const Pairs p;
    // 0: if_flag 3 -> 12      (F7 03 lo hi)
    // 4: sound 18             (E4 12)
    // 6: print, packed text   (FC 80 81 00): two pairs, expanded to four letters
    // 10: pay 5               (EE 05 00)
    // 13: menu 0 -> 4, 17     (F9 00 lo hi lo hi)
    // 19: goto -> 6           (F8 lo hi)
    // 22: end                 (FF)
    // The if_flag's target, 12, is inside the pay: an odd jump, but the offset still moves with it.
    const Bytes packed{0xF7, 0x03, 12, 0, 0xE4, 18, 0xFC, 0x80, 0x81, 0x00, 0xEE, 0x05, 0x00,
                       0xF9, 0x00, 4,  0, 17,   0, 0xF8, 6,    0,    0xFF};
    const std::array<MenuRecord, 1> menus{{{0, 0, 2, 0, 0}}};
    const Bytes out = relay_amiga_script(packed, p.tables(), menus);
    // The text grows by 2, so everything after offset 9 moves by 2.
    const Bytes want{0xF7, 0x03, 14, 0, 0xE4, 18, 0xFC, 'A', 'a', 'A', 'b', 0x00, 0xEE, 0x05, 0x00,
                     0xF9, 0x00, 4,  0, 19,   0, 0xF8, 6,   0,   0xFF};
    CHECK(out == want);

    // Edits: the sound becomes 17, the pay goes (jumps to it land on what follows).
    const std::array<ScriptEdit, 2> edits{{{4, 0xE4, ScriptEdit::Kind::SetByte, 17}, {10, 0xEE, ScriptEdit::Kind::Drop, 0}}};
    const Bytes edited = relay_amiga_script(packed, p.tables(), menus, edits);
    const Bytes want_edited{0xF7, 0x03, 12, 0, 0xE4, 17, 0xFC, 'A', 'a', 'A', 'b', 0x00,
                            0xF9, 0x00, 4,  0, 16,   0,  0xF8, 6,   0,   0xFF};
    CHECK(edited == want_edited);

    // Sounds put in: one before the print (the goto's jump to the print now lands on it, as in
    // DOS) and two before the menu, in the order given.
    const std::array<ScriptEdit, 3> inserted{{{6, 0xFC, ScriptEdit::Kind::InsertSound, 7},
                                              {13, 0xF9, ScriptEdit::Kind::InsertSound, 8},
                                              {13, 0xF9, ScriptEdit::Kind::InsertSound, 9}}};
    const Bytes with_sounds = relay_amiga_script(packed, p.tables(), menus, inserted);
    const Bytes want_sounds{0xF7, 0x03, 16, 0,   0xE4, 18, 0xE4, 7,  0xFC, 'A', 'a', 'A', 'b', 0x00, 0xEE, 0x05, 0x00,
                            0xE4, 8,    0xE4, 9, 0xF9, 0x00, 4,  0,  25,   0,   0xF8, 6,   0,   0xFF};
    CHECK(with_sounds == want_sounds);
    CHECK(!amiga_dos_edits(13).empty() && !amiga_dos_edits(16).empty() && amiga_dos_edits(0).empty());

    // An edit naming an instruction that isn't there, or one of another kind, is refused.
    for (const ScriptEdit& bad : {ScriptEdit{5, 0xE4, ScriptEdit::Kind::SetByte, 1}, ScriptEdit{4, 0xEE, ScriptEdit::Kind::Drop, 0}}) {
        bool refused = false;
        try {
            relay_amiga_script(packed, p.tables(), menus, std::span<const ScriptEdit>(&bad, 1));
        } catch (const FormatError&) {
            refused = true;
        }
        CHECK(refused);
    }
}

namespace {

// A picture file: a palette (colour 1 is 0x0F00, written with a stray top nibble), the
// big-endian length, then `body` (type byte and stream, or map.icn's tiles).
Bytes amiga_picture_file(const Bytes& body) {
    Bytes b(32, 0);
    b[2] = 0xFF, b[3] = 0x00;
    b.push_back(std::uint8_t(body.size() >> 8)), b.push_back(std::uint8_t(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

// A stream of four planes, each 8,000 bytes: `first` is plane `plane`'s first bytes, the rest 0.
Bytes amiga_planes(std::uint8_t type, int plane, const Bytes& first) {
    Bytes s{type};
    auto zeros = [&](std::size_t n) { s.insert(s.end(), {0x00, std::uint8_t(n >> 8), std::uint8_t(n), 0x00}); };
    if (plane > 0) zeros(std::size_t(plane) * 8000);
    s.push_back(std::uint8_t(first.size()));
    s.insert(s.end(), first.begin(), first.end());
    zeros(32000 - std::size_t(plane) * 8000 - first.size());
    return s;
}

}  // namespace

TEST(amiga_pictures_come_out_in_dos_form) {
    // Row order: plane 1's second byte is row 0, pixels 8-15; 0x40 lights pixel 9 (colour 2).
    const AmigaPicture rows = decode_amiga_cmp(amiga_picture_file(amiga_planes(1, 1, {0x00, 0x40})));
    CHECK(rows.palette[1] == 0x0F00 && rows.palette[0] == 0);
    CHECK(rows.picture.order == CmpOrder::Rows);
    CHECK(rows.picture.packed.size() == kCmpPackedSize);
    CHECK(rows.picture.packed[4] == 0x02 && rows.picture.packed[0] == 0);
    // Column order: plane 0's second byte is row 1, pixels 0-7; 0x80 lights pixel 0 (colour 1).
    const AmigaPicture cols = decode_amiga_cmp(amiga_picture_file(amiga_planes(2, 0, {0x00, 0x80})));
    CHECK(cols.picture.order == CmpOrder::Columns);
    CHECK(cols.picture.packed[kCmpRowBytes] == 0x10 && cols.picture.packed[0] == 0);
}

TEST(amiga_tile_sheets_come_out_in_dos_form) {
    // Plane 3's byte 0 is tile 0, row 0, pixels 0-7 (colour 8); byte 37 is tile 1, row 2,
    // pixels 8-15.
    Bytes first(38, 0);
    first[0] = 0x80, first[37] = 0x01;
    const AmigaTileSheet sheet = decode_amiga_icn(amiga_picture_file(amiga_planes(1, 3, first)));
    CHECK(sheet.sheet.packed[0] == 0x80);
    CHECK(sheet.sheet.packed[std::size_t(kTileBytes) + 2 * 8 + 7] == 0x08);

    // map.icn: tiles stored as they are, row by row, each row four planes of 2 bytes; its tile 0
    // is DOS tile 80. Plane 2's left byte of tile 0, row 1 lights pixel 0 (colour 4).
    Bytes tiles(170 * 128, 0);
    tiles[8 + 2 * 2] = 0x80;
    const AmigaTileSheet map = decode_amiga_map_icn(amiga_picture_file(tiles));
    CHECK(map.palette[1] == 0x0F00);
    CHECK(map.sheet.packed[std::size_t(kAmigaMapIcnFirstTile) * kTileBytes + 8] == 0x40);
    CHECK(map.sheet.packed[0] == 0 && map.sheet.packed[8] == 0);

    bool refused = false;
    try {
        decode_amiga_map_icn(amiga_picture_file(Bytes(100, 0)));
    } catch (const FormatError&) {
        refused = true;
    }
    CHECK(refused);
    refused = false;
    try {
        Bytes cut = amiga_picture_file(amiga_planes(1, 0, {0x01}));
        cut.resize(cut.size() - 3);  // the length now runs past the end
        decode_amiga_icn(cut);
    } catch (const FormatError&) {
        refused = true;
    }
    CHECK(refused);
}

TEST(amiga_maps_are_cut_out_in_dos_form) {
    // Fifteen made-up maps: 1-14 of 0x220 bytes each (a header and a little more), 15 of 10.
    // Map n's first name byte is 'a' + n; its first entrance x word is 0x1234 + n, big-endian.
    Bytes maps, offsets;
    for (int n = 1; n <= 15; ++n) {
        put32(offsets, std::uint32_t(maps.size()));
        Bytes m(n == 15 ? 10 : 0x220, std::uint8_t(n));
        if (n < 15) {
            m[5] = std::uint8_t('a' + n);
            m[0x185] = 0x12, m[0x186] = std::uint8_t(0x34 + n);
        }
        maps.insert(maps.end(), m.begin(), m.end());
    }
    const Bytes three = amiga_map(maps, offsets, 3);
    CHECK(three.size() == 0x220 && three[0] == 3 && three[5] == 'a' + 3);
    CHECK(three[0x185] == 0x34 + 3 && three[0x186] == 0x12);  // now little-endian
    CHECK(three[0x204] == 3);                                 // the last word swapped is all 3s
    CHECK(amiga_map(maps, offsets, 15) == Bytes(10, 15));
    // Map 2's first name is map 1's, as in DOS; the rest of map 2 is its own.
    const Bytes two = amiga_map(maps, offsets, 2);
    CHECK(two[5] == 'a' + 1 && two[6] == 1 && two[0] == 2);

    bool refused = false;
    try {
        amiga_map(Bytes(maps.begin(), maps.end() - 20), offsets, 14);
    } catch (const FormatError&) {
        refused = true;
    }
    CHECK(refused);
}

TEST(amiga_place_pictures_are_cut_out) {
    // 22 made-up pictures, picture n being n + 1 bytes of n.
    Bytes anm, offsets;
    for (int n = 0; n < 22; ++n) {
        put32(offsets, std::uint32_t(anm.size()));
        anm.insert(anm.end(), std::size_t(n + 1), std::uint8_t(n));
    }
    CHECK(amiga_place_picture(anm, offsets, 0) == Bytes(1, 0));
    CHECK(amiga_place_picture(anm, offsets, 7) == Bytes(8, 7));
    CHECK(amiga_place_picture(anm, offsets, 21) == Bytes(22, 21));
    for (int bad : {-1, 22}) {
        bool refused = false;
        try {
            amiga_place_picture(anm, offsets, bad);
        } catch (const FormatError&) {
            refused = true;
        }
        CHECK(refused);
    }
    bool refused = false;
    try {
        amiga_place_picture(Bytes(anm.begin(), anm.begin() + 30), offsets, 10);
    } catch (const FormatError&) {
        refused = true;
    }
    CHECK(refused);
}

TEST(amiga_menus_and_demo_keys_come_out_in_dos_form) {
    // Menu m: words 0-7 big-endian; the record keeps words 0, 1, 2, 4 and 5.
    Bytes menus;
    for (int m = 0; m < kMenuCount; ++m)
        for (int w = 0; w < 8; ++w) menus.push_back(0), menus.push_back(std::uint8_t(m + w));
    const auto records = amiga_menus(menus);
    CHECK((records[0] == MenuRecord{0, 1, 2, 4, 5}));
    CHECK((records[40] == MenuRecord{40, 41, 42, 44, 45}));
    bool refused = false;
    try {
        amiga_menus(Bytes(10, 0));
    } catch (const FormatError&) {
        refused = true;
    }
    CHECK(refused);

    // The number pad's 8 2 4 6 3 become DOS's codes; every other byte stays.
    const Bytes demo{0x38, 0x32, 0x34, 0x36, 0x33, 0x48, 0x35, 0x00, 0xb8};
    CHECK(amiga_demo_keys(demo) == Bytes({0xb8, 0xb0, 0xb5, 0xb3, 0xaf, 0x48, 0x35, 0x00, 0xb8}));
}

TEST(amiga_texts_cover_the_dos_import_table) {
    // One entry per DOS text, in the table's order; edits within reason.
    std::vector<std::string_view> dos;
    for (const auto& span : OriginalExe::table())
        if (span.kind == ExeSpan::Kind::Text) dos.push_back(span.name);
    const auto amiga = amiga_texts();
    CHECK(amiga.size() == dos.size());
    bool same_order = amiga.size() == dos.size();
    for (std::size_t i = 0; same_order && i < amiga.size(); ++i) same_order = amiga[i].id == dos[i];
    CHECK(same_order);
    bool sane = true;
    for (const auto& t : amiga) sane = sane && t.hunk <= 1 && t.flip_count <= t.flips.size() && (t.sha256.empty() || t.sha256.size() == 16);
    CHECK(sane);
}

TEST(amiga_tables_are_in_the_dos_import_table) {
    // In the DOS table's order, each once, with edits inside the table; the tables not read yet
    // are named here, so a table can't be left out unnoticed.
    // The four palettes are colours, not tables in DOS's form (amiga_colour_tables).
    const std::vector<std::string_view> not_yet{"tables.palette", "tables.endmech_palette", "tables.stats_palette",
                                                "tables.stats_pulse", "tables.sound_lists", "tables.music_note_hz"};
    std::vector<std::string_view> dos;
    for (const auto& span : OriginalExe::table())
        if (span.kind == ExeSpan::Kind::Table &&
            std::find(not_yet.begin(), not_yet.end(), span.name) == not_yet.end())
            dos.push_back(span.name);
    const auto amiga = amiga_tables();
    bool same_order = amiga.size() == dos.size();
    for (std::size_t i = 0; same_order && i < amiga.size(); ++i) same_order = amiga[i].id == dos[i];
    CHECK(same_order);
    bool sane = true;
    for (const auto& t : amiga) {
        sane = sane && t.hunk <= 1 && t.edit_count <= t.edits.size() && (t.sha256.empty() || t.sha256.size() == 16);
        for (const auto& span : OriginalExe::table())
            for (std::size_t i = 0; span.name == t.id && i < t.edit_count; ++i) sane = sane && t.edits[i].at < span.size;
    }
    CHECK(sane);
}

TEST(amiga_colours_come_out_in_dos_numbers) {
    // Every Amiga number has one DOS number, and the other way round.
    std::array<int, 16> seen{};
    for (int n = 0; n < 16; ++n) ++seen[dos_colour(std::uint8_t(n))];
    CHECK(std::all_of(seen.begin(), seen.end(), [](int k) { return k == 1; }));
    CHECK(dos_colour_pair(0x3c) == 0xed);  // Amiga 3 is DOS's 14, 12 its 13

    // Three bits a component, doubled on the 0-15 scale; the fourth bit is dropped.
    CHECK((amiga_colour(0x0700) == Rgb{0xee, 0, 0}));
    CHECK((amiga_colour(0x0f1c) == Rgb{0xee, 0x22, 0x88}));

    Bytes words(32, 0);
    words[2 * 3 + 1] = 0x07;  // Amiga colour 3: blue
    const Palette16 palette = amiga_palette(words);
    CHECK((palette[14] == Rgb{0, 0, 0xee}));
    CHECK((palette[3] == Rgb{}));
    bool refused = false;
    try {
        amiga_palette(Bytes(31, 0));
    } catch (const FormatError&) {
        refused = true;
    }
    CHECK(refused);

    // Only the colour codes' arguments change, up to the text's end.
    Bytes text{'a', 0x06, 3, 'b', 0x02, 12, 0x09, 0x06, 3, 0x13, 0x02, 'c', 0x06, 0, 0x06, 3, 'd' | 0x80, 0x06, 3};
    renumber_text_colours(text);
    CHECK(text == Bytes({'a', 0x06, 14, 'b', 0x02, 13, 0x09, 0x06, 3, 0x13, 0x02, 'c', 0x06, 0, 0x06, 14, 'd' | 0x80, 0x06, 3}));

    for (const auto& c : amiga_colour_tables()) CHECK(c.hunk == 1 && (c.words == 16 || c.words == 2));
}
