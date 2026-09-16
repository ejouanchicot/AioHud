// fake_device.cpp -- see fake_device.h.
//
// The vtable indices are the ones d3d.h calls by number (they are IDirect3DDevice8's, reversed):
//   8 GetDisplayMode · 40 SetViewport · 41 GetViewport · 50 SetRenderState · 57 CreateStateBlock
//   61 SetTexture · 63 SetTextureStageState · 72 DrawPrimitiveUP · 76 SetVertexShader
// Anything gfx calls that is not filled stays null, and vfn's valid_ptr check turns that into a no-op --
// which is the same thing that happens in game on a device that lost these entry points.
#include "fake_device.h"

namespace fakedev {

static Log g_log;
Log& log() { return g_log; }

// ---- the recorded entry points -------------------------------------------------------------------
static void push(Call::Kind k, u32 a, u32 b, u32 c, u32 d) {
    if (g_log.nCall >= MAX_CALLS) return;
    Call& c2 = g_log.call[g_log.nCall++];
    c2.kind = k; c2.a = a; c2.b = b; c2.c = c; c2.d = d;
}

static long __stdcall fd_SetRenderState(u32, u32 state, u32 value) {
    if (state < 256) { g_log.rs[state] = value; g_log.rsSet[state] = true; }
    push(Call::SetRS, state, value, 0, 0);
    return 0;
}
static long __stdcall fd_SetTextureStageState(u32, u32 stage, u32 type, u32 value) {
    push(Call::SetTSS, stage, type, value, 0);
    return 0;
}
static long __stdcall fd_SetTexture(u32, u32 stage, u32 tex) {
    if (stage == 0) g_log.tex0 = tex;
    push(Call::SetTex, stage, tex, 0, 0);
    return 0;
}
static long __stdcall fd_SetVertexShader(u32, u32 fvf) {
    g_log.fvf = fvf;
    push(Call::SetFVF, fvf, 0, 0, 0);
    return 0;
}
static long __stdcall fd_DrawPrimitiveUP(u32, u32 type, u32 count, const void* data, u32 stride) {
    // How many vertices a primitive count means, per topology (4 = list, 5 = strip, 6 = fan).
    const u32 n = (type == 4) ? count * 3 : count + 2;
    const unsigned char* p = (const unsigned char*)data;
    const bool hasUV = (stride >= 28);            // Vtx is 28 bytes (pos+rhw+colour+uv), VtxC is 20
    for (u32 i = 0; i < n && g_log.nVert < MAX_VERTS; ++i) {
        const float* f = (const float*)(p + i * stride);
        Vertex& v = g_log.vert[g_log.nVert++];
        v.x = f[0]; v.y = f[1]; v.z = f[2]; v.rhw = f[3];
        v.color = *(const u32*)(p + i * stride + 16);
        v.u = hasUV ? f[5] : 0.0f;
        v.v = hasUV ? f[6] : 0.0f;
        v.hasUV = hasUV;
    }
    ++g_log.nDraw;
    push(Call::DrawUP, type, count, stride, n);
    return 0;
}
static long __stdcall fd_GetViewport(u32, void* vp) {
    u32* p = (u32*)vp;                            // D3DVIEWPORT8 : X, Y, Width, Height, MinZ, MaxZ
    p[0] = 0; p[1] = 0; p[2] = 1920; p[3] = 1080;
    ((float*)vp)[4] = 0.0f; ((float*)vp)[5] = 1.0f;
    return 0;
}
static long __stdcall fd_SetViewport(u32, const void*)          { return 0; }
static long __stdcall fd_GetDisplayMode(u32, void* m) {
    u32* p = (u32*)m;                             // w, h, refresh, format (21 = A8R8G8B8)
    p[0] = 1920; p[1] = 1080; p[2] = 60; p[3] = 21;
    return 0;
}
static long __stdcall fd_CreateStateBlock(u32, u32, u32* token)  { if (token) *token = 0xB10C; return 0; }

// ---- the object -----------------------------------------------------------------------------------
// A device is an address whose first dword is the vtable pointer, so: one pointer-sized header followed
// by nothing. The table is static, which keeps it inside valid_ptr's window (>= 0x10000, < 0x80000000).
static void* g_vtbl[100];
struct FakeObj { void* vtbl; };
static FakeObj g_obj;

static void build_once() {
    static bool built = false;
    if (built) return;
    built = true;
    for (int i = 0; i < 100; ++i) g_vtbl[i] = 0;
    g_vtbl[8]  = (void*)&fd_GetDisplayMode;
    g_vtbl[40] = (void*)&fd_SetViewport;
    g_vtbl[41] = (void*)&fd_GetViewport;
    g_vtbl[50] = (void*)&fd_SetRenderState;
    g_vtbl[57] = (void*)&fd_CreateStateBlock;
    g_vtbl[61] = (void*)&fd_SetTexture;
    g_vtbl[63] = (void*)&fd_SetTextureStageState;
    g_vtbl[72] = (void*)&fd_DrawPrimitiveUP;
    g_vtbl[76] = (void*)&fd_SetVertexShader;
    g_obj.vtbl = (void*)g_vtbl;
}

u32 device() { build_once(); return (u32)(uintptr_t)&g_obj; }

void reset() {
    build_once();
    memset(&g_log, 0, sizeof(g_log));
}

u32  state(u32 rsId)          { return rsId < 256 ? g_log.rs[rsId] : 0u; }
bool state_written(u32 rsId)  { return rsId < 256 && g_log.rsSet[rsId]; }
int  draws()                  { return g_log.nDraw; }
int  verts()                  { return g_log.nVert; }
u32  bound_texture()          { return g_log.tex0; }
const Vertex& vertex(int i)   { return g_log.vert[i]; }

void trace(char* out, int cap) {
    int w = 0;
    out[0] = 0;
    for (int i = 0; i < g_log.nCall && w < cap - 80; ++i) {
        const Call& c = g_log.call[i];
        switch (c.kind) {
            case Call::SetRS:  w += _snprintf(out + w, cap - w, "rs %u=%u\n", c.a, c.b); break;
            case Call::SetTSS: w += _snprintf(out + w, cap - w, "tss %u.%u=%u\n", c.a, c.b, c.c); break;
            case Call::SetTex: w += _snprintf(out + w, cap - w, "tex %u=%u\n", c.a, c.b); break;
            case Call::SetFVF: w += _snprintf(out + w, cap - w, "fvf %u\n", c.a); break;
            case Call::DrawUP: w += _snprintf(out + w, cap - w, "draw t%u n%u s%u v%u\n", c.a, c.b, c.c, c.d); break;
        }
    }
    // the geometry itself, rounded to 1/16 px so a harmless float wobble is not a golden change
    for (int i = 0; i < g_log.nVert && w < cap - 80; ++i) {
        const Vertex& v = g_log.vert[i];
        w += _snprintf(out + w, cap - w, "v %.4f %.4f %08X\n", v.x, v.y, v.color);
    }
    if (w >= 0 && w < cap) out[w] = 0; else out[cap - 1] = 0;
}

} // namespace fakedev

// ---- texture stubs -------------------------------------------------------------------------------
// corner_mask.cpp uploads its baked quarter-discs through gfx/texture.cpp, which is a whole D3D
// resource lifecycle we do not want in a geometry test. Returning 0 is not a cop-out: it is the state
// the plugin is genuinely in before the masks exist (first frame, and after a device loss), and it is
// what makes rrect() take its GEOMETRIC path -- the feathered triangle fan the assertions describe.
// The masked path is a texture blit whose correctness is a pixel question, not a geometry one.
namespace aio {
unsigned make_texture_argb_mip(unsigned, int, int, const unsigned*) { return 0; }
void     release_texture(unsigned) {}
}
