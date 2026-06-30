// ui_config.cpp -- see ui_config.h.
#include "model/ui_config.h"
#include <cstdio>
#include <cstring>
#include <windows.h>

namespace aio {

UiConfig& ui_config() { static UiConfig c; return c; }

static const char* CONFIG_PATH  = "D:\\Windower Tetsouo\\plugins\\_aiohud_re\\aio_config.txt";
static const char* PROFILE_DIR  = "D:\\Windower Tetsouo\\plugins\\_aiohud_re\\aiohud_profiles";
static const char* ACTIVE_PATH  = "D:\\Windower Tetsouo\\plugins\\_aiohud_re\\aio_active.txt";

static void profile_path(const char* name, char* out, int cap);   // (defined below)
static char g_active[48] = {0};   // last loaded/saved profile name -> persisted + auto-applied at startup

static bool g_scaleReset = true;   // true initially -> the HUD adopts the loaded scales as baseline (no re-anchor) on the first frame
void request_scale_baseline_reset() { g_scaleReset = true; }
bool take_scale_baseline_reset() { bool r = g_scaleReset; g_scaleReset = false; return r; }

void set_active_profile(const char* name) {
    if (!name) name = "";
    strncpy(g_active, name, sizeof(g_active) - 1); g_active[sizeof(g_active) - 1] = 0;
    FILE* f = fopen(ACTIVE_PATH, "w"); if (f) { fputs(g_active, f); fclose(f); }
}
const char* active_profile_name() { return g_active; }

// ---- core (de)serialization : the live config <-> a key=value file at `path`. The named-profile
// API and the single live-config file both go through these, so they always share one format. ----
static void save_config_to(const char* path) {
    FILE* f = fopen(path, "w"); if (!f) return;
    UiConfig& c = ui_config();
    fprintf(f, "skinTheme=%d\n", c.skinTheme);
    fprintf(f, "fontFace=%d\n", c.fontFace);
    fprintf(f, "buffScale=%.4f\n", c.buffScale);
    fprintf(f, "barHeight=%.4f\n", c.barHeight);
    fprintf(f, "barWidth=%.4f\n", c.barWidth);
    fprintf(f, "jobBadge=%d\n", c.jobBadge);
    fprintf(f, "casts=%d,%d\n", c.castParty ? 1 : 0, c.castAlly ? 1 : 0);
    fprintf(f, "dist=%d,%d,%d\n", c.dist[0] ? 1 : 0, c.dist[1] ? 1 : 0, c.dist[2] ? 1 : 0);
    fprintf(f, "lang=%d\n", c.lang);
    fprintf(f, "partyRef=%.5f,%.5f,%.5f,%.5f,%.5f,%.5f\n", c.partyRef[0], c.partyRef[1], c.partyRef[2], c.partyRef[3], c.partyRef[4], c.partyRef[5]);
    fprintf(f, "partyBottom=%.5f\n", c.partyBottomY);
    fprintf(f, "partyRefX=%.5f,%.5f\n", c.partyRefX[0], c.partyRefX[1]);
    fprintf(f, "allyRef=%.5f,%.5f,%.5f,%.5f\n", c.allyRefY[0], c.allyRefY[1], c.allyRefY[2], c.allyRefY[3]);
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
        else if (sscanf(line, "barHeight=%f", &fv) == 1) c.barHeight = fv;
        else if (sscanf(line, "barWidth=%f", &fv) == 1) c.barWidth = fv;
        else if (sscanf(line, "jobBadge=%d", &v) == 1) c.jobBadge = v;
        else if (sscanf(line, "casts=%d,%d", &b0, &b1) == 2) { c.castParty = (b0 != 0); c.castAlly = (b1 != 0); }
        else if (sscanf(line, "dist=%d,%d,%d", &b0, &b1, &b2) == 3) { c.dist[0] = (b0 != 0); c.dist[1] = (b1 != 0); c.dist[2] = (b2 != 0); }
        else if (sscanf(line, "lang=%d", &v) == 1) c.lang = v;
        else if (strncmp(line, "partyRef=", 9) == 0) {   // 3 (old) or 6 values -> fill as many as present
            float pr[6]; int got = sscanf(line + 9, "%f,%f,%f,%f,%f,%f", &pr[0], &pr[1], &pr[2], &pr[3], &pr[4], &pr[5]);
            for (int k = 0; k < got && k < 6; ++k) c.partyRef[k] = pr[k];
        }
        else if (sscanf(line, "partyBottom=%f", &fv) == 1) c.partyBottomY = fv;
        else if (sscanf(line, "partyRefX=%f,%f", &x, &y) == 2) { c.partyRefX[0] = x; c.partyRefX[1] = y; }
        else if (strncmp(line, "allyRef=", 8) == 0) { float ar[4]; int g = sscanf(line + 8, "%f,%f,%f,%f", &ar[0], &ar[1], &ar[2], &ar[3]); for (int k = 0; k < g && k < 4; ++k) c.allyRefY[k] = ar[k]; }
        else if (sscanf(line, "border=%d,%d,%d,%d", &b0, &b1, &b2, &bc) == 4) {
            c.border[0] = (b0 != 0); c.border[1] = (b1 != 0); c.border[2] = (b2 != 0); c.borderCost = (bc != 0);
        }
        else if (sscanf(line, "box%d=%d,%f,%f,%f", &idx, &ps, &x, &y, &s) == 5 && idx >= 0 && idx < 3) {
            // sanitise : a corrupt position (out of the [0,1] screen fraction) must never brick the box
            // off-screen where it can't be grabbed back. Clamp the placed top-left to the viewport.
            x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
            y = y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
            c.box[idx].posSet = (ps != 0); c.box[idx].x = x; c.box[idx].y = y; c.box[idx].scale = s;
        }
    }
    fclose(f);
    return true;
}

