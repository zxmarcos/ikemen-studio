// What the fields of a stage's or a screen's def name: beside each one, the sprite or action it shows
// (a thumbnail, which picks another) or the sound it plays; on Animation, the fields using an action.
#include "app_internal.h"

#include <cfloat>

namespace app {

// A sprite in a square, on the checkerboard.
static void Thumb(ImDrawList *draw, SpriteFile *spf, int index, ImVec2 a, float size) {
    Checkerboard(draw, a, ImVec2(a.x + size, a.y + size));
    GLuint tex = spf && index >= 0 ? SpriteTexture(*spf, index) : 0;
    if (!tex) return;
    const SffSprite &s = spf->doc.sprites[index];
    float k = std::min(size / std::max(1, s.w), size / std::max(1, s.h));
    ImVec2 p(a.x + (size - s.w * k) * 0.5f, a.y + (size - s.h * k) * 0.5f);
    draw->AddImage(TexRef(tex), p, ImVec2(p.x + s.w * k, p.y + s.h * k));
}

// The tooltip of a thumbnail: the sprite larger, under a line of text.
static void ThumbTip(SpriteFile *spf, int index, const std::string &text) {
    if (!ImGui::BeginItemTooltip()) return;
    ImGui::TextUnformatted(text.c_str());
    if (spf && index >= 0) {
        float size = ImGui::GetFontSize() * 8;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(size, size));
        Thumb(ImGui::GetWindowDrawList(), spf, index, p, size);
    }
    ImGui::EndTooltip();
}

static const int kGridColumns = 6;

void SpriteGridSize() {
    const ImGuiStyle &st = ImGui::GetStyle();
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSizeConstraints(ImVec2((em * 5 + st.ItemSpacing.x) * kGridColumns + st.WindowPadding.x * 2 + st.ScrollbarSize, 0), ImVec2(FLT_MAX, em * 28));
}

bool SpriteGrid(SpriteFile *spf, int &group, int curGroup, int curImage, int *pickGroup, int *pickImage) {
    float em = ImGui::GetFontSize(), cell = em * 5;
    ImGui::SetNextItemWidth(em * 6);
    ImGui::InputInt("Group", &group, 0);
    if (!spf) ImGui::TextDisabled("Waiting for the sprites...");
    int col = 0;
    bool picked = false;
    for (int i = 0; spf && i < (int)spf->doc.sprites.size(); i++) {
        const SffSprite &s = spf->doc.sprites[i];
        if (s.group != group) continue;
        if (col++ % kGridColumns) ImGui::SameLine();
        ImGui::PushID(i);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("s", ImVec2(cell, cell + em))) *pickGroup = s.group, *pickImage = s.number, picked = true;
        ImDrawList *draw = ImGui::GetWindowDrawList();
        bool cur = s.group == curGroup && s.number == curImage;
        Thumb(draw, spf, i, p0, cell);
        draw->AddRect(p0, ImVec2(p0.x + cell, p0.y + cell),
                      cur ? ImGui::GetColorU32(Ink(kYellow)) : ImGui::GetColorU32(ImGuiCol_Text, ImGui::IsItemHovered() ? 0.8f : 0.16f));
        char label[32];
        snprintf(label, sizeof(label), "%d, %d", s.group, s.number);
        draw->AddText(ImVec2(p0.x, p0.y + cell), ImGui::GetColorU32(ImGuiCol_Text), label);
        ImGui::PopID();
    }
    if (col == 0 && spf) ImGui::TextDisabled("No sprites in group %d.", group);
    return picked;
}

