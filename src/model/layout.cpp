// layout.cpp -- see layout.h.
#include "model/layout.h"
#include <windows.h>

namespace aio {

// ---- Win32 file I/O (matches gfx/texture.cpp ; no <fstream>, no exceptions) ----
static bool read_file(const char* path, std::string& out) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD sz = GetFileSize(h, 0);
    bool ok = false;
    if (sz != INVALID_FILE_SIZE) {
        out.resize(sz);
        DWORD got = 0;
        if (sz == 0 || (ReadFile(h, &out[0], sz, &got, 0) && got == sz)) ok = true;
    }
    CloseHandle(h);
    return ok;
}
static bool write_file(const char* path, const std::string& data) {
    // ATOMIC : write a temp then rename over the target, so a crash / torn write mid-save can never leave a
    // half-written or empty layout.json (the in-game edit-mode save was CREATE_ALWAYS = truncate-in-place before).
    // Same temp+MoveFileEx pattern as ui_config.cpp's save_config_to.
    const std::string tmp = std::string(path) + ".tmp";
    HANDLE h = CreateFileA(tmp.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &wrote, 0);
    CloseHandle(h);
    if (!ok || wrote != (DWORD)data.size()) { DeleteFileA(tmp.c_str()); return false; }
    if (!MoveFileExA(tmp.c_str(), path, MOVEFILE_REPLACE_EXISTING)) { DeleteFileA(tmp.c_str()); return false; }
    return true;
}

static double round1(double v) { return (double)((long long)(v * 10.0 + (v < 0 ? -0.5 : 0.5))) / 10.0; }

// ---- load ----
bool load_layout(const char* path, Layout& out) {
    std::string text;
    if (!read_file(path, text)) return false;
    json::Value root;
    if (!json::parse(text, root) || !root.is_obj()) return false;

    out = Layout();
    out.version = (int)root["version"].as_num(1);
    out.font = root["font"].as_str("Segoe UI");        // global HUD text face
    out.fontWeight = (int)root["fontWeight"].as_num(600);
    const json::Value& vp = root["viewport"];
    out.vpW = vp["w"].as_num(0);
    out.vpH = vp["h"].as_num(0);

    const json::Value& ws = root["widgets"];
    if (ws.is_arr()) {
        for (size_t i = 0; i < ws.arr.size(); ++i) {
            const json::Value& w = ws.arr[i];
            LWidget lw;
            lw.id   = w["id"].as_str();
            lw.type = w["type"].as_str();
            std::string an = w["anchor"].as_str("tl");
            lw.av = (an.size() >= 1 && an[0] == 'b') ? 'b' : 't';
            lw.ah = (an.size() >= 2 && an[1] == 'r') ? 'r' : 'l';
            const json::Value& pos = w["pos"];
            lw.x = pos["x"].as_num(0);
            lw.y = pos["y"].as_num(0);
            const json::Value& sw = w["size"]["w"];
            if (sw.is_num()) { lw.wAuto = false; lw.w = sw.num; } else { lw.wAuto = true; lw.w = 0; }
            lw.z       = (int)w["z"].as_num(0);
            lw.visible = w["visible"].as_bool(true);
            lw.growDown= w["growDown"].as_bool(false);
            lw.bare    = w["bare"].as_bool(false);
            const json::Value& jb = w["jobs"];
            if (jb.is_arr()) for (size_t j = 0; j < jb.arr.size(); ++j) lw.jobs.push_back(jb.arr[j].as_str());
            lw.config = w["config"];                  // kept as raw JSON
            out.widgets.push_back(lw);
        }
    }

    const json::Value& zs = root["zones"];
    if (zs.is_arr()) {
        for (size_t i = 0; i < zs.arr.size(); ++i) {
            const json::Value& z = zs.arr[i];
            LZone lz;
            lz.label = z["label"].as_str();
            lz.x = z["x"].as_num(0); lz.y = z["y"].as_num(0);
            lz.w = z["w"].as_num(0); lz.h = z["h"].as_num(0);
            const json::Value& al = z["allow"];
            if (al.is_arr()) for (size_t j = 0; j < al.arr.size(); ++j) lz.allow.push_back(al.arr[j].as_str());
            out.zones.push_back(lz);
        }
    }
    return true;
}

