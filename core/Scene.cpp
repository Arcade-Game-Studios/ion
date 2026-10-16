#include <ion/scene/Scene.hpp>

namespace ion {

SceneManager::~SceneManager() {
    pending_.clear();
    while (!stack_.empty()) {
        popNow_();
    }
}

void SceneManager::push(std::unique_ptr<Scene> scene) {
    if (!scene) {
        return;
    }
    pending_.push_back({Op::Push, std::move(scene)});
    if (!busy_) {
        applyPending();
    }
}

void SceneManager::pop() {
    pending_.push_back({Op::Pop, nullptr});
    if (!busy_) {
        applyPending();
    }
}

void SceneManager::replace(std::unique_ptr<Scene> scene) {
    if (!scene) {
        return;
    }
    pending_.push_back({Op::Replace, std::move(scene)});
    if (!busy_) {
        applyPending();
    }
}

void SceneManager::clear() {
    pending_.push_back({Op::Clear, nullptr});
    if (!busy_) {
        applyPending();
    }
}

void SceneManager::pushNow_(std::unique_ptr<Scene> scene) {
    if (!stack_.empty()) {
        stack_.back()->onPause();
    }
    scene->manager_ = this;
    stack_.push_back(std::move(scene));
    stack_.back()->onEnter();
}

void SceneManager::popNow_() {
    if (stack_.empty()) {
        return;
    }
    stack_.back()->onExit();
    stack_.pop_back();
    if (!stack_.empty()) {
        stack_.back()->onResume();
    }
}

void SceneManager::applyPending() {
    if (busy_) {
        return;
    }
    busy_ = true;
    // Hooks run here may request more changes; process until stable.
    for (size_t i = 0; i < pending_.size(); i++) {
        Change change = std::move(pending_[i]);
        switch (change.op) {
            case Op::Push: pushNow_(std::move(change.scene)); break;
            case Op::Pop: popNow_(); break;
            case Op::Replace:
                if (!stack_.empty()) {
                    stack_.back()->onExit();
                    stack_.pop_back();
                }
                // The scene below sees no pause/resume for a replace.
                change.scene->manager_ = this;
                stack_.push_back(std::move(change.scene));
                stack_.back()->onEnter();
                break;
            case Op::Clear:
                while (!stack_.empty()) {
                    stack_.back()->onExit();
                    stack_.pop_back();
                }
                break;
        }
    }
    pending_.clear();
    busy_ = false;
}

void SceneManager::update(float deltaTime) {
    applyPending();
    if (stack_.empty()) {
        return;
    }
    busy_ = true;
    size_t first = stack_.size() - 1;
    while (first > 0 && stack_[first]->updatesBelow()) {
        first--;
    }
    for (size_t i = first; i < stack_.size(); i++) {
        stack_[i]->update(deltaTime);
    }
    busy_ = false;
}

void SceneManager::fixedUpdate(float fixedDeltaTime) {
    applyPending();
    if (stack_.empty()) {
        return;
    }
    busy_ = true;
    size_t first = stack_.size() - 1;
    while (first > 0 && stack_[first]->updatesBelow()) {
        first--;
    }
    for (size_t i = first; i < stack_.size(); i++) {
        stack_[i]->fixedUpdate(fixedDeltaTime);
    }
    busy_ = false;
}

void SceneManager::render() {
    if (stack_.empty()) {
        return;
    }
    busy_ = true;
    size_t first = stack_.size() - 1;
    while (first > 0 && stack_[first]->isOverlay()) {
        first--;
    }
    for (size_t i = first; i < stack_.size(); i++) {
        stack_[i]->render();
    }
    busy_ = false;
}

Scene* SceneManager::top() const {
    return stack_.empty() ? nullptr : stack_.back().get();
}

} // namespace ion
