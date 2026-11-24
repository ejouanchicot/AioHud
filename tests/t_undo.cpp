// t_undo.cpp -- the Game UI's crash-recovery files, one per client (model/native_ui_undo.h, audit R4).
//
// The bug it locks down : ONE shared data\native_ui_undo.txt. In a two-client multibox the second client's load read,
// applied and DELETED the first client's file -- and a partial recovery deleted it whole -- so the first client's
// windows stayed off-screen with nothing left to put them back.
#include "check.h"
#include "model/native_ui_undo.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

using namespace aio;

namespace {

struct World {
    unsigned live[4]; int nLive;
    const char* missing[4]; int nMissing;     // records the "client" cannot find
    int applied; char lastName[16]; int lastX, lastY;
    int logs;
};

bool w_alive(unsigned pid, void* u) {
    const World* w = (const World*)u;
    for (int i = 0; i < w->nLive; ++i) if (w->live[i] == pid) return true;
    return false;
}
bool w_apply(const char* name, int x, int y, void* u) {
    World* w = (World*)u;
    for (int i = 0; i < w->nMissing; ++i) if (!strcmp(w->missing[i], name)) return false;
    ++w->applied; strncpy(w->lastName, name, sizeof(w->lastName) - 1); w->lastX = x; w->lastY = y;
    return true;
}
void w_log(const char*, void* u) { ++((World*)u)->logs; }

void put(const char* dir, const char* file, const char* text) {
    char p[MAX_PATH]; _snprintf(p, sizeof(p), "%s\\%s", dir, file); p[sizeof(p) - 1] = 0;
    FILE* f = fopen(p, "w"); if (f) { fputs(text, f); fclose(f); }
}
bool exists(const char* dir, const char* file) {
    char p[MAX_PATH]; _snprintf(p, sizeof(p), "%s\\%s", dir, file); p[sizeof(p) - 1] = 0;
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}
int read_back(const char* dir, const char* file, char* out, int n) {
    char p[MAX_PATH]; _snprintf(p, sizeof(p), "%s\\%s", dir, file); p[sizeof(p) - 1] = 0;
    FILE* f = fopen(p, "r"); if (!f) return -1;
    const int got = (int)fread(out, 1, (size_t)(n - 1), f); out[got < 0 ? 0 : got] = 0; fclose(f);
    int lines = 0; for (const char* c = out; *c; ++c) if (*c == '\n') ++lines;
    return lines;
}
void fresh_dir(char* dir, int n) {
    char tmp[MAX_PATH]; GetTempPathA(MAX_PATH, tmp);
    _snprintf(dir, (size_t)n, "%saiohud_t_undo_%lu", tmp, GetCurrentProcessId()); dir[n - 1] = 0;
    CreateDirectoryA(dir, NULL);
    char pat[MAX_PATH]; _snprintf(pat, sizeof(pat), "%s\\*.txt", dir); pat[sizeof(pat) - 1] = 0;
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { char p[MAX_PATH]; _snprintf(p, sizeof(p), "%s\\%s", dir, fd.cFileName); p[sizeof(p) - 1] = 0; DeleteFileA(p); }
        while (FindNextFileA(h, &fd));
        FindClose(h);
    }
}
undo::Hooks hooks(World& w) { undo::Hooks h = { w_alive, w_apply, w_log, &w }; return h; }

}  // namespace

