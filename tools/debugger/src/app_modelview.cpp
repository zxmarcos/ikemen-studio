// A glTF model drawn by the debugger itself (the Model window's preview, the game not needed):
// OpenGL's fixed pipeline into a framebuffer of its own, the textures decoded by Windows (WIC).
// The look is the engine's in its broad lines (base colour and texture, emission, alpha mask and
// blend, Ikemen's add / sub / mul, both faces, unlit when the stage has no light), not its
// shading.
#include "app_internal.h"

#include <wincodec.h>

#include <cmath>

#include "gltf.h"

namespace app {

namespace {

// OpenGL past 1.1: framebuffers and the blend equation, from the driver
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_MIRRORED_REPEAT 0x8370
#define GL_GENERATE_MIPMAP 0x8191
#define GL_FUNC_ADD 0x8006
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
typedef void(APIENTRY *GenFn)(GLsizei, GLuint *);
typedef void(APIENTRY *BindFn)(GLenum, GLuint);
typedef void(APIENTRY *FbTexFn)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void(APIENTRY *RbStorageFn)(GLenum, GLenum, GLsizei, GLsizei);
typedef void(APIENTRY *FbRbFn)(GLenum, GLenum, GLenum, GLuint);
typedef GLenum(APIENTRY *StatusFn)(GLenum);
typedef void(APIENTRY *EquationFn)(GLenum);
struct Gl {
    bool tried = false, ok = false;
    GenFn genFramebuffers = nullptr, genRenderbuffers = nullptr;
    BindFn bindFramebuffer = nullptr, bindRenderbuffer = nullptr;
    FbTexFn framebufferTexture2D = nullptr;
    RbStorageFn renderbufferStorage = nullptr;
    FbRbFn framebufferRenderbuffer = nullptr;
    StatusFn checkFramebufferStatus = nullptr;
    EquationFn blendEquation = nullptr;
} g_gl;

template <class T> T Proc(const char *name) {
    PROC p = wglGetProcAddress(name);
    if (!p) p = wglGetProcAddress((std::string(name) + "EXT").c_str());
    return (T)(void *)p;
}

bool LoadGl() {
    if (g_gl.tried) return g_gl.ok;
    g_gl.tried = true;
    g_gl.genFramebuffers = Proc<GenFn>("glGenFramebuffers");
    g_gl.genRenderbuffers = Proc<GenFn>("glGenRenderbuffers");
    g_gl.bindFramebuffer = Proc<BindFn>("glBindFramebuffer");
    g_gl.bindRenderbuffer = Proc<BindFn>("glBindRenderbuffer");
    g_gl.framebufferTexture2D = Proc<FbTexFn>("glFramebufferTexture2D");
    g_gl.renderbufferStorage = Proc<RbStorageFn>("glRenderbufferStorage");
    g_gl.framebufferRenderbuffer = Proc<FbRbFn>("glFramebufferRenderbuffer");
    g_gl.checkFramebufferStatus = Proc<StatusFn>("glCheckFramebufferStatus");
    g_gl.blendEquation = Proc<EquationFn>("glBlendEquation");
    g_gl.ok = g_gl.genFramebuffers && g_gl.genRenderbuffers && g_gl.bindFramebuffer && g_gl.bindRenderbuffer &&
              g_gl.framebufferTexture2D && g_gl.renderbufferStorage && g_gl.framebufferRenderbuffer &&
              g_gl.checkFramebufferStatus;
    return g_gl.ok;
}

// The picture drawn into: a colour texture and a depth buffer, made again when its size changes
struct Target {
    GLuint fbo = 0, color = 0, depth = 0;
    int w = 0, h = 0;
} g_target;

bool MakeTarget(int w, int h) {
    Target &t = g_target;
    if (t.fbo && t.w == w && t.h == h) return true;
    if (!t.fbo) {
        g_gl.genFramebuffers(1, &t.fbo);
        g_gl.genRenderbuffers(1, &t.depth);
        glGenTextures(1, &t.color);
    }
    t.w = w, t.h = h;
    glBindTexture(GL_TEXTURE_2D, t.color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    g_gl.bindRenderbuffer(GL_RENDERBUFFER, t.depth);
    g_gl.renderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    g_gl.bindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    g_gl.framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0);
    g_gl.framebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depth);
    bool ok = g_gl.checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    g_gl.bindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}

// The model's textures on the GPU, for one model read (its version)
struct Textures {
    unsigned version = 0;
    std::vector<GLuint> ids; // by image
} g_textures;

void UploadTextures(const GltfDoc &doc, unsigned version) {
    Textures &t = g_textures;
    if (t.version == version && t.ids.size() == doc.images.size()) return;
    if (!t.ids.empty()) glDeleteTextures((GLsizei)t.ids.size(), t.ids.data());
    t.ids.assign(doc.images.size(), 0);
    t.version = version;
    for (size_t i = 0; i < doc.images.size(); i++) {
        const GltfImage &im = doc.images[i];
        if (im.rgba.empty()) continue;
        glGenTextures(1, &t.ids[i]);
        glBindTexture(GL_TEXTURE_2D, t.ids[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, im.w, im.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, im.rgba.data());
    }
}

GLint Wrap(int gltf) { return gltf == 33071 ? GL_CLAMP_TO_EDGE : gltf == 33648 ? GL_MIRRORED_REPEAT : GL_REPEAT; }

} // namespace

// ---------------------------------------------------------------------------

bool DecodeImage(const unsigned char *data, size_t size, int &w, int &h, std::vector<unsigned char> &rgba) {
    static IWICImagingFactory *factory;
    if (!factory) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
            return false;
    }
    IWICStream *stream = nullptr;
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICFormatConverter *conv = nullptr;
    bool ok = SUCCEEDED(factory->CreateStream(&stream)) &&
              SUCCEEDED(stream->InitializeFromMemory((BYTE *)data, (DWORD)size)) &&
              SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
              SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&conv)) &&
              SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                         WICBitmapPaletteTypeCustom));
    UINT uw = 0, uh = 0;
    if (ok) ok = SUCCEEDED(conv->GetSize(&uw, &uh)) && uw > 0 && uh > 0;
    if (ok) {
        w = (int)uw, h = (int)uh;
        rgba.resize((size_t)w * h * 4);
        ok = SUCCEEDED(conv->CopyPixels(nullptr, w * 4, (UINT)rgba.size(), rgba.data()));
    }
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    return ok;
}

