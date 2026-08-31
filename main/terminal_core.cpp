#include "terminal_core.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace pocketssh {
namespace {

constexpr size_t kMaxControlSequence = 128;

int param_or(const std::vector<int> &params, size_t index, int fallback)
{
    if (index >= params.size() || params[index] < 0) return fallback;
    return params[index];
}

size_t positive_count(const std::vector<int> &params, size_t index)
{
    return static_cast<size_t>(std::max(1, param_or(params, index, 1)));
}

}  // namespace

TerminalCore::TerminalCore(size_t columns, size_t rows, size_t scrollback_rows)
    : columns_(std::max<size_t>(1, columns)), rows_(std::max<size_t>(1, rows)),
      scrollback_limit_(scrollback_rows), scroll_bottom_(rows_ - 1)
{
    normal_.assign(rows_, blank_row());
    alternate_.assign(rows_, blank_row());
    dirty_rows_.assign(rows_, true);
    reset_attributes();
}

std::vector<TerminalCell> TerminalCore::blank_row() const
{
    return std::vector<TerminalCell>(columns_);
}

std::vector<std::vector<TerminalCell>> &TerminalCore::screen()
{
    return alternate_screen_active_ ? alternate_ : normal_;
}

const std::vector<std::vector<TerminalCell>> &TerminalCore::screen() const
{
    return alternate_screen_active_ ? alternate_ : normal_;
}

void TerminalCore::resize(size_t columns, size_t rows)
{
    columns_ = std::max<size_t>(1, columns);
    rows_ = std::max<size_t>(1, rows);
    auto resize_screen = [this](std::vector<std::vector<TerminalCell>> &target) {
        target.resize(rows_, blank_row());
        for (auto &line : target) line.resize(columns_);
    };
    resize_screen(normal_);
    resize_screen(alternate_);
    cursor_row_ = std::min(cursor_row_, rows_ - 1);
    cursor_col_ = std::min(cursor_col_, columns_ - 1);
    saved_row_ = std::min(saved_row_, rows_ - 1);
    saved_col_ = std::min(saved_col_, columns_ - 1);
    scroll_top_ = 0;
    scroll_bottom_ = rows_ - 1;
    dirty_rows_.assign(rows_, true);
}

void TerminalCore::reset_attributes()
{
    attributes_ = TerminalCell{};
}

void TerminalCore::reset()
{
    normal_.assign(rows_, blank_row());
    alternate_.assign(rows_, blank_row());
    scrollback_.clear();
    alternate_screen_active_ = false;
    application_cursor_keys_ = false;
    bracketed_paste_ = false;
    origin_mode_ = false;
    autowrap_ = true;
    cursor_visible_ = true;
    cursor_row_ = cursor_col_ = saved_row_ = saved_col_ = 0;
    scroll_top_ = 0;
    scroll_bottom_ = rows_ - 1;
    parser_state_ = ParserState::Ground;
    csi_buffer_.clear();
    osc_length_ = 0;
    utf8_codepoint_ = 0;
    utf8_remaining_ = 0;
    reset_attributes();
    mark_all_dirty();
}

void TerminalCore::mark_dirty(size_t row)
{
    if (row < dirty_rows_.size()) dirty_rows_[row] = true;
}

void TerminalCore::mark_all_dirty()
{
    std::fill(dirty_rows_.begin(), dirty_rows_.end(), true);
}

void TerminalCore::line_feed()
{
    if (cursor_row_ == scroll_bottom_) {
        scroll_up(1);
    } else {
        cursor_row_ = std::min(cursor_row_ + 1, rows_ - 1);
    }
}

void TerminalCore::reverse_index()
{
    if (cursor_row_ == scroll_top_) {
        scroll_down(1);
    } else if (cursor_row_ > 0) {
        --cursor_row_;
    }
}

void TerminalCore::scroll_up(size_t count)
{
    auto &active = screen();
    count = std::min(count, scroll_bottom_ - scroll_top_ + 1);
    while (count--) {
        if (!alternate_screen_active_ && scroll_top_ == 0) {
            scrollback_.push_back(active[scroll_top_]);
            if (scrollback_.size() > scrollback_limit_) scrollback_.erase(scrollback_.begin());
        }
        for (size_t row = scroll_top_; row < scroll_bottom_; ++row) active[row] = active[row + 1];
        active[scroll_bottom_] = blank_row();
    }
    for (size_t row = scroll_top_; row <= scroll_bottom_; ++row) mark_dirty(row);
}

