// tune.h -- TUNE("name", default) : a float read from <plugin>\data\tune.txt in a DEV build, re-read whenever the
// file's timestamp changes (polled at most every 250 ms) ; the bare default everywhere else.
//
// For visual tuning without a rebuild : write `rev.crest_w = 1.35` in data\tune.txt, save, watch the next frame.
// The value found by tuning then goes back into the code as the new default, and the TUNE() may stay (it costs
// nothing in a release) or be removed.
//
// RELEASE : AIOHUD_DEVTOOLS is defined only when the private dev tree is compiled in (build.bat, never CI /
// package.bat). Without it the macro is `(def)` -- the name string, the file name and the code do not exist in the
// DLL (checked : no "tune.txt" nor any TUNE name in build\release\AioHud.dll).
//
// DEV : each call site owns one static slot, registered on its first run ; a call is a GetTickCount + an index read.
// No heap, no per-frame file access. Implementation : dev/src/tune.cpp.
#pragma once

#ifdef AIOHUD_DEVTOOLS
namespace aio {
struct TuneSlot {
    int i;                                   // table index, -1 = table full (the default is returned, logged once)
    float d;                                 // the code default
    TuneSlot(const char* name, float def);
    float get() const;
};
}  // namespace aio
#define TUNE(name, def) ([]() -> float { static const ::aio::TuneSlot s_tune_(name, def); return s_tune_.get(); }())
#else
#define TUNE(name, def) (def)
#endif
