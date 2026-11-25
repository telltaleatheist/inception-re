// The setup recipe for re-setup-kit (extern/re-setup-kit, DESIGN.md there):
// what a copy of the game is, where it may be, how it is copied into the
// player's folder, and the launcher contract's --status. The kit knows no
// game; everything about this one is here. Nothing in it reads or prints the
// game's text: BTECH.EXE is judged by the importer's check (formats/exe_data.h)
// and the data files by their names. No SDL: the screen's side is
// platform/sdl_setup.h.
#pragma once

#include "setup/old_names.h"

#include <rekit/search.h>
#include <rekit/sources.h>
#include <rekit/status.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace chr::setup {

inline constexpr const char* kId = "inception";        // the launcher contract's game id (--status)
inline constexpr const char* kTitle = "The Crescent Hawk's Inception";  // the setup screen's sentences
inline constexpr const char* kFolderName = "Inception Rebuild";  // a game folder made inside a picked one
inline constexpr const char* kFolderKey = "chi";  // its mark's game=<key> (the kit's game_folder.h), the launcher's name
// The player's folder (the import, the saves, settings.ini): SDL's per-user
// folder for these names, under the family's folder (BattleTech).
inline constexpr const char* kOrganisation = "BattleTech";
inline constexpr const char* kApplication = "Inception Rebuild";
// The same folder's earlier names, newest first, each straight in the
// per-user base (SDL_GetPrefPath("", name)). The first with something in it
// is renamed to the new one once (settle_user_folder).
inline const std::vector<std::string> kOldApplications = {"inception-re", "crescent-hawks-rebuild"};
// The player's settings beside the saves; game_folder=<the game's files>.
inline constexpr const char* kSettingsFile = "settings.ini";
// In a folder our install made, and only there (rekit::swap_in_folder replaces only such a folder).
inline constexpr const char* kInstallMarker = "inception-install.txt";
// The game is under 2 MB: a pick holding far more (a Games folder, a drive's top) is refused before copying.
inline constexpr std::uint64_t kMaxGameBytes = std::uint64_t(64) << 20;
// The versions the rebuild plays (formats/exe_data.h, kSupportedVersion; formats/amiga_program_data.h).
inline constexpr const char* kVersion = "DOS 1.03";
inline constexpr const char* kAmigaVersion = "Amiga 2.3";

// The per-user folder for (organisation, application), ending in a separator,
// as SDL_GetPrefPath names it (%APPDATA%, ~/Library/Application Support,
// $XDG_DATA_HOME or ~/.local/share) WITHOUT making it: --status writes
// nothing. An empty organisation leaves its level out. "" when the platform's
// base cannot be found.
std::string pref_folder(const std::string& organisation, const std::string& application);
// The earlier names' folders beside `folder`'s organisation (kOldApplications, newest first).
std::vector<std::string> old_folders(const std::string& folder);
// A folder with something in it besides the system's .DS_Store.
bool has_content(const std::string& dir);
// For the player's folder `folder`: the first earlier folder (old_folders)
// with something in it while `folder` has nothing (a move not made yet, or
// one that failed: it stays in use), ending in a separator; "" otherwise.
std::string unmoved_folder(const std::string& folder);
// The player's folder: CHR_DATA_DIR, else pref_folder(kOrganisation,
// kApplication), or an earlier one while it is still in use (unmoved_folder).
// Not made here.
std::string user_folder();
// <user folder>/install: where the setup puts the game's files unless the
// player picks another folder.
std::string default_install(const std::string& user);

// The player's folder settled on each start:
//   1. `folder` has something in it: used as it is (kInUse).
//   2. Else the first earlier folder with something in it is renamed to
//      `folder` once (rekit::move_once: never a copy, never merged; kMoved).
//      A move that fails keeps the old folder in use for this start and is
//      tried again at the next (kMoveFailed, `folder` = the old one).
//   3. Else nothing to do (kNew).
struct Settled {
    enum Kind { kInUse, kMoved, kMoveFailed, kNew } kind = kNew;
    std::string folder;  // the player's folder for this start
    std::string from;    // kMoved, kMoveFailed: the old folder
    std::string error;
};
Settled settle_user_folder(const std::string& folder);

// BTECH.EXE's version by its bytes: kVersion when the importer accepts it
// (formats/exe_data.h), else "".
std::string btech_version(const std::vector<std::uint8_t>& bytes);

