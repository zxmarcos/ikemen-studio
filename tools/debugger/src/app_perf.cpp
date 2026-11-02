// Performance window: the game's frames phase by phase (the game, the motif and Lua, sounds and
// input, the draw, showing the frame, the wait), measured by the engine while the window is shown
// (debugPerf), as a graph of the last frames and their averages and peaks; and this program's own
// frame.
#include "app_internal.h"

#include <deque>

namespace app {

namespace {

const int kPhases = 6; // the wait is the last one
const char *kPhaseNames[kPhases] = {"Game", "Motif and Lua", "Sounds and input", "Draw", "Show the frame", "Wait"};
const char *kPhaseHelp[kPhases] = {
    "sys.action: the characters' states, hits, the stage, the camera, the fight screen.",
    "sys.uiAction: the motif, the storyboards and the common Lua (the debugger's mod among it).",
    "Sounds, the console's commands, round resets: from the end of the motif to the draw.",
    "Drawing the frame (cueDraw, renderFrame): the draw calls, not the GPU's work.",
    "Ending the frame and showing it (SwapBuffers waits for the GPU and the display); the debugger's "
    "capture of the frame is in it.",
    "Waiting for the frame's time (the game's speed), and the window's events.",
};
const ImU32 kPhaseColors[kPhases] = {IM_COL32(220, 90, 90, 255), IM_COL32(230, 170, 60, 255),
                                     IM_COL32(150, 150, 160, 255), IM_COL32(90, 170, 230, 255),
                                     IM_COL32(150, 110, 220, 255), IM_COL32(90, 190, 110, 90)};
const size_t kKeep = 600;

struct PerfFrame {
    float ms[kPhases];
    float capture;
    bool skipped;
};

struct PerfState {
    bool known = false, none = false, frozen = false;
    std::deque<PerfFrame> frames;
    int fps = 60;
    int lost = 0;
    double polled = 0;
} g_perf;

double g_appFrameMs = 0; // this program's work in its last frame

struct Stat {
    float avg = 0, peak = 0;
};

template <class F> Stat StatOf(const std::deque<PerfFrame> &frames, F of) {
    Stat s;
    if (frames.empty()) return s;
    for (const auto &f : frames) {
        float v = of(f);
        s.avg += v;
        s.peak = std::max(s.peak, v);
    }
    s.avg /= frames.size();
    return s;
}

float Busy(const PerfFrame &f) {
    float t = 0;
    for (int p = 0; p < kPhases - 1; p++) t += f.ms[p];
    return t;
}

float Total(const PerfFrame &f) { return Busy(f) + f.ms[kPhases - 1]; }

// The last frames as stacked columns, the frame's time as a line.
void Graph(float em) {
    const auto &fr = g_perf.frames;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(ImGui::GetContentRegionAvail().x, em * 9);
    ImGui::InvisibleButton("graph", size);
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg));
    float budget = 1000.0f / std::max(1, g_perf.fps), top = budget * 1.5f;
    for (const auto &f : fr) top = std::max(top, Busy(f) * 1.1f);
    top = std::min(top, budget * 6); // a long hitch (loading) does not flatten the rest
    auto y = [&](float ms) { return p.y + size.y - std::min(ms, top) / top * size.y; };
    int n = (int)std::min(fr.size(), (size_t)std::max(1.0f, size.x));
    float w = size.x / std::max<size_t>(n, 1);
    size_t first = fr.size() - n;
    int hovered = -1;
    if (ImGui::IsItemHovered() && n > 0)
        hovered = std::clamp((int)((ImGui::GetIO().MousePos.x - p.x) / w), 0, n - 1);
    draw->PushClipRect(p, ImVec2(p.x + size.x, p.y + size.y), true);
    for (int i = 0; i < n; i++) {
        const PerfFrame &f = fr[first + i];
        float x0 = p.x + i * w, x1 = x0 + std::max(1.0f, w - (w > 3 ? 1 : 0)), at = 0;
        for (int ph = 0; ph < kPhases; ph++) {
            float y0 = y(at + f.ms[ph]), y1 = y(at);
            if (y1 - y0 >= 0.5f) draw->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), kPhaseColors[ph]);
            at += f.ms[ph];
        }
        if (f.skipped) draw->AddRectFilled(ImVec2(x0, p.y), ImVec2(x1, p.y + em * 0.3f), IM_COL32(230, 60, 60, 255));
        if (i == hovered) draw->AddRect(ImVec2(x0, p.y), ImVec2(x1, p.y + size.y), ImGui::GetColorU32(ImGuiCol_Text));
    }
    float by = y(budget);
    draw->AddLine(ImVec2(p.x, by), ImVec2(p.x + size.x, by), ImGui::GetColorU32(ImGuiCol_Text, 0.6f), 1.0f);
    char label[48];
    snprintf(label, sizeof(label), "%.1f ms (%d fps)", budget, g_perf.fps);
    draw->AddText(ImVec2(p.x + 4, by - em * 1.1f), ImGui::GetColorU32(ImGuiCol_Text, 0.8f), label);
    draw->PopClipRect();
    if (hovered >= 0) {
        const PerfFrame &f = fr[first + hovered];
        ImGui::BeginTooltip();
        ImGui::Text("%d frames ago: %.2f ms, %.2f busy%s", n - 1 - hovered, Total(f), Busy(f),
                    f.skipped ? ", not drawn" : "");
        for (int ph = 0; ph < kPhases; ph++) ImGui::Text("%-18s %6.2f ms", kPhaseNames[ph], f.ms[ph]);
        ImGui::Text("%-18s %6.2f ms", "  its capture", f.capture);
        ImGui::EndTooltip();
    }
}

} // namespace

