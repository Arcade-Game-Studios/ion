#pragma once

#include <functional>
#include <map>
#include <utility>

namespace ion {

//
// A finite state machine keyed by any ordered type (usually an enum):
//
//   enum class Enemy { Idle, Chase, Attack };
//   StateMachine<Enemy> fsm;
//   fsm.addState(Enemy::Idle, {.onUpdate = [&](float) { if (seesPlayer()) fsm.change(Enemy::Chase); }});
//   fsm.addState(Enemy::Chase, {.onEnter = [&] { growl(); }, .onUpdate = chase});
//   fsm.start(Enemy::Idle);
//   fsm.update(dt);
//
// change() requested inside a callback takes effect after that callback
// returns, so onExit/onEnter never run in the middle of another hook.
//
template <typename State>
class StateMachine {
public:
    struct Callbacks {
        std::function<void()> onEnter;
        std::function<void(float deltaTime)> onUpdate;
        std::function<void()> onExit;
    };

    void addState(State state, Callbacks callbacks) {
        states_[state] = std::move(callbacks);
    }

    bool hasState(State state) const { return states_.count(state) > 0; }

    // Enters the initial state (runs its onEnter).
    void start(State state) {
        started_ = true;
        current_ = state;
        previous_ = state;
        timeInState_ = 0.0f;
        enter_(state);
        applyPending_();
    }

    // Switches state: current onExit, then the new state's onEnter.
    // Changing to the current state re-enters it.
    void change(State state) {
        pending_ = state;
        hasPending_ = true;
        if (!inCallback_) {
            applyPending_();
        }
    }

    void update(float deltaTime) {
        if (!started_) {
            return;
        }
        timeInState_ += deltaTime;
        auto it = states_.find(current_);
        if (it != states_.end() && it->second.onUpdate) {
            inCallback_ = true;
            it->second.onUpdate(deltaTime);
            inCallback_ = false;
        }
        applyPending_();
    }

    bool isStarted() const { return started_; }
    State current() const { return current_; }
    State previous() const { return previous_; }
    bool is(State state) const { return started_ && current_ == state; }
    float timeInState() const { return timeInState_; }

    // Called after every transition with (from, to).
    void onTransition(std::function<void(State from, State to)> callback) {
        onTransition_ = std::move(callback);
    }

private:
    void enter_(State state) {
        auto it = states_.find(state);
        if (it != states_.end() && it->second.onEnter) {
            inCallback_ = true;
            it->second.onEnter();
            inCallback_ = false;
        }
    }

    void applyPending_() {
        // A bounded loop: onEnter may request another change.
        for (int guard = 0; hasPending_ && guard < 64; guard++) {
            State next = pending_;
            hasPending_ = false;
            if (started_) {
                auto it = states_.find(current_);
                if (it != states_.end() && it->second.onExit) {
                    inCallback_ = true;
                    it->second.onExit();
                    inCallback_ = false;
                }
            }
            previous_ = current_;
            current_ = next;
            timeInState_ = 0.0f;
            started_ = true;
            if (onTransition_) {
                onTransition_(previous_, current_);
            }
            enter_(next);
        }
    }

    std::map<State, Callbacks> states_;
    std::function<void(State, State)> onTransition_;
    State current_{};
    State previous_{};
    State pending_{};
    float timeInState_ = 0.0f;
    bool started_ = false;
    bool hasPending_ = false;
    bool inCallback_ = false;
};

} // namespace ion