// ---- save (reconstructs the exact schema, keys in the same order as the exporter) ----
bool save_layout(const char* path, const Layout& lay) {
    using namespace json;
    Value root = mkObj();
    set(root, "version", mkNum(lay.version));
    set(root, "font", mkStr(lay.font));
    set(root, "fontWeight", mkNum(lay.fontWeight));
    Value vp = mkObj(); set(vp, "w", mkNum(lay.vpW)); set(vp, "h", mkNum(lay.vpH));
    set(root, "viewport", vp);

    Value ws = mkArr();
    for (size_t i = 0; i < lay.widgets.size(); ++i) {
        const LWidget& w = lay.widgets[i];
        Value o = mkObj();
        set(o, "id",   mkStr(w.id));
        set(o, "type", mkStr(w.type));
        std::string an; an += w.av; an += w.ah;
        set(o, "anchor", mkStr(an));
        Value pos = mkObj(); set(pos, "x", mkNum(w.x)); set(pos, "y", mkNum(w.y)); set(o, "pos", pos);
        Value sz = mkObj(); set(sz, "w", w.wAuto ? mkNull() : mkNum(w.w)); set(o, "size", sz);
        set(o, "z", mkNum(w.z));
        set(o, "visible", mkBool(w.visible));
        if (w.jobs.empty()) set(o, "jobs", mkNull());
        else { Value j = mkArr(); for (size_t k = 0; k < w.jobs.size(); ++k) j.arr.push_back(mkStr(w.jobs[k])); set(o, "jobs", j); }
        set(o, "growDown", mkBool(w.growDown));
        set(o, "bare", mkBool(w.bare));
        set(o, "config", w.config);
        ws.arr.push_back(o);
    }
    set(root, "widgets", ws);

    Value zs = mkArr();
    for (size_t i = 0; i < lay.zones.size(); ++i) {
        const LZone& z = lay.zones[i];
        Value o = mkObj();
        set(o, "label", mkStr(z.label));
        set(o, "x", mkNum(z.x)); set(o, "y", mkNum(z.y)); set(o, "w", mkNum(z.w)); set(o, "h", mkNum(z.h));
        Value al = mkArr(); for (size_t k = 0; k < z.allow.size(); ++k) al.arr.push_back(mkStr(z.allow[k]));
        set(o, "allow", al);
        zs.arr.push_back(o);
    }
    set(root, "zones", zs);

    return write_file(path, dump(root));
}

// ---- anchor + % -> pixels ----
PxRect widget_px(const LWidget& w, float sw, float sh, float contentW, float contentH) {
    float ew = contentW;                          // effective width (px) ; caller passes the scaled size
    float eh = contentH;                          // height always from content (no h in schema)
    PxRect r;
    r.w = ew; r.h = eh;
    float ewc = ew > 0 ? ew : 0.0f, ehc = eh > 0 ? eh : 0.0f;
    r.x = (w.ah == 'l') ? (float)(w.x / 100.0 * sw) : (float)(sw - w.x / 100.0 * sw - ewc);
    r.y = (w.av == 't') ? (float)(w.y / 100.0 * sh) : (float)(sh - w.y / 100.0 * sh - ehc);
    return r;
}

// ---- pixels -> nearest anchor + % (for in-game drag ; mirrors the mockup's anchorPanel) ----
void widget_set_from_px(LWidget& w, float px, float py, float ww, float hh, float sw, float sh) {
    bool right  = (px + ww * 0.5f) > sw * 0.5f;
    bool bottom = (py + hh * 0.5f) > sh * 0.5f;
    w.ah = right  ? 'r' : 'l';
    w.av = bottom ? 'b' : 't';
    w.x = round1(right  ? (sw - (px + ww)) / sw * 100.0 : px / sw * 100.0);
    w.y = round1(bottom ? (sh - (py + hh)) / sh * 100.0 : py / sh * 100.0);
}

} // namespace aio
