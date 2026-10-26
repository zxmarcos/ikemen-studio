/*
 * The code editor of the debugger: a text editor widget
 * for Dear ImGui, independent of the engine.
 */
#ifndef IKDBG_EDITOR_HPP
#define IKDBG_EDITOR_HPP

#include <imgui.h>

#include <functional>
#include <stddef.h>
#include <string>

namespace dbg {

/* A text editor for the files of a character (CNS, CMD, AIR, DEF): line numbers, syntax colours,
   completion and documentation of triggers and controllers. */
struct CodeEditor;
CodeEditor *Editor_Create();
void Editor_Destroy(CodeEditor *ed);
/*
 * Draws the editor in the space left in the current window (size like ImGui::BeginChild: 0 = all).
 * text is the caller's text ('\n' line ends); when the caller changes it, it bumps version and
 * the editor takes the new text (keeping the cursor line when it can). Returns true when the user
 * changed the text this frame (text is then updated and version bumped by the editor).
 */
bool Editor_Draw(CodeEditor *ed, const char *id, std::string &text, unsigned int &version, ImVec2 size = ImVec2(0, 0));
void Editor_GotoLine(CodeEditor *ed, int line, bool select_line, bool focus = true);   /* 1-based, scrolls it into view */
void Editor_Select(CodeEditor *ed, size_t begin, size_t end);       /* byte offsets in text, scrolls, focuses */
int  Editor_CursorLine(const CodeEditor *ed);                       /* 1-based */
size_t Editor_CursorOffset(const CodeEditor *ed);                   /* byte offset in text */
void Editor_SetErrorLine(CodeEditor *ed, int line, const char *message);  /* 0: none; red line with a tooltip */
bool Editor_IsFocused(const CodeEditor *ed);
/*
 * Marks left of the line numbers and line backgrounds, asked for each line in view (1-based).
 * mark_of: 0 none, 1 a small dot, 2 a breakpoint, 3 a disabled breakpoint. background_of: 0 none.
 * Either may be empty. A click on the marks does not move the cursor: Editor_MarkClicked tells
 * the line (0: none) until the next draw.
 */
void Editor_SetDecorations(CodeEditor *ed, std::function<int(int)> mark_of, std::function<ImU32(int)> background_of);
int  Editor_MarkClicked(const CodeEditor *ed);
/* A note after the end of each line in view (1-based): its text ("" for none), and *color. */
void Editor_SetNotes(CodeEditor *ed, std::function<std::string(int, ImU32 *)> note_of);
/* The colours of every editor: dark (the default) or light, for a light theme. */
void Editor_SetLight(bool light);

} // namespace dbg

#endif /* IKDBG_EDITOR_HPP */
