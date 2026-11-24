#include "setup/setup.h"
#include "setup/versions.h"

#include "formats/amiga_disk.h"
#include "formats/amiga_program.h"
#include "formats/amiga_program_data.h"
#include "formats/bld.h"
#include "formats/exe_data.h"
#include "formats/open_source.h"

#include <rekit/archive.h>
#include <rekit/family_folder.h>
#include <rekit/files.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <span>

namespace fs = std::filesystem;

namespace chr::setup {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// `dir` ending in a separator (this system's, as SDL_GetPrefPath names it).
std::string with_slash(std::string dir) {
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += char(fs::path::preferred_separator);
    return dir;
}

std::string without_slash(std::string dir) {
    while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    return dir;
}

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

std::vector<std::uint8_t> read_start(const fs::path& p, std::size_t limit) {
    std::ifstream in(p, std::ios::binary);
    std::vector<std::uint8_t> out;
    if (!in) return out;
    out.resize(limit);
    in.read(reinterpret_cast<char*>(out.data()), std::streamsize(limit));
    out.resize(std::size_t(in.gcount()));
    return out;
}

std::string any_case(const std::string& dir, const std::string& name) { return rekit::any_case_path(dir, name); }

bool is_dir(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::is_directory(p, ec);
}

// The folder's regular files' bytes, stopping once past `cap`.
bool larger_than(const std::string& dir, std::uint64_t cap) {
    std::error_code ec;
    std::uint64_t total = 0;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2)) total += it->file_size(e2);
        if (total > cap) return true;
    }
    return false;
}

// `entry` ('/'-separated, as an archive stores it) under the game folder
// `root` ("" or ending '/'): its parts below root, made safe; false when
// outside root or unsafe.
bool under_root(const std::string& entry, const std::string& root, std::string* rel) {
    std::string e = entry;
    std::replace(e.begin(), e.end(), '\\', '/');
    if (lower(e.substr(0, root.size())) != lower(root)) return false;
    *rel = rekit::safe_relative(e.substr(root.size()));
    return !rel->empty();
}

// The line for --status naming the data files that are absent.
std::string files_missing(const std::vector<std::string>& absent) {
    std::string list;
    for (std::size_t i = 0; i < absent.size() && i < 4; ++i) list += (i ? ", " : "") + absent[i];
    if (absent.size() > 4) list += " and " + std::to_string(absent.size() - 4) + " more";
    return std::to_string(absent.size()) + " of the game's data files (" + list + ")";
}

// The data files the rebuild reads besides BTECH.EXE (docs/original.md):
// $VERIFY.EXE is the original's own check and is not needed.
std::vector<std::string> data_files() {
    std::vector<std::string> names;
    for (const auto file : formats::kBldFiles) names.push_back(std::string(file) + ".BLD");
    for (int n = 1; n <= 15; ++n) names.push_back("MAP" + std::to_string(n) + ".MTP");
    for (int n = 0; n <= 21; ++n) names.push_back("O" + std::to_string(n) + ".ANM");
    for (const char* name : {"ANIMATE.ICN", "BTTLTECH.ICN", "DESTRUCT.ICN", "MAP.ICN", "STARLEAG.ICN",
                             "BTBORDER.CMP", "BTSTATS.CMP", "BTTITLE.CMP", "ENDMECH.CMP", "INFOCOM.CMP",
                             "MECHSHAP.CMP", "TINYLAND.CMP", "WWOODBT.SIF", "DEMOFILE"})
        names.push_back(name);
    return names;
}

