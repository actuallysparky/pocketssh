#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pocketssh {

constexpr uint16_t kTerminalDefaultColor = 0xFFFF;

enum CellStyle : uint8_t {
    CellStyleNone = 0,
    CellStyleBold = 1 << 0,
    CellStyleDim = 1 << 1,
    CellStyleUnderline = 1 << 2,
    CellStyleInverse = 1 << 3,
    CellStyleConceal = 1 << 4,
    CellStyleStrike = 1 << 5,
};

struct TerminalCell {
    uint32_t codepoint = ' ';
    uint16_t foreground = kTerminalDefaultColor;
    uint16_t background = kTerminalDefaultColor;
    uint8_t style = CellStyleNone;
};

enum class KeyCode : uint8_t {
    Character, Enter, Backspace, Tab, Escape, Up, Down, Right, Left, Home,
    End, PageUp, PageDown, Insert, Delete, F1, F2, F3, F4, F5, F6, F7, F8,
    F9, F10, F11, F12,
};

struct KeyEvent {
    KeyCode code = KeyCode::Character;
    uint32_t codepoint = 0;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
};

class TerminalCore {
public:
    TerminalCore(size_t columns = 80, size_t rows = 24, size_t scrollback_rows = 512);

    void resize(size_t columns, size_t rows);
    void feed(const char *bytes, size_t length);
    void reset();

    size_t columns() const { return columns_; }
    size_t rows() const { return rows_; }
    bool application_cursor_keys() const { return application_cursor_keys_; }
    bool bracketed_paste() const { return bracketed_paste_; }
    bool alternate_screen_active() const { return alternate_screen_active_; }
    size_t scrollback_size() const { return scrollback_.size(); }
    size_t scrollback_limit() const { return scrollback_limit_; }
    size_t scrollback_offset() const { return scrollback_offset_; }
    void scroll_view(int lines);

    const std::vector<TerminalCell> &row(size_t visible_row) const;
    bool row_dirty(size_t visible_row) const;
    void clear_dirty();
    std::string plain_text() const;
    std::string text_region(size_t start_row, size_t start_col, size_t end_row, size_t end_col) const;
    std::string encode_key(const KeyEvent &event) const;

private:
    enum class ParserState : uint8_t { Ground, Escape, Csi, Osc, OscEscape, Utf8 };

    size_t columns_;
    size_t rows_;
    size_t scrollback_limit_;
    std::vector<std::vector<TerminalCell>> normal_;
    std::vector<std::vector<TerminalCell>> alternate_;
    std::vector<std::vector<TerminalCell>> scrollback_;
    size_t scrollback_offset_ = 0;
    std::vector<bool> dirty_rows_;
    bool alternate_screen_active_ = false;
    bool application_cursor_keys_ = false;
    bool bracketed_paste_ = false;
    bool origin_mode_ = false;
    bool autowrap_ = true;
    bool cursor_visible_ = true;
    size_t cursor_row_ = 0;
    size_t cursor_col_ = 0;
    size_t saved_row_ = 0;
    size_t saved_col_ = 0;
    size_t scroll_top_ = 0;
    size_t scroll_bottom_ = 0;
    TerminalCell attributes_;
    ParserState parser_state_ = ParserState::Ground;
    std::string csi_buffer_;
    size_t osc_length_ = 0;
    uint32_t utf8_codepoint_ = 0;
    uint8_t utf8_remaining_ = 0;

    std::vector<std::vector<TerminalCell>> &screen();
    const std::vector<std::vector<TerminalCell>> &screen() const;
    std::vector<TerminalCell> blank_row() const;
    void mark_dirty(size_t row);
    void mark_all_dirty();
    void put_codepoint(uint32_t codepoint);
    void line_feed();
    void reverse_index();
    void scroll_up(size_t count);
    void scroll_down(size_t count);
    void erase_in_display(int mode);
    void erase_in_line(int mode);
    void insert_lines(size_t count);
    void delete_lines(size_t count);
    void insert_chars(size_t count);
    void delete_chars(size_t count);
    void execute_csi(char final);
    void execute_sgr(const std::vector<int> &params);
    static std::vector<int> parse_params(const std::string &raw);
    static uint16_t ansi_color(int value, bool background);
    static uint16_t rgb_to_xterm(uint8_t red, uint8_t green, uint8_t blue);
    void reset_attributes();
};

}  // namespace pocketssh