// What the kit's walk looks for: BTECH.EXE judged by its version, the Amiga's
// program `battletech` (its files in a folder, or in an archive), and
// MAP1.MTP, which only this game has (MechWarrior 1's BTECH.EXE shares the
// name), so a copy of another version is still found as this game. A pick of
// BTECH.EXE stands for its folder.
rekit::SourceRules rules();
// The Amiga copy on a disk image in `path`, which the kit's walk can't read
// (choose_amiga_copy): a find for install() and for the screen's find hook, or
// kNothing. Its label is kAmigaDisk, its data_track says where the image is
// and its detail, when there are several copies, which it is in short. `note`
// (may be null) gets the choice's note, also when there is no find.
inline constexpr const char* kAmigaDisk = "Amiga disk image";
rekit::Found find_amiga(const std::string& path, std::string* note = nullptr);
// The find of `path`: the kit's, else find_amiga's.
rekit::Found find_game(const std::string& path, std::string* note = nullptr);
// Where a DOS game's copy is usually kept: no installer, so no registry; the
// usual folder names under the system drive, the home folder and any Wine
// prefix, and a folder dropped beside the program.
rekit::SearchHints hints();

struct Check {
    rekit::Status::State state = rekit::Status::kMissing;
    std::string version;               // kVersion, kAmigaVersion, or "" for another version
    std::vector<std::string> missing;  // kIncomplete: in plain words
    std::string why;                   // kMissing: why this folder is not the game
};
// Whether `dir` is the game: BTECH.EXE of kVersion and every data file is
// ready; another BTECH.EXE beside MAP1.MTP, or data files missing, is
// incomplete. Without BTECH.EXE, an Amiga copy as open_game_source takes it
// (a folder of its files, a folder with a disk image, the disk image itself)
// is ready when its program is one the importer reads and its files are all
// there. Anything else is missing.
Check check_install(const std::string& dir);

// `dir` when it holds BTECH.EXE or the Amiga's files, else the first folder
// under it, `depth` levels down at most (shallowest first, names sorted); ""
// when none.
std::string game_in(const std::string& dir, int depth = 2);

// The first of `places` that `look` finds something for, as `look` names it; "" when none. A place
// `look` throws for is passed over, with why on stderr.
std::string first_found(const std::vector<std::string>& places,
                        const std::function<std::string(const std::string&)>& look);

// The game folder a start uses: the first of `known` (the remembered one,
// then a build's own default) that is still the game, then the player's own
// default_install, then the search (hints(), each place looked into two
// levels down). `searched` says the answer came from the search (offered,
// never taken unasked). "" when none. What can't be read is passed over and
// nothing is thrown: why is said on stderr.
std::string find_install(const std::vector<std::string>& known, const std::string& user,
                         const std::string& program_dir, bool* searched);

// The answer to --status: nothing written, no window. `version` is the program's.
rekit::Status status(const std::vector<std::string>& known, const std::string& user,
                     const std::string& program_dir, const std::string& version);

// settings.ini's game_folder in `user`, read without SDL; "" when unset or nothing can be made of it.
// What 1.0.0 wrote on Windows is read as it may have meant it (folder_readings): the first reading
// that is the game's folder, else the first.
std::string remembered_folder(const std::string& user, const PageText& page = old_page());
// settings.ini's game_folder set to `dir`, its other lines kept. False when it can't be written.
bool remember_folder(const std::string& user, const std::string& dir);

// Installing what the kit's walk found into `out_dir`: a folder on disk
// copied (rekit::copy_folder), an archive's game folder unpacked in one
// forward read, an Amiga disk image's game files (find_amiga) written out;
// staged in `out_dir`.new and swapped in
// (rekit::swap_in_folder), so a failure or a cancel leaves nothing and
// `out_dir` is replaced only when our install made it. The player's original
// (folder or archive) is only read, never moved or deleted.
struct Installed {
    bool ok = false, cancelled = false;
    std::string dir;    // the folder now holding the game
    std::string error;  // why not, in plain words
    int files = 0;
    std::uint64_t bytes = 0;
};
using Progress = std::function<bool(double fraction, const std::string& file)>;  // false cancels
Installed install(const rekit::Found& found, const std::string& out_dir, const Progress& progress = nullptr);

}  // namespace chr::setup