// check_install for a copy without BTECH.EXE.
Check check_amiga(const std::string& dir) {
    Check c;
    std::error_code ec;
    if (is_dir(dir) && !formats::amiga_folder(dir) && formats::amiga_images_in(dir).empty()) return c.why = "no BTECH.EXE", c;
    try {
        const auto source = formats::open_game_source(dir);
        if (source->program_name() != formats::kAmigaProgramName) return c.why = "no BTECH.EXE", c;
        source->program();
    } catch (const std::exception& e) {
        return c.why = e.what(), c;
    }
    c.version = kAmigaVersion;
    std::vector<std::string> absent;
    if (const auto folder = is_dir(dir) ? formats::amiga_folder(dir) : std::nullopt) {
        for (const auto name : formats::kAmigaFiles)
            if (any_case(folder->string(), std::string(name)).empty()) absent.emplace_back(name);
    } else {
        const auto images = is_dir(dir) ? formats::amiga_images_in(dir) : std::vector<formats::AmigaImage>{};
        absent = formats::amiga_files_missing(!images.empty() ? images.front().files
                                                              : formats::amiga_disk_files(read_start(dir, 2u << 20)));
    }
    if (!absent.empty()) c.missing.push_back(files_missing(absent));
    c.state = c.missing.empty() ? rekit::Status::kReady : rekit::Status::kIncomplete;
    return c;
}

}  // namespace

std::string pref_folder(const std::string& organisation, const std::string& application) {
    std::string base;
#if defined(_WIN32)
    base = env("APPDATA");
#elif defined(__APPLE__)
    if (!env("HOME").empty()) base = env("HOME") + "/Library/Application Support";
#else
    base = env("XDG_DATA_HOME");
    if (base.empty() && !env("HOME").empty()) base = env("HOME") + "/.local/share";
#endif
    if (base.empty()) return "";
    fs::path p = fs::path(with_slash(base));
    if (!organisation.empty()) p /= organisation;
    return with_slash((p / application).string());
}

std::vector<std::string> old_folders(const std::string& folder) {
    std::vector<std::string> out;
    if (folder.empty()) return out;
    const fs::path base = fs::path(without_slash(folder)).parent_path().parent_path();
    for (const auto& name : kOldApplications) out.push_back(with_slash((base / name).string()));
    return out;
}

bool has_content(const std::string& dir) {
    std::error_code ec;
    if (!is_dir(dir)) return false;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->path().filename() != ".DS_Store") return true;
    return false;
}

std::string unmoved_folder(const std::string& folder) {
    if (folder.empty() || has_content(folder)) return "";
    for (const auto& old : old_folders(folder))
        if (has_content(old)) return old;
    return "";
}

std::string user_folder() {
    const std::string configured = env("CHR_DATA_DIR");
    if (!configured.empty()) return with_slash(configured);
    const std::string folder = pref_folder(kOrganisation, kApplication);
    const std::string unmoved = unmoved_folder(folder);
    return unmoved.empty() ? folder : unmoved;
}

std::string default_install(const std::string& user) {
    return user.empty() ? "" : (fs::path(user).lexically_normal() / "install").lexically_normal().string();
}

Settled settle_user_folder(const std::string& folder) {
    Settled s;
    s.folder = folder;
    if (has_content(folder)) return s.kind = Settled::kInUse, s;
    const std::string old = unmoved_folder(folder);
    if (old.empty()) return s;
    s.from = old;
    const std::string from = without_slash(old), to = without_slash(folder);
    const rekit::FolderMove m = rekit::move_once(from, to);
    if (m.kind == rekit::FolderMove::kMoved) return s.kind = Settled::kMoved, s;
    s.kind = Settled::kMoveFailed, s.folder = old;
    s.error = m.error.empty() ? "nothing moved from " + from + " to " + to : m.error;
    return s;
}

std::string btech_version(const std::vector<std::uint8_t>& bytes) {
    try {
        formats::OriginalExe exe{std::span<const std::uint8_t>(bytes)};
        return kVersion;
    } catch (const std::exception&) {
        return "";
    }
}

