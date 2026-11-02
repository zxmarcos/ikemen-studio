// Sounds: the SND file of the character worked on (the selected player's, or one picked in Explorer),
// or any other a window showed here (a screen's, common.snd).
#include "app_internal.h"

#include <mmsystem.h>
#include <fstream>

namespace app {

struct SoundFile {
    SndFile snd;
    std::string path, error; // path as the game names it; error: why it could not be read
    bool dirty = false;
    int sel = -1;
};
std::map<std::string, SoundFile> g_soundFiles; // by path
static SoundFile *g_soundsShown;               // a file another window showed here
static std::string g_soundsShownWhat;
static int g_soundsShownN;
static bool g_soundsScroll;

static SoundFile &SoundFileAt(const std::string &path) {
    auto it = g_soundFiles.find(NormPath(path));
    if (it != g_soundFiles.end()) return it->second;
    SoundFile &sf = g_soundFiles[NormPath(path)];
    sf.path = path;
    std::string data = ReadWholeFile(GamePath(path)), error;
    if (!Snd_Parse(data, sf.snd, &error)) sf.error = path + ": " + error;
    return sf;
}

SoundFile *SoundsOf(int pn) {
    std::string path = CharFile(pn, "sound");
    if (path.empty()) {
        AirPath(pn); // asks for the files
        return nullptr;
    }
    return &SoundFileAt(path);
}

const SndFile *SndOf(int pn) {
    SoundFile *sf = SoundsOf(pn);
    return sf && sf->error.empty() ? &sf->snd : nullptr;
}

const SndFile *SndAt(const std::string &path) {
    SoundFile &sf = SoundFileAt(path);
    return sf.error.empty() ? &sf.snd : nullptr;
}

void ShowInSounds(const std::string &path, int index, const std::string &what) {
    g_soundsShown = &SoundFileAt(path);
    g_soundsShownWhat = what;
    if (index >= 0) g_soundsShown->sel = index, g_soundsScroll = true;
    g_soundsShownN++;
    ShowWindow("Sounds");
}

// Plays a WAV file here (PlaySound reads it while it plays: it is kept).
void PlayWav(const std::string &wav) {
    static std::string playing;
    PlaySoundA(nullptr, nullptr, 0);
    playing = wav;
    PlaySoundA(playing.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void SaveSounds(SoundFile &sf) {
    std::string path = GamePath(sf.path), bak = path + ".bak";
    if (GetFileAttributesA(bak.c_str()) == INVALID_FILE_ATTRIBUTES) CopyFileA(path.c_str(), bak.c_str(), TRUE);
    std::ofstream out(path, std::ios::binary);
    out << Snd_Build(sf.snd);
    out.close();
    if (!out) {
        Log(LogLine::Error, "Could not write " + path);
        return;
    }
    sf.dirty = false;
    Log(LogLine::Info, "Saved " + sf.path);
    ScreenSoundsSaved(sf.path);
    for (const auto &kv : g_charFiles) {
        auto f = kv.second.find("sound");
        if (f != kv.second.end() && !f->second.empty() && NormPath(f->second[0]) == NormPath(sf.path)) {
            SendRaw("ikdbg.sounds(" + std::to_string(kv.first) + ")"); // every player using the file
            break;
        }
    }
}

void SoundsUnsaved(std::vector<Unsaved> &out) {
    for (auto &kv : g_soundFiles)
        if (kv.second.dirty) {
            SoundFile *sf = &kv.second;
            out.push_back({sf->path, [sf] {
                               SaveSounds(*sf);
                               return !sf->dirty;
                           }});
        }
}

bool ReadWavFile(const std::string &path, std::string &wav) {
    WavInfo info;
    std::string error;
    wav = ReadWholeFile(path);
    if (!Wav_Describe(wav, &info, &error)) {
        Log(LogLine::Error, path + ": " + error);
        return false;
    }
    return true;
}

const char kWavFilter[] = "WAV files (*.wav)\0*.wav\0All files (*.*)\0*.*\0";

void SoundsWindow() {
    if (!BeginTool("Sounds")) return;
    // Whose: the character worked on, or the file another window showed here, whichever came last
    static bool shown = false;
    static int seenSel = -2, seenSelPicked = 0, seenShown = 0;
    static std::string seenWork;
    if (g_selId != seenSel || g_selPicked != seenSelPicked) seenSel = g_selId, seenSelPicked = g_selPicked, shown = false;
    if (std::string work = WorkDef(); work != seenWork) seenWork = work, shown = false;
    if (g_soundsShownN != seenShown) seenShown = g_soundsShownN, shown = true;
    int pn = shown ? 0 : WorkPn(); // the selected player's character, or one picked in Explorer
    SoundFile *sp = shown ? g_soundsShown : pn > 0 ? SoundsOf(pn) : nullptr;
    if (!sp) {
        ImGui::TextDisabled(!pn ? "Pick a player in the game, or a character in Explorer. An SND file in Explorer opens here too."
                            : g_canTrace ? "Waiting for the files of P%d..." : "Needs the patched Ikemen GO.", pn);
        ImGui::End();
        return;
    }
    SoundFile &sf = *sp;
    SndFile &snd = sf.snd;
    if (shown) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("The %s", g_soundsShownWhat.c_str());
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(!sf.dirty);
    if (ImGui::Button(ICON_SAVE " Save")) SaveSounds(sf);
    ImGui::SetItemTooltip("Writes the SND file (the old one is kept as .bak the first time) and the game reads it again.");
    ImGui::SameLine();
    if (ImGui::Button(ICON_REVERT " Revert")) {
        sf.error.clear();
        std::string error;
        if (!Snd_Parse(ReadWholeFile(GamePath(sf.path)), snd, &error)) sf.error = sf.path + ": " + error;
        sf.dirty = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    static int addGroup = 0;
    if (ImGui::Button(ICON_PLUS " Add WAV files...")) ImGui::OpenPopup("add");
    if (ImGui::BeginPopup("add")) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::InputInt("Group", &addGroup, 0);
        if (ImGui::Button(ICON_FOLDER_OPEN " Choose the files...")) {
            for (const std::string &f : PickFiles(kWavFilter, true)) {
                std::string wav;
                if (!ReadWavFile(f, wav)) continue;
                SndSound s;
                s.group = addGroup;
                while (Snd_Find(snd, s.group, s.number) >= 0) s.number++;
                s.wav = wav;
                snd.sounds.push_back(s);
                sf.sel = (int)snd.sounds.size() - 1;
                sf.dirty = true;
                Log(LogLine::Info, "Added " + f + " as sound " + std::to_string(s.group) + ", " + std::to_string(s.number));
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_STOP " Stop")) PlaySoundA(nullptr, nullptr, 0);
    ImGui::TextDisabled("%s%s  %zu sounds", sf.path.c_str(), sf.dirty ? " *" : "", snd.sounds.size());
    if (!sf.error.empty()) ImGui::TextColored(Ink(kRed), "%s", sf.error.c_str());

    // The selected sound
    if (sf.sel >= 0 && sf.sel < (int)snd.sounds.size()) {
        SndSound &s = snd.sounds[sf.sel];
        static int num[2], forSel = -1;
        static const SoundFile *forFile;
        if (forSel != sf.sel || forFile != &sf) {
            forSel = sf.sel, forFile = &sf;
            num[0] = s.group, num[1] = s.number;
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::InputInt2("##num", num);
        ImGui::SameLine();
        bool taken = (num[0] != s.group || num[1] != s.number) && Snd_Find(snd, num[0], num[1]) >= 0;
        ImGui::BeginDisabled(taken || (num[0] == s.group && num[1] == s.number));
        if (ImGui::Button(ICON_LIST " Renumber")) {
            s.group = num[0], s.number = num[1];
            sf.dirty = true;
        }
        ImGui::EndDisabled();
        if (ImGui::Button(ICON_UPLOAD " Replace...")) {
            std::vector<std::string> files = PickFiles(kWavFilter, false);
            std::string wav;
            if (!files.empty() && ReadWavFile(files[0], wav)) {
                s.wav = wav;
                sf.dirty = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_DOWNLOAD " Save as...")) {
            std::string name = "sound_" + std::to_string(s.group) + "_" + std::to_string(s.number) + ".wav";
            std::string path = PickSaveFile(kWavFilter, "wav", name);
            if (!path.empty()) {
                std::ofstream out(path, std::ios::binary);
                out << s.wav;
                Log(out ? LogLine::Info : LogLine::Error, (out ? "Saved " : "Could not write ") + path);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_TRASH " Delete")) {
            snd.sounds.erase(snd.sounds.begin() + sf.sel);
            sf.sel = -1;
            sf.dirty = true;
        }
    }

    if (ImGui::BeginTable("sounds", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("");
        ImGui::TableSetupColumn("Sound");
        ImGui::TableSetupColumn("Length");
        ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        // A screen's file has hundreds of sounds: only the rows in view
        ImGuiListClipper clip;
        clip.Begin((int)snd.sounds.size());
        if (g_soundsScroll && sf.sel >= 0) clip.IncludeItemByIndex(sf.sel);
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
                const SndSound &s = snd.sounds[i];
                WavInfo info;
                std::string error;
                bool ok = Wav_Describe(s.wav, &info, &error);
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::SmallButton(ICON_PLAY " Play")) PlayWav(s.wav);
                ImGui::TableNextColumn();
                char label[48];
                snprintf(label, sizeof(label), "%d, %d", s.group, s.number);
                if (ImGui::Selectable(label, sf.sel == i, ImGuiSelectableFlags_SpanAllColumns)) sf.sel = i;
                if (g_soundsScroll && sf.sel == i) ImGui::SetScrollHereY(), g_soundsScroll = false;
                ImGui::TableNextColumn();
                ImGui::Text("%.2f s", info.Seconds());
                ImGui::TableNextColumn();
                if (ok)
                    ImGui::TextDisabled("%u Hz, %u bits, %s", info.rate, info.bits, info.channels == 1 ? "mono" : "stereo");
                else
                    ImGui::TextColored(Ink(kRed), "%s", error.c_str());
                ImGui::PopID();
            }
        clip.End();
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace app
