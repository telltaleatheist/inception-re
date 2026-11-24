// The Amiga copy as a GameSource (formats/amiga_source.h).

#include "formats/amiga_source.h"

#include <cctype>
#include <utility>

#include "formats/amiga_anm.h"
#include "formats/amiga_bld.h"
#include "formats/amiga_colours.h"
#include "formats/amiga_map.h"
#include "formats/amiga_picture.h"
#include "formats/amiga_program_data.h"
#include "formats/bld.h"
#include "formats/game_files.h"

namespace chr::formats {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::map<std::string, std::vector<std::uint8_t>> lowered(std::map<std::string, std::vector<std::uint8_t>> files) {
    std::map<std::string, std::vector<std::uint8_t>> out;
    for (auto& [name, bytes] : files) out[lower(name)] = std::move(bytes);
    return out;
}

const std::vector<std::uint8_t>& program_of(const std::map<std::string, std::vector<std::uint8_t>>& files) {
    const auto it = files.find("battletech");
    if (it == files.end()) throw FormatError("the Amiga copy has no program, battletech");
    return it->second;
}

// The movement keys: the Amiga's number-pad digit, DOS's code (docs/formats/amiga.md, "The demo").
constexpr std::array<std::pair<std::uint8_t, std::uint8_t>, 5> kDemoKeys = {{
    {0x38, 0xb8}, {0x32, 0xb0}, {0x34, 0xb5}, {0x36, 0xb3}, {0x33, 0xaf},
}};

}  // namespace

std::vector<std::uint8_t> amiga_demo_keys(std::span<const std::uint8_t> demofile) {
    std::vector<std::uint8_t> out(demofile.begin(), demofile.end());
    for (auto& b : out)
        for (const auto& [amiga, dos] : kDemoKeys)
            if (b == amiga) {
                b = dos;
                break;
            }
    return out;
}

AmigaGameSource::AmigaGameSource(std::map<std::string, std::vector<std::uint8_t>> files, std::string where)
    : files_(lowered(std::move(files))), where_(std::move(where)), program_(program_of(files_)),
      menus_(amiga_menus(program_.span("menus"))) {}

AmigaGameSource AmigaGameSource::from_folder(const std::filesystem::path& dir) {
    std::map<std::string, std::vector<std::uint8_t>> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
        if (entry.is_regular_file()) files[entry.path().filename().string()] = read_file(entry.path());
    return AmigaGameSource(std::move(files), dir.string());
}

const std::vector<std::uint8_t>* AmigaGameSource::file(const std::string& name) const {
    const auto it = files_.find(lower(name));
    return it == files_.end() ? nullptr : &it->second;
}

std::optional<std::vector<std::uint8_t>> AmigaGameSource::script(int number) const {
    const auto* builds = file("builds.bld");
    if (!builds || number < 0 || number >= static_cast<int>(kBldFiles.size())) return std::nullopt;
    const AmigaTextPairs pairs{program_.span("text.pair_first"), program_.span("text.pair_second")};
    auto script = relay_amiga_script(amiga_place_script(*builds, program_.span("bld.offsets"), number), pairs, menus_,
                                     amiga_dos_edits(number));
    for (const auto& [at, in] : walk(script, menus_))
        if (in.op == Op::Print) renumber_text_colours(std::span(script).subspan(std::size_t(at) + 1));
    return script;
}

std::optional<std::vector<std::uint8_t>> AmigaGameSource::map(int number) const {
    const auto* maps = file("maps");
    if (!maps) return std::nullopt;
    return amiga_map(*maps, program_.span("maps.offsets"), number);
}

std::optional<AnmPicture> AmigaGameSource::place_picture(int number) const {
    const auto* anm = file("animation.anm");
    if (!anm || number < 0 || number >= kAmigaPlacePictures) return std::nullopt;
    AnmPicture picture = decode_anm(amiga_place_picture(*anm, program_.span("anm.offsets"), number));
    for (auto& frame : picture.frames)
        for (auto& pixel : frame) pixel = dos_colour(pixel);
    return picture;
}

std::optional<CmpPicture> AmigaGameSource::picture(const std::string& name) const {
    const auto* bytes = file(name);
    if (!bytes) return std::nullopt;
    CmpPicture picture = decode_amiga_cmp(*bytes).picture;
    for (auto& pair : picture.packed) pair = dos_colour_pair(pair);
    return picture;
}

std::optional<TileSheet> AmigaGameSource::tile_sheet(const std::string& name) const {
    const auto* bytes = file(name);
    if (!bytes) return std::nullopt;
    TileSheet sheet = (lower(name) == "map.icn" ? decode_amiga_map_icn(*bytes) : decode_amiga_icn(*bytes)).sheet;
    for (auto& pair : sheet.packed) pair = dos_colour_pair(pair);
    return sheet;
}

// The two start-up pictures fade in with their own palettes (0:245b4, 0:23798), and the cache's
// tiles are shown with STARLEAG.ICN's on both levels (docs/formats/amiga.md, "Colours").
std::optional<Palette16> AmigaGameSource::picture_palette(const std::string& name) const {
    const std::string shown = lower(name);
    const std::string* from = nullptr;
    static const std::string kCache = "starleag.icn";
    if (shown == "infocom.cmp" || shown == "bttitle.cmp") from = &shown;
    if (shown == "starleag.icn" || shown == "map.icn") from = &kCache;
    const auto* bytes = from ? file(*from) : nullptr;
    if (!bytes) return std::nullopt;
    return amiga_palette(*bytes);
}

std::unique_ptr<const ProgramData> AmigaGameSource::program() const {
    const auto* endmech = file("endmech.cmp");
    if (!endmech) throw FormatError("ENDMECH.CMP is missing from " + where_);
    return std::make_unique<const AmigaProgramData>(*file("battletech"), *endmech);
}

std::optional<std::vector<std::uint8_t>> AmigaGameSource::demo() const {
    const auto* bytes = file("demofile");
    if (!bytes) return std::nullopt;
    return amiga_demo_keys(*bytes);
}

}  // namespace chr::formats
