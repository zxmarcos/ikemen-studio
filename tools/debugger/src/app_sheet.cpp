// The sprite sheet tool of the Sprites window: an image loaded, an area dragged on it, the pieces found
// there (sheet.h) made a group of sprites of the file. It stays open for the next row of the sheet.
#include "app_internal.h"
#include "sheet.h"

#include <cfloat>
#include <cmath>
#include <set>

#include "imgui_internal.h"

namespace app {

namespace {

const char kSheetFilter[] = "Images (*.png, *.bmp, *.gif, *.jpg, *.pcx)\0*.png;*.bmp;*.gif;*.jpg;*.jpeg;*.pcx\0All files (*.*)\0*.*\0";

struct Sheet {
    std::string path;
    SheetImage img;
    GLuint tex = 0;
    SheetOptions opt;
    SheetCut cut;
    bool stale = false;  // opt changed: cut again
    std::set<int> left;  // pieces left out
    bool asked = false;  // the dialog opens
    bool fit = true;     // the view takes the whole image
    bool picking = false; // the next click takes the background's colour
    float zoom = 1;
    ImVec2 origin; // of the image, from the canvas's corner
    int group = 0, number = 0, axis = SFF_AXIS_FEET, pal = 0;
    std::string done; // what the last group was
    Sheet() { opt.gap = 2, opt.minPixels = 4; } // a detached hand is one with its sprite, a stray dot is not one
} g;

const int kPalModes[] = {SFF_PAL_ONE, SFF_PAL_SHARED, SFF_PAL_OWN};

bool Decode(const std::string &data, SheetImage &img) {
    if (DecodeImage((const unsigned char *)data.data(), data.size(), img.w, img.h, img.rgba)) return true;
    // WIC has no PCX
    std::vector<unsigned char> px, pal(1024);
    if (!Sff_DecodePcx(data, &img.w, &img.h, px, pal.data())) return false;
    img.rgba.assign(px.size() * 4, 0);
    for (size_t i = 0; i < px.size(); i++) {
        if (px[i] == 0) continue;
        memcpy(&img.rgba[i * 4], &pal[px[i] * 4], 3);
        img.rgba[i * 4 + 3] = 255;
    }
    return true;
}

void Upload() {
    if (!g.tex) glGenTextures(1, &g.tex);
    glBindTexture(GL_TEXTURE_2D, g.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // zoomed: the sheet's pixels
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g.img.w, g.img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, g.img.rgba.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

// With nothing transparent, the colour of its corner is the background, as Add PNG takes it for a BMP.
bool Load(const std::string &path) {
    SheetImage img;
    if (!Decode(ReadWholeFile(path), img) || img.w <= 0 || img.h <= 0) {
        Log(LogLine::Error, "Could not read the image " + path);
        return false;
    }
    g.path = path;
    g.img = std::move(img);
    Upload();
    SheetOptions fresh;
    fresh.gap = g.opt.gap, fresh.minPixels = g.opt.minPixels;
    fresh.useKey = !Sheet_HasTransparency(g.img);
    memcpy(fresh.key, g.img.rgba.data(), 3);
    g.opt = fresh;
    g.cut = SheetCut();
    g.left.clear();
    g.stale = false, g.fit = true, g.picking = false;
    g.done.clear();
    return true;
}

void MakeGroup(SpriteFile &sf) {
    std::vector<SffPicture> pictures;
    for (int i = 0; i < (int)g.cut.pieces.size(); i++) {
        if (g.left.count(i)) continue;
        SffPicture p;
        p.w = g.cut.pieces[i].w, p.h = g.cut.pieces[i].h;
        p.rgba = Sheet_Picture(g.img, g.cut, i);
        pictures.push_back(std::move(p));
    }
    std::set<std::string> said; // pieces of a sheet tend to say the same
    std::vector<SffSprite> made = Sff_SpritesOfPictures(sf.doc, pictures, kPalModes[g.pal], [&](const std::string &s) {
        if (said.insert(s).second) SpriteLog(s);
    });
    if (made.empty()) return;
    std::vector<int> added;
    int number = g.number;
    for (SffSprite &s : made) {
        while (Sff_FindSprite(sf.doc, g.group, number) >= 0) number++;
        s.group = g.group, s.number = number++;
        Sff_SetAxis(s, g.axis);
        sf.doc.sprites.push_back(s);
        added.push_back((int)sf.doc.sprites.size() - 1);
    }
    sf.picks.clear();
    SpritesSelect(sf, added[0]);
    if (added.size() > 1) sf.picks.insert(added.begin(), added.end());
    SpritesChanged(sf);
    g.done = "Group " + std::to_string(g.group) + " made: " + std::to_string(added.size()) + (added.size() == 1 ? " sprite." : " sprites.");
    Log(LogLine::Info, g.done);
    // The next row of the sheet
    g.group = FreeGroup(sf.doc), g.number = 0;
    g.opt.area = SheetRect();
    g.cut = SheetCut();
    g.left.clear();
}

// Drawn on the canvas: the image, what is out of the area dimmed, the pieces with the numbers they take.
void Canvas(float height) {
    float em = ImGui::GetFontSize();
    ImGuiIO &io = ImGui::GetIO();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool open = ImGui::BeginChild("canvas", ImVec2(-(em * 18 + ImGui::GetStyle().ItemSpacing.x), height), ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs);
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::EndChild();
        return;
    }
    ImVec2 p0 = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 1.0f), size.y = std::max(size.y, 1.0f);
    ImGui::InvisibleButton("c", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    Checkerboard(draw, p0, ImVec2(p0.x + size.x, p0.y + size.y));
    if (g.fit) {
        g.fit = false;
        g.zoom = std::min((size.x - em * 2) / g.img.w, (size.y - em * 2) / g.img.h);
        g.zoom = g.zoom >= 1 ? floorf(g.zoom) : std::max(g.zoom, 0.05f);
        g.origin = ImVec2(floorf((size.x - g.img.w * g.zoom) * 0.5f), floorf((size.y - g.img.h * g.zoom) * 0.5f));
    }
    if (hovered && io.MouseWheel != 0) {
        float before = g.zoom;
        g.zoom = std::max(0.05f, std::min(32.0f, g.zoom * powf(1.25f, io.MouseWheel)));
        ImVec2 m(io.MousePos.x - p0.x, io.MousePos.y - p0.y);
        g.origin = ImVec2(m.x - (m.x - g.origin.x) * g.zoom / before, m.y - (m.y - g.origin.y) * g.zoom / before);
    }
    if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0))) {
        g.origin.x += io.MouseDelta.x, g.origin.y += io.MouseDelta.y;
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    auto at = [&](float x, float y) { return ImVec2(floorf(p0.x + g.origin.x + x * g.zoom), floorf(p0.y + g.origin.y + y * g.zoom)); };
    float mx = (io.MousePos.x - p0.x - g.origin.x) / g.zoom, my = (io.MousePos.y - p0.y - g.origin.y) / g.zoom;
    int px = std::clamp((int)floorf(mx), 0, g.img.w - 1), py = std::clamp((int)floorf(my), 0, g.img.h - 1);
    draw->AddImage(TexRef(g.tex), at(0, 0), at((float)g.img.w, (float)g.img.h));

    // A click or a drag from the image
    static bool dragging;
    static int fromX, fromY;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (g.picking) {
            memcpy(g.opt.key, &g.img.rgba[((size_t)py * g.img.w + px) * 4], 3);
            g.opt.useKey = true;
            g.picking = false;
            g.stale = true;
        } else {
            dragging = true, fromX = px, fromY = py;
        }
    }
    if (hovered && g.picking) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    SheetRect rubber;
    rubber.x = std::min(fromX, px), rubber.y = std::min(fromY, py);
    rubber.w = std::abs(px - fromX) + 1, rubber.h = std::abs(py - fromY) + 1;
    if (dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        dragging = false;
        if (rubber.w > 2 && rubber.h > 2) {
            g.opt.area = rubber;
            g.stale = true;
        } else {
            // A click on a piece: left out, or back in
            for (int i = 0; i < (int)g.cut.pieces.size(); i++) {
                const SheetRect &r = g.cut.pieces[i];
                if (px < r.x || py < r.y || px >= r.x + r.w || py >= r.y + r.h) continue;
                if (!g.left.erase(i)) g.left.insert(i);
                break;
            }
        }
    }

