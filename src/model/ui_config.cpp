// ui_config.cpp -- see ui_config.h.
#include "model/ui_config.h"
#include <cstdio>
#include <cstring>
#include <windows.h>

namespace aio {

UiConfig& ui_config() { static UiConfig c; return c; }

static const char* CONFIG_PATH  = "D:\\Windower Tetsouo\\plugins\\_aiohud_re\\aio_config.txt";
static const char* PROFILE_DIR  = "D:\\Windower Tetsouo\\plugins\\_aiohud_re\\aiohud_profiles";

// ---- core (de)serialization : the live config <-> a key=value file at `path`. The named-profile
// API and the single live-config file both go through these, so they always share one format. ----
static void save_config_to(const char* path) {
    FILE* f = fopen(path, "w"); if (!f) return;
    UiConfig& c = ui_config();
    fprintf(f, "skinTheme=%d\n", c.skinTheme);
    fprintf(f, "fontFace=%d\n", c.fontFace);
    fprintf(f, "buffScale=%.4f\n", c.buffScale);
    fprintf(f, "partyRefY=%.5f\n", c.partyRefY);
    fprintf(f, "border=%d,%d,%d,%d\n", c.border[0] ? 1 : 0, c.border[1] ? 1 : 0, c.border[2] ? 1 : 0, c.borderCost ? 1 : 0);
    for (int i = 0; i < 3; ++i)
        fprintf(f, "box%d=%d,%.5f,%.5f,%.4f\n", i, c.box[i].posSet ? 1 : 0, c.box[i].x, c.box[i].y, c.box[i].scale);
    fclose(f);
}

static bool load_config_from(const char* path) {
    FILE* f = fopen(path, "r"); if (!f) return false;
    UiConfig& c = ui_config();
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        int v, ps, idx, b0, b1, b2, bc; float x, y, s, fv;
        if      (sscanf(line, "skinTheme=%d", &v) == 1) c.skinTheme = v;
        else if (sscanf(line, "fontFace=%d", &v) == 1)  c.fontFace = v;
        else if (sscanf(line, "buffScale=%f", &fv) == 1) c.buffScale = fv;
        else if (sscanf(line, "partyRefY=%f", &fv) == 1) c.partyRefY = fv;
        else if (sscanf(line, "border=%d,%d,%d,%d", &b0, &b1, &b2, &bc) == 4) {
            c.border[0] = (b0 != 0); c.border[1] = (b1 != 0); c.border[2] = (b2 != 0); c.borderCost = (bc != 0);
        }
        else if (sscanf(line, "box%d=%d,%f,%f,%f", &idx, &ps, &x, &y, &s) == 5 && idx >= 0 && idx < 3) {
            c.box[idx].posSet = (ps != 0); c.box[idx].x = x; c.box[idx].y = y; c.box[idx].scale = s;
        }
    }
    fclose(f);
    return true;
}

void save_ui_config() { save_config_to(CONFIG_PATH); }
void load_ui_config() { load_config_from(CONFIG_PATH); }

// ---- profiles ----
// The display NAME and the FILE name are decoupled : the user can type any printable character
// (incl. / \ : etc.), and we %-encode the filename-illegal ones into the file name (reversible, no
// collisions). So "Tetsouo/Default" lives in "Tetsouo%2FDefault.txt" and shows back as typed.
static char g_profNames[64][48];   // DISPLAY names (decoded)
static int  g_profCount = 0;

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
// display name -> file base name (no extension). %-encode the chars Windows forbids in a file name.
static void name_to_file(const char* name, char* out, int cap) {
    int o = 0;
    for (int i = 0; name[i] && o < cap - 4; ++i) {
        unsigned char ch = (unsigned char)name[i];
        bool bad = ch < 32 || ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '/' ||
                   ch == '\\' || ch == '|' || ch == '?' || ch == '*' || ch == '%';
        if (bad) { o += sprintf(out + o, "%%%02X", ch); }
        else out[o++] = (char)ch;
    }
    out[o] = 0;
}
// file base name -> display name (%XX -> byte).
static void file_to_name(const char* file, char* out, int cap) {
    int o = 0;
    for (int i = 0; file[i] && o < cap - 1; ++i) {
        if (file[i] == '%' && file[i + 1] && file[i + 2]) {
            int hi = hexval(file[i + 1]), lo = hexval(file[i + 2]);
            if (hi >= 0 && lo >= 0) { out[o++] = (char)(hi * 16 + lo); i += 2; continue; }
        }
        out[o++] = file[i];
    }
    out[o] = 0;
}