void OnPerf(const Json &j) {
    PerfState &s = g_perf;
    s.known = true;
    s.none = j["none"].truthy();
    if (s.none || s.frozen) return;
    s.fps = j["fps"].i(60);
    s.lost += j["lost"].i();
    int values = j["values"].i(8);
    const auto &v = j["frames"].items;
    for (size_t at = 0; values >= kPhases + 2 && at + values <= v.size(); at += values) {
        PerfFrame f;
        for (int p = 0; p < kPhases; p++) f.ms[p] = (float)v[at + p].num() / 1000.0f;
        f.capture = (float)v[at + kPhases].num() / 1000.0f;
        f.skipped = v[at + kPhases + 1].i() != 0;
        s.frames.push_back(f);
    }
    while (s.frames.size() > kKeep) s.frames.pop_front();
}

void AppFrameTime(double ms) { g_appFrameMs = ms; }

void PerformanceWindow() {
    if (!BeginTool("Performance")) return;
    PerfState &s = g_perf;
    float em = ImGui::GetFontSize();
    double now = ImGui::GetTime();
    if (Connected() && now - s.polled > 0.25) {
        SendRaw("ikdbg.perf()");
        s.polled = now;
    }
    if (!Connected()) {
        ImGui::TextDisabled("Launch the game to measure its frames.");
    } else if (s.none) {
        ImGui::TextDisabled("Needs the patched Ikemen GO.");
    } else if (s.frames.empty()) {
        ImGui::TextDisabled("Measuring... (frames are measured during a match)");
    } else {
        // fps over the frames kept: their count over their time
        float total = 0;
        int skipped = 0;
        for (const auto &f : s.frames) {
            total += Total(f);
            skipped += f.skipped;
        }
        ImGui::Text("%.1f fps (the game asks %d), %zu frames", total > 0 ? s.frames.size() * 1000.0f / total : 0.0f,
                    s.fps, s.frames.size());
        ImGui::SameLine();
        if (skipped) ImGui::TextColored(Ink(kRed), " %d not drawn (late)", skipped);
        ImGui::SameLine(0, em * 1.5f);
        ImGui::Checkbox("Freeze", &s.frozen);
        ImGui::SameLine();
        if (ImGui::Button("Clear")) s.frames.clear(), s.lost = 0;
        HelpMarker("Measured in the engine while this window is shown: the end of each phase of a frame in the "
                   "match loop. A frame is late when its busy time goes past the line (the frame's time at the "
                   "game's speed); then the next one is not drawn. Run without the debugger's Game window, or "
                   "with the game's own window, to leave the capture out.");
        if (!s.frames.empty()) Graph(em);
        if (ImGui::BeginTable("phases", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Phase", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Average");
            ImGui::TableSetupColumn("Peak");
            ImGui::TableSetupColumn("Share");
            ImGui::TableHeadersRow();
            float budget = 1000.0f / std::max(1, s.fps);
            auto row = [&](const char *name, const char *help, ImU32 color, Stat st) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (color) {
                    ImVec2 q = ImGui::GetCursorScreenPos();
                    float h = ImGui::GetTextLineHeight();
                    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(q.x, q.y + h * 0.2f),
                                                              ImVec2(q.x + h * 0.6f, q.y + h * 0.8f), color);
                    ImGui::Dummy(ImVec2(h * 0.6f, h));
                    ImGui::SameLine();
                }
                ImGui::TextUnformatted(name);
                if (help) ImGui::SetItemTooltip("%s", help);
                ImGui::TableNextColumn();
                ImGui::Text("%6.2f ms", st.avg);
                ImGui::TableNextColumn();
                if (st.peak > budget) ImGui::TextColored(Ink(kRed), "%6.2f ms", st.peak);
                else ImGui::Text("%6.2f ms", st.peak);
                ImGui::TableNextColumn();
                ImGui::Text("%5.1f%%", st.avg / budget * 100);
            };
            for (int ph = 0; ph < kPhases; ph++)
                row(kPhaseNames[ph], kPhaseHelp[ph], kPhaseColors[ph],
                    StatOf(s.frames, [ph](const PerfFrame &f) { return f.ms[ph]; }));
            row("  its capture", "The debugger copying the frame for its Game window (in Show the frame).", 0,
                StatOf(s.frames, [](const PerfFrame &f) { return f.capture; }));
            row("Busy", "All but the wait.", 0, StatOf(s.frames, Busy));
            row("Whole frame", nullptr, 0, StatOf(s.frames, Total));
            ImGui::EndTable();
        }
        if (s.lost) ImGui::TextDisabled("%d frames dropped between two reads.", s.lost);
    }
    ImGui::SeparatorText("This program");
    ImGui::Text("%.1f fps, %.2f ms a frame; its windows %.2f ms", ImGui::GetIO().Framerate,
                1000.0f / std::max(1.0f, ImGui::GetIO().Framerate), g_appFrameMs);
    ImGui::SetItemTooltip("The debugger's own frames, apart from the game's: its windows' work (the rest of a "
                          "frame is drawing them and waiting for the display).");
    ImGui::End();
}

} // namespace app
