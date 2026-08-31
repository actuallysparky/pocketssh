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

    const char app_cursor[] = "\x1b[?1h";
    term.feed(app_cursor, std::strlen(app_cursor));
    assert(term.application_cursor_keys());
    assert(term.encode_key({KeyCode::Up}) == "\x1bOA");
    const char bracketed[] = "\x1b[?2004h";
    term.feed(bracketed, std::strlen(bracketed));
    assert(term.bracketed_paste());
}

void test_scroll_and_key_encoding()
{
    TerminalCore term(4, 2, 4);
    const char scroll_text[] = "a\r\nb\r\nc";
    term.feed(scroll_text, std::strlen(scroll_text));
    assert(term.row(1)[0].codepoint == 'c');
    assert(term.encode_key({KeyCode::PageUp}) == "\x1b[5~");
    assert(term.encode_key({KeyCode::Character, 'C', true}) == std::string("\x03", 1));
    assert(term.encode_key({KeyCode::Tab, 0, false, false, true}) == "\x1b[Z");
}

void test_truecolor_and_utf8_streaming()
{
    TerminalCore term(8, 2, 8);
    const char truecolor[] = "\x1b[38;2;255;0;0mR";
    term.feed(truecolor, std::strlen(truecolor));
    assert(term.row(0)[0].foreground == 196);
    const char snowman[] = "\xE2\x98\x83";
    feed_bytewise(term, snowman);
    assert(term.row(0)[1].codepoint == 0x2603);
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

}  // namespace

int main()
{
    test_chunked_sgr_and_cursor();
    test_alternate_screen_and_modes();
    test_scroll_and_key_encoding();
    test_truecolor_and_utf8_streaming();
    test_scrollback_viewport_and_limit();
    std::cout << "terminal_core tests passed\n";
    return 0;
}
