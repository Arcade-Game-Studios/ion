#pragma once

#include <ion/platform/Input.hpp>
#include <ion/render/Color.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ion {

//
// Terminal (text-mode) rendering: draw characters with 24-bit colors into a
// TerminalCanvas, then present() it. Only cells that changed since the last
// present() are rewritten, so full-screen animation stays cheap.
//
//   TerminalCanvas canvas(80, 24);
//   TerminalInput input;            // raw keyboard, restores on destruction
//   while (running) {
//       for (const TerminalKey& k : input.poll()) { ... }
//       canvas.clear();
//       canvas.drawBox(0, 0, 80, 24, Color::gray());
//       canvas.drawText(2, 1, "Score: 42", Color::yellow());
//       canvas.present();
//   }
//
// Cells hold one Unicode code point each (wide characters such as CJK are
// not width-adjusted).
//
struct TerminalCell {
    char32_t ch = U' ';
    Color fg = Color::white();
    Color bg = Color::black();

    bool operator==(const TerminalCell& o) const {
        return ch == o.ch && fg.toRGBA8() == o.fg.toRGBA8() && bg.toRGBA8() == o.bg.toRGBA8();
    }
    bool operator!=(const TerminalCell& o) const { return !(*this == o); }
};

enum class BoxStyle : uint8_t { Single, Double, Rounded, Heavy, Ascii };

class TerminalCanvas {
public:
    TerminalCanvas(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }
    // Resizing clears the canvas and forces a full redraw.
    void resize(int width, int height);

    void clear(const Color& bg = Color::black());
    // Out-of-range coordinates are ignored (drawing is clipped).
    void set(int x, int y, char32_t ch, const Color& fg = Color::white(),
             const Color& bg = Color::black());
    // Changes only the colors of a cell.
    void tint(int x, int y, const Color& fg, const Color& bg);
    const TerminalCell* at(int x, int y) const;

    // UTF-8 text on one row; '\n' moves to the next row at the start column.
    void drawText(int x, int y, const std::string& utf8, const Color& fg = Color::white(),
                  const Color& bg = Color::black());
    void fillRect(int x, int y, int w, int h, char32_t ch, const Color& fg = Color::white(),
                  const Color& bg = Color::black());
    void drawBox(int x, int y, int w, int h, const Color& fg = Color::white(),
                 const Color& bg = Color::black(), BoxStyle style = BoxStyle::Single);
    void drawLine(int x0, int y0, int x1, int y1, char32_t ch, const Color& fg = Color::white(),
                  const Color& bg = Color::black());
    // A horizontal bar filled to `fraction` (0..1) with partial-block glyphs.
    void drawBar(int x, int y, int w, float fraction, const Color& fg = Color::white(),
                 const Color& bg = Color::black());

    // Paints an ASCII sprite: each string is a row, `transparent` cells are
    // skipped.
    void drawSprite(int x, int y, const std::vector<std::string>& rows,
                    const Color& fg = Color::white(), char transparent = ' ');

    // Full-frame ANSI text (cursor home, every cell, colors reset at the
    // end). Useful for snapshot tests and logs.
    std::string render() const;

    // ANSI text that updates the terminal from the last presented frame to
    // this one (only changed cells), and records this frame as presented.
    std::string renderDiff();

    // Writes renderDiff() to stdout and flushes. Hides the cursor on the
    // first call.
    void present();

    // Forces the next renderDiff()/present() to redraw everything (e.g.
    // after the terminal was resized or cleared by something else).
    void invalidate() { presentedValid_ = false; }

private:
    int width_;
    int height_;
    std::vector<TerminalCell> cells_;
    std::vector<TerminalCell> presented_;
    bool presentedValid_ = false;
    bool cursorHidden_ = false;
};

// A key read from the terminal. Printable characters set `ch` (and `key`
// for letters, digits and space); special keys set `key` only.
struct TerminalKey {
    Key key = Key::Unknown;
    char32_t ch = 0;
    bool ctrl = false;
    bool alt = false;
};

//
// Non-blocking keyboard input from the controlling terminal. The
// constructor switches stdin to raw mode (no echo, no line buffering) and
// the destructor restores it. On platforms without a POSIX terminal,
// isActive() is false and poll() returns nothing.
//
class TerminalInput {
public:
    TerminalInput();
    ~TerminalInput();
    TerminalInput(const TerminalInput&) = delete;
    TerminalInput& operator=(const TerminalInput&) = delete;

    bool isActive() const { return active_; }

    // Keys typed since the last poll.
    std::vector<TerminalKey> poll();

    // Decodes raw terminal bytes (UTF-8 text and VT escape sequences) into
    // keys. Exposed for testing and for custom input sources.
    static std::vector<TerminalKey> decode(const std::string& bytes);

private:
    bool active_ = false;
    void* saved_ = nullptr; // platform terminal state
};

// Current terminal size in cells; false if stdout isn't a terminal.
bool terminalSize(int& columns, int& rows);

// Restores the cursor, colors and clears the screen (call before exit).
void terminalReset();

// Encodes a code point as UTF-8 (helper shared with the canvas).
void appendUtf8(std::string& out, char32_t codepoint);

} // namespace ion
