// New layers and animations, for a stage's def and a screen's: the buttons adding, duplicating, moving
// and deleting the BGs of a group, and an animation made where one is asked for (a new BG, a storyboard's
// layer, a field naming an action), from images or from the sprites of a group.
#include "app_internal.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstring>

#include "imgui_stdlib.h"
#include "ini.h"

namespace app {

void BgKeyCell(const IniSection &s, const IniField &f) {
    ImGui::AlignTextToFramePadding();
    if (Ini_BgKeyRead(s, f.key)) {
        ImGui::TextUnformatted(f.key.c_str());
        ImGui::SetItemTooltip("%s", f.key.c_str());
    } else {
        ImGui::TextDisabled("%s", f.key.c_str());
        ImGui::SetItemTooltip("%s: the engine does not read it for this layer, as its type or its other fields are.", f.key.c_str());
    }
}

// A list as wide as its names, whatever the width of its field
static bool ChoiceCombo(const char *id, const std::string &key, const std::string &now, std::string *value, ImGuiComboFlags flags) {
    float width = 0;
    const std::vector<const char *> &choices = *Ini_BgChoices(key);
    for (const char *c : choices) width = std::max(width, ImGui::CalcTextSize(c).x);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width + ImGui::GetFontSize() * 2, 0), ImVec2(FLT_MAX, FLT_MAX));
    bool picked = false;
    if (ImGui::BeginCombo(id, now.c_str(), flags)) {
        for (const char *c : choices)
            if (ImGui::Selectable(c, _stricmp(c, now.c_str()) == 0)) *value = c, picked = true;
        ImGui::EndCombo();
    }
    return picked;
}

bool BgChoiceCell(const IniField &f, std::string *value) {
    if (!Ini_BgChoices(f.key)) return false;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ChoiceCombo("##v", f.key, f.value, value, ImGuiComboFlags_NoArrowButton); // the column is narrow
    return true;
}

bool BgTypeLine(SourceFile &sf, const IniSection &s) {
    const IniField *f = s.Field("type");
    std::string now = f ? f->value : "", picked;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("type");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool changed = ChoiceCombo("##type", "type", now.empty() ? "none: not drawn" : now, &picked, 0);
    ImGui::SetItemTooltip("normal: a sprite. anim: an action. parallax: a sprite whose bottom can be wider than its top, for floors.\n"
                          "video: a video file. dummy: draws nothing, for what follows it with positionlink.\n"
                          "What the type needs shows in the fields below; what it does not read is dimmed.");
    return changed && Ini_SetField(sf.text, s, "type", picked);
}

bool BgMissingRows(SourceFile &sf, const IniSection &s) {
    static std::map<std::string, std::string> typed; // by key, until it is written
    bool changed = false;
    for (const IniBgKey &k : Ini_BgMissingKeys(s)) {
        ImGui::PushID(k.key);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (k.needed) ImGui::TextColored(Ink(kYellow), "%s", k.key);
        else ImGui::TextDisabled("%s", k.key);
        ImGui::SetItemTooltip(k.needed ? "%s: this type shows nothing without it" : "%s: this type can use it", k.key);
        ImGui::TableNextColumn();
        IniField ghost;
        ghost.key = k.key;
        std::string picked;
        if (!BgChoiceCell(ghost, &picked)) {
            std::string &value = typed[k.key];
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##v", k.hint, &value);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                picked = value;
                value.clear();
            }
        } else {
            ImGui::SetItemTooltip("%s", k.hint);
        }
        ImGui::TableNextColumn();
        std::string chosen;
        if (FieldRefCell(sf, s, ghost, &chosen)) picked = chosen;
        ImGui::TableNextColumn();
        if (!picked.empty() && !changed) changed = Ini_SetField(sf.text, s, k.key, picked);
        ImGui::PopID();
    }
    return changed;
}

// What a new BG shows: a sprite, as Create > A stage writes them, or an action.
static std::string BgOfSprite(int group, int number, int layer, float delta = 1) {
    return "type = normal\nspriteno = " + std::to_string(group) + ", " + std::to_string(number) + "\nlayerno = " +
           std::to_string(layer) + "\nstart = 0, 0\ndelta = " + Ini_Floats(&delta, 1) + ", 1\nmask = 1";
}

