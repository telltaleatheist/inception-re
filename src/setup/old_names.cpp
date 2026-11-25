#include "setup/old_names.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace chr::setup {
namespace {

bool is_ascii(std::string_view s) {
    return std::all_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
}

bool has_saves(const std::string& folder) {
    std::error_code ec;
    for (char n = '1'; n <= '6'; ++n)
        if (fs::is_regular_file(fs::path(folder) / (std::string("Game") + n), ec)) return true;
    return false;
}

}  // namespace

bool is_utf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const auto first = static_cast<unsigned char>(s[i]);
        if (first < 0x80) {
            ++i;
            continue;
        }
        const std::size_t more = first >= 0xc2 && first < 0xe0 ? 1 : first >= 0xe0 && first < 0xf0 ? 2
                                 : first >= 0xf0 && first < 0xf5 ? 3 : 0;
        if (more == 0 || s.size() - i <= more) return false;
        std::uint32_t code = first & (0x3fu >> more);
        for (std::size_t k = 1; k <= more; ++k) {
            const auto next = static_cast<unsigned char>(s[i + k]);
            if (next < 0x80 || next > 0xbf) return false;
            code = code << 6 | (next & 0x3fu);
        }
        // Longer than it needs to be, half a character, or past the last one.
        if ((more == 2 && code < 0x800) || (more == 3 && (code < 0x10000 || code > 0x10ffff)) ||
            (code >= 0xd800 && code <= 0xdfff))
            return false;
        i += more + 1;
    }
    return true;
}

PageText old_page() {
#if defined(_WIN32)
    if (GetACP() != CP_UTF8) return {};
    DWORD page = 0;  // GetACP no longer says it
    if (GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                        reinterpret_cast<wchar_t*>(&page), sizeof page / sizeof(wchar_t)) == 0 ||
        page == CP_ACP || page == CP_UTF8)
        return {};
    return [page](const std::string& bytes) -> std::optional<std::string> {
        const int size = static_cast<int>(bytes.size());
        std::wstring wide(bytes.size(), L'\0');
        // As the C++ library read a narrow name in 1.0.0.
        const int units = MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, bytes.data(), size, wide.data(), size);
        if (units <= 0) return std::nullopt;
        std::string out(std::size_t(units) * 3, '\0');
        const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), units, out.data(),
                                               static_cast<int>(out.size()), nullptr, nullptr);
        if (length <= 0) return std::nullopt;
        out.resize(std::size_t(length));
        return out;
    };
#else
    return {};
#endif
}

std::vector<std::string> folder_readings(const std::string& bytes, const PageText& page) {
    if (!page || is_ascii(bytes)) return {bytes};
    std::vector<std::string> out;
    if (is_utf8(bytes)) out.push_back(bytes);
    if (auto old = page(bytes)) out.push_back(std::move(*old));
    return out;
}

std::string left_saves(const std::string& user, const PageText& page) {
    if (!page || is_ascii(user)) return "";
    try {
        const auto old = page(user);
        return old && *old != user && !has_saves(user) && has_saves(*old) ? *old : "";
    } catch (const std::exception&) {
        return "";  // a name nothing can be made of
    }
}

}  // namespace chr::setup
