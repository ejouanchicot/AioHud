// cfg_engine.cpp -- see cfg_engine.h.
#include "ui/cfg_engine.h"
#include "ui/config_controls.h"   // tr / ctrl_uid_i / color_picker ; save_ui_config through ui_config.h
#include "ui/box_grow.h"          // box_grow_set : a grow change keeps the box where it is
#include "gfx/window.h"           // the box-theme families / hues / FFXI skins
#include "gfx/draw.h"
#include "gfx/font.h"
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace aio {

// ---- the FACTORY values : a value-initialised config (the same rule config_rules.h's config_defaults uses) ; a field's
// factory twin sits at the same offset in it ----
static const UiConfig& factory() { static const UiConfig F{}; return F; }
static const void* factory_of(const void* p, int size) {
    if (!p || size <= 0) return nullptr;
    const char* b = (const char*)&ui_config();
    const ptrdiff_t off = (const char*)p - b;
    if (off < 0 || off + size > (ptrdiff_t)sizeof(UiConfig)) return nullptr;   // not a UiConfig field : no marker, no reset
    return (const char*)&factory() + off;
}
static bool differs(const void* p, int size, bool isFloat) {
    const void* d = factory_of(p, size);
    if (!d) return false;
    if (isFloat) return fabsf(*(const float*)p - *(const float*)d) > 0.0005f;
    return memcmp(p, d, size) != 0;
}
static int s_pendingSave = 0;   // a slider saves on RELEASE (one write per gesture)
static int s_band = 0;          // alternate rows get a faint band, so the eye follows a long row from label to control

// ---- one row : ground + label (+ reason), the right-click reset ; the control is drawn by the caller between ----
struct Row { float y, h, cy, right; int st; };
static Row row_begin(Kit& K, float x, float y, float w, const char* label, const char* why, bool modified) {
    Row r; r.y = y; r.st = (why ? KS_DISABLED : 0) | (modified ? KS_MODIFIED : 0);
    r.h = K.px(why ? 52.0f : 44.0f); r.cy = y + r.h * 0.5f; r.right = x + w - K.px(18.0f);
    if ((s_band++ & 1) != 0) { cs(K.dev); rrect(K.dev, x, y, w, r.h, K.px(6.0f), fa(0x0AFFFFFFu), fa(0x0AFFFFFFu)); }
    kit_row(K, x, y, w, r.h, r.st);
    kit_label(K, x + K.px(18.0f), r.cy, label, r.st, why);
    return r;
}
// right-click on a modified, live row : back to the factory value. `custom` replaces the plain copy (a grow keeps its box).
static void row_end(Kit& K, float x, float& y, float w, const Row& r, void* p, int size, void (*custom)(void*) = nullptr) {
    if ((r.st & KS_MODIFIED) && K.mo && K.mo->rightClicked && inrect(K.mo, x, r.y, w, r.h)) {
        if (custom) custom(p); else if (const void* d = factory_of(p, size)) memcpy(p, d, size);
        save_ui_config();
    }
    y += r.h + K.px(4.0f);
}

static bool culled(Kit& K, float& y, const char* why) {
    const float h = K.px(why ? 52.0f : 44.0f);
    if (y + h >= K.clipTop && y <= K.clipBot) return false;
    y += h + K.px(4.0f); return true;
}
bool cfg_toggle_row(Kit& K, int uid, float x, float& y, float w, const char* label, bool on, const char* why, void* p, int size) {
    if (culled(K, y, why)) return false;
    const Row r = row_begin(K, x, y, w, label, why, differs(p, size, false));
    const bool hit = kit_toggle(K, uid, r.right - K.px(56.0f), r.cy - K.px(14.0f), on, r.st & KS_DISABLED) && !why;
    row_end(K, x, y, w, r, p, size);
    return hit;
}
bool cfg_slider_row(Kit& K, int uid, float x, float& y, float w, const char* label, float* v01, const char* val, const char* why, void* p, int size, bool isFloat) {
    if (s_pendingSave != uid && culled(K, y, why)) return false;   // (the row being dragged keeps drawing : its drag owns the pointer)
    const Row r = row_begin(K, x, y, w, label, why, differs(p, size, isFloat));
    const float sw = K.px(330.0f);
    const bool moved = kit_slider(K, uid, r.right - sw, r.cy - K.px(14.0f), sw, v01, val, r.st & KS_DISABLED) && !why;
    if (moved) s_pendingSave = uid;
    row_end(K, x, y, w, r, p, size);
    return moved;
}
static int select_row(Kit& K, int uid, float x, float& y, float w, const char* label, const char* val, const char* why, void* p, int size, void (*custom)(void*)) {
    if (culled(K, y, why)) return 0;
    const Row r = row_begin(K, x, y, w, label, why, differs(p, size, false));
    const float sw = K.px(230.0f);   // lighter than a wide window : 300 weighed three toggles
    const int d = kit_selector(K, uid, r.right - sw, r.cy - K.px(20.0f), sw, val, r.st & KS_DISABLED);
    row_end(K, x, y, w, r, p, size, custom);
    return why ? 0 : d;
}
int cfg_select_row(Kit& K, int uid, float x, float& y, float w, const char* label, const char* val, const char* why, void* p, int size) {
    return select_row(K, uid, x, y, w, label, val, why, p, size, nullptr);
}
void cfg_note(Kit& K, float x, float& y, const char* text) {
    const float h = K.px(34.0f);
    K.fr->begin(K.dev);
    K.fr->draw_lc(K.dev, x + K.px(18.0f), y + h * 0.5f, text, K.px(13.5f), fa(C_DIM), 0, 0.0f);
    y += h;
}
void cfg_color_row(Kit& K, int uidBase, float x, float& y, float w, unsigned* col) {
    const float h = color_picker_height() + K.px(12.0f);
    if (y + h >= K.clipTop && y <= K.clipBot && color_picker(K.dev, K.fr, K.mo, ctrl_uid_i(uidBase, 0), ctrl_uid_i(uidBase, 1), x + K.px(18.0f), y + K.px(4.0f), w - K.px(36.0f), col))
        save_ui_config();
    y += h;
}
void cfg_flush(const MouseState* mo) {
    if (s_pendingSave && !(mo && mo->down)) { s_pendingSave = 0; save_ui_config(); }
}

// ---- the OptDef tables ----
static void* fptr(const OptDef& o) { return o.field ? o.field(ui_config()) : nullptr; }
static void* optp(const OptDef& o) { return o.kind == OK_GROW ? (void*)&ui_config().boxGrow[o.growBox] : fptr(o); }
static bool shows(const OptDef& o, bool adv) { return adv || o.level == OL_SIMPLE; }
static int  osz(const OptDef& o) { return o.kind == OK_BTOGGLE ? 1 : 4; }
static bool opt_modified(const OptDef& o) { return o.kind != OK_NOTE && differs(optp(o), osz(o), o.kind == OK_PCT); }
static int  s_growBox = 0;
static void grow_reset(void*) { box_grow_set(s_growBox, factory().boxGrow[s_growBox]); }

int cfg_count_modified(const OptDef* d, int n, bool adv) {
    int k = 0; for (int i = 0; i < n; ++i) if (shows(d[i], adv) && opt_modified(d[i])) ++k; return k;
}
int cfg_count_modified_bytes(const void* p, int size) { return differs(p, size, false) ? 1 : 0; }

bool cfg_section(Kit& K, int uid, float x, float& y, float w, const char* title, bool* open, int nMod, bool foldable) {
    s_band = 0;   // each section's banding starts on a plain row
    const bool op = foldable ? *open : true;
    const bool hit = kit_section(K, uid, x, y, w, title, op, nMod, 0);
    if (foldable && hit) *open = !*open;
    y += K.px(34.0f) + K.px(6.0f);
    return foldable ? *open : true;
}

bool cfg_rows(Kit& K, int uidBase, const OptDef* d, int n, float x, float& y, float w, bool adv) {
    UiConfig& c = ui_config();
    bool changed = false;
    for (int i = 0; i < n; ++i) {
        const OptDef& o = d[i];
        if (!shows(o, adv)) continue;
        const int uid = ctrl_uid_i(uidBase, i);
        const char* label = tr(o.en, o.fr);
        if (o.kind == OK_NOTE) { cfg_note(K, x, y, label); continue; }
        const char* why = o.why ? o.why(c) : nullptr;
        void* f = fptr(o);
        switch (o.kind) {
        case OK_TOGGLE: {
            int* v = (int*)f;
            if (cfg_toggle_row(K, uid, x, y, w, label, *v != 0, why, f, 4)) { *v = *v ? 0 : 1; changed = true; save_ui_config(); }
        } break;
        case OK_BTOGGLE: {
            bool* v = (bool*)f;
            if (cfg_toggle_row(K, uid, x, y, w, label, *v, why, f, 1)) { *v = !*v; changed = true; save_ui_config(); }
        } break;
        case OK_PCT: case OK_INT: {
            const bool isInt = o.kind == OK_INT;
            const float cur = isInt ? (float)*(int*)f : *(float*)f;
            float v01 = (cur - o.lo) / (o.hi - o.lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            char b[24];
            if (isInt) _snprintf(b, sizeof(b), "%d", (int)(cur + 0.5f)); else _snprintf(b, sizeof(b), "%d %%", (int)(cur * 100.0f + 0.5f));
            b[23] = 0;
            if (cfg_slider_row(K, uid, x, y, w, label, &v01, b, why, f, 4, !isInt)) {
                float nv = o.lo + v01 * (o.hi - o.lo);
                if (isInt) *(int*)f = (int)(nv + 0.5f);
                else { nv = floorf(nv / 0.05f + 0.5f) * 0.05f; *(float*)f = nv < o.lo ? o.lo : (nv > o.hi ? o.hi : nv); }
                changed = true;
            }
        } break;
        case OK_SELECT: case OK_GROW: {
            static const char* GEN[3] = { "Right", "Both sides", "Left" };
            static const char* GFR[3] = { "Droite", "Des deux c\xC3\xB4t\xC3\xA9s", "Gauche" };
            const bool grow = o.kind == OK_GROW;
            const int nCh = grow ? 3 : o.nCh;
            int v = grow ? c.boxGrow[o.growBox] : *(int*)f;
            if (v < 0 || v >= nCh) v = 0;
            const char* vt = grow ? tr(GEN[v], GFR[v]) : tr(o.chEn[v], o.chFr[v]);
            s_growBox = o.growBox;
            if (int dlt = select_row(K, uid, x, y, w, label, vt, why, optp(o), 4, grow ? grow_reset : nullptr)) {
                const int nv = (v + dlt + nCh) % nCh;
                if (grow) box_grow_set(o.growBox, nv); else *(int*)f = nv;
                changed = true; save_ui_config();
            }
        } break;
        default: break;
        }
    }
    return changed;
}

// ======== the shared FRAME block ========
// The frame grammar of every panel (docs/architecture/config-panels.md) : Box · Border · Transparency · Theme (Same as
// Party / Custom) · Box theme · game skin or Custom colour · hue swatches or picker · Luminosity. A row a choice above
// makes inert is GREYED with its reason ; only the swatch grid / picker (a choice's own sub-form) disappears.
int cfg_frame_modified(const FrameRefs& r) {
    int k = 0;
    if (r.on && differs(r.on, 4, false)) ++k;
    if (r.border && differs(r.border, 4, false)) ++k;
    if (r.borderB && differs(r.borderB, 1, false)) ++k;
    if (r.alphaOwn && differs(r.alphaOwn, 4, false)) ++k;
    if (r.alpha && !(r.alphaOwn && !*r.alphaOwn) && differs(r.alpha, 4, true)) ++k;
    if (r.themeCopy && differs(r.themeCopy, 4, false)) ++k;
    if (r.theme && differs(r.theme, 4, false)) ++k;
    const bool proc = r.theme && window_theme_family(*r.theme) != 0;   // an FFXI skin draws no hue / luminosity row
    if (proc && r.hue && differs(r.hue, 4, false)) ++k;
    if (proc && r.lum && differs(r.lum, 4, true)) ++k;
    return k;
}
void cfg_frame_rows(Kit& K, int uidBase, float x, float& y, float w, const FrameRefs& r) {
    const UiConfig& c = ui_config();
    const bool on = !r.on || *r.on;
    if (r.on && cfg_toggle_row(K, ctrl_uid_i(uidBase, 0), x, y, w, tr("Box", "Cadre"), *r.on != 0, nullptr, r.on, 4)) { *r.on = !*r.on; save_ui_config(); }
    const char* offWhy = on ? nullptr : tr("inactive : box off", "inactif : cadre coup\xC3\xA9");
    if (r.borderB) { if (cfg_toggle_row(K, ctrl_uid_i(uidBase, 1), x, y, w, tr("Border", "Bordure"), *r.borderB, offWhy, r.borderB, 1)) { *r.borderB = !*r.borderB; save_ui_config(); } }
    else if (cfg_toggle_row(K, ctrl_uid_i(uidBase, 1), x, y, w, tr("Border", "Bordure"), *r.border != 0, offWhy, r.border, 4)) { *r.border = !*r.border; save_ui_config(); }
    if (r.alphaOwn) {
        if (cfg_select_row(K, ctrl_uid_i(uidBase, 2), x, y, w, tr("Transparency", "Transparence"),
                           *r.alphaOwn ? tr("Custom", "Perso") : tr("Same as Party", "Comme Party"), offWhy, r.alphaOwn, 4)) {
            if (!*r.alphaOwn) *r.alpha = c.skinBoxAlpha;   // Custom starts from what is shown
            *r.alphaOwn = !*r.alphaOwn; save_ui_config(); }
    }
    {   // the EFFECTIVE value ; moving it makes it this box's own
        const float shown = (r.alphaOwn && !*r.alphaOwn) ? c.skinBoxAlpha : *r.alpha;
        float v01 = 1.0f - shown; v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
        char b[16]; _snprintf(b, sizeof(b), "%d %%", (int)(v01 * 100.0f + 0.5f)); b[15] = 0;
        if (cfg_slider_row(K, ctrl_uid_i(uidBase, 3), x, y, w, tr("Box transparency", "Transparence du cadre"), &v01, b, offWhy, (r.alphaOwn && !*r.alphaOwn) ? nullptr : r.alpha, 4, true)) {   // following the Party : its own value is not what shows, no marker
            *r.alpha = 1.0f - floorf(v01 * 100.0f + 0.5f) / 100.0f; if (r.alphaOwn) *r.alphaOwn = 1; }
    }
    if (r.themeCopy) {
        if (cfg_select_row(K, ctrl_uid_i(uidBase, 4), x, y, w, tr("Theme", "Th\xC3\xA8me"),
                           *r.themeCopy ? tr("Same as Party", "Comme Party") : tr("Custom", "Perso"), offWhy, r.themeCopy, 4)) {
            *r.themeCopy = !*r.themeCopy; save_ui_config(); }
    }
    const bool own = !r.themeCopy || !*r.themeCopy;
    const char* ownWhy = offWhy ? offWhy : (own ? nullptr : tr("inactive : theme same as Party", "inactif : th\xC3\xA8me comme Party"));
    const int fam = window_theme_family(*r.theme), var = window_theme_variant(*r.theme), nF = box_family_count();
    if (int d = cfg_select_row(K, ctrl_uid_i(uidBase, 5), x, y, w, tr("Box theme", "Th\xC3\xA8me de cadre"), box_family_name(fam), ownWhy, r.theme, 4)) {
        *r.theme = window_theme_index((fam + d + nF) % nF, var); save_ui_config(); }
    if (fam == 0) {   // FFXI : the game's window skin
        const int nV = window_tex_theme_count() < 1 ? 1 : window_tex_theme_count();
        const char* nm = window_theme_name(var);
        if (int d = cfg_select_row(K, ctrl_uid_i(uidBase, 6), x, y, w, tr("Game skin", "Habillage du jeu"), nm ? nm : "?", ownWhy, nullptr, 0)) {
            *r.theme = window_theme_index(0, (var + d + nV) % nV); save_ui_config(); }
        return;
    }
    if (cfg_toggle_row(K, ctrl_uid_i(uidBase, 7), x, y, w, tr("Custom colour", "Couleur perso"), *r.hue != 0, ownWhy, r.hue, 4)) {
        *r.hue = *r.hue ? 0u : (box_hue_color(var) | 0xFF000000u); save_ui_config(); }
    if (!ownWhy && *r.hue) {   // the free colour
        if (color_picker(K.dev, K.fr, K.mo, ctrl_uid_i(uidBase, 8), ctrl_uid_i(uidBase, 9), x + K.px(18.0f), y + K.px(4.0f), w - K.px(36.0f), r.hue)) save_ui_config();
        y += color_picker_height() + K.px(12.0f);
    } else if (!ownWhy) {      // the preset hues : enamel in steel bezels, the chosen one ringed in brass
        const float s = K.px(34.0f), gap = K.px(8.0f), x0 = x + K.px(18.0f);
        int perRow = (int)((w - K.px(36.0f) + gap) / (s + gap)); if (perRow < 1) perRow = 1;
        const int n = box_hue_count();
        const float gy = y + K.px(6.0f);
        for (int i = 0; i < n; ++i) {
            const float sx = x0 + (i % perRow) * (s + gap), sy = gy + (i / perRow) * (s + gap);
            if (kit_swatch(K, ctrl_uid_i(uidBase, 100 + i), sx, sy, box_hue_color(i) | 0xFF000000u, i == var ? KS_FOCUS : 0)) {
                *r.theme = window_theme_index(fam, i); save_ui_config(); }
        }
        y = gy + ((n + perRow - 1) / perRow) * (s + gap) + K.px(6.0f);
    }
    float v01 = (*r.lum + 1.0f) * 0.5f; v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
    const int pct = (int)(*r.lum * 100.0f + (*r.lum >= 0.0f ? 0.5f : -0.5f));
    char b[16]; _snprintf(b, sizeof(b), "%+d %%", pct); b[15] = 0;
    if (cfg_slider_row(K, ctrl_uid_i(uidBase, 10), x, y, w, tr("Luminosity", "Luminosit\xC3\xA9"), &v01, b, ownWhy, r.lum, 4, true))
        *r.lum = floorf((v01 * 2.0f - 1.0f) * 100.0f + 0.5f) / 100.0f;
}

// ======== the shared TEXT block ========
int cfg_text_modified(TextStyle* const* ts, int n) {   // an element counts when one of its ROWS would carry the bead
    int k = 0;
    for (int i = 0; i < n; ++i) {
        TextStyle& t = *ts[i];
        if (differs(&t.face, 4, false) || differs(&t.size, 4, true) || differs(&t.outline, 4, true) || differs(&t.bold, 1, false) ||
            differs(&t.italic, 1, false) || differs(&t.upper, 1, false) || differs(&t.colorOn, 1, false)) ++k;
    }
    return k;
}
void cfg_text_rows(Kit& K, int uidBase, float x, float& y, float w, const char* const* labels, TextStyle* const* tsl, int n, int* sel) {
    if (n <= 0) return;
    int te = (*sel < 0 || *sel >= n) ? 0 : *sel;
    // the element : marked when ITS style differs ; no reset from here (a right-click resets one setting, not a block)
    if (int d = cfg_select_row(K, ctrl_uid_i(uidBase, 0), x, y, w, tr("Element", "\xC3\x89l\xC3\xA9ment"), labels[te], nullptr, nullptr, 0)) {
        te = (te + d + n) % n; *sel = te; }
    TextStyle& ts = *tsl[te];
    const int nf = ui_font_count() < 1 ? 1 : ui_font_count();
    int fc = ts.face; if (fc < 0 || fc >= nf) fc = 0;
    if (int d = cfg_select_row(K, ctrl_uid_i(uidBase, 1), x, y, w, tr("Font", "Police"), ui_font_label(fc), nullptr, &ts.face, 4)) {
        ts.face = (fc + d + nf) % nf; save_ui_config(); }
    auto pct = [&](int id, const char* en, const char* fr, float* f, float lo, float hi) {
        float v01 = (*f - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
        char b[16]; _snprintf(b, sizeof(b), "%d %%", (int)(*f * 100.0f + 0.5f)); b[15] = 0;
        if (cfg_slider_row(K, ctrl_uid_i(uidBase, id), x, y, w, tr(en, fr), &v01, b, nullptr, f, 4, true)) {
            const float v = floorf((lo + v01 * (hi - lo)) / 0.05f + 0.5f) * 0.05f; *f = v < lo ? lo : (v > hi ? hi : v); }
    };
    pct(2, "Size", "Taille", &ts.size, 0.5f, 2.0f);
    pct(3, "Outline", "Contour", &ts.outline, 0.0f, 2.0f);
    if (cfg_toggle_row(K, ctrl_uid_i(uidBase, 4), x, y, w, tr("Bold", "Gras"), ts.bold, nullptr, &ts.bold, (int)sizeof(bool)))           { ts.bold = !ts.bold; save_ui_config(); }
    if (cfg_toggle_row(K, ctrl_uid_i(uidBase, 5), x, y, w, tr("Italic", "Italique"), ts.italic, nullptr, &ts.italic, (int)sizeof(bool)))  { ts.italic = !ts.italic; save_ui_config(); }
    if (cfg_toggle_row(K, ctrl_uid_i(uidBase, 6), x, y, w, tr("Capitals", "Majuscules"), ts.upper, nullptr, &ts.upper, (int)sizeof(bool))) { ts.upper = !ts.upper; save_ui_config(); }
    if (cfg_toggle_row(K, ctrl_uid_i(uidBase, 7), x, y, w, tr("Own colour", "Couleur perso"), ts.colorOn, nullptr, &ts.colorOn, (int)sizeof(bool))) {
        ts.colorOn = !ts.colorOn; if (ts.colorOn && (ts.color >> 24) == 0) ts.color |= 0xFF000000u; save_ui_config(); }
    if (ts.colorOn) {
        if (color_picker(K.dev, K.fr, K.mo, ctrl_uid_i(uidBase, 8), ctrl_uid_i(uidBase, 9), x + K.px(18.0f), y + K.px(4.0f), w - K.px(36.0f), &ts.color)) save_ui_config();
        y += color_picker_height() + K.px(12.0f);
        const int a = (int)((ts.color >> 24) & 0xFFu); float v01 = a / 255.0f;
        char b[16]; _snprintf(b, sizeof(b), "%d %%", (int)(v01 * 100.0f + 0.5f)); b[15] = 0;
        if (cfg_slider_row(K, ctrl_uid_i(uidBase, 10), x, y, w, tr("Opacity", "Opacit\xC3\xA9"), &v01, b, nullptr, nullptr, 0, false)) {
            int nv = (int)(v01 * 255.0f + 0.5f); nv = nv < 0 ? 0 : (nv > 255 ? 255 : nv); ts.color = (ts.color & 0x00FFFFFFu) | ((u32)nv << 24); }
    }
}

} // namespace aio
