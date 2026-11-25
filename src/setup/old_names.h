// Names written by version 1.0.0 on Windows. Its code page was the system's, so it took the bytes
// SDL gave it, UTF-8, for text in that page, and wrote other names in that page
// (docs/setup.md, "Names on Windows"). This version's code page is UTF-8 (tools/windows/utf8.manifest).
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chr::setup {

bool is_utf8(std::string_view bytes);

// Bytes read in another code page, as UTF-8; nothing when they are no text in it.
using PageText = std::function<std::optional<std::string>(const std::string& bytes)>;

// The system's code page, where this program's is UTF-8 and the system's isn't. Empty anywhere
// else: not Windows, and a Windows before 10 1903, which doesn't read the manifest.
PageText old_page();

// The folders `bytes` may name, the likeliest first. With no `page`, the bytes as they are.
std::vector<std::string> folder_readings(const std::string& bytes, const PageText& page);

// Where 1.0.0 kept the saved games of the player's folder `user`, as `page` reads its name, when it
// has some and `user` has none; "" otherwise. Only looked for: nothing is moved or changed.
std::string left_saves(const std::string& user, const PageText& page);

}  // namespace chr::setup