void TerminalCore::scroll_down(size_t count)
{
    auto &active = screen();
    count = std::min(count, scroll_bottom_ - scroll_top_ + 1);
    while (count--) {
        for (size_t row = scroll_bottom_; row > scroll_top_; --row) active[row] = active[row - 1];
        active[scroll_top_] = blank_row();
    }
    for (size_t row = scroll_top_; row <= scroll_bottom_; ++row) mark_dirty(row);
}

void TerminalCore::put_codepoint(uint32_t codepoint)
{
    if (cursor_col_ >= columns_) {
        if (!autowrap_) cursor_col_ = columns_ - 1;
        else { cursor_col_ = 0; line_feed(); }
    }
    auto &cell = screen()[cursor_row_][cursor_col_];
    cell = attributes_;
    cell.codepoint = codepoint;
    mark_dirty(cursor_row_);
    ++cursor_col_;
}

void TerminalCore::erase_in_display(int mode)
{
    auto &active = screen();
    if (mode == 2 || mode == 3) {
        for (auto &line : active) line = blank_row();
        if (mode == 3) scrollback_.clear();
        mark_all_dirty();
        return;
    }
    const size_t begin = mode == 1 ? 0 : cursor_row_;
    const size_t end = mode == 1 ? cursor_row_ : rows_ - 1;
    for (size_t row = begin; row <= end; ++row) {
        size_t from = 0, to = columns_;
        if (row == cursor_row_) {
            if (mode == 1) to = cursor_col_ + 1;
            else from = cursor_col_;
        }
        std::fill(active[row].begin() + from, active[row].begin() + to, TerminalCell{});
        mark_dirty(row);
    }
}

void TerminalCore::erase_in_line(int mode)
{
    auto &line = screen()[cursor_row_];
    const size_t from = mode == 1 ? 0 : cursor_col_;
    const size_t to = mode == 0 ? columns_ : std::min(columns_, cursor_col_ + 1);
    std::fill(line.begin() + from, line.begin() + to, TerminalCell{});
    mark_dirty(cursor_row_);
}

void TerminalCore::insert_lines(size_t count)
{
    if (cursor_row_ < scroll_top_ || cursor_row_ > scroll_bottom_) return;
    auto &active = screen();
    count = std::min(count, scroll_bottom_ - cursor_row_ + 1);
    while (count--) {
        for (size_t row = scroll_bottom_; row > cursor_row_; --row) active[row] = active[row - 1];
        active[cursor_row_] = blank_row();
    }
    for (size_t row = cursor_row_; row <= scroll_bottom_; ++row) mark_dirty(row);
}

void TerminalCore::delete_lines(size_t count)
{
    if (cursor_row_ < scroll_top_ || cursor_row_ > scroll_bottom_) return;
    auto &active = screen();
    count = std::min(count, scroll_bottom_ - cursor_row_ + 1);
    while (count--) {
        for (size_t row = cursor_row_; row < scroll_bottom_; ++row) active[row] = active[row + 1];
        active[scroll_bottom_] = blank_row();
    }
    for (size_t row = cursor_row_; row <= scroll_bottom_; ++row) mark_dirty(row);
}

void TerminalCore::insert_chars(size_t count)
{
    auto &line = screen()[cursor_row_];
    count = std::min(count, columns_ - cursor_col_);
    std::move_backward(line.begin() + cursor_col_, line.end() - count, line.end());
    std::fill(line.begin() + cursor_col_, line.begin() + cursor_col_ + count, TerminalCell{});
    mark_dirty(cursor_row_);
}

void TerminalCore::delete_chars(size_t count)
{
    auto &line = screen()[cursor_row_];
    count = std::min(count, columns_ - cursor_col_);
    std::move(line.begin() + cursor_col_ + count, line.end(), line.begin() + cursor_col_);
    std::fill(line.end() - count, line.end(), TerminalCell{});
    mark_dirty(cursor_row_);
}

std::vector<int> TerminalCore::parse_params(const std::string &raw)
{
    std::vector<int> params;
    std::string value;
    for (char ch : raw) {
        if (ch == ';') {
            params.push_back(value.empty() ? -1 : std::stoi(value));
            value.clear();
        } else if (std::isdigit(static_cast<unsigned char>(ch))) value.push_back(ch);
    }
    params.push_back(value.empty() ? -1 : std::stoi(value));
    return params;
}

