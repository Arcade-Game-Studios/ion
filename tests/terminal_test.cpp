#include <ion/terminal/Terminal.hpp>

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(condition)                                                             \
    do {                                                                             \
        if (!(condition)) {                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);         \
            failures++;                                                              \
        }                                                                            \
    } while (0)

using namespace ion;

static void testCanvasDrawing() {
    TerminalCanvas canvas(10, 4);
    CHECK(canvas.width() == 10 && canvas.height() == 4);
    canvas.drawText(1, 1, "Hi\xC3\xA9!", Color::yellow());
    CHECK(canvas.at(1, 1)->ch == U'H');
    CHECK(canvas.at(3, 1)->ch == U'é'); // UTF-8 decoded to one cell
    CHECK(canvas.at(4, 1)->ch == U'!');
    CHECK(canvas.at(1, 1)->fg == Color::yellow());
    canvas.drawText(8, 0, "clip");         // clipped at the right edge
    CHECK(canvas.at(9, 0)->ch == U'l');
    canvas.set(-1, 0, U'x');
    canvas.set(0, 99, U'x');
    CHECK(canvas.at(10, 0) == nullptr);
    canvas.set(0, 0, U'\x07'); // control characters become spaces
    CHECK(canvas.at(0, 0)->ch == U' ');

    canvas.clear();
    canvas.drawBox(0, 0, 10, 4, Color::white(), Color::black(), BoxStyle::Rounded);
    CHECK(canvas.at(0, 0)->ch == U'╭');
    CHECK(canvas.at(9, 3)->ch == U'╯');
    CHECK(canvas.at(5, 0)->ch == U'─');
    CHECK(canvas.at(0, 2)->ch == U'│');
    canvas.drawBox(0, 0, 1, 1); // too small: ignored
    CHECK(canvas.at(0, 0)->ch == U'╭');

    canvas.clear();
    canvas.drawLine(0, 0, 9, 3, U'*');
    CHECK(canvas.at(0, 0)->ch == U'*' && canvas.at(9, 3)->ch == U'*');
    canvas.clear();
    canvas.drawBar(0, 0, 4, 0.5f);
    CHECK(canvas.at(0, 0)->ch == U'█' && canvas.at(1, 0)->ch == U'█');
    CHECK(canvas.at(2, 0)->ch == U' ');
    canvas.drawBar(0, 1, 4, 0.3125f); // 1.25 cells
    CHECK(canvas.at(1, 1)->ch == U'▎');
    canvas.fillRect(0, 2, 3, 2, U'#', Color::red(), Color::blue());
    CHECK(canvas.at(2, 3)->ch == U'#' && canvas.at(2, 3)->bg == Color::blue());
    canvas.tint(2, 3, Color::green(), Color::black());
    CHECK(canvas.at(2, 3)->ch == U'#' && canvas.at(2, 3)->fg == Color::green());

    canvas.clear();
    canvas.drawSprite(1, 0, {"/\\", "\\/"}, Color::cyan(), ' ');
    CHECK(canvas.at(1, 0)->ch == U'/' && canvas.at(2, 1)->ch == U'/');
    canvas.drawSprite(1, 0, {" X"}, Color::red(), ' ');
    CHECK(canvas.at(1, 0)->ch == U'/'); // transparent cell kept
    CHECK(canvas.at(2, 0)->ch == U'X');
}

static void testRendering() {
    TerminalCanvas canvas(3, 2);
    std::string full = canvas.render();
    CHECK(full.rfind("\x1b[H", 0) == 0);
    CHECK(full.find("\x1b[38;2;255;255;255m") != std::string::npos);
    CHECK(full.size() > 6);

    // First diff redraws everything; an unchanged frame emits nothing.
    std::string first = canvas.renderDiff();
    CHECK(first.find("\x1b[2J") != std::string::npos);
    CHECK(canvas.renderDiff().empty());

    // Changing one cell moves the cursor there and writes only that cell.
    canvas.set(2, 1, U'Z', Color::red());
    std::string diff = canvas.renderDiff();
    CHECK(diff.find("\x1b[2;3H") != std::string::npos);
    CHECK(diff.find("Z") != std::string::npos);
    CHECK(diff.find("\x1b[38;2;255;0;0m") != std::string::npos);
    CHECK(diff.size() < first.size());
    CHECK(canvas.renderDiff().empty());

    canvas.invalidate();
    CHECK(canvas.renderDiff().find("\x1b[2J") != std::string::npos);
    canvas.resize(4, 4);
    CHECK(canvas.at(3, 3) != nullptr);
    CHECK(canvas.renderDiff().find("\x1b[2J") != std::string::npos);
}

static void testInputDecoding() {
    std::vector<TerminalKey> keys = TerminalInput::decode("a\x1b[A\x1b[B\x1b[C\x1b[D");
    CHECK(keys.size() == 5);
    CHECK(keys[0].key == Key::A && keys[0].ch == U'a');
    CHECK(keys[1].key == Key::ArrowUp && keys[4].key == Key::ArrowLeft);

    keys = TerminalInput::decode("\x1b[1;5C");
    CHECK(keys.size() == 1 && keys[0].key == Key::ArrowRight && keys[0].ctrl && !keys[0].alt);
    keys = TerminalInput::decode("\x1b[3~\x1b[5~\x1b[15~\x1bOP");
    CHECK(keys.size() == 4);
    CHECK(keys[0].key == Key::Delete && keys[1].key == Key::PageUp);
    CHECK(keys[2].key == Key::F5 && keys[3].key == Key::F1);

    keys = TerminalInput::decode("\x1b");
    CHECK(keys.size() == 1 && keys[0].key == Key::Escape);
    keys = TerminalInput::decode("\x1bx");
    CHECK(keys.size() == 1 && keys[0].key == Key::X && keys[0].alt);
    keys = TerminalInput::decode("\x03\r\t\x7f 7Q");
    CHECK(keys.size() == 7);
    CHECK(keys[0].key == Key::C && keys[0].ctrl);
    CHECK(keys[1].key == Key::Enter && keys[2].key == Key::Tab);
    CHECK(keys[3].key == Key::Backspace && keys[4].key == Key::Space);
    CHECK(keys[5].key == Key::Num7 && keys[6].key == Key::Q && keys[6].ch == U'Q');
    keys = TerminalInput::decode("\xC3\xA9");
    CHECK(keys.size() == 1 && keys[0].ch == U'é' && keys[0].key == Key::Unknown);
    keys = TerminalInput::decode("\x1b[1;"); // truncated sequence: no crash
    CHECK(keys.empty());
    keys = TerminalInput::decode("\xFF");
    CHECK(keys.size() == 1 && keys[0].ch == 0xFFFD);

    // Running under ctest stdin isn't a terminal; constructing must be safe.
    TerminalInput input;
    if (!input.isActive()) {
        CHECK(input.poll().empty());
    }
    int cols = 0, rows = 0;
    terminalSize(cols, rows); // may fail without a tty; must not crash
}

int main() {
    testCanvasDrawing();
    testRendering();
    testInputDecoding();
    if (failures == 0) {
        std::printf("All terminal tests passed\n");
        return 0;
    }
    std::printf("%d terminal test(s) FAILED\n", failures);
    return 1;
}
