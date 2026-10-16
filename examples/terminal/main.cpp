// Terminal Snake: a complete game in the terminal using TerminalCanvas,
// TerminalInput, StateMachine, Scheduler, Tween and Random.
//
//   Arrow keys / WASD: steer    P: pause    R: restart    Q / Esc: quit

#include <ion/core/Scheduler.hpp>
#include <ion/core/StateMachine.hpp>
#include <ion/core/Timer.hpp>
#include <ion/core/Tween.hpp>
#include <ion/math/Random.hpp>
#include <ion/terminal/Terminal.hpp>

#include <chrono>
#include <cstdio>
#include <deque>
#include <random>
#include <string>
#include <thread>

namespace {

constexpr int kWidth = 40;
constexpr int kHeight = 20;

struct Cell {
    int x;
    int y;
    bool operator==(const Cell& o) const { return x == o.x && y == o.y; }
};

enum class Phase { Playing, Paused, GameOver };

class SnakeGame {
public:
    SnakeGame() : canvas_(kWidth + 2, kHeight + 4), rng_(std::random_device{}()) {
        fsm_.addState(Phase::Playing, {nullptr, [this](float dt) { play(dt); }, nullptr});
        fsm_.addState(Phase::Paused, {});
        fsm_.addState(Phase::GameOver, {[this] { flash_ = ion::Tween<float>(1.0f, 0.0f, 0.6f, ion::Ease::QuadOut); },
                                        [this](float dt) { flash_.update(dt); }, nullptr});
        reset();
    }

    bool running() const { return running_; }

    void handle(const ion::TerminalKey& k) {
        using ion::Key;
        if (k.key == Key::Q || k.key == Key::Escape || (k.ctrl && k.key == Key::C)) {
            running_ = false;
        } else if (k.key == Key::R) {
            reset();
        } else if (k.key == Key::P && !fsm_.is(Phase::GameOver)) {
            fsm_.change(fsm_.is(Phase::Paused) ? Phase::Playing : Phase::Paused);
        } else if (k.key == Key::ArrowUp || k.key == Key::W) {
            turn({0, -1});
        } else if (k.key == Key::ArrowDown || k.key == Key::S) {
            turn({0, 1});
        } else if (k.key == Key::ArrowLeft || k.key == Key::A) {
            turn({-1, 0});
        } else if (k.key == Key::ArrowRight || k.key == Key::D) {
            turn({1, 0});
        }
    }

    void update(float dt) {
        fsm_.update(dt);
        scheduler_.update(dt);
    }

    void draw() {
        canvas_.clear();
        canvas_.drawBox(0, 0, kWidth + 2, kHeight + 2, ion::Color::gray(), ion::Color::black(),
                        ion::BoxStyle::Rounded);
        canvas_.set(food_.x + 1, food_.y + 1, U'●', ion::Color::fromHex(0xFF5555));
        for (size_t i = 0; i < snake_.size(); i++) {
            // Head bright, tail fading toward dark green.
            float t = snake_.size() > 1 ? (float)i / (float)(snake_.size() - 1) : 0.0f;
            ion::Color c = ion::Color::lerp(ion::Color::fromHex(0x7CFF7C), ion::Color::fromHex(0x1F6F2F), t);
            canvas_.set(snake_[i].x + 1, snake_[i].y + 1, i == 0 ? U'█' : U'▓', c);
        }
        std::string status = "Score " + std::to_string(score_) + "   Best " + std::to_string(best_);
        canvas_.drawText(1, kHeight + 2, status, ion::Color::white());
        canvas_.drawBar(kWidth - 12, kHeight + 2, 12, 1.0f - (float)stepTime_ / 0.15f,
                        ion::Color::cyan());
        if (fsm_.is(Phase::Paused)) {
            banner("PAUSED  (P to resume)", ion::Color::yellow());
        } else if (fsm_.is(Phase::GameOver)) {
            ion::Color bg = ion::Color::lerp(ion::Color::black(), ion::Color::red(), flash_.value());
            canvas_.fillRect(1, 1, kWidth, kHeight, U' ', ion::Color::white(), bg);
            banner("GAME OVER  (R to restart, Q to quit)", ion::Color::white());
        }
        canvas_.drawText(1, kHeight + 3, "arrows/WASD move  P pause  Q quit", ion::Color::darkGray());
        canvas_.present();
    }

private:
    void banner(const std::string& text, const ion::Color& color) {
        int x = 1 + (kWidth - (int)text.size()) / 2;
        canvas_.drawText(x, kHeight / 2, text, color);
    }

    void reset() {
        snake_.clear();
        for (int i = 0; i < 4; i++) {
            snake_.push_back({kWidth / 2 - i, kHeight / 2});
        }
        dir_ = {1, 0};
        queued_ = dir_;
        score_ = 0;
        stepTime_ = 0.12;
        accumulator_ = 0.0f;
        placeFood();
        scheduler_.clear();
        fsm_.start(Phase::Playing);
    }

    void turn(Cell d) {
        // Ignore reversing straight into ourselves.
        if (d.x != -dir_.x || d.y != -dir_.y) {
            queued_ = d;
        }
    }

    void placeFood() {
        for (;;) {
            Cell c{rng_.range(0, kWidth - 1), rng_.range(0, kHeight - 1)};
            bool onSnake = false;
            for (const Cell& s : snake_) {
                onSnake = onSnake || s == c;
            }
            if (!onSnake) {
                food_ = c;
                return;
            }
        }
    }

    void play(float dt) {
        accumulator_ += dt;
        while (accumulator_ >= stepTime_ && fsm_.is(Phase::Playing)) {
            accumulator_ -= (float)stepTime_;
            step();
        }
    }

    void step() {
        dir_ = queued_;
        Cell head{snake_.front().x + dir_.x, snake_.front().y + dir_.y};
        bool hitWall = head.x < 0 || head.y < 0 || head.x >= kWidth || head.y >= kHeight;
        bool hitSelf = false;
        for (size_t i = 0; i + 1 < snake_.size(); i++) { // tail moves away this step
            hitSelf = hitSelf || snake_[i] == head;
        }
        if (hitWall || hitSelf) {
            best_ = score_ > best_ ? score_ : best_;
            fsm_.change(Phase::GameOver);
            return;
        }
        snake_.push_front(head);
        if (head == food_) {
            score_ += 10;
            stepTime_ = stepTime_ > 0.05 ? stepTime_ - 0.004 : stepTime_;
            placeFood();
        } else {
            snake_.pop_back();
        }
    }

    ion::TerminalCanvas canvas_;
    ion::Random rng_;
    ion::StateMachine<Phase> fsm_;
    ion::Scheduler scheduler_;
    ion::Tween<float> flash_;
    std::deque<Cell> snake_;
    Cell dir_{1, 0};
    Cell queued_{1, 0};
    Cell food_{0, 0};
    int score_ = 0;
    int best_ = 0;
    double stepTime_ = 0.12;
    float accumulator_ = 0.0f;
    bool running_ = true;
};

} // namespace

int main() {
    ion::TerminalInput input;
    if (!input.isActive()) {
        std::printf("terminal_example needs an interactive terminal.\n");
        return 1;
    }
    SnakeGame game;
    ion::Timer timer;
    while (game.running()) {
        for (const ion::TerminalKey& key : input.poll()) {
            game.handle(key);
        }
        game.update(timer.tick());
        game.draw();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    ion::terminalReset();
    return 0;
}