uint16_t TerminalCore::ansi_color(int value, bool background)
{
    int index = background ? value - 40 : value - 30;
    if (value >= (background ? 100 : 90)) index = value - (background ? 100 : 90) + 8;
    return static_cast<uint16_t>(std::clamp(index, 0, 15));
}

uint16_t TerminalCore::rgb_to_xterm(uint8_t red, uint8_t green, uint8_t blue)
{
    const int cube_r = static_cast<int>((red * 5 + 127) / 255);
    const int cube_g = static_cast<int>((green * 5 + 127) / 255);
    const int cube_b = static_cast<int>((blue * 5 + 127) / 255);
    return static_cast<uint16_t>(16 + 36 * cube_r + 6 * cube_g + cube_b);
}

void TerminalCore::execute_sgr(const std::vector<int> &params)
{
    for (size_t i = 0; i < params.size(); ++i) {
        const int code = params[i] < 0 ? 0 : params[i];
        if (code == 0) reset_attributes();
        else if (code == 1) attributes_.style |= CellStyleBold;
        else if (code == 2) attributes_.style |= CellStyleDim;
        else if (code == 4) attributes_.style |= CellStyleUnderline;
        else if (code == 7) attributes_.style |= CellStyleInverse;
        else if (code == 8) attributes_.style |= CellStyleConceal;
        else if (code == 9) attributes_.style |= CellStyleStrike;
        else if (code == 22) attributes_.style &= static_cast<uint8_t>(~(CellStyleBold | CellStyleDim));
        else if (code == 24) attributes_.style &= static_cast<uint8_t>(~CellStyleUnderline);
        else if (code == 27) attributes_.style &= static_cast<uint8_t>(~CellStyleInverse);
        else if (code == 28) attributes_.style &= static_cast<uint8_t>(~CellStyleConceal);
        else if (code == 29) attributes_.style &= static_cast<uint8_t>(~CellStyleStrike);
        else if ((code >= 30 && code <= 37) || (code >= 90 && code <= 97)) attributes_.foreground = ansi_color(code, false);
        else if ((code >= 40 && code <= 47) || (code >= 100 && code <= 107)) attributes_.background = ansi_color(code, true);
        else if (code == 39) attributes_.foreground = kTerminalDefaultColor;
        else if (code == 49) attributes_.background = kTerminalDefaultColor;
        else if ((code == 38 || code == 48) && i + 1 < params.size()) {
            const bool background = code == 48;
            if (params[i + 1] == 5 && i + 2 < params.size()) {
                (background ? attributes_.background : attributes_.foreground) = static_cast<uint16_t>(std::clamp(params[i + 2], 0, 255));
                i += 2;
            } else if (params[i + 1] == 2 && i + 4 < params.size()) {
                (background ? attributes_.background : attributes_.foreground) = rgb_to_xterm(
                    static_cast<uint8_t>(std::clamp(params[i + 2], 0, 255)),
                    static_cast<uint8_t>(std::clamp(params[i + 3], 0, 255)),
                    static_cast<uint8_t>(std::clamp(params[i + 4], 0, 255)));
                i += 4;
            }
        }
    }
}

