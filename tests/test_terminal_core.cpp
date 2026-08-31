#include "terminal_core.hpp"

#include <cassert>
#include <cstring>
#include <iostream>

using pocketssh::CellStyleBold;
using pocketssh::KeyCode;
using pocketssh::KeyEvent;
using pocketssh::TerminalCore;

namespace {

void feed_bytewise(TerminalCore &term, const char *text)
{
    for (size_t i = 0; text[i] != '\0'; ++i) term.feed(text + i, 1);
}

void test_chunked_sgr_and_cursor()
{
    TerminalCore term(8, 3, 8);
    feed_bytewise(term, "A\x1b[31;1mB\x1b[0mC");
    assert(term.row(0)[0].codepoint == 'A');
    assert(term.row(0)[1].codepoint == 'B');
    assert(term.row(0)[1].foreground == 1);
    assert((term.row(0)[1].style & CellStyleBold) != 0);
    assert(term.row(0)[2].codepoint == 'C');
    assert(term.row(0)[2].foreground == pocketssh::kTerminalDefaultColor);

    term.feed("\rZ", 2);
    assert(term.row(0)[0].codepoint == 'Z');
}

void test_alternate_screen_and_modes()
{
    TerminalCore term(8, 2, 8);
    term.feed("normal", 6);
    const char enter_alt[] = "\x1b[?1049hALT";
    term.feed(enter_alt, std::strlen(enter_alt));
    assert(term.alternate_screen_active());
    assert(term.row(0)[0].codepoint == 'A');
    const char leave_alt[] = "\x1b[?1049l";
    term.feed(leave_alt, std::strlen(leave_alt));
    assert(!term.alternate_screen_active());
    assert(term.row(0)[0].codepoint == 'n');
    term.feed("\x1b[2;3H\x1b[?1049h\x1b[?1049lZ", 23);
    assert(term.row(1)[2].codepoint == 'Z');

    const char app_cursor[] = "\x1b[?1h";
    term.feed(app_cursor, std::strlen(app_cursor));
    assert(term.application_cursor_keys());
    assert(term.encode_key({KeyCode::Up}) == "\x1bOA");
    const char bracketed[] = "\x1b[?2004h";
    term.feed(bracketed, std::strlen(bracketed));
    assert(term.bracketed_paste());
}

void test_form_feed_clears_and_homes()
{
    TerminalCore term(4, 2, 2);
    term.feed("abcd\fZ", 6);
    assert(term.row(0)[0].codepoint == 'Z');
    assert(term.row(0)[1].codepoint == ' ');
}

void test_ignored_control_sequences_are_bounded()
{
    TerminalCore term(8, 2, 2);
    feed_bytewise(term, "A\x1b]0;remote-title\x07" "B");
    assert(term.row(0)[0].codepoint == 'A');
    assert(term.row(0)[1].codepoint == 'B');
    std::string oversized = "C\x1b[";
    oversized.append(160, '1');
    oversized += "mD";
    term.feed(oversized.data(), oversized.size());
    assert(term.row(0)[2].codepoint == 'C');
    assert(term.row(0)[3].codepoint == 'D');
}

void test_scroll_and_key_encoding()
{
    TerminalCore term(4, 2, 4);
    const char scroll_text[] = "a\r\nb\r\nc";
    term.feed(scroll_text, std::strlen(scroll_text));
    assert(term.row(1)[0].codepoint == 'c');
    assert(term.encode_key({KeyCode::PageUp}) == "\x1b[5~");
    assert(term.encode_key({KeyCode::Character, 'C', true}) == std::string("\x03", 1));
    assert(term.encode_key({KeyCode::Character, 'C', true, true}) == std::string("\x1b\x03", 2));
    assert(term.encode_key({KeyCode::Tab, 0, false, false, true}) == "\x1b[Z");
    assert(term.encode_key({KeyCode::Up, 0, true}) == "\x1b[1;5A");
    assert(term.encode_key({KeyCode::F5, 0, false, true}) == "\x1b[15;3~");
    assert(term.encode_key({KeyCode::Character, 0x2603}) == std::string("\xe2\x98\x83"));

    const char *function_keys[] = {
        "\x1bOP", "\x1bOQ", "\x1bOR", "\x1bOS", "\x1b[15~", "\x1b[17~",
        "\x1b[18~", "\x1b[19~", "\x1b[20~", "\x1b[21~", "\x1b[23~", "\x1b[24~",
    };
    for (int index = 0; index < 12; ++index) {
        const auto key = static_cast<KeyCode>(static_cast<int>(KeyCode::F1) + index);
        assert(term.encode_key({key}) == function_keys[index]);
    }
}

void test_truecolor_and_utf8_streaming()
{
    TerminalCore term(8, 2, 8);
    const char truecolor[] = "\x1b[38;2;255;0;0mR";
    term.feed(truecolor, std::strlen(truecolor));
    assert(term.row(0)[0].foreground == 196);
    const char background[] = "\x1b[48;5;21mB\x1b[0mC";
    term.feed(background, std::strlen(background));
    assert(term.row(0)[1].background == 21);
    assert(term.row(0)[2].background == pocketssh::kTerminalDefaultColor);
    const char snowman[] = "\xE2\x98\x83";
    feed_bytewise(term, snowman);
    assert(term.row(0)[3].codepoint == 0x2603);
}

void test_scrollback_viewport_and_limit()
{
    TerminalCore term(4, 2, 2);
    const char text[] = "a\r\nb\r\nc\r\nd";
    term.feed(text, std::strlen(text));
    assert(term.scrollback_size() == 2);
    assert(term.row(0)[0].codepoint == 'c');
    term.scroll_view(1);
    assert(term.scrollback_offset() == 1);
    assert(term.row(0)[0].codepoint == 'b');
    term.scroll_view(99);
    assert(term.scrollback_offset() == 2);
    assert(term.row(0)[0].codepoint == 'a');
    term.scroll_view(-99);
    assert(term.scrollback_offset() == 0);
}

void test_utf8_replacement_and_viewport_copy()
{
    TerminalCore term(4, 2, 2);
    const char malformed[] = "\xe2X";
    feed_bytewise(term, malformed);
    assert(term.row(0)[0].codepoint == 0xfffd);
    assert(term.row(0)[1].codepoint == 'X');
    const char text[] = "a\r\nb\r\nc\r\nd";
    term.feed(text, std::strlen(text));
    term.scroll_view(1);
    assert(term.plain_text().substr(0, 1) == "b");
    assert(term.text_region(0, 0, 0, 0) == "b");
}

void test_erase_insert_and_resize()
{
    TerminalCore term(6, 3, 4);
    term.feed("abcdef\x1b[1;3H\x1b[2P", 16);
    assert(term.row(0)[0].codepoint == 'a');
    assert(term.row(0)[2].codepoint == 'e');
    term.feed("\x1b[1;2H\x1b[2@XY", 12);
    assert(term.row(0)[1].codepoint == 'X');
    assert(term.row(0)[2].codepoint == 'Y');
    term.feed("\x1b[2J", 4);
    assert(term.row(0)[0].codepoint == ' ');
    term.resize(4, 2);
    assert(term.columns() == 4 && term.rows() == 2);
    term.feed("ABCD\r\nEFGH\r\nI", 13);
    assert(term.row(1)[0].codepoint == 'I');
}

}  // namespace

int main()
{
    test_chunked_sgr_and_cursor();
    test_alternate_screen_and_modes();
    test_scroll_and_key_encoding();
    test_truecolor_and_utf8_streaming();
    test_scrollback_viewport_and_limit();
    test_utf8_replacement_and_viewport_copy();
    test_erase_insert_and_resize();
    test_form_feed_clears_and_homes();
    test_ignored_control_sequences_are_bounded();
    std::cout << "terminal_core tests passed\n";
    return 0;
}