void save_ui_config() { save_config_to(CONFIG_PATH); }
void load_ui_config() {
    load_config_from(CONFIG_PATH);
    // remember + AUTO-APPLY the last loaded profile so a relaunch comes back on the same profile.
    FILE* f = fopen(ACTIVE_PATH, "r");
    if (f) {
        if (fgets(g_active, sizeof(g_active), f)) {
            size_t n = strlen(g_active);
            while (n && (g_active[n-1] == '\n' || g_active[n-1] == '\r' || g_active[n-1] == ' ')) g_active[--n] = 0;
        }
        fclose(f);
    }
    if (g_active[0]) {
        char p[300]; profile_path(g_active, p, sizeof(p));
        if (load_config_from(p)) save_config_to(CONFIG_PATH);   // adopt the profile as the live config
        else g_active[0] = 0;                                   // profile was deleted -> no active profile
    }
    profile_mark_clean();
    request_scale_baseline_reset();   // startup : adopt the loaded scales as baseline (no re-anchor)
}

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
    profile_mark_clean();
    set_active_profile(name);   // saving makes it the active profile too
    return true;
}
bool profile_load(const char* name) {
    if (!name || !name[0]) return false;
    char p[300]; profile_path(name, p, sizeof(p));
    if (!load_config_from(p)) return false;
    save_ui_config();   // adopt as the live config too
    profile_mark_clean();
    set_active_profile(name);   // remember for auto-load at next startup
    request_scale_baseline_reset();   // adopt the profile's box scales as-is (don't re-anchor -> no false "modified")
    return true;
}

// ---- "unsaved changes" tracking : snapshot the persisted fields, compare to live ----
static UiConfig g_snap; static bool g_snapValid = false;
static bool persist_eq(const UiConfig& a, const UiConfig& b) {
    if (a.skinTheme != b.skinTheme || a.fontFace != b.fontFace) return false;
    if (a.buffScale != b.buffScale) return false;
    for (int i = 0; i < 6; ++i) if (a.partyRef[i] != b.partyRef[i]) return false;
    if (a.partyBottomY != b.partyBottomY) return false;
    if (a.partyRefX[0] != b.partyRefX[0] || a.partyRefX[1] != b.partyRefX[1]) return false;
    for (int i = 0; i < 4; ++i) if (a.allyRefY[i] != b.allyRefY[i]) return false;
    if (a.barHeight != b.barHeight || a.barWidth != b.barWidth) return false;
    if (a.jobBadge != b.jobBadge || a.castParty != b.castParty || a.castAlly != b.castAlly) return false;
    if (a.dist[0] != b.dist[0] || a.dist[1] != b.dist[1] || a.dist[2] != b.dist[2]) return false;
    if (a.borderCost != b.borderCost) return false;
    for (int i = 0; i < 3; ++i) {
        if (a.border[i] != b.border[i]) return false;
        if (a.box[i].posSet != b.box[i].posSet || a.box[i].x != b.box[i].x ||
            a.box[i].y != b.box[i].y || a.box[i].scale != b.box[i].scale) return false;
    }
    return true;
}
void profile_mark_clean() { g_snap = ui_config(); g_snapValid = true; }
bool profile_dirty()      { return g_snapValid && !persist_eq(g_snap, ui_config()); }
bool profile_delete(const char* name) {
    if (!name || !name[0]) return false;
    char p[300]; profile_path(name, p, sizeof(p));
    bool ok = DeleteFileA(p) != 0;
    profile_refresh();
    if (g_active[0] && strcmp(g_active, name) == 0) set_active_profile("");   // deleting the active profile clears it
    return ok;
}

void reset_boxes() {   // edit-mode Default : positions + sizes only
    UiConfig& c = ui_config();
    for (int i = 0; i < 3; ++i) c.box[i] = BoxLayout();   // posSet=false, x=y=0, scale=1
    for (int i = 0; i < 6; ++i) c.partyRef[i] = -1.0f;
    c.partyBottomY = -1.0f;
    c.partyRefX[0] = c.partyRefX[1] = -1.0f;
    for (int i = 0; i < 4; ++i) c.allyRefY[i] = -1.0f;
    save_ui_config();
    request_scale_baseline_reset();   // scales went back to 1.0 -> adopt as baseline (no re-anchor)
}

void reset_ui_config() {   // general Default : everything
    UiConfig& c = ui_config();
    c.skinTheme = 0; c.fontFace = 0; c.buffScale = 0.92f; c.barHeight = 1.0f; c.barWidth = 1.0f;
    c.jobBadge = 2; c.castParty = true; c.castAlly = true; c.dist[0] = c.dist[1] = c.dist[2] = true;
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
