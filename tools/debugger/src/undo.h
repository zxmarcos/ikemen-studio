// Undo and redo by whole states: a window records the state each time a change is over, and goes
// back and forth among them. The state last saved to a file is remembered, so that going back to
// it leaves nothing to save.
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

template <class State> class UndoStack {
public:
    explicit UndoStack(size_t limit = 64) : limit_(limit) {}

    bool Started() const { return !states_.empty(); }
    void Start(State now, bool saved) {
        states_.clear();
        states_.push_back(std::move(now));
        at_ = 0;
        saved_ = saved ? 0 : kNone;
    }
    const State &Current() const { return states_[at_]; }

    // A new state after the current one; what had been undone is gone
    void Record(State now) {
        states_.resize(at_ + 1);
        if (saved_ != kNone && saved_ > at_) saved_ = kNone;
        states_.push_back(std::move(now));
        at_++;
        if (states_.size() > limit_) {
            states_.erase(states_.begin());
            at_--;
            saved_ = saved_ == 0 ? kNone : saved_ == kNone ? kNone : saved_ - 1;
        }
    }

    bool CanUndo() const { return at_ > 0; }
    bool CanRedo() const { return at_ + 1 < states_.size(); }
    const State &Undo() { return states_[--at_]; }
    const State &Redo() { return states_[++at_]; }

    void MarkSaved() { saved_ = at_; }
    bool Saved() const { return saved_ == at_; }

private:
    static constexpr size_t kNone = (size_t)-1;
    std::vector<State> states_;
    size_t at_ = 0, saved_ = kNone, limit_;
};
