// Sounds: the SND file of the character worked on (the selected player's, or one picked in Explorer).
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

SoundFile *SoundsOf(int pn) {
    std::string path = CharFile(pn, "sound");
    if (path.empty()) {
        AirPath(pn); // asks for the files
        return nullptr;
    }
    auto it = g_soundFiles.find(NormPath(path));
    if (it != g_soundFiles.end()) return &it->second;
    SoundFile &sf = g_soundFiles[NormPath(path)];
    sf.path = path;
    std::string data = ReadWholeFile(GamePath(path)), error;
    if (!Snd_Parse(data, sf.snd, &error)) sf.error = path + ": " + error;
    return &sf;
}

const SndFile *SndOf(int pn) {
    SoundFile *sf = SoundsOf(pn);
    return sf && sf->error.empty() ? &sf->snd : nullptr;
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
    Log(LogLine::Info, "Saved " + path);
    for (const auto &kv : g_charFiles) {
        auto f = kv.second.find("sound");
        if (f != kv.second.end() && !f->second.empty() && NormPath(f->second[0]) == NormPath(sf.path)) {
            SendRaw("ikdbg.sounds(" + std::to_string(kv.first) + ")"); // every player using the file
            break;
        }
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
    int pn = WorkPn(); // the selected player's character, or one picked in Explorer
    SoundFile *sp = pn > 0 ? SoundsOf(pn) : nullptr;
    if (!sp) {
        ImGui::TextDisabled(!pn ? "Pick a player in the game, or a character in Explorer."
                            : g_canTrace ? "Waiting for the files of P%d..." : "Needs the patched Ikemen GO.", pn);
        ImGui::End();
        return;
    }
    SoundFile &sf = *sp;
    SndFile &snd = sf.snd;
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
        for (int i = 0; i < (int)snd.sounds.size(); i++) {
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
            ImGui::TableNextColumn();
            ImGui::Text("%.2f s", info.Seconds());
            ImGui::TableNextColumn();
            if (ok)
                ImGui::TextDisabled("%u Hz, %u bits, %s", info.rate, info.bits, info.channels == 1 ? "mono" : "stereo");
            else
                ImGui::TextColored(Ink(kRed), "%s", error.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace app
