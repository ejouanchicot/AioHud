// native_ui_undo.cpp -- see native_ui_undo.h.
#include "model/native_ui_undo.h"

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace aio {
namespace undo {

static const char PREFIX[] = "native_ui_undo";

void path_for(char* out, int n, const char* dir, unsigned pid) {
    _snprintf(out, (size_t)n, "%s\\%s_%u.txt", dir, PREFIX, pid);
    out[n - 1] = 0;
}

bool pid_of(const char* fileName, unsigned& pid) {
    pid = 0;
    const size_t pl = sizeof(PREFIX) - 1;
    if (_strnicmp(fileName, PREFIX, pl) != 0) return false;
    const char* r = fileName + pl;
    if (!_stricmp(r, ".txt")) return true;                 // the legacy shared file : nobody owns it any more
    if (*r != '_') return false;
    ++r;
    unsigned v = 0; int digits = 0;
    while (*r >= '0' && *r <= '9' && digits < 10) { v = v * 10 + (unsigned)(*r - '0'); ++r; ++digits; }
    if (!digits || _stricmp(r, ".txt") != 0 || !v) return false;
    pid = v;
    return true;
}

static void say(const Hooks& h, const char* fmt, ...) {
    if (!h.log) return;
    char b[384];
    va_list a; va_start(a, fmt); _vsnprintf(b, sizeof(b), fmt, a); va_end(a);
    b[sizeof(b) - 1] = 0;
    h.log(b, h.u);
}

// the files this client may recover, through a callback : own + dead clients ; a live other client's is counted
template <class F> static void each_file(const char* dir, unsigned selfPid, const Hooks& h, int* skipped, F f) {
    char pat[MAX_PATH]; _snprintf(pat, sizeof(pat), "%s\\%s*.txt", dir, PREFIX); pat[sizeof(pat) - 1] = 0;
    WIN32_FIND_DATAA fd;
    HANDLE fh = FindFirstFileA(pat, &fd);
    if (fh == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        unsigned pid = 0;
        if (!pid_of(fd.cFileName, pid)) continue;
        const bool own = pid && pid == selfPid;
        if (!own && pid && h.alive && h.alive(pid, h.u)) { if (skipped) ++*skipped; continue; }
        char path[MAX_PATH]; _snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName); path[sizeof(path) - 1] = 0;
        f(path, fd.cFileName, pid, own);
    } while (FindNextFileA(fh, &fd));
    FindClose(fh);
}

bool pending(const char* dir, unsigned selfPid, const Hooks& h) {
    bool any = false;
    each_file(dir, selfPid, h, nullptr, [&](const char*, const char*, unsigned, bool) { any = true; });
    return any;
}

Result recover_all(const char* dir, unsigned selfPid, const Hooks& h) {
    Result R = {};
    struct Keep { char name[12]; int x, y, tries; };
    each_file(dir, selfPid, h, &R.skipped, [&](const char* path, const char* fileName, unsigned pid, bool own) {
        FILE* f = fopen(path, "r");
        if (!f) { say(h, "NATIVEUI undo : %s could not be opened -- left for the next load", fileName); return; }
        ++R.files;
        static Keep keep[256]; int nk = 0, put = 0, miss = 0, drop = 0, bad = 0;
        char ln[128];
        while (fgets(ln, sizeof(ln), f)) {
            char nm[12] = {0}; int x = 0, y = 0, tries = 0;
            const int got = sscanf(ln, "%11[^|]|%d|%d|%d", nm, &x, &y, &tries);
            if (got < 3) { if (ln[0] != '\n' && ln[0] != '\r') ++bad; continue; }
            if (h.apply && h.apply(nm, x, y, h.u)) { ++put; continue; }
            if (tries + 1 >= MAX_TRIES) {
                ++drop;
                say(h, "NATIVEUI undo : '%s' still not found after %d loads -- given up (%s)", nm, tries + 1, fileName);
                continue;
            }
            if (nk < (int)(sizeof(keep) / sizeof(keep[0]))) {
                Keep& k = keep[nk++];
                memcpy(k.name, nm, sizeof(k.name)); k.x = x; k.y = y; k.tries = tries + 1;
            }
            ++miss;
        }
        fclose(f);
        R.put += put; R.missed += miss; R.dropped += drop;
        const char* whose = own ? "this client's" : pid ? "a closed client's" : "the old shared";
        say(h, "NATIVEUI undo : %s file %s -- %d record(s) put back, %d missed, %d given up%s", whose, fileName, put, miss,
            drop, bad ? " (unreadable lines skipped)" : "");
        if (nk) {
            // ONLY the missed lines go back in the file (a partial recovery used to delete it whole)
            FILE* w = fopen(path, "w");
            if (!w) { say(h, "NATIVEUI undo : %s NOT rewritten -- its %d missed line(s) stay as they were", fileName, nk); return; }
            for (int i = 0; i < nk; ++i) fprintf(w, "%s|%d|%d|%d\n", keep[i].name, keep[i].x, keep[i].y, keep[i].tries);
            fclose(w);
            say(h, "NATIVEUI undo : %s kept with its %d missed line(s), tried again at the next load", fileName, nk);
        } else if (DeleteFileA(path)) {
            ++R.cleaned;
            say(h, "NATIVEUI undo : %s cleaned", fileName);
        } else {
            say(h, "NATIVEUI undo : %s could NOT be deleted (error %lu)", fileName, GetLastError());
        }
    });
    if (R.skipped) say(h, "NATIVEUI undo : %d file(s) of another running client left alone", R.skipped);
    return R;
}

}  // namespace undo
}  // namespace aio