    // What is out of the area, dimmed
    const SheetRect &area = g.cut.area.w > 0 ? g.cut.area : g.opt.area;
    if (area.w > 0) {
        ImVec2 a = at((float)area.x, (float)area.y), b = at((float)(area.x + area.w), (float)(area.y + area.h));
        ImVec2 lo = at(0, 0), hi = at((float)g.img.w, (float)g.img.h);
        ImU32 dim = IM_COL32(0, 0, 0, 130);
        draw->AddRectFilled(lo, ImVec2(hi.x, a.y), dim);
        draw->AddRectFilled(ImVec2(lo.x, b.y), hi, dim);
        draw->AddRectFilled(ImVec2(lo.x, a.y), ImVec2(a.x, b.y), dim);
        draw->AddRectFilled(ImVec2(b.x, a.y), ImVec2(hi.x, b.y), dim);
        draw->AddRect(a, b, IM_COL32(255, 230, 80, 255), 0.0f, 2.0f);
    }
    int number = 0, hot = -1;
    ImU32 kept = IM_COL32(90, 230, 130, 255), out = IM_COL32(255, 80, 80, 255);
    for (int i = 0; i < (int)g.cut.pieces.size(); i++) {
        const SheetRect &r = g.cut.pieces[i];
        bool left = g.left.count(i) != 0;
        ImVec2 a = at((float)r.x, (float)r.y), b = at((float)(r.x + r.w), (float)(r.y + r.h));
        bool over = hovered && !dragging && px >= r.x && py >= r.y && px < r.x + r.w && py < r.y + r.h;
        if (over && hot < 0) hot = i;
        if (left) draw->AddRectFilled(a, b, IM_COL32(255, 60, 60, 70));
        draw->AddRect(a, b, left ? out : kept, 0.0f, over ? 2.0f : 1.0f);
        if (left) continue;
        std::string label = std::to_string(number++);
        ImVec2 t = ImGui::CalcTextSize(label.c_str());
        draw->AddRectFilled(a, ImVec2(a.x + t.x + em * 0.4f, a.y + t.y), kept);
        draw->AddText(ImVec2(a.x + em * 0.2f, a.y), IM_COL32(20, 20, 24, 255), label.c_str());
    }
    if (hot >= 0) {
        const SheetRect &r = g.cut.pieces[hot];
        if (g.left.count(hot)) ImGui::SetTooltip("Left out: a click brings it back");
        else ImGui::SetTooltip("%d x %d. A click leaves it out", r.w, r.h);
    }
    if (dragging) {
        ImVec2 a = at((float)rubber.x, (float)rubber.y), b = at((float)(rubber.x + rubber.w), (float)(rubber.y + rubber.h));
        draw->AddRectFilled(a, b, IM_COL32(255, 230, 80, 40));
        draw->AddRect(a, b, IM_COL32(255, 230, 80, 255), 0.0f, 2.0f);
    }
    if (g.cut.area.w <= 0 && !dragging) {
        const char *hint = "Drag around the sprites of a row";
        ImVec2 t = ImGui::CalcTextSize(hint);
        draw->AddRectFilled(ImVec2(p0.x, p0.y), ImVec2(p0.x + t.x + em, p0.y + t.y + em * 0.5f), IM_COL32(0, 0, 0, 170));
        draw->AddText(ImVec2(p0.x + em * 0.5f, p0.y + em * 0.25f), IM_COL32(255, 255, 255, 255), hint);
    }
    ImGui::EndChild();
}

