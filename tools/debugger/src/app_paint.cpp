// Paint: the sprite's pixels edited on the Sprites window's preview. The picture being edited is
// kept here as palette indices; it goes back to the sprite when the mouse lets go, one step of undo.
// Indexed sprites only.
#include "app_internal.h"
#include "pixels.h"

#include <cmath>

namespace app {

enum Tool { kPen, kEraser, kBucket, kDropper, kSelect };

static struct {
    const SpriteFile *file = nullptr;
    int sprite = -1;
    unsigned version = 0;
    PixelGrid grid;
    bool indexed = false;
    GLuint tex = 0;
    bool stale = true; // the texture is behind the picture
} g_paint;

static int g_tool = kPen, g_size = 1, g_colour = 1;
static bool g_contiguous = true;
static PixelRect g_sel;     // the selection, in the sprite's pixels
static PixelGrid g_clip;    // Copy
static PixelGrid g_lifted;  // the selection's pixels while they are dragged
static PixelRect g_rubber;  // a selection being drawn
static int g_lastX, g_lastY; // where the stroke was in the last frame
static int g_startX, g_startY, g_liftX, g_liftY;
static enum { None, Stroke, Rubber, Lift } g_drag = None;

// The sprite's picture, read when it is another sprite or changed from elsewhere
static void Load(SpriteFile &sf) {
    if (g_paint.file == &sf && g_paint.sprite == sf.sel && g_paint.version == sf.version) return;
    g_sel = PixelRect();
    g_paint.file = &sf, g_paint.sprite = sf.sel, g_paint.version = sf.version;
    g_paint.indexed = false;
    g_paint.stale = true;
    g_drag = None;
    if (sf.sel < 0 || sf.sel >= (int)sf.doc.sprites.size()) return;
    const SffSprite &s = sf.doc.sprites[sf.sel];
    std::vector<unsigned char> px;
    int bpp = 0;
    if (!Sff_Decode(s, px, &bpp) || bpp != 1 || s.pal < 0) return;
    g_paint.grid.w = s.w, g_paint.grid.h = s.h;
    g_paint.grid.px.swap(px);
    g_paint.indexed = true;
}

// The picture back in the sprite: a step of undo
static void Commit(SpriteFile &sf) {
    SffSprite &s = sf.doc.sprites[sf.sel];
    Sff_SetPixels(s, g_paint.grid.w, g_paint.grid.h, g_paint.grid.px, 1);
    SpritesChanged(sf);
    g_paint.version = sf.version;
}

// The whole picture, or the selection when there is one
static PixelRect Target() {
    if (!g_sel.Empty()) return g_sel;
    PixelRect all;
    all.w = g_paint.grid.w, all.h = g_paint.grid.h;
    return all;
}

bool PaintCanvas(SpriteFile &sf, const PalChoice &view, ImDrawList *draw, ImVec2 at, float zoom, bool hovered, ImVec2 clipA, ImVec2 clipB) {
    Load(sf);
    if (!g_paint.indexed) return false;
    PixelGrid &g = g_paint.grid;
    ImGuiIO &io = ImGui::GetIO();
    int px = (int)floorf((io.MousePos.x - at.x) / zoom), py = (int)floorf((io.MousePos.y - at.y) / zoom);
    bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        g_lastX = g_startX = px, g_lastY = g_startY = py;
        switch (g_tool) {
        case kPen:
        case kEraser:
            g_drag = Stroke;
            Px_Line(g, px, py, px, py, g_size, g_tool == kPen ? (unsigned char)g_colour : 0);
            g_paint.stale = true;
            break;
        case kBucket:
            if (g.Inside(px, py)) {
                Px_Fill(g, px, py, (unsigned char)g_colour, g_contiguous);
                Commit(sf);
                g_paint.stale = true;
            }
            break;
        case kDropper:
            if (g.Inside(px, py) && g.At(px, py) != 0) g_colour = g.At(px, py);
            break;
        case kSelect:
            if (g_sel.Contains(px, py)) {
                g_drag = Lift;
                g_lifted = Px_Copy(g, g_sel);
                g_liftX = g_sel.x, g_liftY = g_sel.y;
                Px_Clear(g, g_sel);
                g_paint.stale = true;
            } else {
                g_drag = Rubber;
                g_sel = PixelRect();
            }
            break;
        }
    }
    if (g_drag == Stroke && (px != g_lastX || py != g_lastY)) {
        Px_Line(g, g_lastX, g_lastY, px, py, g_size, g_tool == kPen ? (unsigned char)g_colour : 0);
        g_lastX = px, g_lastY = py;
        g_paint.stale = true;
    }
    if (g_drag == Rubber) {
        g_rubber.x = std::min(g_startX, px), g_rubber.y = std::min(g_startY, py);
        g_rubber.w = abs(px - g_startX) + 1, g_rubber.h = abs(py - g_startY) + 1;
    }
    if (g_drag == Lift) g_sel.x = g_liftX + px - g_startX, g_sel.y = g_liftY + py - g_startY;
    if (g_drag != None && !down) {
        if (g_drag == Stroke) Commit(sf);
        if (g_drag == Lift) {
            Px_Paste(g, g_lifted, g_sel.x, g_sel.y);
            g_sel = Px_Inside(g, g_sel);
            Commit(sf);
            g_paint.stale = true;
        }
        if (g_drag == Rubber) {
            g_sel = Px_Inside(g, g_rubber);
            if (g_sel.w <= 1 && g_sel.h <= 1) g_sel = PixelRect(); // a click takes it away
        }
        g_drag = None;
    }

