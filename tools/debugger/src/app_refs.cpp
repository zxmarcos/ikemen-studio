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
    ImGui::InputInt("Group", &group);
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
        if (ImGui::SmallButton(ICON_PLUS " A new animation...")) ImGui::OpenPopup("refnew");
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
    if (ImGui::SmallButton(ICON_PLAY)) PlayWav(snd->sounds[index].wav);
    ImGui::SameLine(0, 2);
    if (ImGui::SmallButton(ICON_SOUND)) ShowInSounds(path, index, what);
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