void TerminalCore::execute_csi(char final)
{
    const bool private_mode = !csi_buffer_.empty() && csi_buffer_[0] == '?';
    const std::string raw = private_mode ? csi_buffer_.substr(1) : csi_buffer_;
    const std::vector<int> params = parse_params(raw);
    switch (final) {
    case 'A': cursor_row_ = cursor_row_ > positive_count(params, 0) ? cursor_row_ - positive_count(params, 0) : 0; break;
    case 'B': cursor_row_ = std::min(rows_ - 1, cursor_row_ + positive_count(params, 0)); break;
    case 'C': cursor_col_ = std::min(columns_ - 1, cursor_col_ + positive_count(params, 0)); break;
    case 'D': cursor_col_ = cursor_col_ > positive_count(params, 0) ? cursor_col_ - positive_count(params, 0) : 0; break;
    case 'H': case 'f': {
        const size_t base = origin_mode_ ? scroll_top_ : 0;
        cursor_row_ = std::min(rows_ - 1, base + static_cast<size_t>(std::max(1, param_or(params, 0, 1)) - 1));
        cursor_col_ = std::min(columns_ - 1, static_cast<size_t>(std::max(1, param_or(params, 1, 1)) - 1));
        break;
    }
    case 'J': erase_in_display(param_or(params, 0, 0)); break;
    case 'K': erase_in_line(param_or(params, 0, 0)); break;
    case 'L': insert_lines(positive_count(params, 0)); break;
    case 'M': delete_lines(positive_count(params, 0)); break;
    case 'P': delete_chars(positive_count(params, 0)); break;
    case '@': insert_chars(positive_count(params, 0)); break;
    case 'S': scroll_up(positive_count(params, 0)); break;
    case 'T': scroll_down(positive_count(params, 0)); break;
    case 'r': {
        scroll_top_ = static_cast<size_t>(std::max(1, param_or(params, 0, 1)) - 1);
        scroll_bottom_ = static_cast<size_t>(std::max(1, param_or(params, 1, static_cast<int>(rows_))) - 1);
        if (scroll_top_ >= rows_ || scroll_bottom_ >= rows_ || scroll_top_ >= scroll_bottom_) { scroll_top_ = 0; scroll_bottom_ = rows_ - 1; }
        cursor_row_ = origin_mode_ ? scroll_top_ : 0;
        cursor_col_ = 0;
        break;
    }
    case 's': saved_row_ = cursor_row_; saved_col_ = cursor_col_; break;
    case 'u': cursor_row_ = saved_row_; cursor_col_ = saved_col_; break;
    case 'm': execute_sgr(params); break;
    case 'h': case 'l': {
        const bool enable = final == 'h';
        for (int value : params) {
            if (private_mode && (value == 1047 || value == 1049)) {
                alternate_screen_active_ = enable;
                if (enable) {
                    alternate_.assign(rows_, blank_row());
                    cursor_row_ = 0;
                    cursor_col_ = 0;
                }
                mark_all_dirty();
            }
            else if (private_mode && value == 1) application_cursor_keys_ = enable;
            else if (private_mode && value == 7) autowrap_ = enable;
            else if (private_mode && value == 25) cursor_visible_ = enable;
            else if (private_mode && value == 2004) bracketed_paste_ = enable;
            else if (private_mode && value == 6) origin_mode_ = enable;
        }
        break;
    }
    default: break;
    }
}

void TerminalCore::feed(const char *bytes, size_t length)
{
    if (!bytes) return;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char byte = static_cast<unsigned char>(bytes[i]);
        if (parser_state_ == ParserState::Utf8) {
            if ((byte & 0xC0) == 0x80) {
                utf8_codepoint_ = (utf8_codepoint_ << 6) | (byte & 0x3F);
                if (--utf8_remaining_ == 0) { put_codepoint(utf8_codepoint_); parser_state_ = ParserState::Ground; }
            } else { put_codepoint(0xFFFD); parser_state_ = ParserState::Ground; --i; }
            continue;
        }
        if (parser_state_ == ParserState::Osc) {
            if (byte == 0x07) parser_state_ = ParserState::Ground;
            else if (byte == 0x1B) parser_state_ = ParserState::OscEscape;
            else if (++osc_length_ > kMaxControlSequence) parser_state_ = ParserState::Ground;
            continue;
        }
        if (parser_state_ == ParserState::OscEscape) { parser_state_ = byte == '\\' ? ParserState::Ground : ParserState::Osc; continue; }
        if (parser_state_ == ParserState::Escape) {
            if (byte == '[') { csi_buffer_.clear(); parser_state_ = ParserState::Csi; }
            else if (byte == ']') { osc_length_ = 0; parser_state_ = ParserState::Osc; }
            else { if (byte == 'D') line_feed(); else if (byte == 'M') reverse_index(); else if (byte == '7') { saved_row_ = cursor_row_; saved_col_ = cursor_col_; } else if (byte == '8') { cursor_row_ = saved_row_; cursor_col_ = saved_col_; } parser_state_ = ParserState::Ground; }
            continue;
        }
        if (parser_state_ == ParserState::Csi) {
            if (byte >= 0x40 && byte <= 0x7E) { execute_csi(static_cast<char>(byte)); parser_state_ = ParserState::Ground; }
            else if (csi_buffer_.size() < kMaxControlSequence) csi_buffer_.push_back(static_cast<char>(byte));
            else parser_state_ = ParserState::Ground;
            continue;
        }
        if (byte == 0x1B) { parser_state_ = ParserState::Escape; continue; }
        if (byte == '\r') { cursor_col_ = 0; continue; }
        if (byte == '\n') { line_feed(); continue; }
        if (byte == '\b') { if (cursor_col_ > 0) --cursor_col_; continue; }
        if (byte == '\t') { cursor_col_ = std::min(columns_ - 1, ((cursor_col_ / 8) + 1) * 8); continue; }
        if (byte < 0x20 || byte == 0x7F) continue;
        if (byte < 0x80) put_codepoint(byte);
        else if ((byte & 0xE0) == 0xC0) { utf8_codepoint_ = byte & 0x1F; utf8_remaining_ = 1; parser_state_ = ParserState::Utf8; }
        else if ((byte & 0xF0) == 0xE0) { utf8_codepoint_ = byte & 0x0F; utf8_remaining_ = 2; parser_state_ = ParserState::Utf8; }
        else if ((byte & 0xF8) == 0xF0) { utf8_codepoint_ = byte & 0x07; utf8_remaining_ = 3; parser_state_ = ParserState::Utf8; }
        else put_codepoint(0xFFFD);
    }
}