static void profile_path(const char* name, char* out, int cap) {
    char fb[160]; name_to_file(name, fb, sizeof(fb));
    _snprintf(out, cap, "%s\\%s.txt", PROFILE_DIR, fb); out[cap - 1] = 0;
}
void profile_refresh() {
    g_profCount = 0;
    char pat[300]; _snprintf(pat, sizeof(pat), "%s\\*.txt", PROFILE_DIR); pat[sizeof(pat) - 1] = 0;
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char base[160]; int n = (int)strlen(fd.cFileName);
        if (n > 4 && _stricmp(fd.cFileName + n - 4, ".txt") == 0) n -= 4;   // strip extension
        if (n <= 0) continue; if (n > 159) n = 159;
        memcpy(base, fd.cFileName, n); base[n] = 0;
        if (g_profCount < 64) { file_to_name(base, g_profNames[g_profCount], 48); ++g_profCount; }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
int         profile_count()       { return g_profCount; }
const char* profile_name(int i)   { return (i >= 0 && i < g_profCount) ? g_profNames[i] : ""; }
bool        profile_exists(const char* name) {
    if (!name || !name[0]) return false;
    char p[300]; profile_path(name, p, sizeof(p));
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}
bool profile_save(const char* name) {
    if (!name || !name[0]) return false;
    CreateDirectoryA(PROFILE_DIR, NULL);
    char p[300]; profile_path(name, p, sizeof(p));
    save_config_to(p);
    profile_refresh();
    return true;
}
bool profile_load(const char* name) {
    if (!name || !name[0]) return false;
    char p[300]; profile_path(name, p, sizeof(p));
    if (!load_config_from(p)) return false;
    save_ui_config();   // adopt as the live config too
    return true;
}
bool profile_delete(const char* name) {
    if (!name || !name[0]) return false;
    char p[300]; profile_path(name, p, sizeof(p));
    bool ok = DeleteFileA(p) != 0;
    profile_refresh();
    return ok;
}

void reset_boxes() {   // edit-mode Default : positions + sizes only
    UiConfig& c = ui_config();
    for (int i = 0; i < 3; ++i) c.box[i] = BoxLayout();   // posSet=false, x=y=0, scale=1
    c.partyRefY = -1.0f;
    save_ui_config();
}

void reset_ui_config() {   // general Default : everything
    UiConfig& c = ui_config();
    c.skinTheme = 0; c.fontFace = 0; c.buffScale = 0.92f;
    c.border[0] = c.border[1] = c.border[2] = c.borderCost = true;   // all borders back on
    reset_boxes();   // (also saves)
}

// index 0 = Default (keep the layout face). The rest are common Windows GDI faces.
static const char* FACE_LABEL[] = {
    "Default", "Segoe UI", "Arial", "Tahoma", "Verdana", "Calibri", "Trebuchet MS", "Consolas", "Georgia",
};
static const char* FACE_NAME[] = {
    "",        "Segoe UI", "Arial", "Tahoma", "Verdana", "Calibri", "Trebuchet MS", "Consolas", "Georgia",
};
static const int NFACE = (int)(sizeof(FACE_LABEL) / sizeof(FACE_LABEL[0]));

int         ui_font_count()        { return NFACE; }
const char* ui_font_label(int i)   { return (i >= 0 && i < NFACE) ? FACE_LABEL[i] : ""; }
const char* ui_font_face(int i)    { return (i >= 0 && i < NFACE) ? FACE_NAME[i]  : ""; }

} // namespace aio