static std::string BgOfAction(int action, int layer) {
    return "type = anim\nactionno = " + std::to_string(action) + "\nlayerno = " + std::to_string(layer) +
           "\nstart = 0, 0\ndelta = 1, 1";
}

static std::string FileStem(const std::string &path) {
    std::string name = path.substr(path.find_last_of("/\\") + 1);
    return name.substr(0, name.find_last_of('.'));
}

// frame9 before frame10
static std::vector<std::string> InOrder(std::vector<std::string> files) {
    std::sort(files.begin(), files.end(), [](const std::string &a, const std::string &b) { return a.size() != b.size() ? a.size() < b.size() : a < b; });
    return files;
}

int MakeAnim(SourceFile &sf, SpriteFile *spf, const NewAnim &a, const std::string &name) {
    if (a.action >= 0) return a.action;
    std::vector<std::pair<int, int>> frames = a.sprites;
    if (!a.files.empty() && (!spf || !AddSpritesOfImages(*spf, InOrder(a.files), frames))) return -1;
    if (frames.empty()) return -1;
    const AirFile &air = AirOf(sf);
    int no = 0;
    while (air.actions.count(no)) no++;
    std::vector<AirNewElem> elems;
    for (const auto &f : frames) {
        AirNewElem e;
        e.group = f.first, e.image = f.second, e.time = a.time;
        elems.push_back(e);
    }
    Air_NewAction(sf.text, no, name, elems);
    return no;
}

void SaveAddedSprites(SpriteFile &sprites, bool others) {
    if (others) Log(LogLine::Info, "The new sprites are not on disk yet: save the Sprites window to see them in the game.");
    else SaveSprites(sprites);
}