rekit::SourceRules rules() {
    rekit::SourceRules r;
    rekit::Clue btech;
    btech.kind = rekit::Clue::kInstall;
    btech.path = "btech.exe";
    btech.limit = 1u << 20;
    btech.judge = [](const std::vector<std::uint8_t>& bytes) {
        rekit::Identity id;
        if (btech_version(bytes).empty()) {
            id.why = "BTECH.EXE (" + std::to_string(bytes.size()) + " bytes) is not " + std::string(kVersion);
            return id;
        }
        id.game = kId, id.version = kVersion, id.rank = 3;
        return id;
    };
    r.clues.push_back(std::move(btech));
    // The Amiga's files, in a folder of their own or the disk's Battletech folder.
    for (const char* path : {"battletech/battletech", "battletech"}) {
        rekit::Clue amiga;
        amiga.kind = rekit::Clue::kInstall;
        amiga.path = path;
        amiga.limit = 1u << 20;
        amiga.judge = [](const std::vector<std::uint8_t>& bytes) {
            rekit::Identity id;
            try {
                formats::AmigaProgram program{std::span<const std::uint8_t>(bytes)};
                id.game = kId, id.version = kAmigaVersion, id.rank = 2;
            } catch (const std::exception&) {
            }
            return id;
        };
        r.clues.push_back(std::move(amiga));
    }
    // Another version's copy: found by its first map, ranked below a known BTECH.EXE.
    rekit::Clue map;
    map.kind = rekit::Clue::kInstall;
    map.path = "map1.mtp";
    map.limit = 1u << 16;
    map.judge = [](const std::vector<std::uint8_t>& bytes) {
        rekit::Identity id;
        if (bytes.empty()) return id.why = "MAP1.MTP is empty", id;
        id.game = kId, id.rank = 1;
        return id;
    };
    r.clues.push_back(std::move(map));
    r.programs = {"btech.exe"};
    return r;
}

rekit::Found find_amiga(const std::string& path, std::string* note) {
    rekit::Found f;
    f.path = path;
    AmigaChoice choice = choose_amiga_copy(path);
    if (note) *note = choice.note;
    if (!choice.copy) return f;
    f.kind = rekit::Found::kInstall, f.game = kId, f.version = kAmigaVersion, f.rank = 2;
    f.label = kAmigaDisk, f.data_track = choice.copy->where;
    if (choice.different > 1) f.detail = "The unchanged one of " + std::to_string(choice.different) + " different copies there.";
    return f;
}

rekit::Found find_game(const std::string& path, std::string* note) {
    if (note) note->clear();
    rekit::Found f = rekit::find_source(path, rules());
    if (f.kind != rekit::Found::kNothing || f.cancelled) return f;
    rekit::Found amiga = find_amiga(path, note);
    return amiga.kind == rekit::Found::kNothing ? f : amiga;
}

rekit::SearchHints hints() {
    rekit::SearchHints h;
    // The folder names a copy of the DOS game is kept under, alone and inside
    // the usual Games, DOSBox and DOS folders. Each found folder is looked
    // into two levels down (find_install).
    const char* const names[] = {"BattleTech", "BTECH", "Crescent Hawks Inception", "CrescentHawk",
                                 "Crescent Hawk's Inception", "Inception", "CHI"};
    const char* const parents[] = {"", "Games/", "DOSBox/", "DOS/", "GOG Games/"};
    for (const char* p : parents)
        for (const char* n : names) h.other_folders.push_back(std::string(p) + n);
    h.drop_in = true;
    return h;
}

Check check_install(const std::string& dir) {
    Check c;
    std::error_code ec;
    if (!is_dir(dir)) {
        if (fs::is_regular_file(dir, ec)) return check_amiga(dir);
        return c.why = "no such folder", c;
    }
    const std::string btech = any_case(dir, "BTECH.EXE");
    if (btech.empty()) return check_amiga(dir);
    c.version = btech_version(read_start(btech, 1u << 20));
    if (c.version.empty()) {
        if (any_case(dir, "MAP1.MTP").empty()) return c.why = "BTECH.EXE is not this game's", c;
        c.missing.push_back("BTECH.EXE from " + std::string(kVersion) + " (this copy's is another version)");
    }
    std::vector<std::string> absent;
    for (const auto& name : data_files())
        if (any_case(dir, name).empty()) absent.push_back(name);
    if (!absent.empty()) c.missing.push_back(files_missing(absent));
    c.state = c.missing.empty() ? rekit::Status::kReady : rekit::Status::kIncomplete;
    return c;
}