// A click on a sprite of the group shown: it alone; with Ctrl, one more or one less; with Shift, those of
// its group from the last one clicked to it, whatever of the other groups was chosen staying.
static void ChooseSprite(SpritePicks &p, const SffDoc &d, const std::vector<int> &list, int i) {
    ImGuiIO &io = ImGui::GetIO();
    std::pair<int, int> one(d.sprites[i].group, d.sprites[i].number);
    auto &chosen = p.sprites;
    auto place = [&](const std::pair<int, int> &s) {
        for (size_t k = 0; k < list.size(); k++)
            if (d.sprites[list[k]].number == s.second) return (int)k;
        return -1;
    };
    if (io.KeyShift && p.from.first == one.first && place(p.from) >= 0) {
        int a = place(p.from), b = place(one);
        chosen.erase(std::remove_if(chosen.begin(), chosen.end(), [&](const std::pair<int, int> &s) { return s.first == one.first; }), chosen.end());
        for (int k = std::min(a, b); k <= std::max(a, b); k++) chosen.push_back({one.first, d.sprites[list[k]].number});
        return;
    }
    p.from = one;
    if (!io.KeyCtrl) {
        chosen = {one};
        return;
    }
    auto at = std::find(chosen.begin(), chosen.end(), one);
    if (at != chosen.end()) chosen.erase(at);
    else chosen.push_back(one);
}