const std::vector<TerminalCell> &TerminalCore::row(size_t visible_row) const
{
    static const std::vector<TerminalCell> empty;
    return visible_row < rows_ ? screen()[visible_row] : empty;
}

bool TerminalCore::row_dirty(size_t visible_row) const { return visible_row < dirty_rows_.size() && dirty_rows_[visible_row]; }
void TerminalCore::clear_dirty() { std::fill(dirty_rows_.begin(), dirty_rows_.end(), false); }

std::string TerminalCore::plain_text() const
{
    std::string out;
    for (size_t r = 0; r < rows_; ++r) {
        for (const auto &cell : screen()[r]) out.push_back(cell.codepoint >= 0x20 && cell.codepoint < 0x7F ? static_cast<char>(cell.codepoint) : '?');
        if (r + 1 < rows_) out.push_back('\n');
    }
    return out;
}

std::string TerminalCore::encode_key(const KeyEvent &event) const
{
    if (event.code == KeyCode::Character) {
        if (event.ctrl && event.codepoint >= '@' && event.codepoint <= '_') return std::string(1, static_cast<char>(event.codepoint & 0x1F));
        std::string text;
        if (event.codepoint < 0x80) text.push_back(static_cast<char>(event.codepoint));
        if (event.alt) text.insert(text.begin(), '\x1B');
        return text;
    }
    const char *sequence = "";
    switch (event.code) {
    case KeyCode::Enter: sequence = "\r"; break; case KeyCode::Backspace: sequence = "\x7F"; break;
    case KeyCode::Tab: sequence = event.shift ? "\x1B[Z" : "\t"; break; case KeyCode::Escape: sequence = "\x1B"; break;
    case KeyCode::Up: sequence = application_cursor_keys_ ? "\x1BOA" : "\x1B[A"; break; case KeyCode::Down: sequence = application_cursor_keys_ ? "\x1BOB" : "\x1B[B"; break;
    case KeyCode::Right: sequence = application_cursor_keys_ ? "\x1BOC" : "\x1B[C"; break; case KeyCode::Left: sequence = application_cursor_keys_ ? "\x1BOD" : "\x1B[D"; break;
    case KeyCode::Home: sequence = "\x1B[H"; break; case KeyCode::End: sequence = "\x1B[F"; break;
    case KeyCode::PageUp: sequence = "\x1B[5~"; break; case KeyCode::PageDown: sequence = "\x1B[6~"; break;
    case KeyCode::Insert: sequence = "\x1B[2~"; break; case KeyCode::Delete: sequence = "\x1B[3~"; break;
    case KeyCode::F1: sequence = "\x1BOP"; break; case KeyCode::F2: sequence = "\x1BOQ"; break; case KeyCode::F3: sequence = "\x1BOR"; break; case KeyCode::F4: sequence = "\x1BOS"; break;
    case KeyCode::F5: sequence = "\x1B[15~"; break; case KeyCode::F6: sequence = "\x1B[17~"; break; case KeyCode::F7: sequence = "\x1B[18~"; break; case KeyCode::F8: sequence = "\x1B[19~"; break;
    case KeyCode::F9: sequence = "\x1B[20~"; break; case KeyCode::F10: sequence = "\x1B[21~"; break; case KeyCode::F11: sequence = "\x1B[23~"; break; case KeyCode::F12: sequence = "\x1B[24~"; break;
    default: break;
    }
    std::string result(sequence);
    if (event.alt && event.code != KeyCode::Escape) result.insert(result.begin(), '\x1B');
    return result;
}

}  // namespace pocketssh