std::string game_in(const std::string& dir, int depth) {
    if (!is_dir(dir)) return "";
    std::vector<std::string> level{dir};
    for (int d = 0; d <= depth && !level.empty(); ++d) {
        std::vector<std::string> next;
        for (const std::string& f : level) {
            std::error_code ec;
            std::vector<std::string> subs;
            std::set<std::string> names;  // listed once: the search looks into many folders
            for (fs::directory_iterator it(f, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec)) {
                std::error_code e2;
                names.insert(lower(it->path().filename().string()));
                if (d < depth && it->is_directory(e2) && !it->is_symlink(e2)) subs.push_back(it->path().string());
            }
            if (names.contains("btech.exe")) return f;
            if (std::all_of(formats::kAmigaTellingFiles.begin(), formats::kAmigaTellingFiles.end(),
                            [&](std::string_view name) { return names.contains(std::string(name)); }) &&
                formats::amiga_folder(f))
                return f;
            std::sort(subs.begin(), subs.end());
            next.insert(next.end(), subs.begin(), subs.end());
        }
        level.swap(next);
    }
    return "";
}

std::string find_install(const std::vector<std::string>& known, const std::string& user,
                         const std::string& program_dir, bool* searched) {
    if (searched) *searched = false;
    auto usable = [](const std::string& d) {
        return !d.empty() && check_install(d).state != rekit::Status::kMissing;
    };
    for (const auto& k : known)
        if (usable(k)) return k;
    const std::string own = default_install(user);
    if (usable(own)) return own;
    for (const std::string& place : rekit::install_candidates(hints(), program_dir)) {
        const std::string g = game_in(place, 2);
        if (usable(g)) {
            if (searched) *searched = true;
            return g;
        }
    }
    return "";
}

rekit::Status status(const std::vector<std::string>& known, const std::string& user,
                     const std::string& program_dir, const std::string& version) {
    rekit::Status st;
    st.game = kId, st.version = version;
    if (user.empty()) st.note = "the player's folder cannot be found";
    bool searched = false;
    const std::string found = find_install(known, user, program_dir, &searched);
    if (!found.empty()) {
        const Check c = check_install(found);
        st.where = found, st.data = c.version, st.state = c.state, st.missing = c.missing;
        if (c.version.empty())
            st.note = "this copy's BTECH.EXE is another version; the rebuild needs " + std::string(kVersion) + " or " + kAmigaVersion;
        else if (searched) st.note = "found by the search, not set up yet";
        return st;
    }
    // Another version's copy where the player said: named, not played.
    for (const auto& k : known) {
        std::error_code ec;
        OtherCopy other;
        if (k.empty() || !fs::exists(k, ec)) continue;
        // An Amiga copy a start can't play from there (in an archive, or several): the setup takes it.
        if (const AmigaChoice amiga = choose_amiga_copy(k); amiga.copy || !amiga.note.empty()) {
            st.where = k, st.data = kAmigaVersion, st.state = rekit::Status::kIncomplete;
            st.missing.push_back("the setup: it copies the game out of there");
            st.note = amiga.note.empty() ? "the Amiga version was found, not set up yet" : amiga.note;
            break;
        }
        if (!find_other_copy(k, &other)) continue;
        st.where = k, st.data = other.label(), st.state = rekit::Status::kIncomplete;
        st.missing = other.needed();
        st.note = other.note();
        break;
    }
    return st;
}

std::string remembered_folder(const std::string& user) {
    if (user.empty()) return "";
    std::ifstream in(fs::path(with_slash(user)) / kSettingsFile);
    std::string line, found;
    while (std::getline(in, line)) {
        const std::size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line.compare(start, 11, "game_folder") != 0) continue;
        const std::size_t eq = line.find_first_not_of(" \t", start + 11);
        if (eq == std::string::npos || line[eq] != '=') continue;
        std::string v = line.substr(eq + 1);
        v.erase(0, std::min(v.size(), v.find_first_not_of(" \t")));
        while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) v.pop_back();
        found = v;
    }
    return found;
}

