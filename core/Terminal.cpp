#include <ion/terminal/Terminal.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace ion {

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back((char)cp);
    } else if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x110000) {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

namespace {

// Decodes one UTF-8 sequence at s[i], advancing i. Invalid bytes become
// U+FFFD.
char32_t nextCodepoint(const std::string& s, size_t& i) {
    unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) {
        return c;
    }
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
    if (extra < 0 || i + (size_t)extra > s.size()) {
        return 0xFFFD;
    }
    char32_t cp = c & (0x3F >> extra);
    for (int k = 0; k < extra; k++) {
        unsigned char cc = (unsigned char)s[i];
        if ((cc & 0xC0) != 0x80) {
            return 0xFFFD;
        }
        cp = (cp << 6) | (cc & 0x3F);
        i++;
    }
    return cp;
}

void appendColor(std::string& out, bool foreground, const Color& c) {
    uint32_t rgba = c.toRGBA8();
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "\x1b[%d;2;%u;%u;%um", foreground ? 38 : 48,
                  rgba & 0xFF, (rgba >> 8) & 0xFF, (rgba >> 16) & 0xFF);
    out += buffer;
}

void appendMove(std::string& out, int x, int y) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "\x1b[%d;%dH", y + 1, x + 1);
    out += buffer;
}

struct BoxGlyphs {
    char32_t h, v, tl, tr, bl, br;
};

BoxGlyphs boxGlyphs(BoxStyle style) {
    switch (style) {
    case BoxStyle::Double: return {U'═', U'║', U'╔', U'╗', U'╚', U'╝'};
    case BoxStyle::Rounded: return {U'─', U'│', U'╭', U'╮', U'╰', U'╯'};
    case BoxStyle::Heavy: return {U'━', U'┃', U'┏', U'┓', U'┗', U'┛'};
    case BoxStyle::Ascii: return {U'-', U'|', U'+', U'+', U'+', U'+'};
    case BoxStyle::Single:
    default: return {U'─', U'│', U'┌', U'┐', U'└', U'┘'};
    }
}

} // namespace

// ---------------------------------------------------------------------------
// TerminalCanvas
// ---------------------------------------------------------------------------

TerminalCanvas::TerminalCanvas(int width, int height) {
    resize(width, height);
}

void TerminalCanvas::resize(int width, int height) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    cells_.assign((size_t)width_ * height_, TerminalCell());
    presented_.clear();
    presentedValid_ = false;
}

void TerminalCanvas::clear(const Color& bg) {
    TerminalCell blank;
    blank.bg = bg;
    std::fill(cells_.begin(), cells_.end(), blank);
}

void TerminalCanvas::set(int x, int y, char32_t ch, const Color& fg, const Color& bg) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return;
    }
    TerminalCell& cell = cells_[(size_t)y * width_ + x];
    cell.ch = ch < 0x20 ? U' ' : ch; // control characters would corrupt output
    cell.fg = fg;
    cell.bg = bg;
}

void TerminalCanvas::tint(int x, int y, const Color& fg, const Color& bg) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return;
    }
    TerminalCell& cell = cells_[(size_t)y * width_ + x];
    cell.fg = fg;
    cell.bg = bg;
}

const TerminalCell* TerminalCanvas::at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return nullptr;
    }
    return &cells_[(size_t)y * width_ + x];
}

void TerminalCanvas::drawText(int x, int y, const std::string& utf8, const Color& fg,
                              const Color& bg) {
    int cx = x;
    size_t i = 0;
    while (i < utf8.size()) {
        char32_t cp = nextCodepoint(utf8, i);
        if (cp == U'\n') {
            cx = x;
            y++;
            continue;
        }
        set(cx++, y, cp, fg, bg);
    }
}

void TerminalCanvas::fillRect(int x, int y, int w, int h, char32_t ch, const Color& fg,
                              const Color& bg) {
    for (int row = y; row < y + h; row++) {
        for (int col = x; col < x + w; col++) {
            set(col, row, ch, fg, bg);
        }
    }
}

void TerminalCanvas::drawBox(int x, int y, int w, int h, const Color& fg, const Color& bg,
                             BoxStyle style) {
    if (w < 2 || h < 2) {
        return;
    }
    BoxGlyphs g = boxGlyphs(style);
    for (int col = x + 1; col < x + w - 1; col++) {
        set(col, y, g.h, fg, bg);
        set(col, y + h - 1, g.h, fg, bg);
    }
    for (int row = y + 1; row < y + h - 1; row++) {
        set(x, row, g.v, fg, bg);
        set(x + w - 1, row, g.v, fg, bg);
    }
    set(x, y, g.tl, fg, bg);
    set(x + w - 1, y, g.tr, fg, bg);
    set(x, y + h - 1, g.bl, fg, bg);
    set(x + w - 1, y + h - 1, g.br, fg, bg);
}