    // The picture with the colours of the palette seen
    if (g_paint.stale) {
        const std::vector<unsigned char> *cols = PalColours(sf, PalFor(sf, view, sf.sel));
        SffSprite s = sf.doc.sprites[sf.sel];
        s.w = g.w, s.h = g.h;
        std::vector<unsigned char> rgba;
        Sff_ToRgbaWith(s, g.px, 1, rgba, cols ? cols->data() : nullptr);
        if (!g_paint.tex) glGenTextures(1, &g_paint.tex);
        glBindTexture(GL_TEXTURE_2D, g_paint.tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g.w, g.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        g_paint.stale = false;
    }
    ImVec2 end(at.x + g.w * zoom, at.y + g.h * zoom);
    draw->AddImage(TexRef(g_paint.tex), at, end);
    draw->AddRect(at, end, IM_COL32(255, 255, 255, 60));
    if (zoom >= 6) {
        // The pixels' grid, the part in view
        ImU32 col = IM_COL32(255, 255, 255, 28);
        for (int x = std::max(0, (int)((clipA.x - at.x) / zoom)); x <= g.w && at.x + x * zoom <= clipB.x; x++)
            draw->AddLine(ImVec2(at.x + x * zoom, at.y), ImVec2(at.x + x * zoom, end.y), col);
        for (int y = std::max(0, (int)((clipA.y - at.y) / zoom)); y <= g.h && at.y + y * zoom <= clipB.y; y++)
            draw->AddLine(ImVec2(at.x, at.y + y * zoom), ImVec2(end.x, at.y + y * zoom), col);
    }
    // What is being dragged, and the selection
    const std::vector<unsigned char> *cols = PalColours(sf, PalFor(sf, view, sf.sel));
    if (g_drag == Lift && cols) {
        for (int y = 0; y < g_lifted.h; y++)
            for (int x = 0; x < g_lifted.w; x++) {
                unsigned char v = g_lifted.At(x, y);
                if (!v) continue;
                const unsigned char *c = &(*cols)[v * 4];
                ImVec2 a(at.x + (g_sel.x + x) * zoom, at.y + (g_sel.y + y) * zoom);
                draw->AddRectFilled(a, ImVec2(a.x + zoom, a.y + zoom), IM_COL32(c[0], c[1], c[2], 255));
            }
    }
    const PixelRect &r = g_drag == Rubber ? g_rubber : g_sel;
    if (!r.Empty()) {
        ImVec2 a(at.x + r.x * zoom, at.y + r.y * zoom), b(a.x + r.w * zoom, a.y + r.h * zoom);
        draw->AddRect(a, b, IM_COL32(0, 0, 0, 255), 0, 3.0f);
        draw->AddRect(a, b, IM_COL32(255, 255, 255, 255));
    }
    // Where the pen would go
    if (hovered && g_drag == None && (g_tool == kPen || g_tool == kEraser)) {
        int x0 = px - (g_size - 1) / 2, y0 = py - (g_size - 1) / 2;
        ImVec2 a(at.x + x0 * zoom, at.y + y0 * zoom);
        draw->AddRect(a, ImVec2(a.x + g_size * zoom, a.y + g_size * zoom), IM_COL32(255, 255, 255, 200));
    }
    if (hovered) ImGui::SetMouseCursor(g_tool == kSelect && g_sel.Contains(px, py) ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_Hand);
    return true;
}

void PaintRefresh() { g_paint.stale = true; }

void PaintPanel(SpriteFile &sf, const PalChoice &view) {
    Load(sf);
    float em = ImGui::GetFontSize();
    const ImGuiStyle &style = ImGui::GetStyle();
    if (!g_paint.indexed) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Only sprites with a palette can be painted here.");
        ImGui::PopStyleColor();
        return;
    }
    // The tools
    static const struct {
        const char *icon, *tip;
        int tool;
    } kTools[] = {{ICON_PEN, "Pen", kPen},
                  {ICON_ERASER, "Eraser", kEraser},
                  {ICON_FILL, "Bucket", kBucket},
                  {ICON_DROPPER, "Pick a colour of the picture", kDropper},
                  {ICON_SELECT, "Select a rectangle; drag it to move its pixels", kSelect}};
    for (const auto &t : kTools) {
        if (t.tool != kPen) ImGui::SameLine(0, 2);
        bool on = g_tool == t.tool;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(t.icon, ImVec2(em * 2, 0))) g_tool = t.tool;
        if (on) ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", t.tip);
    }
    if (g_tool == kPen || g_tool == kEraser) {
        ImGui::SetNextItemWidth(em * 8);
        ImGui::SliderInt("Size", &g_size, 1, 32);
    }
    if (g_tool == kBucket) ImGui::Checkbox("Only what touches", &g_contiguous);

    // The colours
    const std::vector<unsigned char> *cols = PalColours(sf, PalFor(sf, view, sf.sel));
    if (cols && cols->size() >= 1024) {
        float cell = floorf(std::min(ImGui::GetContentRegionAvail().x / 16, em * 1.1f));
        int ncol = 256;
        const SffSprite &s = sf.doc.sprites[sf.sel];
        if (s.pal >= 0 && s.pal < (int)sf.doc.pals.size()) ncol = std::max(2, std::min(256, sf.doc.pals[s.pal].ncolors));
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("paintcolours", ImVec2(cell * 16, cell * ((ncol + 15) / 16)));
        ImVec2 m = ImGui::GetMousePos();
        int hx = (int)((m.x - p0.x) / cell), hy = (int)((m.y - p0.y) / cell), hot = hy * 16 + hx;
        if (ImGui::IsItemActive() && hx >= 0 && hx < 16 && hy >= 0 && hot >= 1 && hot < ncol) g_colour = hot;
        ImDrawList *draw = ImGui::GetWindowDrawList();
        for (int i = 0; i < ncol; i++) {
            ImVec2 a(p0.x + (i % 16) * cell, p0.y + (i / 16) * cell), b(a.x + cell, a.y + cell);
            const unsigned char *c = &(*cols)[i * 4];
            if (i == 0) Checkerboard(draw, a, b);
            else draw->AddRectFilled(a, b, IM_COL32(c[0], c[1], c[2], 255));
            if (i == g_colour) {
                draw->AddRect(a, b, IM_COL32(0, 0, 0, 255), 0, 3.0f);
                draw->AddRect(a, b, IM_COL32(255, 230, 80, 255));
            }
        }
        if (ImGui::IsItemHovered() && hot >= 0 && hot < ncol && hx < 16) ImGui::SetTooltip("Colour %d", hot);
    }
    ImGui::Spacing();

    // On the selection, or the whole picture
    bool some = !g_sel.Empty();
    ImGui::TextDisabled(some ? "On the selection" : "On the whole picture");
    auto change = [&](const std::function<void()> &f) {
        f();
        Commit(sf);
        g_paint.stale = true;
    };
    auto fits = [&](const char *label) { SameLineFits(ImGui::CalcTextSize(label).x + style.FramePadding.x * 2); };
    if (ImGui::Button("Flip H")) change([] { Px_FlipH(g_paint.grid, Target()); });
    fits("Flip V");
    if (ImGui::Button("Flip V")) change([] { Px_FlipV(g_paint.grid, Target()); });
    fits("Clear");
    if (ImGui::Button("Clear")) change([] { Px_Clear(g_paint.grid, Target()); });
    fits("Copy");
    if (ImGui::Button("Copy")) g_clip = Px_Copy(g_paint.grid, Target());
    ImGui::SetItemTooltip("Its pixels, to paste here or on another sprite with the same palette");
    fits("Paste");
    ImGui::BeginDisabled(g_clip.px.empty());
    if (ImGui::Button("Paste")) {
        PixelRect place = Target();
        place.w = g_clip.w, place.h = g_clip.h;
        change([&] { Px_Paste(g_paint.grid, g_clip, place.x, place.y); });
        g_sel = Px_Inside(g_paint.grid, place);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("At the selection's corner; with the Select tool it can be dragged from there");
    if (ImGui::Button("More room")) {
        const int room = 8;
        SffSprite &s = sf.doc.sprites[sf.sel];
        s.x += room, s.y += room; // the picture stays where it is on the axis
        g_paint.grid = Px_Grow(g_paint.grid, room, room, room, room);
        g_sel = PixelRect();
        Commit(sf);
        g_paint.stale = true;
    }
    ImGui::SetItemTooltip("Eight empty pixels around it, to draw past its edges (Crop takes off what stays empty)");
}

} // namespace app
