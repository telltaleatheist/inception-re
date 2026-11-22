// The original's text screen: 40x25 character cells of 8x8 pixels, written
// through numbered text windows (docs/text.md). The logic only says which
// glyph, frame piece or fill colour goes in which cell; the renderer decides
// how a cell looks, so a makeover can change the font and frames.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "formats/tables.h"
#include "game/asset_id.h"
#include "game/text_colour.h"

namespace chr::game {

// The grid is the original's layout in character cells; how big a cell is
// on screen is the renderer's business (it measures the font).
inline constexpr int kTextColumns = 40;
inline constexpr int kTextRows = 25;

// Glyph ids: the font is read from the player's BTECH.EXE (formats/exe_data.h).
inline AssetId font_glyph(std::uint8_t ch) { return {"FONT", std::uint16_t(ch & 0x7f)}; }
// Frame pieces: the 8x8 cells the program cuts from the top of BTBORDER.CMP
// (1527:0044, with 2656:0ae5).
inline AssetId frame_piece(int n) { return {"BTBORDER.CMP:8x8", std::uint16_t(n)}; }

struct TextCell {
    enum class Kind : std::uint8_t { Empty, Fill, Glyph, Frame };
    Kind kind = Kind::Empty;  // Empty: whatever is drawn underneath shows
    std::uint8_t code = 0;    // character (Glyph) or frame piece (Frame)
    TextColour fg = TextRole::Text;
    TextColour bg = TextRole::Background;
    // The menus whose highlight bar is on the cell, as 1 + the menu number
    // (0: none). The renderer draws the bar (menu_bar).
    std::array<std::uint8_t, 2> bars{};
    // The marks of a box over text on the map (box_mark), as bits of
    // BoxMark; the renderer draws them.
    std::uint8_t box_marks = 0;
    AssetId asset() const { return kind == Kind::Frame ? frame_piece(code) : font_glyph(code); }
};

// One text window, as the program keeps it (a 16-byte record per window).
struct TextWindow {
    int x = 0, y = 0;           // top-left inside cell
    int width = 0, height = 0;  // in cells
    TextColour fg = TextRole::Text, bg = TextRole::Background;
    int col = 0, row = 0;       // cursor
};

class TextScreen {
public:
    // The windows and frame styles come from the imported tables, which
    // must outlive the screen.
    explicit TextScreen(const formats::Tables& tables);

    const TextCell& cell(int col, int row) const { return cells_[row * kTextColumns + col]; }
    const TextWindow& window() const { return windows_[current_]; }
    int window_number() const { return current_; }

    // BTECH.EXE 2656:0281 (Ghidra FUN_2656_0281)
    // Makes window n current, keeping the old window's colours and cursor.
    void select_window(int n);
    // BTECH.EXE 2656:0388 (Ghidra FUN_2656_0388)
    // Fills the current window with its background colour; cursor to its corner.
    void clear_window();
    // BTECH.EXE 2656:0004 (Ghidra FUN_2656_0004)
    // Draws frame style `style` around the current window, one cell outside it.
    void frame(int style);
    // BTECH.EXE 2656:03f5 (Ghidra FUN_2656_03f5)
    // Prints the program's coded text into the current window, word-wrapped.
    void print(std::span<const std::uint8_t> text);
    void print(std::string_view text);
    // BTECH.EXE 273d:00d5 (Ghidra FUN_273d_00d5)
    // Writes characters straight into cells from (col, row), no wrapping.
    void put_text(std::string_view text, int col, int row, TextColour fg, TextColour bg);
    // Forgets everything (the whole screen shows what is underneath).
    void clear_all();
    // Empties a rectangle of cells, for when the map is drawn over them.
    void forget(int col, int row, int width, int height);
    // Fills a rectangle of cells with one colour.
    void fill(int col, int row, int width, int height, TextColour colour);
    // BTECH.EXE 287f:2b87 (Ghidra FUN_287f_2b87), for a menu
    // Menu `menu`'s highlight bar on a row of cells, or off again if it is
    // there: the original XORs the menu's highlight colour in, so the same
    // call takes it away. Painting a cell takes it away too.
    void menu_bar(int col, int row, int width, int menu);
    // BTECH.EXE 287f:2b87 (Ghidra FUN_287f_2b87), for a box over text
    // A box's marks (XorBox) on a row of text cells on the map, XORed with
    // the marks already there: the same call again takes them away.
    void box_mark(int col, int row, int width, std::uint8_t marks);

    // The current window's text colour (F:37fe).
    void set_fg(TextColour fg) { windows_[current_].fg = fg; }
    // Puts the current window's cursor at (col, row) inside it
    // (F:3748/374e for the current window).
    void set_cursor(int col, int row) {
        windows_[current_].col = col;
        windows_[current_].row = row;
    }
    // Window n's top row and height, changed at run time (385b:0000 + 16 n,
    // words +2 and +6: the weapons list sizes window 5 to fit).
    void place_window(int n, int y, int height) {
        windows_[n].y = y;
        windows_[n].height = height;
    }
    // Moves the current window's left edge right by `cells`, keeping its
    // right edge (F:39a0 and F:3990, the current window's x and width).
    void indent_window(int cells) {
        windows_[current_].x += cells;
        windows_[current_].width -= cells;
    }
    // Moves the cursor up a row (the yes/no question rewrites its line).
    void cursor_up() { --windows_[current_].row; }

    // A count of the painting calls (fill, clear, frame, put_text), and the
    // count when each cell was last painted: what was XORed over a cell
    // before it was painted is gone (Town::boxes).
    std::uint32_t paint_clock() const { return paint_clock_; }
    std::uint32_t painted_at(int col, int row) const { return painted_[std::size_t(row * kTextColumns + col)]; }

    // Every cell, for tests and the renderer.
    std::span<const TextCell> cells() const { return cells_; }
    // The characters of one row, '\0' cells as spaces, for tests.
    std::string row_text(int row) const;

private:
    // BTECH.EXE 2656:07cb (Ghidra FUN_2656_07cb)
    void put_line(std::string& line, bool newline);
    void put_piece(int piece, int col, int row);

    std::array<TextCell, kTextColumns * kTextRows> cells_{};
    std::array<std::uint32_t, kTextColumns * kTextRows> painted_{};
    std::uint32_t paint_clock_ = 0;
    const formats::Tables* tables_;
    std::array<TextWindow, formats::kWindowCount> windows_{};
    int current_ = 0;
    bool selected_once_ = false;
};

}  // namespace chr::game
