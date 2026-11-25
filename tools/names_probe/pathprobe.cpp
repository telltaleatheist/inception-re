// What the program's ways of taking a path make of a name on this system. tools/names_probe/probe.py runs it
// and looks at the disk itself. A GUI program through SDL_main, as chr is in a package; its answer is one line of
// JSON, in ASCII.
//   pathprobe info
//   pathprobe argv FOLDER | env NAME | reg KEY | utf8 HEX     a folder as text from argv, std::getenv, RegGetValueA
//                                                             (HKCU\KEY, the value "folder") or the bytes given
// For a folder: made into a path as main.cpp makes it; Game1 saved in it with SaveFiles; "fopen.txt" written with
// std::fopen and "sdl.txt" with SDL_IOFromFile, both from path::string().

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "game/text_provider.h"
#include "platform/save_files.h"
#include "setup/setup.h"

#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

std::string hex(const std::string& s) {
    std::string o;
    char b[4];
    for (const unsigned char c : s) {
        std::snprintf(b, sizeof b, "%02x", c);
        o += b;
    }
    return o;
}

// The path's own text, four digits a unit.
std::string wide(const std::filesystem::path& p) {
    std::string o;
    char b[8];
    for (const auto c : p.native()) {
        std::snprintf(b, sizeof b, "%04x", unsigned(c) & 0xffffu);
        o += b;
    }
    return o;
}

std::string plain(const char* text) {
    std::string o;
    for (; *text; ++text) o += (*text >= 0x20 && *text < 0x7f && *text != '"' && *text != '\\') ? *text : '?';
    return o;
}

std::string unhex(const std::string& s) {
    std::string o;
    for (std::size_t i = 0; i + 1 < s.size(); i += 2) o += char(std::stoi(s.substr(i, 2), nullptr, 16));
    return o;
}

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

// A folder's text: what each step made of it.
std::string folder(const std::string& text) {
    std::string o = "{\"bytes\": " + quoted(hex(text));
    const auto part = [&o](const char* name, const auto& body) {
        o += std::string(", \"") + name + "\": ";
        try {
            o += body();
        } catch (const std::exception& e) {
            o += "{\"threw\": " + quoted(plain(e.what())) + "}";
        }
    };
    std::filesystem::path dir;
    part("path", [&] {
        dir = text;
        return quoted(wide(dir));
    });
    if (dir.empty()) return o + "}";  // nothing to write in
    part("save", [&] {
        chr::platform::SaveFiles files(dir);
        const std::vector<std::uint8_t> bytes{1, 2, 3};
        return std::string(files.write(0, bytes) && files.read(0) == bytes ? "true" : "false");
    });
    part("string", [&] { return quoted(hex(dir.string())); });
    part("fopen", [&] {
        std::FILE* f = std::fopen((dir / "fopen.txt").string().c_str(), "wb");
        if (f) std::fclose(f);
        return std::string(f ? "true" : "false");
    });
    part("sdl", [&] {
        SDL_IOStream* f = SDL_IOFromFile((dir / "sdl.txt").string().c_str(), "wb");
        if (f) SDL_CloseIO(f);
        return std::string(f ? "true" : "false");
    });
    return o + "}";
}

std::string info() {
    std::string o = "{";
#if defined(_WIN32)
    o += "\"acp\": " + std::to_string(GetACP()) + ", \"oemcp\": " + std::to_string(GetOEMCP()) +
         ", \"file_apis_ansi\": " + (AreFileApisANSI() ? "true" : "false") + ", ";
    wchar_t page[16] = L"";
    GetLocaleInfoW(LOCALE_SYSTEM_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE, page, 16);
    o += "\"system_acp\": " + std::to_string(_wtoi(page)) + ", ";
#endif
    const auto path_of = [&o](const char* name, const std::string& text) {
        o += std::string("\"") + name + "\": {\"bytes\": " + quoted(hex(text));
        try {
            const std::filesystem::path p(text);
            std::error_code ec;
            o += ", \"path\": " + quoted(wide(p));
            o += std::string(", \"there\": ") + (std::filesystem::exists(p, ec) ? "true" : "false");
            o += std::string(", \"interface\": ") +
                 (std::filesystem::exists(p / chr::game::kInterfaceTextFile, ec) ? "true" : "false");
        } catch (const std::exception& e) {
            o += ", \"threw\": " + quoted(plain(e.what()));
        }
        o += "}";
    };
    const char* base = SDL_GetBasePath();
    path_of("base", base ? base : "");
#if defined(_WIN32)  // elsewhere it would be the player's own
    o += ", ";
    char* pref = SDL_GetPrefPath(chr::setup::kOrganisation, chr::setup::kApplication);
    path_of("pref", pref ? pref : "");
    SDL_free(pref);
#endif
    return o + "}";
}

}  // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "";
    std::string answer;
    if (mode == "info") {
        answer = info();
    } else if (argc > 2 && mode == "argv") {
        answer = folder(argv[2]);
    } else if (argc > 2 && mode == "env") {
        const char* v = std::getenv(argv[2]);
        answer = folder(v ? v : "");
    } else if (argc > 2 && mode == "utf8") {
        answer = folder(unhex(argv[2]));
#if defined(_WIN32)
    } else if (argc > 2 && mode == "reg") {
        char buf[1024] = "";
        DWORD size = sizeof buf;
        if (RegGetValueA(HKEY_CURRENT_USER, argv[2], "folder", RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return 3;
        answer = folder(buf);
#endif
    } else {
        return 2;
    }
    std::printf("%s\n", answer.c_str());
    std::fflush(stdout);
    return 0;
}
