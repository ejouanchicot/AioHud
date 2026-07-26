// paths.cpp -- see paths.h. Derive "<windower>\plugins\AioHud" from this DLL's own module path.
#include "model/paths.h"
#include <windows.h>
#include <string.h>
#include <stdio.h>   // _snprintf / _snwprintf (bounded formatting)

namespace aio {

// The DLL deploys as "<windower>\plugins\AioHud.dll" ; the data folder is the sibling "AioHud". Strip the filename
// (-> ...\plugins), then append "AioHud". (A file "AioHud.dll" and a folder "AioHud" coexist fine -- distinct names.)
// One-time migration : the folder used to be "_aiohud_re" -> rename it (a dir rename is atomic and keeps all contents).
const char* plugin_dir() {
    static char dir[MAX_PATH] = { 0 };
    static bool tried = false;   // rule10-ok: memoises the DLL's OWN module path. GetModuleHandleEx on our own address cannot fail transiently -- we are executing inside that module -- so there is no miss for a retry to recover from.
    if (tried) return dir;       // rule10-ok: see above
    tried = true;                // rule10-ok: see above
    HMODULE hm = 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&plugin_dir, &hm) && hm) {
        char p[MAX_PATH];
        if (GetModuleFileNameA(hm, p, MAX_PATH)) {
            char* s = strrchr(p, '\\');                     // ...\plugins\AioHud.dll -> ...\plugins
            if (s) {
                *s = 0; wsprintfA(dir, "%s\\AioHud", p);
                if (GetFileAttributesA(dir) == INVALID_FILE_ATTRIBUTES) {   // new folder absent -> migrate the legacy one if present
                    char old[MAX_PATH]; wsprintfA(old, "%s\\_aiohud_re", p);
                    if (GetFileAttributesA(old) != INVALID_FILE_ATTRIBUTES) MoveFileA(old, dir);   // _aiohud_re -> AioHud (keeps everything)
                }
            }
        }
    }
    return dir;
}

void plugin_path(char* out, int cap, const char* rel) {
    if (!out || cap <= 0) return;
    const char* d = plugin_dir();
    if (d[0]) { _snprintf(out, cap, "%s\\%s", d, rel ? rel : ""); out[cap - 1] = 0; }   // honour cap (buffers are 260)
    else out[0] = 0;
}

const char* plugin_path_r(const char* rel) {
    static char bufs[8][MAX_PATH]; static int rr = 0;
    char* b = bufs[rr++ & 7];
    plugin_path(b, MAX_PATH, rel);
    return b;
}

void plugin_path_w(wchar_t* out, int cap, const wchar_t* rel) {
    if (!out || cap <= 0) return;
    wchar_t wdir[MAX_PATH];
    int n = MultiByteToWideChar(CP_ACP, 0, plugin_dir(), -1, wdir, MAX_PATH);   // narrow plugin_dir -> wide
    if (n > 0 && wdir[0]) { _snwprintf(out, cap, L"%s\\%s", wdir, rel ? rel : L""); out[cap - 1] = 0; }   // honour cap
    else out[0] = 0;
}

} // namespace aio