bool AnimChooser(SourceFile &sf, SpriteFile *spf, NewAnim *out, bool listActions) {
    float em = ImGui::GetFontSize();
    static int ticks = 5, group;
    static SpriteFile *seen;
    if (seen != spf) {
        seen = spf;
        group = INT_MAX;
        for (size_t i = 0; spf && i < spf->doc.sprites.size(); i++) group = std::min(group, spf->doc.sprites[i].group);
        if (group == INT_MAX) group = 0;
    }
    bool chosen = false;
    ImGui::SetNextItemWidth(em * 6);
    ImGui::InputInt("Ticks per frame", &ticks, 0);
    ticks = std::max(ticks, 1);
    ImGui::SetItemTooltip("60 ticks are a second. Each frame can be changed afterwards in Animation.");
    out->time = ticks;
    ImGui::BeginDisabled(!spf);
    if (ImGui::Button(ICON_FOLDER_OPEN " From images...")) {
        out->files = PickFiles(kImageFilter, true);
        chosen = !out->files.empty();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", spf ? ("Choose several: a frame each, in the order of their names. They go in " + spf->path + ", saved at once: the game needs them there.").c_str()
                                    : "The def names no SFF.");
    ImGui::SetNextItemWidth(em * 6);
    ImGui::InputInt("Group##frames", &group, 0);
    std::vector<std::pair<int, int>> frames;
    for (size_t i = 0; spf && i < spf->doc.sprites.size(); i++)
        if (spf->doc.sprites[i].group == group) frames.push_back({group, spf->doc.sprites[i].number});
    std::sort(frames.begin(), frames.end());
    char label[64];
    snprintf(label, sizeof(label), ICON_IMAGE " From its %zu sprites", frames.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(frames.empty());
    if (ImGui::Button(label)) {
        out->sprites = frames;
        chosen = true;
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("A new action of this def, a frame for each sprite of the group, in the order of their numbers.");
    const AirFile &air = AirOf(sf);
    if (listActions && !air.actions.empty()) {
        ImGui::SeparatorText("Or one the def has");
        float rows = std::min<float>(air.actions.size(), 12) * ImGui::GetTextLineHeightWithSpacing() + 2;
        if (ImGui::BeginChild("##actions", ImVec2(em * 22, rows))) {
            for (const auto &[no, a] : air.actions) {
                std::string row = std::to_string(no) + "  " + a.name + "##" + std::to_string(no);
                if (ImGui::Selectable(row.c_str())) out->action = no, chosen = true;
            }
        }
        ImGui::EndChild();
    }
    return chosen;
}

// A new section, after that BG: its index.
static int AddBg(SourceFile &sf, const BgHost &h, int after, const std::string &name, const std::string &body) {
    const IniFile &ini = h.parsed(sf);
    return Ini_AddBg(sf.text, ini, after, Ini_NewBgName(ini, name, h.group), body, h.group);
}

// The text names what was added: the game shows it, through the SFF saved when images went in it.
static void ShowAdded(SourceFile &sf, SpriteFile *spf, bool images, bool others, const BgHost &h) {
    if (!images) {
        h.edited(sf);
        return;
    }
    bool reloads = !others && h.showsSff(spf->path);
    SaveAddedSprites(*spf, others); // the game reads the text too, then
    if (reloads) sf.version++;
    else h.edited(sf);
}

// Add layer: what it shows, and whether it goes behind what the screen draws or in front of it.
static void AddBgPopup(SourceFile &sf, const BgHost &h, BgEdit &edit) {
    if (!ImGui::IsPopupOpen("addbg")) return;
    std::string sff = SpritesFileOf(sf.path);
    SpriteFile *spf = sff.empty() ? nullptr : &SpriteFileAt(sff);
    SpriteGridSize();
    if (!ImGui::BeginPopup("addbg")) return;
    float em = ImGui::GetFontSize();
    static std::string name;
    static int front, group;
    if (ImGui::IsWindowAppearing()) {
        name.clear();
        group = INT_MAX;
        for (size_t i = 0; spf && i < spf->doc.sprites.size(); i++) group = std::min(group, spf->doc.sprites[i].group);
        if (group == INT_MAX) group = 0;
    }
    ImGui::SetNextItemWidth(em * 12);
    ImGui::InputTextWithHint("Name", "Layer", &name);
    ImGui::SetNextItemWidth(em * 12);
    ImGui::Combo("Drawn", &front, h.layers);
    ImGui::SetItemTooltip("Its layerno. Among those on the same side, the list's order decides: lower in the list is in front.");
    std::string chosen = name;
    int layer = front, after = h.selected;
    BgHost host = h;
    if (ImGui::BeginTabBar("shows")) {
        if (ImGui::BeginTabItem("A sprite")) {
            ImGui::BeginDisabled(!spf);
            if (ImGui::Button(ICON_FOLDER_OPEN " From an image...")) {
                std::vector<std::string> files = PickFiles(kImageFilter, false);
                if (!files.empty())
                    edit = [=](SourceFile &s) {
                        SpriteFile &sprites = SpriteFileAt(sff);
                        bool others = sprites.dirty;
                        std::vector<std::pair<int, int>> made;
                        if (!AddSpritesOfImages(sprites, files, made)) return after;
                        int bg = AddBg(s, host, after, chosen.empty() ? FileStem(files[0]) : chosen, BgOfSprite(made[0].first, made[0].second, layer));
                        ShowAdded(s, &sprites, true, others, host);
                        return bg;
                    };
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", spf ? ("Becomes a sprite of " + sff + ", saved at once: the game needs it there.").c_str() : "The def names no SFF.");
            int g, n;
            if (SpriteGrid(spf, group, -1, -1, &g, &n)) {
                edit = [=](SourceFile &s) {
                    int bg = AddBg(s, host, after, chosen, BgOfSprite(g, n, layer));
                    host.edited(s);
                    return bg;
                };
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("An animation")) {
            NewAnim a;
            if (AnimChooser(sf, spf, &a, true)) {
                edit = [=](SourceFile &s) {
                    SpriteFile *sprites = sff.empty() ? nullptr : &SpriteFileAt(sff);
                    bool others = sprites && sprites->dirty;
                    std::string named = chosen.empty() && !a.files.empty() ? FileStem(InOrder(a.files)[0]) : chosen;
                    int no = MakeAnim(s, sprites, a, named);
                    if (no < 0) return after;
                    int bg = AddBg(s, host, after, named, BgOfAction(no, layer));
                    ShowAdded(s, sprites, !a.files.empty(), others, host);
                    AnimOpenFile(s.path, no, 1, false);
                    return bg;
                };
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndTabItem();
        }
        // A picture exported layer by layer: the parallax comes from their order. Only a stage's camera moves.
        if (h.group == "BG" && ImGui::BeginTabItem("Several images")) {
            static float furthest = 0.2f, nearest = 1;
            ImGui::SetNextItemWidth(em * 6);
            ImGui::DragFloat("Delta of the furthest", &furthest, 0.01f, 0, 2, "%.2f");
            ImGui::SetNextItemWidth(em * 6);
            ImGui::DragFloat("Delta of the nearest", &nearest, 0.01f, 0, 2, "%.2f");
            ImGui::SetItemTooltip("The layers between them get even steps: 1 scrolls with the fighters.");
            ImGui::BeginDisabled(!spf);
            if (ImGui::Button(ICON_FOLDER_OPEN " Choose the images...")) {
                std::vector<std::string> picked = PickFiles(kImageFilter, true);
                if (!picked.empty()) {
                    std::vector<std::string> files = InOrder(picked);
                    float from = furthest, to = nearest;
                    edit = [=](SourceFile &s) {
                        SpriteFile &sprites = SpriteFileAt(sff);
                        bool others = sprites.dirty;
                        std::vector<std::pair<int, int>> made;
                        if (!AddSpritesOfImages(sprites, files, made)) return after;
                        int at = after, last = (int)made.size() - 1;
                        for (int n = 0; n <= last; n++) {
                            float delta = last ? from + (to - from) * n / last : to;
                            delta = std::round(delta * 1000) / 1000;
                            at = AddBg(s, host, at, FileStem(files[n]), BgOfSprite(made[n].first, made[n].second, layer, delta));
                        }
                        ShowAdded(s, &sprites, true, others, host);
                        return at;
                    };
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", spf ? ("A layer for each, the first of their names the furthest back. They go in " + sff + ", saved at once.").c_str() : "The def names no SFF.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndPopup();
}

void BgToolbar(SourceFile &sf, const IniFile &ini, const BgHost &h, BgEdit &edit) {
    ImGui::BeginDisabled(h.group.empty());
    if (ImGui::Button(ICON_PLUS " Add layer")) ImGui::OpenPopup("addbg");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (h.group.empty()) ImGui::SetTooltip("Pick a screen's BG, or its [...BGdef] section: the new one goes among them.");
        else ImGui::SetTooltip("A new [%s ...], after the selected one (without one, after the last): a sprite, an image or an animation.", h.group.c_str());
    }
    AddBgPopup(sf, h, edit);
    int bg = h.selected;
    BgHost host = h;
    ImGui::SameLine();
    ImGui::BeginDisabled(bg < 0);
    if (IconButton(ICON_COPY "##dup"))
        edit = [bg, host](SourceFile &s) {
            int copy = Ini_DuplicateBg(s.text, host.parsed(s), bg, host.group);
            host.edited(s);
            return copy;
        };
    ImGui::SetItemTooltip("Duplicate: the same section, under a name of its own, in front of it. Its id goes along,\n"
                          "so the BGCtrls moving it move the copy too.");
    auto moveBg = [&](const char *label, int dir, const char *tip) {
        ImGui::SameLine();
        ImGui::BeginDisabled(!Ini_CanMoveBg(ini, bg, dir, h.group));
        if (IconButton(label))
            edit = [bg, dir, host](SourceFile &s) {
                int at = Ini_MoveBg(s.text, host.parsed(s), bg, dir, host.group);
                host.edited(s);
                return at;
            };
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tip);
    };
    moveBg(ICON_ARROW_UP "##back", -1, "Up the list: drawn earlier, behind the BGs it passes (on the same layerno).\n"
                                       "The BGs with positionlink after it go along.");
    moveBg(ICON_ARROW_DOWN "##front", 1, "Down the list: drawn later, in front of the BGs it passes (on the same layerno).\n"
                                         "The BGs with positionlink after it go along.");
    ImGui::SameLine();
    if (IconButton(ICON_TRASH "##del"))
        edit = [bg, host](SourceFile &s) {
            int left = Ini_DeleteBg(s.text, host.parsed(s), bg, host.group) ? (int)host.parsed(s).BgsOf(host.group).size() : 0;
            host.edited(s);
            return std::min(bg, left - 1);
        };
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        int following = Ini_BgFollowers(ini, bg, h.group);
        if (following) ImGui::SetTooltip("Delete. %d BGs with positionlink after it will follow the BG before it, and move.", following);
        else ImGui::SetTooltip("Delete it. Undo brings it back.");
    }
}

} // namespace app
