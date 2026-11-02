// What has changes not saved, of every kind of file: the list the status bar counts, File > Save and
// apply saves, and the dialog that asks about them, from the status bar or on closing the window.
#include "app.h"
#include "app_internal.h"

#include <cfloat>

namespace app {

namespace {

const char *kTitle = ICON_SAVE " Unsaved changes";
bool g_asked;              // the dialog, from the next frame
bool g_closing;            // the window closes once they are dealt with
bool g_quit;               // it can close now
bool g_failed;             // Save all left some of them
double g_waitUntil = -1;   // Save all pressed: what the game writes itself says so a few frames later

} // namespace

std::vector<Unsaved> UnsavedList() {
    std::vector<Unsaved> out;
    for (auto &kv : g_files)
        if (kv.second.Dirty()) {
            SourceFile *sf = &kv.second;
            out.push_back({sf->path, [sf] {
                               if (!SaveSource(*sf)) return false;
                               ScreenFileSaved(sf->path);
                               return true;
                           }});
        }
    PalUnsaved(out); // before the sprites: the game reads them with the SFF
    SpritesUnsaved(out);
    SoundsUnsaved(out);
    FontsUnsaved(out);
    ModelUnsaved(out);
    return out;
}

bool SaveAll() {
    bool ok = true;
    for (Unsaved &u : UnsavedList()) ok &= u.save();
    return ok;
}

void UnsavedAsk(bool closing) {
    g_asked = true;
    g_closing = closing;
}

void UnsavedDialog() {
    if (g_asked) {
        g_asked = false;
        g_failed = false;
        g_waitUntil = -1;
        ImGui::OpenPopup(kTitle);
    }
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(em * 32, 0));
    ImGui::SetNextWindowBgAlpha(1.0f);
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        return;
    std::vector<Unsaved> list = UnsavedList();
    bool waiting = g_waitUntil >= 0;
    if (list.empty()) {
        g_quit = g_closing;
        g_waitUntil = -1;
        ImGui::CloseCurrentPopup();
    } else if (waiting && ImGui::GetTime() > g_waitUntil) {
        g_waitUntil = -1;
        g_failed = true;
    }
    ImGui::TextUnformatted(g_closing ? "Save these before closing?" : "These have changes that are not saved:");
    float line = ImGui::GetTextLineHeightWithSpacing();
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, line * 12));
    ImGui::BeginChild("files", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
    for (const Unsaved &u : list) ImGui::TextUnformatted(u.path.c_str());
    ImGui::EndChild();
    if (g_failed) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(Ink(kRed), "Those above could not be saved.");
        ImGui::PopTextWrapPos();
    }
    ImGui::Separator();
    ImGui::BeginDisabled(waiting);
    if (ImGui::Button(ICON_SAVE " Save all")) {
        g_failed = !SaveAll();
        if (!g_failed) {
            if (!g_closing) ApplyToGame();
            g_waitUntil = ImGui::GetTime() + 3;
        }
    }
    if (g_closing) {
        ImGui::SameLine();
        if (ImGui::Button(ICON_TRASH " Don't save")) g_quit = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(ICON_XMARK " Cancel") || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive())) {
        g_closing = false;
        g_waitUntil = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace app

bool App_MayClose() {
    if (app::g_quit || app::UnsavedList().empty()) return true;
    app::UnsavedAsk(true);
    return false;
}

bool App_Quit() { return app::g_quit; }
