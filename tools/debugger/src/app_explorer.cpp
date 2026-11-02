// Explorer: the game's characters, stages and screenpack as a tree of their files, read from
// disk (the files each def names), the ones in the match marked; a file opens in the window
// that edits it (Code, Sprites, Sounds). Also the list of those files for Go to file (Ctrl+P).
#include "app_internal.h"

#include <shellapi.h>

#include <filesystem>
#include <fstream>

#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "ini.h"
#include "pack.h"
#include "story.h"

namespace app {

namespace {
std::string BaseName(const std::string &path) { return path.substr(path.find_last_of("\\/") + 1); }
} // namespace

bool GameFileExists(const std::string &path) {
    DWORD a = GetFileAttributesA(GamePath(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

namespace {

std::string DirOf(const std::string &path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? "" : path.substr(0, slash + 1);
}

std::string ExtOf(const std::string &path) {
    size_t dot = path.find_last_of('.'), slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    std::string ext = path.substr(dot + 1);
    for (char &ch : ext) ch = (char)tolower((unsigned char)ch);
    return ext;
}

// Files no window edits as text
bool IsBinary(const std::string &path) {
    static const char *const kBinary[] = {"sff", "snd", "act",  "pal", "png",  "bmp",  "pcx", "jpg", "gif",
                                          "mp3", "ogg", "wav",  "mid", "midi", "xm",   "mod", "s3m", "it",
                                          "flac", "fnt", "ttf", "otf", "glb",  "gltf", "webm", "mp4", "zip"};
    std::string ext = ExtOf(path);
    for (const char *b : kBinary)
        if (ext == b) return true;
    return false;
}

// ---------------------------------------------------------------------------
// The files a def names: a character's or motif's [Files], a stage's sprites and music, a
// storyboard's; found as the engine looks for them (next to the def, then the game's folder,
// data/, font/, sound/)
// ---------------------------------------------------------------------------

struct DefEntry {
    std::string key;  // as in the def ("cns", "st0", "sprite", "spr", "fight"...)
    std::string path; // as the game names it
    bool exists;
};

struct DefCache {
    bool read = false;
    std::string text;
    FILETIME mtime = {};
    double checked = -100;
    std::vector<DefEntry> files;
};
std::map<std::string, DefCache> g_defs;

std::string Unquote(std::string s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

bool NamesFile(const std::string &section, const std::string &key) {
    auto ends = [&](const char *tail) {
        size_t n = strlen(tail);
        return key.size() >= n && key.compare(key.size() - n, n, tail) == 0;
    };
    if (section == "files" || section == "shaders") return true;
    // A stage's 3D model is [BGdef] model, its environment map [Model] environment
    if (section == "bgdef" || section == "scenedef") return key == "spr" || key == "snd" || key == "model";
    if (section == "music") return key == "bgmusic" || ends(".bgm");
    if (section == "model") return key == "environment";
    if (section == "def") return key == "file";                     // a font's
    if (section == "info") return key.compare(0, 12, "attachedchar") == 0; // a stage's
    return key == "bgm" || key == "storyboard" || ends(".storyboard");
}

std::string MotifPath();

// A character's state files, its "st0".."st9" read as "st" (editorDefFiles in editor_reload.go)
bool IsStKey(const std::string &key) {
    return key.compare(0, 2, "st") == 0 && key.find_first_not_of("0123456789", 2) == std::string::npos;
}

// A state file not found as named is the ".zss" one (common1.cns is data/common1.cns.zss), as
// the engine's stateCompile does
bool IsStateKey(const std::string &key) { return key == "cns" || key == "cmd" || key == "stcommon" || IsStKey(key); }

std::string Resolve(const std::string &def, const std::string &value, bool state) {
    std::string v = value;
    for (char &ch : v)
        if (ch == '\\') ch = '/';
    const std::string dirs[] = {DirOf(def), DirOf(MotifPath()), "", "data/", "font/", "sound/"};
    for (const std::string &dir : dirs)
        if (GameFileExists(dir + v)) return dir + v;
    if (state)
        for (const std::string &dir : dirs)
            if (GameFileExists(dir + v + ".zss")) return dir + v + ".zss";
    return DirOf(def) + v;
}

// The game's save/config.ini, read again when it changes
const IniFile &ConfigIni() {
    static IniFile ini;
    static FILETIME mtime;
    static double checked = -100;
    if (ImGui::GetTime() - checked < 2.0) return ini;
    checked = ImGui::GetTime();
    FILETIME t = FileTime(GamePath("save/config.ini"));
    if (CompareFileTime(&t, &mtime) == 0) return ini;
    mtime = t;
    ini = IniFile();
    Ini_Parse(ReadWholeFile(GamePath("save/config.ini")), ini);
    return ini;
}

std::string ConfigValue(const char *section, const char *key) {
    for (const IniSection &s : ConfigIni().sections)
        if (s.lower == section)
            if (const IniField *f = s.Field(key)) return f->value;
    return "";
}

// Its files, read again when it changes (its text being edited in Code, or on disk)
const std::vector<DefEntry> &DefFiles(const std::string &def) {
    DefCache &c = g_defs[NormPath(def)];
    std::string text;
    auto open = g_files.find(NormPath(def));
    if (open != g_files.end()) {
        text = open->second.text;
    } else {
        double now = ImGui::GetTime();
        if (c.read && now - c.checked < 2.0) return c.files;
        c.checked = now;
        FILETIME t = FileTime(GamePath(def));
        if (c.read && CompareFileTime(&t, &c.mtime) == 0) return c.files;
        c.mtime = t;
        text = ReadWholeFile(GamePath(def));
    }
    if (c.read && text == c.text) return c.files;
    c.read = true;
    c.text = text;
    c.files.clear();
    IniFile ini;
    Ini_Parse(text, ini);
    for (const IniSection &s : ini.sections)
        for (const IniField &f : s.fields) {
            std::string v = Unquote(f.value);
            // A font's value can have more after the file ("f.def, 0, 1")
            if (f.key.compare(0, 4, "font") == 0) v = v.substr(0, v.find(','));
            while (!v.empty() && v.back() == ' ') v.pop_back();
            if (v.empty() || v.find('.') == std::string::npos || !NamesFile(s.lower, f.key)) continue;
            std::string path = Resolve(def, v, IsStateKey(f.key));
            c.files.push_back({f.key, path, GameFileExists(path)});
        }
    return c.files;
}

// The same, a file named twice (a character's cns and st) once
std::vector<DefEntry> DefFilesOnce(const std::string &def) {
    std::vector<DefEntry> out;
    for (const DefEntry &f : DefFiles(def)) {
        bool dup = false;
        for (const DefEntry &o : out) dup |= NormPath(o.path) == NormPath(f.path);
        if (!dup) out.push_back(f);
    }
    return out;
}

// ---------------------------------------------------------------------------
// The projects: the folders' characters and stages, the screenpack
// ---------------------------------------------------------------------------

struct Project {
    std::string name, def;
};
std::vector<Project> g_chars, g_stages;
double g_scanned = -100;

void Scan(bool now) {
    if (!now && ImGui::GetTime() - g_scanned < 3.0) return;
    g_scanned = ImGui::GetTime();
    g_chars.clear();
    g_stages.clear();
    for (const std::string &n : ListDefs(false)) {
        bool def = ExtOf(n) == "def";
        g_chars.push_back({def ? n.substr(0, n.size() - 4) : n, def ? "chars/" + n : "chars/" + n + "/" + n + ".def"});
    }
    for (const std::string &d : ListDefs(true)) {
        std::string name = BaseName(d);
        g_stages.push_back({name.substr(0, name.size() - 4), d});
    }
}

// The game's, else the one the session's arguments give (Copy as a new screenpack's -r), else the
// one config.ini names: the game not running, a copy must not edit the original
std::string MotifPath() {
    std::string m = Connected() ? MotifDef() : "";
    const std::string &extra = g_settings.extra;
    if (m.empty())
        for (size_t r = extra.find("-r "); r != std::string::npos; r = extra.find("-r ", r + 1))
            if (r == 0 || extra[r - 1] == ' ') {
                size_t from = extra.find_first_not_of(' ', r + 3);
                if (from != std::string::npos) m = extra.substr(from, extra.find(' ', from) - from);
                break;
            }
    if (m.empty()) m = ConfigValue("config", "motif");
    return m.empty() ? "data/system.def" : m;
}

// The files every character gets (config.ini's [Common]: Air, Cmd, Const, States, Fx; comma
// separated lists)
std::vector<DefEntry> CommonFiles() {
    std::vector<DefEntry> out;
    for (const char *key : {"air", "cmd", "const", "states", "fx"}) {
        std::string list = ConfigValue("common", key);
        for (size_t at = 0; at < list.size();) {
            size_t comma = list.find(',', at);
            if (comma == std::string::npos) comma = list.size();
            std::string v = list.substr(at, comma - at);
            v.erase(0, v.find_first_not_of(' '));
            while (!v.empty() && v.back() == ' ') v.pop_back();
            at = comma + 1;
            if (v.empty()) continue;
            std::string path = Resolve("", v, strcmp(key, "states") == 0);
            out.push_back({key, path, GameFileExists(path)});
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// The character the windows work on: the player selected in the game, or the character picked
// in Explorer since. One not in the match is read from disk, as player kDiskPn: its files as the
// engine lists them (editorCharFiles: the [Files] keys in order, st0..st9 as "st"; config.ini's
// common states, AIR and CMD), so CharFile, the model of Analysis, SndOf, SpritesOf work on it.
// ---------------------------------------------------------------------------

std::string g_workDef;   // picked in Explorer ("": the player selected)
int g_workSelPicked = -1; // a player picked in the game since takes it back
std::map<std::string, std::vector<std::string>> g_diskFiles;
std::string g_diskFor;
double g_diskRead = -100;

void ReadDiskChar(const std::string &def) {
    if (NormPath(def) == NormPath(g_diskFor) && ImGui::GetTime() - g_diskRead < 1.0) return;
    g_diskFor = def;
    g_diskRead = ImGui::GetTime();
    auto &files = g_diskFiles;
    files.clear();
    files["def"] = {def};
    std::vector<DefEntry> list = DefFiles(def);
    std::stable_sort(list.begin(), list.end(), [](const DefEntry &a, const DefEntry &b) { return a.key < b.key; });
    for (const DefEntry &f : list) files[IsStKey(f.key) ? "st" : f.key].push_back(f.path);
    for (const DefEntry &f : CommonFiles()) {
        const char *key = f.key == "states" ? "common.states" : f.key == "air" ? "common.air" : f.key == "cmd" ? "common.cmd" : nullptr;
        if (key) files[key].push_back(f.path);
    }
}

// A character as the toolbar names it ("kfm", "kfm/kfm720.def"): its def
std::string CharDefOf(const std::string &name) {
    if (name.empty()) return "";
    std::string n = name;
    bool def = ExtOf(n) == "def";
    if (n.find('/') == std::string::npos) return "chars/" + n + "/" + (def ? n : n + ".def");
    return "chars/" + n + (def ? "" : ".def");
}

// The player a character's def is in the match as, 0 none
int PlayerOf(const std::string &def) {
    for (const auto &[pn, files] : g_charFiles) {
        auto f = files.find("def");
        if (f != files.end() && !f->second.empty() && NormPath(f->second[0]) == NormPath(def)) return pn;
    }
    return 0;
}

std::string g_rowChar; // the character whose files the tree is drawing

// ---------------------------------------------------------------------------
// A project as a whole: a zip of it, a copy of the screenpack
// ---------------------------------------------------------------------------

// A def's files and those of the defs among them, each once. The engine's common states are not the project's.
void DefClosure(const std::string &def, std::map<std::string, DefEntry> &out) {
    for (const DefEntry &f : DefFiles(def)) {
        if (f.key == "stcommon" || out.count(NormPath(f.path))) continue;
        out[NormPath(f.path)] = f;
        if (f.exists && ExtOf(f.path) == "def") DefClosure(f.path, out);
    }
}

void PackageDef(const std::string &def) {
    std::map<std::string, DefEntry> found;
    found[NormPath(def)] = {"def", def, true};
    DefClosure(def, found);
    std::string stem = BaseName(def);
    std::string path = PickSaveFile("Zip archives (*.zip)\0*.zip\0", "zip", stem.substr(0, stem.find_last_of('.')) + ".zip");
    if (path.empty()) return;
    std::vector<PackFile> files;
    std::string missing;
    size_t bytes = 0;
    int absent = 0;
    for (const auto &[key, f] : found) {
        if (!f.exists) {
            absent++;
            missing += (missing.empty() ? "" : ", ") + f.path;
            continue;
        }
        // Named as on disk: NormPath's lower case would not be found on Linux
        std::string name = f.path;
        std::replace(name.begin(), name.end(), '\\', '/');
        files.push_back({name, ReadWholeFile(GamePath(f.path))});
        bytes += files.back().data.size();
    }
    std::ofstream out(path, std::ios::binary);
    out << Zip_Build(files);
    out.close();
    Log(out ? LogLine::Info : LogLine::Error, (out ? "Packaged " : "Could not write ") + std::to_string(files.size()) + " files, " +
        std::to_string(bytes / 1024) + " KB, in " + path);
    if (absent) Log(LogLine::Error, std::to_string(absent) + " files the def names are not found: " + missing);
    // In the def's own folder, unless that is shared by others
    std::string dir = DirOf(def);
    if (dir.empty() || NormPath(dir) == "data/" || NormPath(dir) == "stages/") return;
    std::error_code ec;
    int extra = 0;
    std::string names;
    for (const auto &e : std::filesystem::recursive_directory_iterator(GamePath(dir), ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string rel = dir + std::filesystem::relative(e.path(), GamePath(dir), ec).generic_string();
        if (found.count(NormPath(rel))) continue;
        if (extra++ < 8) names += (names.empty() ? "" : ", ") + BaseName(rel);
    }
    if (extra) Log(LogLine::Info, std::to_string(extra) + " files of " + dir + " left out, nothing names them: " + names + (extra > 8 ? "..." : ""));
}

// A copy of the screenpack in data/<name>/, its own from then on: the motif's folder as it is, and the
// files of data/ it names next to it, which the engine looks in first. Sounds, fonts and music
// elsewhere stay shared. The defs naming the old folder ("ikemen1/system.sff") name the new one.
bool CopyScreenpack(const std::string &name, std::string *result) {
    std::string motif = MotifPath(), from = DirOf(motif), to = "data/" + name + "/";
    if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos) {
        *result = "The name takes letters, digits, '_' and '-' only.";
        return false;
    }
    std::error_code ec;
    if (std::filesystem::exists(GamePath(to), ec)) {
        *result = to + " is there already.";
        return false;
    }
    std::map<std::string, DefEntry> found;
    found[NormPath(motif)] = {"motif", motif, true};
    DefClosure(motif, found);
    std::string fromName = from;
    if (!fromName.empty()) fromName.pop_back();
    fromName = BaseName(fromName) + "/";
    int copied = 0;
    for (const auto &[key, f] : found) {
        if (!f.exists) continue;
        bool inside = NormPath(f.path).compare(0, NormPath(from).size(), NormPath(from)) == 0;
        if (!inside && NormPath(DirOf(f.path)) != "data/") continue;
        std::string rel = inside ? f.path.substr(from.size()) : BaseName(f.path), target = to + rel, data = ReadWholeFile(GamePath(f.path));
        if (ExtOf(f.path) == "def" && fromName != "data/")
            for (size_t at = 0; (at = data.find(fromName, at)) != std::string::npos; at += name.size() + 1) data.replace(at, fromName.size(), name + "/");
        std::filesystem::create_directories(std::filesystem::path(GamePath(target)).parent_path(), ec);
        std::ofstream out(GamePath(target), std::ios::binary);
        out << data;
        if (!out) {
            *result = "Could not write " + target;
            return false;
        }
        copied++;
    }
    *result = std::to_string(copied) + " files in " + to;
    return true;
}

bool g_askScreenpack;

// ---------------------------------------------------------------------------
// The tree
// ---------------------------------------------------------------------------

ImGuiTextFilter g_filter;

void ShowInFolder(const std::string &path) {
    std::string full = GamePath(path);
    for (char &ch : full)
        if (ch == '/') ch = '\\';
    ShellExecuteA(nullptr, "open", "explorer.exe", ("/select,\"" + full + "\"").c_str(), nullptr, SW_SHOWNORMAL);
}

void FileMenuItems(const std::string &path, const std::string &pal) {
    if (ImGui::MenuItem(ICON_FOLDER_OPEN " Open")) OpenPath(path, true, pal);
    // A character's AIR file: its actions drawn and edited (the character is the one worked on)
    if (ExtOf(path) == "air" && !g_rowChar.empty() && ImGui::MenuItem("Open in Animation")) {
        WorkOn(g_rowChar);
        AnimOpenFile("");
    }
    // Actions of their own: a stage's or a screen's def, an AIR file no character uses
    bool actions = g_rowChar.empty() && (ExtOf(path) == "air" || (ExtOf(path) == "def" && !SpritesFileOf(path).empty()));
    if (actions && ImGui::MenuItem("Open in Animation")) AnimOpenFile(path);
    if (ExtOf(path) == "def") {
        IniFile ini;
        Ini_Parse(LoadSource(path).text, ini);
        if (Story_Is(ini) && ImGui::MenuItem(ICON_CLAPPER " Open in Storyboard")) {
            StoryOpen(path);
            ShowWindow("Storyboard");
            ShowWindow("Scenes");
        }
    }
    if (ExtOf(path) == "def" && ImGui::MenuItem(ICON_DOWNLOAD " Package as zip...")) PackageDef(path);
    ImGui::SetItemTooltip("A zip of this def and every file it names, for sharing it; says what is missing");
    if (NormPath(path) == NormPath(MotifPath()) && ImGui::MenuItem(ICON_COPY " Copy as a new screenpack...")) g_askScreenpack = true;
    if (ImGui::MenuItem(ICON_FOLDER " Show in folder")) ShowInFolder(path);
    if (ImGui::MenuItem(ICON_COPY " Copy path")) ImGui::SetClipboardText(path.c_str());
}

// A word at the right of the tree node just drawn, when its label leaves room
void RowNote(const char *text, ImU32 color, const std::string &label) {
    ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    float w = ImGui::CalcTextSize(text).x, x = max.x - w - ImGui::GetStyle().FramePadding.x;
    float labelEnd = min.x + ImGui::GetTreeNodeToLabelSpacing() + ImGui::CalcTextSize(label.c_str(), nullptr, true).x;
    if (x < labelEnd + ImGui::GetFontSize()) return;
    ImGui::GetWindowDrawList()->AddText(ImVec2(x, min.y + (max.y - min.y - ImGui::GetFontSize()) * 0.5f), color, text);
}

bool Passes(const std::string &path) { return !g_filter.IsActive() || g_filter.PassFilter(path.c_str()); }

// A def's files that pass the filter (all of them when its project's name does)
bool AnyPasses(const std::string &def, bool all, int depth = 0) {
    if (all || Passes(def)) return true;
    for (const DefEntry &f : DefFiles(def))
        if (Passes(f.path) || (depth < 2 && f.exists && ExtOf(f.path) == "def" && AnyPasses(f.path, false, depth + 1))) return true;
    return false;
}

void FileRow(const std::string &path, const char *key, bool exists, const std::string &pal, bool all, int depth);

// A def's files under it; a def among them (a motif's fight.def) opens to its own
void DefChildren(const std::string &def, const std::string &pal, bool all, int depth) {
    for (const DefEntry &f : DefFilesOnce(def)) {
        if (!all && !Passes(f.path) && !(ExtOf(f.path) == "def" && depth < 2 && AnyPasses(f.path, false))) continue;
        FileRow(f.path, f.key.c_str(), f.exists, f.key == "sprite" || f.key == "spr" ? pal : "", all, depth);
    }
}

void FileRow(const std::string &path, const char *key, bool exists, const std::string &pal, bool all, int depth) {
    bool nested = depth < 2 && ExtOf(path) == "def" && exists && !DefFiles(path).empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
    if (!nested) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (NormPath(path) == NormPath(g_codeFile)) flags |= ImGuiTreeNodeFlags_Selected;
    auto open = g_files.find(NormPath(path));
    bool dirty = open != g_files.end() && open->second.Dirty();
    std::string label = BaseName(path) + (dirty ? " *" : "") + "##" + path;
    if (!exists) ImGui::PushStyleColor(ImGuiCol_Text, Ink(kRed));
    if (nested && g_filter.IsActive()) ImGui::SetNextItemOpen(true);
    bool expanded = ImGui::TreeNodeEx(label.c_str(), flags);
    if (!exists) ImGui::PopStyleColor();
    bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen();
    if (clicked && !g_rowChar.empty()) WorkOn(g_rowChar);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) OpenPath(path, true, pal);
    else if (clicked) OpenPath(path, false, pal);
    ImGui::SetItemTooltip("%s%s", path.c_str(), exists ? "" : "\nnot found");
    RowNote(key, ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
    if (ImGui::BeginPopupContextItem()) {
        FileMenuItems(path, pal);
        ImGui::EndPopup();
    }
    if (nested && expanded) {
        DefChildren(path, "", all, depth + 1);
        ImGui::TreePop();
    }
}

// The players a character's def is in the match as ("P1 P2")
std::string PlayersOf(const std::string &def) {
    std::string out;
    for (const auto &[pn, files] : g_charFiles) {
        auto f = files.find("def");
        if (f != files.end() && !f->second.empty() && NormPath(f->second[0]) == NormPath(def))
            out += (out.empty() ? "P" : " P") + std::to_string(pn);
    }
    return out;
}

void CharNode(const Project &p) {
    bool all = Passes(p.name);
    if (!AnyPasses(p.def, all)) return;
    if (g_filter.IsActive()) ImGui::SetNextItemOpen(true);
    // The character the windows work on: selected
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (NormPath(WorkDef()) == NormPath(p.def)) flags |= ImGuiTreeNodeFlags_Selected;
    bool open = ImGui::TreeNodeEx(p.name.c_str(), flags);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) WorkOn(p.def);
    ImGui::SetItemTooltip("%s\nA click: Analysis, Sounds, Commands and Sprites work on it (from disk when it is not "
                          "in the match).", p.def.c_str());
    std::string players = PlayersOf(p.def);
    if (!players.empty()) RowNote(players.c_str(), ImGui::GetColorU32(Ink(kGreen)), p.name);
    if (ImGui::BeginPopupContextItem()) {
        for (int pn : {1, 2}) {
            char label[48];
            snprintf(label, sizeof(label), "%s P%d%s", Connected() ? "Put in" : "Use as", pn, Connected() ? "'s place" : "");
            if (ImGui::MenuItem(label)) {
                std::string name = p.name.find('/') == std::string::npos ? p.name : p.name + ".def";
                (pn == 1 ? g_settings.p1 : g_settings.p2) = name;
                if (Connected()) SwapChar(pn, name);
            }
        }
        ImGui::SetItemTooltip(Connected() ? "The match starts again with it." : "In the match the game starts with.");
        ImGui::Separator();
        FileMenuItems(p.def, "");
        ImGui::EndPopup();
    }
    if (!open) return;
    std::string pal;
    for (const DefEntry &f : DefFiles(p.def))
        if (f.key == "pal1") pal = f.path;
    g_rowChar = p.def;
    if (all || Passes(p.def)) FileRow(p.def, "def", true, "", all, 2);
    DefChildren(p.def, pal, all, 2);
    g_rowChar.clear();
    ImGui::TreePop();
}

void StageNode(const Project &p) {
    bool all = Passes(p.name);
    if (!AnyPasses(p.def, all)) return;
    if (g_filter.IsActive()) ImGui::SetNextItemOpen(true);
    SourceFile *shown = Connected() ? nullptr : StageSource(); // the game not running: the Stage window's
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (shown && NormPath(shown->path) == NormPath(p.def)) flags |= ImGuiTreeNodeFlags_Selected;
    bool open = ImGui::TreeNodeEx(p.def.c_str(), flags, "%s", p.name.c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) StageOpen(p.def);
    bool playing = NormPath(p.def) == NormPath(g_stageDef);
    if (playing) RowNote("playing", ImGui::GetColorU32(Ink(kGreen)), p.name);
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Use as the stage")) g_settings.stage = p.def.compare(0, 7, "stages/") == 0 ? p.def.substr(7) : p.def;
        ImGui::SetItemTooltip("In the match the game starts with (Launch, Restart).");
        ImGui::BeginDisabled(Connected());
        if (ImGui::MenuItem("Open in the Stage window")) {
            StageOpen(p.def);
            ShowWindow("Stage");
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip(Connected() ? "With the game running, the window shows the match's stage." : "Its sections and fields, edited from disk.");
        ImGui::Separator();
        FileMenuItems(p.def, "");
        ImGui::EndPopup();
    }
    if (!open) return;
    if (all || Passes(p.def)) FileRow(p.def, "def", true, "", all, 2);
    DefChildren(p.def, "", all, 2);
    ImGui::TreePop();
}

bool Group(const char *label, size_t count, bool open = true) {
    if (g_filter.IsActive()) ImGui::SetNextItemOpen(true);
    char text[64];
    snprintf(text, sizeof(text), "%s (%zu)###%s", label, count, label);
    return ImGui::TreeNodeEx(text, ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed | (open ? ImGuiTreeNodeFlags_DefaultOpen : 0));
}

} // namespace

bool IsTextFile(const std::string &path) { return !IsBinary(path); }

std::string ScreenpackDef() { return MotifPath(); }

bool DefNamesFile(const std::string &def, const std::string &name) { return GameFileExists(Resolve(def, name, false)); }

std::string DefFile(const std::string &def, const char *key) {
    for (const DefEntry &f : DefFiles(def))
        if (f.key == key) return f.path;
    return "";
}

std::string SpritesFileOf(const std::string &file) {
    if (std::string inUse = ScreenSpritesOf(file); !inUse.empty()) return inUse;
    if (ExtOf(file) == "air") {
        // The fight screen's FX; else the SFF named alike
        if (std::string fight = FightScreenDef(); !fight.empty() && NormPath(DefFile(fight, "fightfx.air")) == NormPath(file))
            return DefFile(fight, "fightfx.sff");
        std::string alike = file.substr(0, file.size() - 3) + "sff";
        return GameFileExists(alike) ? alike : "";
    }
    if (std::string spr = DefFile(file, "spr"); !spr.empty()) return spr;
    return DefFile(file, "sff");
}

std::vector<std::pair<std::string, std::string>> StoryboardFiles() {
    Scan(false);
    std::vector<std::pair<std::string, std::string>> out;
    auto add = [&](const std::string &def, const std::string &who) {
        for (const DefEntry &f : DefFiles(def)) {
            size_t n = f.key.size();
            bool story = f.key == "storyboard" || (n > 11 && f.key.compare(n - 11, 11, ".storyboard") == 0);
            bool listed = false;
            for (const auto &o : out) listed = listed || NormPath(o.second) == NormPath(f.path);
            if (story && f.exists && !listed) out.push_back({who + f.key, f.path});
        }
    };
    add(MotifPath(), "");
    for (const Project &p : g_chars) add(p.def, p.name + " ");
    return out;
}

std::string SelectDef() {
    if (!g_selectDef.empty()) return g_selectDef;
    for (const DefEntry &f : DefFiles(MotifPath()))
        if (f.key == "select" && f.exists) return f.path;
    return "";
}

void WorkOn(const std::string &def) {
    g_workDef = def;
    g_workSelPicked = g_selPicked;
}

int WorkPn() {
    if (!g_workDef.empty() && g_selPicked != g_workSelPicked) g_workDef.clear();
    std::string def = g_workDef;
    if (def.empty()) {
        if (const Entity *e = FindEntity(g_selId)) return e->pn;
        if (Connected() || g_stagePick || g_screenPick) return 0; // on a stage or a screen: nobody chose one
        def = CharDefOf(g_settings.p1); // the game not running: the character its match starts with
    }
    if (int pn = PlayerOf(def)) return pn;
    if (!GameFileExists(def)) return 0;
    ReadDiskChar(def);
    return kDiskPn;
}

std::string WorkDef() {
    int pn = WorkPn();
    return pn ? CharFile(pn, "def") : "";
}

std::string WorkLabel(int pn) { return pn == kDiskPn ? std::string("from disk") : "P" + std::to_string(pn); }

const std::map<std::string, std::vector<std::string>> *FilesFor(int pn) {
    if (pn == kDiskPn) return g_diskFor.empty() ? nullptr : &g_diskFiles;
    auto it = g_charFiles.find(pn);
    return it == g_charFiles.end() ? nullptr : &it->second;
}

void OpenPath(const std::string &path, bool keep, const std::string &pal) {
    std::string ext = ExtOf(path);
    if (ext == "glb" || ext == "gltf") {
        ModelOpenFile(path);
        return;
    }
    if (!IsBinary(path)) {
        CodeOpen(path, keep);
        return;
    }
    if (ext == "sff") {
        ShowInSprites(SpriteFileAt(path, pal), -1, BaseName(path));
        ShowWindow("Sprites");
        return;
    }
    if (ext == "snd") {
        int pn = WorkPn();
        if (pn && NormPath(CharFile(pn, "sound")) == NormPath(path)) ShowWindow("Sounds");
        else ShowInSounds(path, -1, "file " + BaseName(path));
        return;
    }
    Log(LogLine::Info, "No window edits ." + ext + " files (" + path + "); Show in folder opens its folder.");
}

std::string StageDefOfModel(const std::string &model) {
    Scan(false);
    for (const Project &p : g_stages)
        for (const DefEntry &f : DefFiles(p.def))
            if (f.key == "model" && NormPath(f.path) == NormPath(model)) return p.def;
    return "";
}

void ProjectFiles(std::vector<std::string> &out) {
    Scan(false);
    std::map<std::string, bool> seen;
    auto add = [&](const std::string &p) {
        if (!seen[NormPath(p)]) seen[NormPath(p)] = true, out.push_back(p);
    };
    auto addDef = [&](const std::string &def, int depth, auto &self) -> void {
        add(def);
        for (const DefEntry &f : DefFiles(def)) {
            if (!f.exists) continue;
            if (ExtOf(f.path) == "def" && depth < 2) self(f.path, depth + 1, self);
            else add(f.path);
        }
    };
    for (const Project &p : g_chars) addDef(p.def, 1, addDef);
    for (const Project &p : g_stages) addDef(p.def, 1, addDef);
    addDef(MotifPath(), 0, addDef);
    add("save/config.ini");
    for (const DefEntry &f : CommonFiles())
        if (f.exists) addDef(f.path, 1, addDef);
    for (const auto &kv : g_files) add(kv.second.path);
}

void ExplorerWindow() {
    if (!BeginTool("Explorer")) return;
    Scan(false);
    // The players' files, to mark their characters (asked again after a swap)
    for (const Entity &e : g_ents)
        if (!e.helper) AirPath(e.pn);
    const ImGuiStyle &st = ImGui::GetStyle();
    ImGui::SetNextItemWidth(-(ImGui::CalcTextSize(ICON_RESTART " Reload").x + st.FramePadding.x * 2 + st.ItemSpacing.x));
    if (ImGui::InputTextWithHint("##filter", "Filter", g_filter.InputBuf,
                                 IM_ARRAYSIZE(g_filter.InputBuf)))
        g_filter.Build();
    ImGui::SetItemTooltip("Shows the files whose path has one of the words (comma separated); -word hides them.");
    ImGui::SameLine();
    if (ImGui::Button(ICON_RESTART " Reload")) {
        Scan(true);
        g_defs.clear();
    }
    ImGui::SetItemTooltip("Reads the folders and the defs again");
    if (g_askScreenpack) g_askScreenpack = false, ImGui::OpenPopup("screenpack");
    if (ImGui::BeginPopup("screenpack")) {
        static std::string name, status;
        ImGui::TextUnformatted("A copy of the screenpack, its own from then on");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(), status.clear();
        bool ok = ImGui::InputTextWithHint("##name", "data/...", &name, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ok |= ImGui::Button(ICON_CHECK " Copy");
        if (ok) {
            if (CopyScreenpack(name, &status)) {
                // The motif of the next launch, without touching config.ini
                std::string &extra = g_settings.extra;
                size_t r = extra.find("-r ");
                if (r != std::string::npos) extra.erase(r, extra.find(' ', r + 3) == std::string::npos ? std::string::npos : extra.find(' ', r + 3) + 1 - r);
                extra += (extra.empty() || extra.back() == ' ' ? "" : " ") + ("-r data/" + name + "/system.def");
                Log(LogLine::Info, "New screenpack: " + status + ". The game starts with it from the next launch (-r in the session's arguments).");
                Scan(true);
                g_defs.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        if (!status.empty()) ImGui::TextColored(Ink(kRed), "%s", status.c_str());
        ImGui::EndPopup();
    }
    if (ImGui::BeginChild("tree")) {
        if (Group("Characters", g_chars.size())) {
            for (const Project &p : g_chars) CharNode(p);
            ImGui::TreePop();
        }
        if (Group("Stages", g_stages.size())) {
            for (const Project &p : g_stages) StageNode(p);
            ImGui::TreePop();
        }
        if (Group("Screenpack", 1)) {
            std::string motif = MotifPath();
            if (AnyPasses(motif, false)) FileRow(motif, "motif", GameFileExists(motif), "", false, 0);
            ImGui::TreePop();
        }
        std::vector<DefEntry> common = CommonFiles();
        if (Group("Common", common.size() + 1, false)) {
            if (Passes("save/config.ini")) FileRow("save/config.ini", "config", GameFileExists("save/config.ini"), "", false, 2);
            for (const DefEntry &f : common)
                if (Passes(f.path) || (ExtOf(f.path) == "def" && AnyPasses(f.path, false)))
                    FileRow(f.path, f.key.c_str(), f.exists, "", false, 1);
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace app