bool remember_folder(const std::string& user, const std::string& dir) {
    if (user.empty()) return false;
    const fs::path path = fs::path(with_slash(user)) / kSettingsFile;
    std::vector<std::string> lines;
    {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            const std::size_t start = line.find_first_not_of(" \t");
            if (start != std::string::npos && line.compare(start, 11, "game_folder") == 0) continue;
            lines.push_back(line);
        }
    }
    lines.push_back("game_folder=" + dir);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const fs::path temp = path.string() + ".new";
    {
        std::ofstream out(temp);
        for (const auto& l : lines) out << l << '\n';
        if (!out) return false;
    }
    fs::rename(temp, path, ec);
    return !ec;
}

Installed install(const rekit::Found& found, const std::string& out_dir, const Progress& progress) {
    Installed r;
    auto fail = [&r](const std::string& why) {
        r.error = why;
        return r;
    };
    if (found.kind != rekit::Found::kInstall || found.game != kId)
        return fail("That is not " + std::string(kTitle) + ": nothing was copied.");
    if (out_dir.empty()) return fail("There is no folder to copy the game into.");
    std::error_code ec;
    const fs::path dest(out_dir), tmp(out_dir + ".new");
    // tmp is ours only when it carries the marker (an earlier run's, stopped); else it is the player's.
    if (fs::exists(tmp, ec)) {
        if (!fs::exists(tmp / kInstallMarker, ec))
            return fail("The folder " + tmp.string() + " is in the way: move it away first.");
        fs::remove_all(tmp, ec);
    }
    fs::create_directories(dest.parent_path(), ec);

    if (found.label == kAmigaDisk) {
        // On a disk image: its game files written out, and nothing else that is on the disk.
        const AmigaChoice choice = choose_amiga_copy(found.path);
        if (!choice.copy || choice.copy->where != found.data_track)
            return fail("Could not find " + found.data_track + " again in " + found.path + ".");
        fs::create_directories(tmp, ec);
        if (ec) return fail("Could not make " + tmp.string() + ": " + ec.message() + ".");
        std::ofstream(tmp / kInstallMarker) << "Copied from " << choice.copy->where << " by the setup (" << found.path
                                            << " was left as it was).\n";
        std::string why;
        for (const auto& [name, bytes] : choice.copy->files) {
            std::ofstream f(tmp / name, std::ios::binary);
            f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            if (!f) why = "Could not write " + (tmp / name).string() + ".";
            ++r.files, r.bytes += bytes.size();
            if (why.empty() && progress && !progress(double(r.files) / double(choice.copy->files.size()), name))
                r.cancelled = true, why = "Stopped. Nothing was copied.";
            if (why.empty()) continue;
            fs::remove_all(tmp, ec);  // ours: made above with the marker
            return fail(why);
        }
    } else if (found.on_disk && found.steps.empty()) {
        // An installed copy in a folder: copied, never moved.
        const std::string from = (fs::path(found.path) / found.root).lexically_normal().string();
        const std::string src = is_dir(from) ? from : fs::path(from).parent_path().string();
        if (fs::exists(dest, ec) && fs::equivalent(src, dest, ec)) return r.ok = true, r.dir = dest.string(), r;
        if (larger_than(src, kMaxGameBytes))
            return fail("The folder " + src + " holds far more than " + std::string(kTitle) +
                        ", which is under 2 MB: nothing was copied. Put the game's files in a folder of "
                        "their own and pick that.");
        const rekit::CopyResult c =
            rekit::copy_folder(src, tmp.string(), [&](std::uint64_t done, std::uint64_t total, const std::string& f) {
                return !progress || progress(total ? double(done) / double(total) : 0.0, f);
            });
        if (!c.ok) {
            r.cancelled = c.cancelled;
            return fail(c.cancelled ? "Stopped. Nothing was copied." : "Could not copy the game from " + src + ": " + c.error);
        }
        r.files = c.files, r.bytes = c.bytes;
        std::ofstream(tmp / kInstallMarker) << "Copied from " << src << " by the setup (the original was left as it was).\n";
    } else {
        // In an archive (at any depth): the game's folder read out in one forward pass.
        if (!rekit::archives_supported())
            return fail("This build cannot open archives: unpack it and pick its BTECH.EXE.");
        std::vector<std::unique_ptr<rekit::ArchiveReader>> chain;
        chain.push_back(std::make_unique<rekit::ArchiveReader>());
        if (!chain.back()->open_file(found.path))
            return fail("Could not open " + found.path + " (" + chain.back()->error() + ").");
        rekit::ArchiveReader::Entry e;
        for (const rekit::Step& s : found.steps) {
            if (s.kind != rekit::Step::kArchive)
                return fail("The game is inside a disc image: that is not how this game came.");
            bool at = false;
            while (chain.back()->next(&e))
                if (e.path == s.entry) {
                    at = true;
                    break;
                }
            if (!at) return fail("Could not find " + s.entry + " again in " + found.path + ".");
            auto inner = std::make_unique<rekit::ArchiveReader>();
            if (!inner->open_stream(chain.back()->entry_stream()))
                return fail("Could not open " + s.entry + " (" + inner->error() + ").");
            chain.push_back(std::move(inner));
        }
        rekit::ArchiveReader& a = *chain.back();
        fs::create_directories(tmp, ec);
        if (ec) return fail("Could not make " + tmp.string() + ": " + ec.message() + ".");
        std::ofstream(tmp / kInstallMarker) << "Unpacked from " << found.path
                                            << " by the setup (the archive was left as it was).\n";
        std::vector<std::uint8_t> buf(1 << 16);
        std::string rel, why;
        const std::int64_t whole = [&] {
            std::error_code e3;
            const auto n = fs::file_size(found.path, e3);
            return e3 ? std::int64_t(0) : static_cast<std::int64_t>(n);
        }();
        while (why.empty() && a.next(&e)) {
            if (e.other || !under_root(e.path, found.root, &rel)) continue;
            if (lower(fs::path(rel).filename().string()) == kInstallMarker) continue;
            const fs::path out = tmp / fs::path(rel);
            if (e.dir) {
                fs::create_directories(out, ec);
                continue;
            }
            fs::create_directories(out.parent_path(), ec);
            std::ofstream f(out, std::ios::binary);
            if (!f) why = "Could not write " + out.string() + ".";
            for (std::int64_t n; why.empty() && (n = a.read(buf.data(), buf.size())) != 0;) {
                if (n < 0) why = "That archive is damaged (" + a.error() + ").";
                else if ((r.bytes += std::uint64_t(n)) > kMaxGameBytes)
                    why = "That archive holds far more than the game: nothing was copied.";
                else f.write(reinterpret_cast<const char*>(buf.data()), n);
            }
            if (why.empty() && !f) why = "Could not write " + out.string() + ".";
            ++r.files;
            if (why.empty() && progress &&
                !progress(whole ? double(chain.front()->position()) / double(whole) : 0.0, rel))
                r.cancelled = true, why = "Stopped. Nothing was copied.";
        }
        if (why.empty() && !a.error().empty()) why = "That archive is damaged (" + a.error() + ").";
        if (!why.empty()) {
            fs::remove_all(tmp, ec);  // ours: made above with the marker
            return fail(why);
        }
    }
    if (check_install(tmp.string()).state == rekit::Status::kMissing) {
        fs::remove_all(tmp, ec);
        return fail("The copy is not a version of the game the rebuild plays: nothing was changed.");
    }
    if (!rekit::swap_in_folder(tmp.string(), dest.string(), kInstallMarker, ec)) {
        const std::string why = ec.message();
        fs::remove_all(tmp, ec);
        return fail("Could not put the game in " + dest.string() + " (" + why + "): nothing was changed.");
    }
    r.ok = true, r.dir = dest.string();
    return r;
}

}  // namespace chr::setup
