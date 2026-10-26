// Animation: the selected player's AIR element, its boxes edited here and on the game; or the
// action a screen's element shows (fight screen, motif, storyboard: [Begin Action] in their def).
#include "app_internal.h"

#include <fstream>

namespace app {

bool g_boxEdit = true;    // the selected player's boxes can be dragged on the Game window
bool g_followSel = true;  // a zoomed Game window keeps the selected player in view
int g_animDrag = 0;       // what a drag on the selected player changes: 0 its boxes, 1 the sprite's offset
bool g_onion = true;      // the previous element's boxes are drawn too, faint
bool g_onionSprite = false; // and its sprite
bool g_animFollow = true; // the Animation window shows what the selected player shows
int g_animAction = 0, g_animElem = 1;

std::map<int, std::pair<std::string, double>> g_swapWanted;

void OnFiles(const Json &j) {
    for (const auto &p : j["players"].items) {
        int pn = p["pn"].i();
        auto &files = g_charFiles[pn];
        files.clear();
        for (const auto &kv : p["files"].fields)
            for (const auto &f : kv.second.items) files[kv.first].push_back(f.str());
        // Still the character before a swap (asked for before the match started again): asked again
        auto want = g_swapWanted.find(pn);
        if (want == g_swapWanted.end()) continue;
        const std::string &name = want->second.first;
        std::string def = files.count("def") && !files["def"].empty() ? NormPath(files["def"][0]) : "";
        std::string expected = NormPath("chars/" + (name.find('/') == std::string::npos ? name + "/" + name + ".def" : name));
        if (def == expected || ImGui::GetTime() - want->second.second > 15) g_swapWanted.erase(want);
        else g_charFiles.erase(pn);
    }
}

// The AIR file of a player's animations ("" until the game has told).
std::string AirPath(int pn) {
    auto it = g_charFiles.find(pn);
    if (it == g_charFiles.end()) {
        if (Connected() && g_canTrace && ImGui::GetTime() - g_filesAsked[pn] > 2.0) {
            g_filesAsked[pn] = ImGui::GetTime();
            SendRaw("ikdbg.files()");
        }
        return "";
    }
    auto f = it->second.find("anim");
    return f == it->second.end() || f->second.empty() ? "" : f->second[0];
}

// Parsed AIR files, parsed again when their text changes.
struct AirCache {
    std::string text;
    bool parsed = false;
    AirFile air;
};
std::map<std::string, AirCache> g_airs;

const AirFile &AirOf(const SourceFile &sf) {
    AirCache &c = g_airs[NormPath(sf.path)];
    if (!c.parsed || c.text != sf.text) {
        c.text = sf.text;
        Air_Parse(c.text, c.air);
        c.parsed = true;
    }
    return c.air;
}

const FrameEntity *FindFrameEntity(int id) {
    for (const auto &f : g_frameEnts)
        if (f.id == id) return &f;
    return nullptr;
}

AnimTarget GetAnimTarget() {
    AnimTarget t;
    t.e = FindEntity(g_selId);
    t.fe = FindFrameEntity(g_selId);
    t.pn = t.fe && t.fe->action >= 0 ? t.fe->animPn : t.e ? t.e->pn : 0;
    if (t.pn <= 0) return t;
    std::string path = AirPath(t.pn);
    if (path.empty()) return t;
    t.sf = &LoadSource(path);
    const AirFile &air = AirOf(*t.sf);
    if (g_animFollow && t.fe && t.fe->action >= 0) {
        g_animAction = t.fe->action;
        g_animElem = t.fe->elem;
    }
    auto it = air.actions.find(g_animAction);
    if (it == air.actions.end() || it->second.elems.empty()) return t;
    t.act = &it->second;
    t.elem = std::max(1, std::min(g_animElem, (int)t.act->elems.size()));
    t.live = t.fe && t.fe->action == g_animAction && t.fe->elem == t.elem;
    return t;
}

// The changed AIR text goes to every player using that file (the file itself is saved apart).
void ApplyAnims(SourceFile &sf) {
    sf.version++; // the Code window's editor takes the new text
    char dir[MAX_PATH];
    GetTempPathA(MAX_PATH, dir);
    for (const auto &kv : g_charFiles) {
        auto f = kv.second.find("anim");
        if (f == kv.second.end() || f->second.empty() || NormPath(f->second[0]) != NormPath(sf.path)) continue;
        std::string tmp = std::string(dir) + "ikdbg_anim_" + std::to_string(kv.first) + ".air";
        std::ofstream out(tmp, std::ios::binary);
        out << sf.text;
        out.close();
        SendRaw("ikdbg.anims(" + std::to_string(kv.first) + ", " + LuaQuote(tmp) + ")");
    }
}

void AddBox(AnimTarget &t, int g) {
    const AirElem &el = t.act->elems[t.elem - 1];
    std::vector<AirBox> boxes = el.clsn[g];
    AirBox b;
    if (boxes.empty()) {
        b.x1 = -15, b.y1 = -80, b.x2 = 15, b.y2 = 0;
    } else {
        b = boxes.back();
        b.y1 -= 10, b.y2 -= 10;
    }
    boxes.push_back(b);
    if (Air_SetOwnBoxes(t.sf->text, el, g, boxes)) ApplyAnims(*t.sf);
}

void DeleteBox(AnimTarget &t, int g, int index) {
    const AirElem &el = t.act->elems[t.elem - 1];
    std::vector<AirBox> boxes = el.clsn[g];
    boxes.erase(boxes.begin() + index);
    if (Air_SetOwnBoxes(t.sf->text, el, g, boxes)) ApplyAnims(*t.sf);
}

// Number of elements of the action whose boxes come from that header line.
int SharedBy(const AirAction &act, int g, int header) {
    int n = 0;
    for (const auto &e : act.elems) n += e.header[g] == header;
    return n;
}

// The action a screen's element shows, as a target: the def is its file.
static AnimTarget ScreenAnimTarget(const ScreenAnim &sa) {
    AnimTarget t;
    t.sf = sa.sf;
    if (g_animFollow) {
        g_animAction = sa.action;
        if (sa.elem > 0) g_animElem = sa.elem;
    }
    const AirFile &air = AirOf(*t.sf);
    auto it = air.actions.find(g_animAction);
    if (it == air.actions.end() || it->second.elems.empty()) return t;
    t.act = &it->second;
    t.elem = std::max(1, std::min(g_animElem, (int)t.act->elems.size()));
    t.live = sa.action == g_animAction && sa.elem == t.elem;
    return t;
}

void AnimationWindow() {
    if (!BeginTool("Animation")) return;
    // What it shows: the selected player's animation, or that of the screen's element picked last
    static bool onScreen = false;
    static int seenSel = -2, seenSelPicked = 0, seenPicked = 0;
    if (g_selId != seenSel || g_selPicked != seenSelPicked) seenSel = g_selId, seenSelPicked = g_selPicked, onScreen = false;
    if (g_screenAnimPicked != seenPicked) seenPicked = g_screenAnimPicked, onScreen = true, g_animFollow = true;
    ScreenAnim sa;
    bool screen = onScreen && SelectedScreenAnim(sa);
    AnimTarget t = screen ? ScreenAnimTarget(sa) : GetAnimTarget();
    // An edit: the game shows it (a character's animations, or the screen's def)
    auto apply = [&](SourceFile &s) {
        if (screen) {
            sa.apply();
        } else {
            ApplyAnims(s);
        }
    };
    if (!screen && !t.e) {
        ImGui::TextDisabled("Pick a player in the Game or Players window, or an animated element of a screen\n"
                           "(Fight screen, Motif, Storyboard).");
        ImGui::End();
        return;
    }
    if (screen)
        ImGui::TextUnformatted(sa.label.c_str());
    else
        ImGui::Text("%s  %s", Who(t.e->pn, t.e->helper, t.e->id).c_str(), t.e->name.c_str());
    ImGui::Checkbox(screen ? "Follow the element" : "Follow the player", &g_animFollow);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
    if (ImGui::InputInt("Action", &g_animAction, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) g_animFollow = false;
    if (screen) {
        ImGui::SameLine();
        HelpMarker("The action this element of the screen shows, in its def ([Begin Action]). Every change goes to "
                   "the game at once (the def is loaded again); Save writes the def, which its window edits too.");
        if (!t.live && sa.elem == 0) ImGui::TextDisabled("Not on screen now.");
    } else {
        ImGui::Checkbox("On the game, drag", &g_boxEdit);
        ImGui::SameLine();
        ImGui::BeginDisabled(!g_boxEdit);
        ImGui::RadioButton("its boxes", &g_animDrag, 0);
        ImGui::SameLine();
        ImGui::RadioButton("the sprite", &g_animDrag, 1);
        ImGui::TextUnformatted("Previous element:");
        ImGui::SameLine();
        ImGui::Checkbox("boxes", &g_onion);
        ImGui::SameLine();
        ImGui::Checkbox("sprite", &g_onionSprite);
        ImGui::EndDisabled();
        HelpMarker("Pause the game (F5). Boxes: drag the edges, the corners or the inside of a box of the selected "
                   "player's element. The sprite: drag near the player to move the element's offset. Changes go to "
                   "the game at once; Save writes the AIR file. Mouse wheel on the game: zoom; right button: pan.");
    }
    if (!t.sf) {
        ImGui::TextDisabled(g_canTrace ? "Waiting for the files of P%d..." : "Needs the patched Ikemen GO.", t.pn);
        ImGui::End();
        return;
    }
    SourceFile &sf = *t.sf;
    ImGui::BeginDisabled(!sf.Dirty());
    if (ImGui::Button("Save")) SaveSource(sf);
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
        ReadSource(sf);
        apply(sf);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s", sf.path.c_str(), sf.Dirty() ? " *" : "");
    if (!t.act) {
        ImGui::TextDisabled("Action %d is not in %s.", g_animAction, sf.path.c_str());
        ImGui::End();
        return;
    }

    // The action: show it on the player, copy, renumber, delete
    static int newNo;
    if (!screen) {
        if (ImGui::Button("Show on the player")) {
            ExecOn(t.e->id, "changeAnim(" + std::to_string(g_animAction) + ")");
            g_animFollow = true;
        }
        ImGui::SetItemTooltip("ChangeAnim on the selected player (not on helpers); pause first to keep it.");
        ImGui::SameLine();
    }
    if (ImGui::Button("Copy as...")) {
        newNo = g_animAction + 1;
        ImGui::OpenPopup("copy");
    }
    ImGui::SameLine();
    if (ImGui::Button("Renumber...")) {
        newNo = g_animAction;
        ImGui::OpenPopup("renumber");
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete action")) ImGui::OpenPopup("delete");
    for (const char *popup : {"copy", "renumber"}) {
        if (!ImGui::BeginPopup(popup)) continue;
        bool copy = popup[0] == 'c';
        ImGui::TextUnformatted(copy ? "A copy of this action, numbered" : "This action's new number");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        bool ok = ImGui::InputInt("##no", &newNo, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ok |= ImGui::Button("OK");
        bool taken = AirOf(sf).actions.count(newNo) != 0;
        if (taken) ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "Action %d exists already.", newNo);
        if (ok && !taken) {
            if (copy ? Air_CopyAction(sf.text, *t.act, newNo) : Air_RenumberAction(sf.text, *t.act, newNo)) {
                apply(sf);
                g_animAction = newNo;
                g_animFollow = false;
            }
            ImGui::CloseCurrentPopup();
        }
        if (!copy) ImGui::TextDisabled("States that use the old number are not changed.");
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("delete")) {
        ImGui::Text("Delete action %d?", g_animAction);
        if (ImGui::Button("Delete")) {
            if (Air_DeleteAction(sf.text, *t.act)) apply(sf);
            g_animFollow = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // The elements; a click shows one on the game (pause first to keep it there)
    ImGui::SeparatorText("Elements");
    for (int n = 1; n <= (int)t.act->elems.size(); n++) {
        if (n > 1) ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 2) ImGui::NewLine();
        bool cur = n == t.elem;
        if (t.act->loopStart == n - 1) {
            ImGui::TextDisabled("|");
            ImGui::SameLine();
        }
        if (cur) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        char label[16];
        snprintf(label, sizeof(label), "%d", n);
        if (ImGui::SmallButton(label)) {
            g_animElem = n;
            if (!screen && t.fe && t.fe->action == g_animAction) {
                char lua[96];
                snprintf(lua, sizeof(lua), "ikdbg.with(%d, function() debugSetAnimElem(%d) end)", t.e->id, n);
                SendRaw(lua);
            } else {
                g_animFollow = false;
            }
        }
        if (cur) ImGui::PopStyleColor();
    }

    // The element: duplicate, delete, move, loop from it
    int index = t.elem - 1, count = (int)t.act->elems.size();
    int goTo = 0; // element to show after an edit
    if (ImGui::SmallButton("Duplicate") && Air_DuplicateElem(sf.text, *t.act, index)) goTo = t.elem + 1;
    ImGui::SameLine();
    ImGui::BeginDisabled(count < 2);
    if (ImGui::SmallButton("Delete") && Air_DeleteElem(sf.text, *t.act, index)) goTo = std::max(1, t.elem - 1);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(index == 0);
    if (ImGui::SmallButton("<") && Air_MoveElem(sf.text, *t.act, index, -1)) goTo = t.elem - 1;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(index == count - 1);
    if (ImGui::SmallButton(">") && Air_MoveElem(sf.text, *t.act, index, 1)) goTo = t.elem + 1;
    ImGui::EndDisabled();
    ImGui::SameLine();
    bool loopHere = t.act->loopStart == index;
    if (ImGui::SmallButton(loopHere ? "No loop start" : "Loop from here") &&
        Air_SetLoopStart(sf.text, *t.act, loopHere ? -1 : index))
        goTo = t.elem;
    if (goTo) {
        apply(sf);
        g_animElem = goTo;
        if (screen) g_animFollow = false;
        if (!screen && t.fe && t.fe->action == g_animAction) {
            char lua[96];
            snprintf(lua, sizeof(lua), "ikdbg.with(%d, function() debugSetAnimElem(%d) end)", t.e->id, goTo);
            SendRaw(lua);
        }
        ImGui::End();
        return; // the lines changed
    }

    const AirElem &el = t.act->elems[t.elem - 1];
    ImGui::SeparatorText("Element");
    // sprite (group, image), offset (x, y), time: a row each
    int v[5] = {el.group, el.image, el.x, el.y, el.time};
    const char *rows[3] = {"sprite", "offset", "time"};
    bool done = false;
    for (int r = 0; r < 3; r++) {
        ImGui::PushID(r);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * (r < 2 ? 8.0f : 4.0f));
        bool changed = r < 2 ? ImGui::DragInt2(rows[r], &v[r * 2], 0.2f) : ImGui::DragInt(rows[r], &v[4], 0.2f);
        if (changed) Air_SetElem(sf.text, el, v[0], v[1], v[2], v[3], v[4]);
        done |= ImGui::IsItemDeactivatedAfterEdit();
        if (r == 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Pick...")) ImGui::OpenPopup("pick");
        }
        ImGui::PopID();
    }
    if (done) apply(sf);
    ImGui::PushID(0);
    ImGui::SetItemTooltip("Pick the element's sprite among the sprites of a group");
    if (ImGui::BeginPopup("pick")) {
        static int group = 0;
        if (ImGui::IsWindowAppearing()) group = el.group;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
        ImGui::InputInt("Group", &group);
        SpriteFile *spf = screen ? sa.sprites : SpritesOf(t.pn);
        if (!spf) ImGui::TextDisabled("Waiting for the sprites...");
        float cell = ImGui::GetFontSize() * 5;
        int col = 0, cols = 8;
        for (int i = 0; spf && i < (int)spf->doc.sprites.size(); i++) {
            const SffSprite &s = spf->doc.sprites[i];
            if (s.group != group) continue;
            if (col++ % cols) ImGui::SameLine();
            ImGui::PushID(i);
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            bool picked = ImGui::InvisibleButton("s", ImVec2(cell, cell + ImGui::GetFontSize()));
            ImDrawList *draw = ImGui::GetWindowDrawList();
            bool cur = s.group == el.group && s.number == el.image;
            Checkerboard(draw, p0, ImVec2(p0.x + cell, p0.y + cell));
            if (GLuint tex = SpriteTexture(*spf, i)) {
                float k = std::min(cell / std::max(1, s.w), cell / std::max(1, s.h));
                ImVec2 a(p0.x + (cell - s.w * k) * 0.5f, p0.y + (cell - s.h * k) * 0.5f);
                draw->AddImage(TexRef(tex), a, ImVec2(a.x + s.w * k, a.y + s.h * k));
            }
            draw->AddRect(p0, ImVec2(p0.x + cell, p0.y + cell),
                          cur ? IM_COL32(255, 220, 60, 255) : ImGui::IsItemHovered() ? IM_COL32(255, 255, 255, 200) : IM_COL32(255, 255, 255, 40));
            char label[32];
            snprintf(label, sizeof(label), "%d, %d", s.group, s.number);
            draw->AddText(ImVec2(p0.x, p0.y + cell), IM_COL32(220, 220, 220, 255), label);
            if (picked && Air_SetElem(sf.text, el, s.group, s.number, el.x, el.y, el.time)) {
                apply(sf);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        if (col == 0 && spf) ImGui::TextDisabled("No sprites in group %d.", group);
        ImGui::EndPopup();
    }
    ImGui::PopID();
    ImGui::TextDisabled("line %d", el.line);
    ImGui::SameLine();
    if (ImGui::SmallButton("Show in Code")) {
        g_codeFile = sf.path;
        g_codeLine = g_codeScrollTo = el.line;
    }

    for (int g = 1; g >= 0 && !screen; g--) {
        ImGui::PushID(g);
        ImGui::SeparatorText(g == 1 ? "Clsn2 (can be hit)" : "Clsn1 (hits)");
        if (el.header[g] == 0)
            ImGui::TextDisabled("none");
        else if (el.own[g])
            ImGui::TextDisabled("its own, line %d", el.header[g]);
        else
            ImGui::TextDisabled("the action's default (line %d), shared by %d elements", el.header[g],
                                SharedBy(*t.act, g, el.header[g]));
        for (size_t i = 0; i < el.clsn[g].size(); i++) {
            const AirBox &b = el.clsn[g][i];
            ImGui::PushID((int)i);
            int r[4] = {b.x1, b.y1, b.x2, b.y2};
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
            if (ImGui::DragInt4("##box", r, 0.2f)) Air_SetBox(sf.text, b, r[0], r[1], r[2], r[3]);
            if (ImGui::IsItemDeactivatedAfterEdit()) apply(sf);
            ImGui::SameLine();
            bool del = ImGui::SmallButton("x");
            ImGui::SetItemTooltip("Delete this box (the element gets its own boxes)");
            ImGui::PopID();
            if (del) {
                DeleteBox(t, g, (int)i);
                ImGui::PopID();
                ImGui::End();
                return; // the element's lines changed
            }
        }
        if (ImGui::SmallButton("Add a box")) {
            AddBox(t, g);
            ImGui::PopID();
            ImGui::End();
            return;
        }
        ImGui::PopID();
    }
    ImGui::End();
}

} // namespace app