GltfMat Perspective(float fovy, float aspect, float zNear, float zFar) {
    float f = 1 / tanf(fovy / 2);
    GltfMat m{};
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zFar + zNear) / (zNear - zFar);
    m[11] = -1;
    m[14] = 2 * zFar * zNear / (zNear - zFar);
    return m;
}

GltfMat LookAt(const float eye[3], const float at[3]) {
    float f[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    float len = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (len <= 0) return Gltf_Identity();
    for (float &v : f) v /= len;
    float up[3] = {0, 1, 0};
    float s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
    float sl = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    if (sl < 1e-6f) s[0] = 1, s[1] = s[2] = 0, sl = 1;
    for (float &v : s) v /= sl;
    float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    GltfMat m = {s[0], u[0], -f[0], 0, s[1], u[1], -f[1], 0, s[2], u[2], -f[2], 0, 0, 0, 0, 1};
    m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    m[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
    return m;
}

bool ModelPreviewPixels(const ModelPreviewArgs &a, std::vector<unsigned char> &rgba) {
    unsigned tex = DrawModelPreview(a);
    if (!tex) return false;
    std::vector<unsigned char> flipped((size_t)a.w * a.h * 4);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
    rgba.resize(flipped.size());
    size_t row = (size_t)a.w * 4;
    for (int y = 0; y < a.h; y++) memcpy(&rgba[y * row], &flipped[(a.h - 1 - y) * row], row);
    for (size_t n = 3; n < rgba.size(); n += 4) rgba[n] = 255;
    return true;
}

unsigned DrawModelPreview(const ModelPreviewArgs &a) {
    if (!LoadGl() || a.w <= 0 || a.h <= 0 || !a.doc || !MakeTarget(a.w, a.h)) return 0;
    const GltfDoc &doc = *a.doc;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadMatrixf(a.proj.data());
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    UploadTextures(doc, a.version);
    g_gl.bindFramebuffer(GL_FRAMEBUFFER, g_target.fbo);
    glViewport(0, 0, a.w, a.h);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.16f, 0.17f, 0.19f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_NORMALIZE);
    glShadeModel(GL_SMOOTH);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    // Lights: the model's (up to 4), in the world, with a little ambient light
    glLoadMatrixf(a.view.data());
    float ambient[4] = {a.lighting == 2 ? 0.55f : 0.25f, a.lighting == 2 ? 0.55f : 0.25f, a.lighting == 2 ? 0.55f : 0.25f, 1};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
    int used = 0;
    for (int i = 0; i < (int)doc.nodes.size() && used < 4 && a.lighting; i++) {
        const GltfNode &n = doc.nodes[i];
        if (n.light < 0 || n.light >= (int)doc.lights.size()) continue;
        const GltfLight &l = doc.lights[n.light];
        const GltfMat &w = (*a.world)[i];
        GLenum id = GL_LIGHT0 + used++;
        float k = std::min(1.0f, std::max(0.0f, l.intensity));
        float col[4] = {l.rgb[0] * k, l.rgb[1] * k, l.rgb[2] * k, 1}, black[4] = {0, 0, 0, 1};
        float pos[4] = {w[12], w[13], w[14], 1};
        if (l.type == 0) pos[0] = w[8], pos[1] = w[9], pos[2] = w[10], pos[3] = 0; // towards the light: its +Z
        glEnable(id);
        glLightfv(id, GL_DIFFUSE, col);
        glLightfv(id, GL_SPECULAR, black);
        glLightfv(id, GL_AMBIENT, black);
        glLightfv(id, GL_POSITION, pos);
        glLightf(id, GL_SPOT_CUTOFF, l.type == 2 ? std::min(90.0f, l.outer * 57.29578f) : 180.0f);
        if (l.type == 2) {
            float dir[3] = {-w[8], -w[9], -w[10]};
            glLightfv(id, GL_SPOT_DIRECTION, dir);
        }
    }
    if (a.lighting == 2 && used == 0) {
        // An environment and no light: one from above the camera
        glLoadIdentity();
        float col[4] = {0.6f, 0.6f, 0.6f, 1}, pos[4] = {0.3f, 1, 0.6f, 0};
        glEnable(GL_LIGHT0);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, col);
        glLightfv(GL_LIGHT0, GL_POSITION, pos);
    }
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    glEnableClientState(GL_VERTEX_ARRAY);
    // Opaque first, then what blends (without writing depth)
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < (int)doc.nodes.size(); i++) {
            const GltfNode &n = doc.nodes[i];
            if (n.mesh < 0 || (i < (int)a.visible->size() && !(*a.visible)[i])) continue;
            const GltfMat &w = (*a.world)[i];
            GltfMat mv = Gltf_Mul(a.view, w);
            glLoadMatrixf(mv.data());
            float det = w[0] * (w[5] * w[10] - w[6] * w[9]) - w[4] * (w[1] * w[10] - w[2] * w[9]) + w[8] * (w[1] * w[6] - w[2] * w[5]);
            glFrontFace(det < 0 ? GL_CW : GL_CCW);
            for (const GltfPrim &p : doc.meshes[n.mesh].prims) {
                const GltfMaterial *m = p.material >= 0 && p.material < (int)doc.materials.size() ? &doc.materials[p.material] : nullptr;
                if (!m) continue; // as the engine: no material, not drawn
                bool blends = m->alpha == 2 || n.trans != 0;
                if (blends != (pass == 1)) continue;
                bool lit = a.lighting && !m->unlit;
                if (lit) glEnable(GL_LIGHTING);
                else glDisable(GL_LIGHTING);
                if (lit) glEnable(GL_COLOR_MATERIAL);
                else glDisable(GL_COLOR_MATERIAL);
                float em[4] = {m->emission[0], m->emission[1], m->emission[2], 1};
                glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, em);
                glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, m->doubleSided ? 1 : 0);
                // Unlit: its colour, plus its emission
                float c[4] = {m->color[0], m->color[1], m->color[2], m->color[3]};
                if (!lit)
                    for (int k = 0; k < 3; k++) c[k] = std::min(1.0f, c[k] + m->emission[k]);
                glColor4fv(c);
                if (m->doubleSided) glDisable(GL_CULL_FACE);
                else glEnable(GL_CULL_FACE), glCullFace(GL_BACK);
                GLuint tex = 0;
                if (m->baseTexture >= 0 && m->baseTexture < (int)doc.textures.size() && !p.uv.empty()) {
                    const GltfTexture &t = doc.textures[m->baseTexture];
                    if (t.image >= 0 && t.image < (int)g_textures.ids.size()) tex = g_textures.ids[t.image];
                    if (tex) {
                        glBindTexture(GL_TEXTURE_2D, tex);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, Wrap(t.wrapS));
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, Wrap(t.wrapT));
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, t.nearest ? GL_NEAREST : GL_LINEAR);
                    }
                }
                if (tex) glEnable(GL_TEXTURE_2D);
                else glDisable(GL_TEXTURE_2D);
                if (m->alpha == 1) glEnable(GL_ALPHA_TEST), glAlphaFunc(GL_GEQUAL, m->cutoff);
                else glDisable(GL_ALPHA_TEST);
                if (pass == 1) {
                    glEnable(GL_BLEND);
                    glDepthMask(GL_FALSE);
                    if (g_gl.blendEquation) g_gl.blendEquation(n.trans == 2 ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
                    if (n.trans == 1 || n.trans == 2) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                    else if (n.trans == 3) glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
                    else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                } else {
                    glDisable(GL_BLEND);
                    glDepthMask(GL_TRUE);
                }
                glDepthFunc(n.ztest ? GL_LEQUAL : GL_ALWAYS);
                if (pass == 0) glDepthMask(n.zwrite ? GL_TRUE : GL_FALSE);
                glVertexPointer(3, GL_FLOAT, 0, p.pos.data());
                if (!p.nrm.empty()) glEnableClientState(GL_NORMAL_ARRAY), glNormalPointer(GL_FLOAT, 0, p.nrm.data());
                else glDisableClientState(GL_NORMAL_ARRAY), glNormal3f(0, 1, 0);
                if (tex) glEnableClientState(GL_TEXTURE_COORD_ARRAY), glTexCoordPointer(2, GL_FLOAT, 0, p.uv.data());
                else glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                GLenum mode = p.mode == 0 ? GL_POINTS : p.mode == 1 ? GL_LINES : p.mode == 2 ? GL_LINE_LOOP
                            : p.mode == 3 ? GL_LINE_STRIP : GL_TRIANGLES;
                if (!p.idx.empty()) glDrawElements(mode, (GLsizei)p.idx.size(), GL_UNSIGNED_INT, p.idx.data());
            }
        }
    }
    if (g_gl.blendEquation) g_gl.blendEquation(GL_FUNC_ADD);

    // The boxes of the nodes selected and under the mouse, over everything
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLoadMatrixf(a.view.data());
    for (int which = 0; which < 2; which++) {
        int node = which == 0 ? a.hover : a.selected;
        if (node < 0 || (which == 0 && a.hover == a.selected)) continue;
        float c[8][3];
        if (!Gltf_Corners(doc, *a.world, node, c)) continue;
        glLineWidth(which == 0 ? 1.0f : 2.0f);
        if (which == 0) glColor4f(1, 1, 1, 0.8f);
        else glColor4f(1, 0.86f, 0.24f, 1);
        glBegin(GL_LINES);
        static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto &e : edges) glVertex3fv(c[e[0]]), glVertex3fv(c[e[1]]);
        glEnd();
    }

    g_gl.bindFramebuffer(GL_FRAMEBUFFER, 0);
    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopClientAttrib();
    glPopAttrib();
    return g_target.color;
}

} // namespace app