void Panel(SpriteFile &sf, float height, bool character) {
    float em = ImGui::GetFontSize();
    if (!ImGui::BeginChild("panel", ImVec2(0, height))) {
        ImGui::EndChild();
        return;
    }
    auto label = [](const char *text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", text);
        ImGui::SameLine(ImGui::GetFontSize() * 8.5f);
    };
    auto number = [&](const char *id, int *v) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        return ImGui::InputInt(id, v, 0);
    };
    size_t slash = g.path.find_last_of("/\\");
    ImGui::TextUnformatted(g.path.c_str() + (slash == std::string::npos ? 0 : slash + 1));
    ImGui::SetItemTooltip("%s", g.path.c_str());
    ImGui::TextDisabled("%d x %d", g.img.w, g.img.h);
    if (ImGui::Button(ICON_FOLDER_OPEN " Another...")) {
        std::vector<std::string> files = PickFiles(kSheetFilter, false);
        if (!files.empty()) Load(files[0]);
    }
    ImGui::SameLine();
    if (ImGui::Button("Fit")) g.fit = true;
    ImGui::SetItemTooltip("The whole image in view. Wheel: zoom; right or middle drag: pan.");

    ImGui::SeparatorText("Area");
    if (g.opt.area.w > 0) ImGui::Text("%d, %d  %d x %d", g.opt.area.x, g.opt.area.y, g.opt.area.w, g.opt.area.h);
    else ImGui::TextDisabled("Drag on the image.");
    if (ImGui::Button("The whole image")) {
        g.opt.area.x = g.opt.area.y = 0, g.opt.area.w = g.img.w, g.opt.area.h = g.img.h;
        g.stale = true;
    }

    ImGui::SeparatorText("Background");
    if (ImGui::Checkbox("This colour", &g.opt.useKey)) g.stale = true;
    ImGui::SetItemTooltip("Transparent pixels always are. For a sheet on a solid colour, pick that colour.");
    ImGui::SameLine();
    float box = ImGui::GetFrameHeight();
    ImGui::ColorButton("##key", ImVec4(g.opt.key[0] / 255.0f, g.opt.key[1] / 255.0f, g.opt.key[2] / 255.0f, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(box, box));
    ImGui::SameLine();
    bool picking = g.picking;
    if (picking) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (IconButton(ICON_DROPPER "##pick")) g.picking = !g.picking;
    if (picking) ImGui::PopStyleColor();
    ImGui::SetItemTooltip("Pick it: the next click on the image takes its colour");
    ImGui::BeginDisabled(!g.opt.useKey);
    label("Tolerance");
    if (number("##tolerance", &g.opt.tolerance)) g.opt.tolerance = std::clamp(g.opt.tolerance, 0, 255), g.stale = true;
    ImGui::SetItemTooltip("How far from it, in each channel, still counts: for a JPEG or a sheet with soft edges");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Pieces");
    label("Join across");
    if (number("##gap", &g.opt.gap)) g.opt.gap = std::clamp(g.opt.gap, 0, 64), g.stale = true;
    ImGui::SetItemTooltip("Parts with up to this many empty pixels between them are one piece: raise it when a sprite comes out in\n"
                          "pieces (a sword, a shadow), lower it when two sprites come out as one");
    label("Ignore under");
    if (number("##min", &g.opt.minPixels)) g.opt.minPixels = std::max(g.opt.minPixels, 1), g.stale = true;
    ImGui::SetItemTooltip("Pieces of fewer pixels than this are left out: stray dots");
    int pieces = (int)g.cut.pieces.size() - (int)g.left.size();
    if (g.cut.area.w > 0) {
        ImGui::Text("%d %s", pieces, pieces == 1 ? "piece" : "pieces");
        if (!g.left.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%d left out", (int)g.left.size());
        }
        if (g.cut.pieces.empty()) ImGui::TextColored(Ink(kOrange), "Nothing here but background.");
    }

    ImGui::SeparatorText("The new group");
    label("Group");
    number("##group", &g.group);
    g.group = std::max(g.group, 0);
    label("From number");
    number("##number", &g.number);
    g.number = std::max(g.number, 0);
    label("Axis");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::Combo("##axis", &g.axis, "At the feet\0In the centre\0Top left\0");
    label("Palette");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::Combo("##pal", &g.pal, character ? "One of their own\0The character's (1,1)\0Each its own\0" : "One of their own\0The shared one (1,1)\0Each its own\0");
    ImGui::SetItemTooltip("One of their own keeps the sheet's colours. The shared one maps each colour to the nearest of 1,1,\n"
                          "for sheets drawn on those colours: the sprites then change with the palette chosen.");
    int inGroup = 0;
    for (const SffSprite &s : sf.doc.sprites) inGroup += s.group == g.group;
    if (inGroup) ImGui::TextColored(Ink(kOrange), "It has %d sprites: the free numbers are taken.", inGroup);
    char make[48];
    snprintf(make, sizeof(make), ICON_CHECK " Make group %d", g.group);
    ImGui::BeginDisabled(pieces <= 0);
    bool go = ImGui::Button(make, ImVec2(-FLT_MIN, 0));
    ImGui::EndDisabled();
    go |= pieces > 0 && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::IsAnyItemActive();
    if (go) MakeGroup(sf);
    if (!g.done.empty()) {
        ImGui::TextColored(Ink(kGreen), "%s", g.done.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Drag another row, or Close. Save in the Sprites window writes the file.");
        ImGui::PopStyleColor();
    }
    if (ImGui::Button(ICON_XMARK " Close", ImVec2(-FLT_MIN, 0)) || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive()))
        ImGui::CloseCurrentPopup();
    ImGui::EndChild();
}

} // namespace

void SheetAsk(SpriteFile &sf) {
    std::vector<std::string> files = PickFiles(kSheetFilter, false);
    if (files.empty() || !Load(files[0])) return;
    g.group = FreeGroup(sf.doc), g.number = 0;
    g.asked = true;
}

void SheetDialog(SpriteFile &sf, bool character) {
    const char *title = ICON_IMAGE " Sprite sheet";
    if (g.asked) {
        g.asked = false;
        ImGui::OpenPopup(title);
    }
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * 0.92f, vp->WorkSize.y * 0.92f));
    ImGui::SetNextWindowBgAlpha(1.0f);
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) return;
    if (g.stale) {
        g.stale = false;
        g.cut = g.opt.area.w > 0 ? Sheet_Cut(g.img, g.opt) : SheetCut();
        g.left.clear();
    }
    float h = ImGui::GetContentRegionAvail().y;
    Canvas(h);
    ImGui::SameLine();
    Panel(sf, h, character);
    ImGui::EndPopup();
}

} // namespace app
