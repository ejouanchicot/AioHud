// fake_device.h -- a D3D8 device that draws nothing and remembers everything.
//
// WHY THIS EXISTS. src/ui is the biggest layer in the project (17 672 lines of .cpp) and, until this file,
// nothing automated ever looked at it: the offline suite covers the model, the in-game harness compares
// STATE against Windower, and neither one can see a quad. That is also why the audit of 2026-09-16 refused
// to split the seven remaining 400-line draw() functions -- cutting them would have traded readability for
// a visual risk nothing could catch.
//
// HOW IT WORKS. gfx never includes d3d8.h: every call goes through d3d.h's helpers, which read
// obj->vtbl[index] (windower.h, vfn) and call it __stdcall. So a "device" is just an address whose first
// dword points at a table of function pointers. This file builds one out of static memory and fills the
// slots gfx actually uses. No D3D, no window, no GPU -- the calls land in a log.
//
// WHAT IT CAN AND CANNOT PROVE. It records geometry and state, not pixels: it will not tell you a gradient
// is ugly. It WILL tell you that an edge is off the half-pixel grid (rule 1), that a blend was left
// additive (rule 3), that a texture was left bound (rule 8), or that a refactor changed one vertex out of
// four thousand (the trace is a golden, exactly like the session replays).
#pragma once
#include "windower.h"
#include <string.h>
#include <stdio.h>

namespace fakedev {

using windower::u32;

struct Vertex { float x, y, z, rhw; u32 color; float u, v; bool hasUV; };

struct Call {                 // one recorded device call, in order
    enum Kind { SetRS, SetTSS, SetTex, SetFVF, DrawUP } kind;
    u32 a, b, c, d;           // meaning depends on kind
};

const int MAX_CALLS = 4096;
const int MAX_VERTS = 8192;

struct Log {
    Call  call[MAX_CALLS];
    int   nCall;
    Vertex vert[MAX_VERTS];
    int   nVert;
    int   nDraw;
    // live state, as the device would hold it
    u32   rs[256];            // render states, indexed by state id (the ids gfx uses are all < 256)
    bool  rsSet[256];
    u32   tex0;               // texture bound on stage 0
    u32   fvf;
};

Log&  log();                  // the one log
void  reset();                // clear it before a case
u32   device();               // the address to pass as `dev`

// Helpers the assertions read.
u32   state(u32 rsId);                 // last value written to a render state (0 if never written)
bool  state_written(u32 rsId);
int   draws();                         // DrawPrimitiveUP calls
int   verts();                         // vertices submitted
const Vertex& vertex(int i);
u32   bound_texture();
// Everything the log holds, as one line per call -- the golden form.
void  trace(char* out, int cap);

} // namespace fakedev
