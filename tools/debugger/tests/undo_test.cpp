// Tests of src/undo.h: going back and forth among states, and what is left to save.
// Built as undo-test; scripts/build.ps1 test builds and runs it.
#include "undo.h"

#include <cstdio>
#include <string>

static int g_failed;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                 \
        }                                                               \
    } while (0)

int main() {
    UndoStack<std::string> h;
    CHECK(!h.Started());
    h.Start("a", true);
    CHECK(h.Started() && h.Saved() && !h.CanUndo() && !h.CanRedo());

    h.Record("b");
    h.Record("c");
    CHECK(h.Current() == "c" && !h.Saved() && h.CanUndo() && !h.CanRedo());
    CHECK(h.Undo() == "b" && h.Undo() == "a");
    CHECK(h.Saved() && !h.CanUndo() && h.CanRedo());
    CHECK(h.Redo() == "b" && h.Current() == "b");

    // A change after an undo: what had been undone is gone
    h.Record("d");
    CHECK(!h.CanRedo() && h.Current() == "d");
    CHECK(h.Undo() == "b" && h.Undo() == "a" && !h.CanUndo());

    // The state saved is past the point where a new change branches: nothing is that state anymore
    h.Redo();
    h.Redo();
    h.MarkSaved();
    h.Undo();
    CHECK(!h.Saved());
    h.Record("e");
    CHECK(!h.Saved());
    h.Undo();
    CHECK(!h.Saved());

    // Started with changes not saved: no state is the file
    UndoStack<std::string> dirty;
    dirty.Start("x", false);
    CHECK(!dirty.Saved());

    // The oldest states go past the limit, the saved mark with them
    UndoStack<int> small(3);
    small.Start(0, true);
    small.Record(1);
    small.Record(2);
    CHECK(small.Undo() == 1 && small.Undo() == 0 && small.Saved());
    small.Redo();
    small.Redo();
    small.Record(3);
    CHECK(small.Current() == 3 && !small.Saved());
    CHECK(small.Undo() == 2 && small.Undo() == 1 && !small.CanUndo() && !small.Saved());
    small.MarkSaved();
    small.Redo();
    small.Redo();
    small.Record(4);
    CHECK(small.Undo() == 3 && small.Undo() == 2 && !small.Saved());

    printf(g_failed ? "undo-test: %d failed\n" : "undo-test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