bool SpritePicker(SpriteFile *spf, SpritePicks &p, float height) {
    float em = ImGui::GetFontSize(), cell = em * 5, listW = em * 9, rowH = em * 2.4f;
    const ImGuiStyle &st = ImGui::GetStyle();
    float lists = height - ImGui::GetFrameHeightWithSpacing();
    // The groups, each one's sprites in the order of their numbers
    std::map<int, std::vector<int>> groups;
    for (int i = 0; spf && i < (int)spf->doc.sprites.size(); i++) groups[spf->doc.sprites[i].group].push_back(i);
    for (auto &kv : groups)
        std::sort(kv.second.begin(), kv.second.end(), [&](int a, int b) { return spf->doc.sprites[a].number < spf->doc.sprites[b].number; });
    if (!groups.empty() && !groups.count(p.group)) {
        auto next = groups.lower_bound(p.group);
        p.group = next == groups.end() ? groups.begin()->first : next->first;
    }
    auto allOf = [&](int g) {
        p.sprites.erase(std::remove_if(p.sprites.begin(), p.sprites.end(), [&](const std::pair<int, int> &s) { return s.first == g; }), p.sprites.end());
        for (int i : groups[g]) p.sprites.push_back({g, spf->doc.sprites[i].number});
        p.from = {-1, -1};
    };

    static char filter[16];
    if (ImGui::IsWindowAppearing()) filter[0] = 0;
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(listW);
    ImGui::InputTextWithHint("##filter", "Group...", filter, sizeof(filter));
    bool typed = ImGui::IsItemEdited();
    std::vector<int> shown;
    for (const auto &kv : groups)
        if (std::to_string(kv.first).rfind(filter, 0) == 0) shown.push_back(kv.first);
    if (typed && !shown.empty()) p.group = shown[0];
    if (ImGui::BeginChild("##groups", ImVec2(listW, lists - ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders)) {
        ImDrawList *draw = ImGui::GetWindowDrawList();
        ImGuiListClipper clip;
        clip.Begin((int)shown.size(), rowH);
        for (int i = 0; i < (int)shown.size(); i++)
            if (shown[i] == p.group) clip.IncludeItemByIndex(i); // so the scroll below finds it
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
                int g = shown[i];
                const std::vector<int> &list = groups[g];
                ImGui::PushID(g);
                ImVec2 a = ImGui::GetCursorScreenPos();
                float h = rowH - st.ItemSpacing.y;
                if (ImGui::Selectable("##g", g == p.group, 0, ImVec2(0, h))) p.group = g;
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) allOf(g);
                ImGui::SetItemTooltip("Group %d: %d sprites. A double click chooses them all", g, (int)list.size());
                if (g == p.group && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
                Thumb(draw, spf, list[0], a, h);
                int taken = (int)std::count_if(p.sprites.begin(), p.sprites.end(), [&](const std::pair<int, int> &s) { return s.first == g; });
                char count[24];
                if (taken) snprintf(count, sizeof(count), "%d of %d", taken, (int)list.size());
                else snprintf(count, sizeof(count), "%d", (int)list.size());
                float x = a.x + h + em * 0.4f, line = ImGui::GetTextLineHeight();
                draw->AddText(ImVec2(x, a.y + h * 0.5f - line), ImGui::GetColorU32(ImGuiCol_Text), std::to_string(g).c_str());
                draw->AddText(ImVec2(x, a.y + h * 0.5f), ImGui::GetColorU32(taken ? ImGuiCol_CheckMark : ImGuiCol_TextDisabled), count);
                ImGui::PopID();
            }
        clip.End();
        if (shown.empty()) ImGui::TextDisabled(groups.empty() ? "No sprites." : "No such group.");
    }
    ImGui::EndChild();
    ImGui::EndGroup();

    ImGui::SameLine();
    bool twice = false;
    if (ImGui::BeginChild("##sprites", ImVec2(0, lists), ImGuiChildFlags_Borders) && groups.count(p.group)) {
        const std::vector<int> &list = groups[p.group];
        ImDrawList *draw = ImGui::GetWindowDrawList();
        ImGuiIO &io = ImGui::GetIO();
        int cols = std::max(1, (int)((ImGui::GetContentRegionAvail().x + st.ItemSpacing.x) / (cell + st.ItemSpacing.x)));
        ImGuiListClipper clip;
        clip.Begin(((int)list.size() + cols - 1) / cols, cell + em + st.ItemSpacing.y);
        while (clip.Step())
            for (int row = clip.DisplayStart; row < clip.DisplayEnd; row++)
                for (int col = 0; col < cols; col++) {
                    int k = row * cols + col;
                    if (k >= (int)list.size()) break;
                    int i = list[k];
                    const SffSprite &s = spf->doc.sprites[i];
                    if (col) ImGui::SameLine();
                    ImGui::PushID(i);
                    ImVec2 a = ImGui::GetCursorScreenPos();
                    if (ImGui::InvisibleButton("s", ImVec2(cell, cell + em))) ChooseSprite(p, spf->doc, list, i);
                    bool hot = ImGui::IsItemHovered();
                    if (hot && !io.KeyCtrl && !io.KeyShift && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) twice = true;
                    Thumb(draw, spf, i, a, cell);
                    auto at = std::find(p.sprites.begin(), p.sprites.end(), std::make_pair(s.group, s.number));
                    bool taken = at != p.sprites.end();
                    ImU32 mark = ImGui::GetColorU32(ImGuiCol_CheckMark);
                    draw->AddRect(a, ImVec2(a.x + cell, a.y + cell), taken ? mark : ImGui::GetColorU32(ImGuiCol_Text, hot ? 0.8f : 0.16f), 0.0f, taken ? 2.0f : 1.0f);
                    if (taken) {
                        // Its place among those chosen: the element it makes
                        std::string place = std::to_string(at - p.sprites.begin() + 1);
                        ImVec2 size = ImGui::CalcTextSize(place.c_str());
                        draw->AddRectFilled(a, ImVec2(a.x + size.x + em * 0.5f, a.y + size.y + em * 0.1f), mark);
                        draw->AddText(ImVec2(a.x + em * 0.25f, a.y), IM_COL32(20, 20, 24, 255), place.c_str());
                    }
                    char label[32];
                    snprintf(label, sizeof(label), "%d, %d", s.group, s.number);
                    draw->AddText(ImVec2(a.x, a.y + cell), ImGui::GetColorU32(ImGuiCol_Text), label);
                    ThumbTip(spf, i, std::string(label) + "  " + std::to_string(s.w) + "x" + std::to_string(s.h));
                    ImGui::PopID();
                }
        clip.End();
    }
    ImGui::EndChild();

    bool has = groups.count(p.group) != 0;
    char all[40];
    snprintf(all, sizeof(all), "All of group %d", has ? p.group : 0);
    ImGui::BeginDisabled(!has);
    if (ImGui::Button(all) && has) allOf(p.group);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Those chosen in the other groups stay. Ctrl+A does it too.");
    if (has && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false) && !ImGui::IsAnyItemActive()) allOf(p.group);
    ImGui::SameLine();
    ImGui::BeginDisabled(p.sprites.empty());
    if (ImGui::Button(ICON_XMARK " Clear")) p.sprites.clear();
    ImGui::EndDisabled();
    ImGui::SameLine(0, em);
    int others = (int)std::count_if(p.sprites.begin(), p.sprites.end(), [&](const std::pair<int, int> &s) { return s.first != p.group; });
    if (others) ImGui::TextDisabled("%d chosen, %d of them in other groups", (int)p.sprites.size(), others);
    else ImGui::TextDisabled("Ctrl: one more. Shift: from one to another. Double click: that one alone.");
    return twice;
}