void TerminalCanvas::drawLine(int x0, int y0, int x1, int y1, char32_t ch, const Color& fg,
                              const Color& bg) {
    // Bresenham.
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        set(x0, y0, ch, fg, bg);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void TerminalCanvas::drawBar(int x, int y, int w, float fraction, const Color& fg,
                             const Color& bg) {
    static const char32_t partial[8] = {U' ', U'▏', U'▎', U'▍', U'▌', U'▋', U'▊', U'▉'};
    if (w <= 0) {
        return;
    }
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    int eighths = (int)std::lround(fraction * (float)w * 8.0f);
    for (int i = 0; i < w; i++) {
        int cell = std::clamp(eighths - i * 8, 0, 8);
        set(x + i, y, cell == 8 ? U'█' : partial[cell], fg, bg);
    }
}

void TerminalCanvas::drawSprite(int x, int y, const std::vector<std::string>& rows,
                                const Color& fg, char transparent) {
    for (size_t row = 0; row < rows.size(); row++) {
        int col = x;
        size_t i = 0;
        const std::string& line = rows[row];
        while (i < line.size()) {
            char32_t cp = nextCodepoint(line, i);
            if (cp != (char32_t)(unsigned char)transparent) {
                const TerminalCell* existing = at(col, y + (int)row);
                set(col, y + (int)row, cp, fg, existing ? existing->bg : Color::black());
            }
            col++;
        }
    }
}

std::string TerminalCanvas::render() const {
    std::string out = "\x1b[H";
    for (int y = 0; y < height_; y++) {
        if (y > 0) {
            appendMove(out, 0, y);
        }
        uint32_t lastFg = 0, lastBg = 0;
        bool haveColor = false;
        for (int x = 0; x < width_; x++) {
            const TerminalCell& cell = cells_[(size_t)y * width_ + x];
            uint32_t fg = cell.fg.toRGBA8();
            uint32_t bg = cell.bg.toRGBA8();
            if (!haveColor || fg != lastFg) {
                appendColor(out, true, cell.fg);
            }
            if (!haveColor || bg != lastBg) {
                appendColor(out, false, cell.bg);
            }
            haveColor = true;
            lastFg = fg;
            lastBg = bg;
            appendUtf8(out, cell.ch);
        }
    }
    out += "\x1b[0m";
    return out;
}

std::string TerminalCanvas::renderDiff() {
    if (!presentedValid_ || presented_.size() != cells_.size()) {
        presented_ = cells_;
        presentedValid_ = true;
        return "\x1b[2J" + render();
    }
    std::string out;
    int cursorX = -1, cursorY = -1;
    uint32_t lastFg = 0, lastBg = 0;
    bool haveColor = false;
    for (int y = 0; y < height_; y++) {
        for (int x = 0; x < width_; x++) {
            size_t index = (size_t)y * width_ + x;
            const TerminalCell& cell = cells_[index];
            if (cell == presented_[index]) {
                continue;
            }
            if (cursorX != x || cursorY != y) {
                appendMove(out, x, y);
            }
            uint32_t fg = cell.fg.toRGBA8();
            uint32_t bg = cell.bg.toRGBA8();
            if (!haveColor || fg != lastFg) {
                appendColor(out, true, cell.fg);
            }
            if (!haveColor || bg != lastBg) {
                appendColor(out, false, cell.bg);
            }
            haveColor = true;
            lastFg = fg;
            lastBg = bg;
            appendUtf8(out, cell.ch);
            presented_[index] = cell;
            cursorX = x + 1;
            cursorY = y;
        }
    }
    if (!out.empty()) {
        out += "\x1b[0m";
    }
    return out;
}

void TerminalCanvas::present() {
    std::string out;
    if (!cursorHidden_) {
        out += "\x1b[?25l";
        cursorHidden_ = true;
#if defined(_WIN32)
        // Let the Windows console interpret ANSI/VT sequences.
        HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (GetConsoleMode(console, &mode)) {
            SetConsoleMode(console, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
        SetConsoleOutputCP(CP_UTF8);
#endif
    }
    out += renderDiff();
    std::fwrite(out.data(), 1, out.size(), stdout);
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

std::vector<TerminalKey> TerminalInput::decode(const std::string& bytes) {
    std::vector<TerminalKey> keys;
    size_t i = 0;
    while (i < bytes.size()) {
        unsigned char c = (unsigned char)bytes[i];
        TerminalKey k;
        if (c == 0x1B) {
            // Escape sequences: CSI "ESC [" or SS3 "ESC O" followed by a
            // final byte; a lone ESC is the Escape key; ESC + char is Alt.
            if (i + 1 < bytes.size() && (bytes[i + 1] == '[' || bytes[i + 1] == 'O')) {
                size_t j = i + 2;
                std::string params;
                while (j < bytes.size() && ((bytes[j] >= '0' && bytes[j] <= '9') || bytes[j] == ';')) {
                    params.push_back(bytes[j++]);
                }
                if (j >= bytes.size()) {
                    break; // incomplete sequence
                }
                char final = bytes[j];
                i = j + 1;
                switch (final) {
                case 'A': k.key = Key::ArrowUp; break;
                case 'B': k.key = Key::ArrowDown; break;
                case 'C': k.key = Key::ArrowRight; break;
                case 'D': k.key = Key::ArrowLeft; break;
                case 'H': k.key = Key::Home; break;
                case 'F': k.key = Key::End; break;
                case 'P': k.key = Key::F1; break;
                case 'Q': k.key = Key::F2; break;
                case 'R': k.key = Key::F3; break;
                case 'S': k.key = Key::F4; break;
                case 'Z': k.key = Key::Tab; break; // shift-tab
                case '~': {
                    int code = std::atoi(params.c_str());
                    switch (code) {
                    case 1: case 7: k.key = Key::Home; break;
                    case 2: k.key = Key::Insert; break;
                    case 3: k.key = Key::Delete; break;
                    case 4: case 8: k.key = Key::End; break;
                    case 5: k.key = Key::PageUp; break;
                    case 6: k.key = Key::PageDown; break;
                    case 15: k.key = Key::F5; break;
                    case 17: k.key = Key::F6; break;
                    case 18: k.key = Key::F7; break;
                    case 19: k.key = Key::F8; break;
                    case 20: k.key = Key::F9; break;
                    case 21: k.key = Key::F10; break;
                    case 23: k.key = Key::F11; break;
                    case 24: k.key = Key::F12; break;
                    default: break;
                    }
                    break;
                }
                default: break;
                }
                // Modifier parameter "1;5A" style: 5 = ctrl, 3 = alt.
                size_t semi = params.find(';');
                if (semi != std::string::npos) {
                    int mod = std::atoi(params.c_str() + semi + 1) - 1;
                    k.alt = (mod & 2) != 0;
                    k.ctrl = (mod & 4) != 0;
                }
                if (k.key != Key::Unknown) {
                    keys.push_back(k);
                }
                continue;
            }
            if (i + 1 < bytes.size() && bytes[i + 1] != 0x1B) {
                // Alt + key: decode the next key and flag it.
                std::vector<TerminalKey> rest = decode(bytes.substr(i + 1, 1));
                i += 2;
                for (TerminalKey& r : rest) {
                    r.alt = true;
                    keys.push_back(r);
                }
                continue;
            }
            k.key = Key::Escape;
            keys.push_back(k);
            i++;
            continue;
        }
        if (c == '\r' || c == '\n') {
            k.key = Key::Enter;
            i++;
        } else if (c == '\t') {
            k.key = Key::Tab;
            i++;
        } else if (c == 0x7F || c == 0x08) {
            k.key = Key::Backspace;
            i++;
        } else if (c < 0x20) {
            // Ctrl+letter arrives as 1..26.
            k.ctrl = true;
            k.key = static_cast<Key>(static_cast<int>(Key::A) + (c - 1));
            i++;
        } else {
            char32_t cp = nextCodepoint(bytes, i);
            k.ch = cp;
            if (cp >= U'a' && cp <= U'z') {
                k.key = static_cast<Key>(static_cast<int>(Key::A) + (int)(cp - U'a'));
            } else if (cp >= U'A' && cp <= U'Z') {
                k.key = static_cast<Key>(static_cast<int>(Key::A) + (int)(cp - U'A'));
            } else if (cp >= U'0' && cp <= U'9') {
                k.key = static_cast<Key>(static_cast<int>(Key::Num0) + (int)(cp - U'0'));
            } else if (cp == U' ') {
                k.key = Key::Space;
            }
        }
        keys.push_back(k);
    }
    return keys;
}

#if defined(_WIN32)

TerminalInput::TerminalInput() {}
TerminalInput::~TerminalInput() {}
std::vector<TerminalKey> TerminalInput::poll() { return {}; }

bool terminalSize(int& columns, int& rows) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        return false;
    }
    columns = info.srWindow.Right - info.srWindow.Left + 1;
    rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    return true;
}

#else

TerminalInput::TerminalInput() {
    if (!isatty(STDIN_FILENO)) {
        return;
    }
    auto* saved = new termios();
    if (tcgetattr(STDIN_FILENO, saved) != 0) {
        delete saved;
        return;
    }
    termios raw = *saved;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 0; // reads return immediately
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        delete saved;
        return;
    }
    saved_ = saved;
    active_ = true;
}

TerminalInput::~TerminalInput() {
    if (saved_) {
        tcsetattr(STDIN_FILENO, TCSANOW, static_cast<termios*>(saved_));
        delete static_cast<termios*>(saved_);
    }
}

std::vector<TerminalKey> TerminalInput::poll() {
    if (!active_) {
        return {};
    }
    std::string bytes;
    char buffer[256];
    for (;;) {
        ssize_t n = ::read(STDIN_FILENO, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        bytes.append(buffer, (size_t)n);
    }
    return decode(bytes);
}

bool terminalSize(int& columns, int& rows) {
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0) {
        return false;
    }
    columns = ws.ws_col;
    rows = ws.ws_row;
    return true;
}

#endif

void terminalReset() {
    std::fputs("\x1b[0m\x1b[2J\x1b[H\x1b[?25h", stdout);
    std::fflush(stdout);
}

} // namespace ion