void test_undo() {
    char dir[MAX_PATH]; fresh_dir(dir, sizeof(dir));

    SECTION("undo : a file name gives its client's pid ; the old shared file belongs to nobody");
    {   unsigned pid = 1;
        CHECK(undo::pid_of("native_ui_undo_4242.txt", pid)); CHECK_EQ(4242, (long long)pid);
        CHECK(undo::pid_of("native_ui_undo.txt", pid));      CHECK_EQ(0, (long long)pid);
        CHECK(!undo::pid_of("native_ui_undo_.txt", pid));
        CHECK(!undo::pid_of("native_ui_undo_12x.txt", pid));
        CHECK(!undo::pid_of("ui_config.txt", pid));
        char p[MAX_PATH]; undo::path_for(p, sizeof(p), "C:\\x\\data", 77);
        CHECK_STR("C:\\x\\data\\native_ui_undo_77.txt", p);
    }

    SECTION("undo : two clients -- B's load never touches A's file while A runs (THE R4 bug)");
    {   fresh_dir(dir, sizeof(dir));
        put(dir, "native_ui_undo_100.txt", "partywin|10|20\nlogwindo|30|40\n");   // client A (pid 100), running
        World w = {}; w.live[w.nLive++] = 100; w.live[w.nLive++] = 200;            // B is pid 200, also running
        CHECK(!undo::pending(dir, 200, hooks(w)));
        const undo::Result r = undo::recover_all(dir, 200, hooks(w));
        CHECK_EQ(0, w.applied);                         // A's windows are NOT written into B's memory
        CHECK_EQ(1, r.skipped);
        CHECK(exists(dir, "native_ui_undo_100.txt"));    // and A's file is still there for A
    }

    SECTION("undo : a client that no longer runs -- its file is recovered, then cleaned");
    {   fresh_dir(dir, sizeof(dir));
        put(dir, "native_ui_undo_100.txt", "partywin|10|20\nlogwindo|30|40\n");   // A crashed : pid 100 gone
        World w = {}; w.live[w.nLive++] = 200;
        CHECK(undo::pending(dir, 200, hooks(w)));
        const undo::Result r = undo::recover_all(dir, 200, hooks(w));
        CHECK_EQ(2, w.applied); CHECK_EQ(2, r.put); CHECK_EQ(1, r.cleaned);
        CHECK(!exists(dir, "native_ui_undo_100.txt"));
        CHECK(w.logs >= 2);                             // rule 10 : recovered AND cleaned both said
    }

    SECTION("undo : a partial recovery keeps ONLY the missed line, with its try counted");
    {   fresh_dir(dir, sizeof(dir));
        put(dir, "native_ui_undo_300.txt", "partywin|10|20\nmapframe|50|60\nlogwindo|30|40\n");   // own file, pid 300
        World w = {}; w.live[w.nLive++] = 300; w.missing[w.nMissing++] = "mapframe";
        const undo::Result r = undo::recover_all(dir, 300, hooks(w));
        CHECK_EQ(2, r.put); CHECK_EQ(1, r.missed); CHECK_EQ(0, r.cleaned);
        char text[256]; CHECK_EQ(1, read_back(dir, "native_ui_undo_300.txt", text, sizeof(text)));
        CHECK_STR("mapframe|50|60|1\n", text);           // the file was NOT deleted, and holds just the missed record
        // the next load finds it : put back, file cleaned
        w.nMissing = 0; w.applied = 0;
        const undo::Result r2 = undo::recover_all(dir, 300, hooks(w));
        CHECK_EQ(1, r2.put); CHECK_EQ(1, r2.cleaned); CHECK_STR("mapframe", w.lastName); CHECK_EQ(50, w.lastX);
        CHECK(!exists(dir, "native_ui_undo_300.txt"));
    }

    SECTION("undo : a record never found is given up after MAX_TRIES loads (bounded, logged)");
    {   fresh_dir(dir, sizeof(dir));
        put(dir, "native_ui_undo_300.txt", "ghostwin|1|2\n");
        World w = {}; w.live[w.nLive++] = 300; w.missing[w.nMissing++] = "ghostwin";
        int loads = 0; undo::Result r = {};
        while (exists(dir, "native_ui_undo_300.txt") && loads < 20) { r = undo::recover_all(dir, 300, hooks(w)); ++loads; }
        CHECK_EQ(undo::MAX_TRIES, loads);
        CHECK_EQ(1, r.dropped);
        CHECK(!exists(dir, "native_ui_undo_300.txt"));
    }

    SECTION("undo : the old shared file is recovered once and cleaned, a live client's kept, in one pass");
    {   fresh_dir(dir, sizeof(dir));
        put(dir, "native_ui_undo.txt", "partywin|10|20\n");          // left by a build before R4
        put(dir, "native_ui_undo_100.txt", "logwindo|30|40\n");      // A, running
        World w = {}; w.live[w.nLive++] = 100; w.live[w.nLive++] = 200;
        const undo::Result r = undo::recover_all(dir, 200, hooks(w));
        CHECK_EQ(1, r.files); CHECK_EQ(1, r.put); CHECK_EQ(1, r.skipped); CHECK_EQ(1, r.cleaned);
        CHECK(!exists(dir, "native_ui_undo.txt"));
        CHECK(exists(dir, "native_ui_undo_100.txt"));
    }

    fresh_dir(dir, sizeof(dir)); RemoveDirectoryA(dir);
}