// A red frame around the thumbnail just drawn: what it names is not there.
static void MissingFrame(ImVec2 p, float h) {
    ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + h, p.y + h), ImGui::GetColorU32(Ink(kRed)), 0.0f, 2.0f);
}

static bool ActionCell(SourceFile &sf, int no, SpriteFile *spf, std::string *value) {
    const AirFile &air = AirOf(sf);
    auto it = air.actions.find(no);
    bool has = it != air.actions.end(), filled = has && !it->second.elems.empty();
    int index = filled && spf ? Sff_FindSprite(spf->doc, it->second.elems[0].group, it->second.elems[0].image) : -1;
    float h = ImGui::GetFrameHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("ref", ImVec2(h, h))) ImGui::OpenPopup("ref");
    Thumb(ImGui::GetWindowDrawList(), spf, index, p, h);
    bool missing = no >= 0 && !has;
    if (missing) MissingFrame(p, h);
    std::string tip = missing ? "There is no [Begin Action " + std::to_string(no) + "] in this file"
                    : no < 0  ? std::string("No action")
                    : "Action " + std::to_string(no) + (has && !it->second.name.empty() ? "  " + it->second.name : "") + ": " +
                          std::to_string(has ? it->second.elems.size() : 0) + " elements";
    ThumbTip(spf, index, tip + "\nClick: open it, or choose another");
    bool chosen = false;
    if (ImGui::BeginPopup("ref")) {
        ImGui::BeginDisabled(!has);
        if (ImGui::Selectable(ICON_ANIM " Open in Animation")) AnimOpenFile(sf.path, no);
        ImGui::EndDisabled();
        ImGui::SeparatorText("Another action");
        float rows = std::min<float>(air.actions.size(), 12) * ImGui::GetTextLineHeightWithSpacing() + 2;
        if (ImGui::BeginChild("##list", ImVec2(ImGui::GetFontSize() * 16, rows))) {
            for (const auto &[n, a] : air.actions) {
                std::string row = std::to_string(n) + "  " + a.name + "##" + std::to_string(n);
                if (ImGui::Selectable(row.c_str(), n == no)) *value = std::to_string(n), chosen = true;
                if (n == no && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
            }
        }
        ImGui::EndChild();
        // Made here: the text only grows at its end, the field's line stays good
        if (ImGui::Button(ICON_PLUS " A new animation...")) ImGui::OpenPopup("refnew");
        if (ImGui::BeginPopup("refnew")) {
            NewAnim a;
            if (AnimChooser(sf, spf, &a, false)) {
                bool others = spf && spf->dirty;
                int made = MakeAnim(sf, spf, a, "");
                if (made >= 0) {
                    *value = std::to_string(made);
                    chosen = true;
                    if (!a.files.empty()) SaveAddedSprites(*spf, others);
                    AnimOpenFile(sf.path, made, 1, false);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (chosen) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return chosen;
}

static bool SpriteCell(int group, int image, SpriteFile *spf, const std::string &what, std::string *value) {
    int index = spf && group >= 0 ? Sff_FindSprite(spf->doc, group, image) : -1;
    float h = ImGui::GetFrameHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("ref", ImVec2(h, h))) ImGui::OpenPopup("ref");
    Thumb(ImGui::GetWindowDrawList(), spf, index, p, h);
    // 9000 is a character's portrait: the game takes it from the character, not from this SFF
    bool portrait = group >= 9000;
    bool missing = spf && group >= 0 && index < 0 && !portrait;
    if (missing) MissingFrame(p, h);
    char tip[96];
    if (portrait && index < 0) snprintf(tip, sizeof(tip), "Sprite %d, %d: the character's", group, image);
    else if (missing) snprintf(tip, sizeof(tip), "Sprite %d, %d is not in the SFF", group, image);
    else snprintf(tip, sizeof(tip), group < 0 ? "No sprite" : "Sprite %d, %d", group, image);
    ThumbTip(spf, index, std::string(tip) + "\nClick: show it, or choose another");
    bool chosen = false;
    SpriteGridSize();
    if (ImGui::BeginPopup("ref")) {
        static int shownGroup;
        if (ImGui::IsWindowAppearing()) shownGroup = std::max(group, 0);
        ImGui::BeginDisabled(index < 0);
        if (ImGui::Selectable(ICON_IMAGE " Show in Sprites") && spf) {
            ShowInSprites(*spf, index, what);
            ShowWindow("Sprites");
        }
        ImGui::EndDisabled();
        int g, n;
        if (SpriteGrid(spf, shownGroup, group, image, &g, &n)) {
            *value = std::to_string(g) + ", " + std::to_string(n);
            chosen = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return chosen;
}

static void SoundCell(int group, int number, const std::string &path, const std::string &what) {
    const SndFile *snd = path.empty() || group < 0 ? nullptr : SndAt(path);
    int index = snd ? Snd_Find(*snd, group, number) : -1;
    ImGui::BeginDisabled(index < 0);
    if (IconButton(ICON_PLAY)) PlayWav(snd->sounds[index].wav);
    ImGui::SetItemTooltip("Play it");
    ImGui::SameLine(0, 2);
    if (IconButton(ICON_SOUND)) ShowInSounds(path, index, what);
    ImGui::SetItemTooltip("Show it in Sounds");
    ImGui::EndDisabled();
    if (snd && index < 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Sound %d, %d is not in %s", group, number, path.c_str());
}

bool FieldRefCell(SourceFile &sf, const IniSection &s, const IniField &f, std::string *value) {
    IniRefKind kind = Ini_FieldRef(s, f);
    if (kind == kIniRefNone) return false;
    int v[2] = {-1, 0};
    sscanf(f.value.c_str(), "%d , %d", &v[0], &v[1]);
    std::string what = ScreenFileName(sf.path);
    if (what.empty()) what = "file " + sf.path.substr(sf.path.find_last_of("/\\") + 1);
    if (kind == kIniRefSound) {
        SoundCell(v[0], v[1], DefFile(sf.path, "snd"), what);
        return false;
    }
    std::string sff = SpritesFileOf(sf.path);
    SpriteFile *spf = sff.empty() ? nullptr : &SpriteFileAt(sff);
    return kind == kIniRefAction ? ActionCell(sf, v[0], spf, value) : SpriteCell(v[0], v[1], spf, what, value);
}

// The def's text parsed, again when it changes.
static const IniFile &IniOf(const SourceFile &sf) {
    struct Parsed {
        std::string text;
        IniFile ini;
        bool done = false;
    };
    static std::map<std::string, Parsed> cache;
    Parsed &c = cache[NormPath(sf.path)];
    if (!c.done || c.text != sf.text) {
        c.text = sf.text;
        c.ini = IniFile();
        Ini_Parse(c.text, c.ini);
        c.done = true;
    }
    return c.ini;
}

void ActionUsersNote(SourceFile &sf, int action) {
    const IniFile &ini = IniOf(sf);
    std::vector<std::pair<int, int>> users = Ini_ActionUsers(ini, action);
    ImGui::SameLine();
    if (users.empty()) {
        ImGui::TextColored(Ink(kYellow), "unused");
        ImGui::SetItemTooltip("No field of this def names this action. The scripts of a screenpack can show others.");
        return;
    }
    ImGui::TextDisabled("used by %zu", users.size());
    if (ImGui::BeginItemTooltip()) {
        for (const auto &[si, fi] : users) {
            const IniSection &s = ini.sections[si];
            ImGui::Text("[%s]  %s", s.name.c_str(), s.fields[fi].key.c_str());
        }
        ImGui::TextDisabled("Click: the first one in Code. Deleted or renumbered, they stop finding it.");
        ImGui::EndTooltip();
    }
    if (ImGui::IsItemClicked()) CodeShowLine(sf.path, ini.sections[users[0].first].fields[users[0].second].line);
}

} // namespace app
