// config_page.cpp -- see config_page.h. Polished, animated, web-style config overlay.
// The AIOHUD window skin fills the WHOLE page (it IS the frame) ; the UI is drawn directly on it.
//
// Motion model : everything interactive eases. A tiny id->value animation table (g_anim) holds one
// 0..1 spring per element ; ease(id, target) nudges it toward target by the frame dt -> smooth hover,
// toggle crossfades, knob grow, etc. The page also fades + scales in, and the content rows stagger.
#include "ui/config_page.h"
#include "ui/config_controls.h"   // shared config toolkit (palette, ease, primitives, controls)
#include "ui/config_rows.h"       // ROW_BAND / ROW_NEXT row-layout macros (shared with the *_config.cpp panels)
#include "gfx/draw.h"      // grad_quad
#include "gfx/texture.h"   // load_raw_texture (logo)
#include "gfx/font.h"
#include "gfx/window.h"
#include "model/ui_config.h"
#include "model/gamestate.h"   // GameState::me (character name) for the Profile page
#include "model/map_dat.h"     // load_zone_map : the Help owns its own copy of the live zone map (radar sample)
#include "ui/edit_box.h"       // edit_drag_busy() : hide the edit-layout toolbar while a box is being dragged
#include "ui/party.h"          // party_gauge() : the REAL HP/MP/TP liquid gauge, for the Help live samples
#include "ui/target.h"         // target_help_* : the REAL Target element samples (HP+trail, range, debuffs, TH)
#include "ui/minimap.h"        // minimap_help_* : the REAL Minimap element samples (round disc, legend, moon, day)
#include <cmath>
#include <cstdio>
#include <cstring>

namespace aio {

#ifndef AIOHUD_VERSION
#define AIOHUD_VERSION "dev"
#endif

// Keyboard text entry is fed by the plugin's slot-14 hook (see aio_plugin_key) into ConfigPage's
// feed_char/backspace/enter while the name field is focused -- the hook CONSUMES those keys so the
// game never sees them. No per-frame Win32 polling here.

static const char* TABS[]     = { "Configuration", "Profile", "Edit Layout", "Help", "Update", "Debug" };
static const int   NTABS      = 6;
static const char* tab_label(int i) {
    static const char* en[] = { "Configuration", "Profile", "Edit Layout", "Help", "Update", "Debug" };
    static const char* fr[] = { "Configuration", "Profil", "\xC3\x89""diter dispo", "Aide", "Mise \xC3\xA0 jour", "Debug" };
    return (ui_config().lang == 1 ? fr : en)[i];
}

// Update-tab bridge (implemented in the plugin layer, aiohud.cpp).
int  aio_update_check_status(char* ver, int n);   // 0 unknown/checking, 1 up-to-date, 2 available, 3 error ; fills ver
void aio_update_spawn_check();                     // re-run the no-window check
void aio_update_request();                          // write the flag the AioUpdate addon polls -> full //aioupdate
int  aio_update_progress(char* msg, int n);         // 0 idle, 1 working, 2 up-to-date, 3 updated, 4 error ; fills msg (version / error)
void aio_update_clear();                            // forget the in-progress state -> back to the normal check flow
const char* aio_version_string();
// Settings modules (the Configuration sidebar). Add a module here = a new settings page ; the profile
// is GLOBAL (a profile snapshots every module), so it lives in the profile bar, not per-module.
static const char* MODULES[]  = { "Party / Alliance", "Target", "Player", "Minimap", "Arcade WS", "Skillchains", "Treasure Pool", "Hate List", "PointWatch", "Grimoire (SCH)", "Zone Tracker", "Timers", "EmpyPop", "Interface" };
static const int   MODULE_N   = (int)(sizeof(MODULES) / sizeof(MODULES[0]));
static const int   SEC_INTERFACE = MODULE_N - 1;               // "Interface" = the config MENU's own look (font + accent) ; last sidebar entry, not a HUD module
static const char* module_label(int i) {                       // Configuration sidebar module name (localized)
    static const char* fr[] = { "Groupe / Alliance", "Cible", "Joueur", "Minicarte", "Arcade WS", "Skillchains", "Tr\xC3\xA9sor", "Liste de haine", "PointWatch", "Grimoire (SCH)", "Zone Tracker", "Timers", "EmpyPop", "Interface" };
    return (ui_config().lang == 1 && i >= 0 && i < (int)(sizeof(fr) / sizeof(fr[0]))) ? fr[i] : MODULES[i];
}

void ConfigPage::dispose() {   // //unload : device is still alive -> RELEASE our owned Help/preview textures (helpCursorTex_ is handed in, NOT owned)
    if (logoTex_)    release_texture(logoTex_);
    if (tgtBuffTex_) release_texture(tgtBuffTex_);
    if (tgtThTex_)   release_texture(tgtThTex_);
    if (mmMkPlayer_) release_texture(mmMkPlayer_);
    if (mmMkMob_)    release_texture(mmMkMob_);
    if (mmElem_)     release_texture(mmElem_);
    if (mmMoonTex_)  release_texture(mmMoonTex_);
    if (mmMapTex_)   release_texture(mmMapTex_);
    on_device_lost();   // zero the handles + retry flags
}
void ConfigPage::set_tab(int t)     { if (t >= 0 && t < NTABS) tab_ = t; }
void ConfigPage::set_section(int s) { if (s >= 0) section_ = s; }   // (sections folded into the profile sidebar)
// a crisp little VECTOR icon for a tab/module, drawn in `col` with an optional soft accent glow behind.
// kind 0 = gear (Configuration), 1 = person (Profile), 2 = "?" (Help). `s` = icon box size (px).
static void tab_icon(u32 dev, int kind, float cx, float cy, float s, u32 col, Font* fo, u32 glowCol, float glow) {
    const float r = s * 0.5f;
    if (glow > 0.01f) {   // soft accent bloom seated under the icon
        cs_add(dev);
        soft_blob(dev, cx, cy, r + snap(6.0f), r + snap(6.0f), (glowCol & 0x00FFFFFF) | ((u32)(70.0f * clampf(glow, 0.0f, 1.0f) * g_fade) << 24));
    }
    if (kind == 0) {                                          // GEAR : 8 teeth poking past a disc body + a dark hub
        cs(dev);
        for (int k = 0; k < 8; ++k) {
            const float a = k * (PI_ / 4.0f), dx = cosf(a), dy = sinf(a);
            seg_soft(dev, cx + dx * r * 0.60f, cy + dy * r * 0.60f, cx + dx * r * 1.04f, cy + dy * r * 1.04f, snap(3.2f), fa(col));
        }
        disc(dev, cx, cy, r * 0.74f, fa(col));               // body
        disc(dev, cx, cy, r * 0.30f, fa(shade(col, -0.55f)));// hub hole (reads as the gear centre)
    } else if (kind == 1) {                                   // PERSON : head disc + a shoulders dome
        cs(dev);
        disc(dev, cx, cy - r * 0.44f, r * 0.36f, fa(col));            // head
        qfan(dev, cx, cy + r * 0.74f, r * 0.66f, PI_, 2.0f * PI_, col);// shoulders (top half-disc)
    } else if (kind == 2) {                                   // EDIT LAYOUT : four corner brackets (a "frame / move" marker)
        cs(dev);
        const float q = r * 0.85f, l = r * 0.52f, t = snap(2.6f);
        const float xs[2] = { cx - q, cx + q }, ys[2] = { cy - q, cy + q };
        for (int a = 0; a < 2; ++a) for (int b = 0; b < 2; ++b) {
            const float x = xs[a], y = ys[b], dxh = (a == 0) ? l : -l, dyv = (b == 0) ? l : -l;
            seg_soft(dev, x, y, x + dxh, y, t, fa(col));
            seg_soft(dev, x, y, x, y + dyv, t, fa(col));
        }
    } else if (kind == 4) {                                   // UPDATE : a download arrow dropping into a tray
        cs(dev);
        const float t = snap(2.8f);
        seg_soft(dev, cx, cy - r * 0.72f, cx, cy + r * 0.18f, t, fa(col));                 // shaft
        seg_soft(dev, cx, cy + r * 0.30f, cx - r * 0.42f, cy - r * 0.16f, t, fa(col));     // left barb
        seg_soft(dev, cx, cy + r * 0.30f, cx + r * 0.42f, cy - r * 0.16f, t, fa(col));     // right barb
        seg_soft(dev, cx - r * 0.62f, cy + r * 0.66f, cx + r * 0.62f, cy + r * 0.66f, t, fa(col)); // tray base
    } else {                                                  // HELP : the universal "?" glyph, icon-sized
        fo->begin(dev);
        fo->draw_c(dev, cx, cy, "?", s * 1.18f, col, fa(C_STROKE), 1.2f);
    }
}
// AioHud's OWN arrow cursor, drawn last on top of everything. Both call sites are config / edit-layout only,
// and those now FULLY capture the mouse (the game no longer gets events -> it stops drawing its native cursor),
// so we always draw ours here whenever the game is focused -- otherwise there would be NO pointer at all. (This
// used to be gated on the "Cursor" option, which only mattered back when the game still received the pointer ;
// that option is now redundant.) Skip when the game isn't foreground : another window owns the pointer then.
static void draw_ui_cursor(u32 dev, const MouseState* mo) {
    // Draw whenever the pointer is over the game window -- NOT only when the game is foreground. Hiding the
    // native cursor and drawing ours must switch together, or the game's cursor reappears in the gap.
    if (!mo || !(mo->focused || mo->overGame)) return;
    const float x = snap(mo->x), y = snap(mo->y), s = snap(1.0f);
    const float cx[3] = { x, x, x + 11.0f * s };
    const float cy[3] = { y, y + 16.0f * s, y + 11.0f * s };
    for (int i = 0; i < 3; ++i) seg_soft(dev, cx[i], cy[i], cx[(i + 1) % 3], cy[(i + 1) % 3], snap(3.2f), 0xE6000000u);   // dark outline (visible on any background)
    const float xy[6] = { cx[0], cy[0], cx[1], cy[1], cx[2], cy[2] };
    fill_poly_aa(dev, xy, 3, 0xFFFFFFFFu);                                                                               // white arrow fill
}

// Hate List Help sample : the REAL Hate List renderer in preview mode (config-aware fiole rows). Defined in
// hud_hatelist.cpp, reused here so the Help shows exactly the box you run.
void hatelist_help_box(const Frame& f, float cx, float cy, float s);
// PointWatch Help sample : the REAL PointWatch renderer in preview mode (config-aware). Defined in hud_pointwatch.cpp.
void pointwatch_help_box(const Frame& f, float cx, float cy, float s);
void pointwatch_help_fit(const Frame& f, float availW, float maxScale, float& outScale, float& outH);
// Timers Help sample : the REAL Timers renderer in preview mode (config-aware). Defined in hud_timers.cpp.
void timers_help_box(const Frame& f, float cx, float cy, float s);
void timers_help_fit(const Frame& f, float availW, float maxScale, float& outScale, float& outH);
// Skillchains Help sample : the REAL Skillchains renderer in preview mode (config-aware). Defined in hud_skillchains.cpp.
void skillchains_help_box(const Frame& f, float cx, float cy, float s);
void skillchains_help_fit(const Frame& f, float availW, float maxScale, float& outScale, float& outH);
// Treasure Pool Help sample : the REAL Treasure renderer in preview mode (config-aware). Defined in hud_treasure.cpp.
void treasure_help_box(const Frame& f, float cx, float cy, float s);
void treasure_help_fit(const Frame& f, float availW, float maxScale, float& outScale, float& outH);
// Zone Tracker Help sample : the REAL Zone Tracker renderer in preview mode for a forced content variant (0 Dynamis /
// 1 Abyssea / 2 Omen / 3 Nyzul / 4 Odyssey), config-aware. Defined in hud_zonetracker.cpp.
void zonetracker_help_box(const Frame& f, float cx, float cy, float s, int variant);
void zonetracker_help_fit(const Frame& f, float availW, float maxScale, int variant, float& outScale, float& outH);
void zonetracker_help_measure(const Frame& f, int variant, float& outW, float& outH);
// EmpyPop Help sample : the REAL EmpyPop stack in preview mode (the built-in chloris chain), config-aware. Defined in hud_empypop.cpp.
void empypop_help_box(const Frame& f, float cx, float cy, float s);
void empypop_help_fit(const Frame& f, float availW, float maxScale, float& outScale, float& outH);

#include "ui/config_help_data.h"   // HELP_* pages + HELP_MODULES[] (data only -- see the header)

// Shared GRABBABLE vertical scrollbar for the config scroll regions (options viewport, Help, Update changelog,
// Debug). Draws the track + thumb AND handles thumb-drag -- every one of these bars used to draw a DEAD thumb and
// scroll ONLY by the wheel, so a user who grabs the bar (or has no wheel) couldn't reach the lower rows (reported
// by morph). One bar dragged at a time, keyed by `barId`. Immediate-mode : the new `scroll` shifts the rows NEXT
// frame ; the thumb is moved THIS frame so it tracks the cursor. Call once per region, after this frame's content
// extent (contentH) is known. No-op (and releases any grab) when the region isn't tall enough to scroll.
static int   s_sbGrab = 0;         // barId currently being dragged (0 = none)
static float s_sbGrabOff = 0.0f;   // cursor -> thumb-top offset captured on grab
static void scrollbar_vert(u32 dev, const MouseState* mo, int barId, float sbx, float top, float sbw,
                           float viewH, float contentH, float& scroll, float maxS) {
    if (maxS <= 0.5f || contentH <= 1.0f) { if (s_sbGrab == barId) s_sbGrab = 0; return; }
    flat(dev, sbx, top, sbw, viewH, 0x22000000u);                                       // track
    float thH = viewH * (viewH / contentH); if (thH < snap(24.0f)) thH = snap(24.0f);
    float thY = top + (viewH - thH) * (scroll / maxS);
    const bool hov = inrect(mo, sbx - snap(3.0f), thY, sbw + snap(6.0f), thH);           // generous grab area
    if (mo && mo->down && !s_sbGrab && hov) { s_sbGrab = barId; s_sbGrabOff = mo->y - thY; }
    if (!(mo && mo->down) && s_sbGrab == barId) s_sbGrab = 0;
    if (s_sbGrab == barId && mo) {
        float t = (viewH - thH > 0.0f) ? (mo->y - s_sbGrabOff - top) / (viewH - thH) : 0.0f;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        scroll = t * maxS;
        thY = top + (viewH - thH) * (scroll / maxS);                                     // reflect the drag this same frame
    }
    const u32 col = (hov || s_sbGrab == barId) ? 0x99FFFFFFu : 0x66FFFFFFu;              // brighter on hover / drag
    rrect_fill(dev, sbx, thY, sbw, thH, sbw * 0.5f, col, col);                           // thumb
}

// Draw word-wrapped text from (x,y), returning the new y. Lines whose center is outside [top,bot] are
// skipped -- a cheap clip for the scrolling viewport (D3D8 has no scissor rect).
// Word-wrap with tiny inline markup : *bold* (brighter + a heavier outline) and _highlight_ (gold).
// Drawn word by word so emphasis can change mid-line. Markers are consumed, not shown. STRICT top/bot clip.
static float draw_wrapped(u32 dev, Font* fo, float x, float y, float maxW, float top, float bot,
                          const char* text, float sz, u32 col, float lineH) {
    const float spaceW = fo->measure(" ", sz);
    const char* p = text; char word[160];
    float lineX = x; bool lineStart = true; int emph = 0;   // 0 normal, 1 bold, 2 highlight (carried across words)
    for (;;) {
        while (*p == ' ') ++p;
        while (*p == '*' || *p == '_') { emph = (*p == '*') ? (emph == 1 ? 0 : 1) : (emph == 2 ? 0 : 2); ++p; }
        const int wEmph = emph;
        int wl = 0;
        while (*p && *p != ' ' && wl < 159) {
            if (*p == '*') { emph = (emph == 1 ? 0 : 1); ++p; continue; }
            if (*p == '_') { emph = (emph == 2 ? 0 : 2); ++p; continue; }
            word[wl++] = *p++;
        }
        word[wl] = 0;
        if (wl > 0) {
            const float ww = fo->measure(word, sz);
            if (!lineStart && lineX + spaceW + ww > x + maxW) { y += lineH; lineX = x; lineStart = true; }   // wrap
            if (!lineStart) lineX += spaceW;
            const u32 wc = (wEmph == 1) ? C_TEXT : (wEmph == 2) ? C_GOLD : col;
            // draw_lv centres on the font's CONSTANT cap box (not each word's own ink box), so words with
            // descenders (Party, gauge) sit on the SAME baseline as words without (Leader) -> no vertical jitter.
            if (y >= top && y + lineH <= bot) { fo->begin(dev); fo->draw_lv(dev, lineX, y + lineH * 0.5f, word, sz, fa(wc), fa(C_STROKE), 1.0f); }   // emphasis is COLOUR only (bright = bold, gold = highlight)
            lineX += ww; lineStart = false;
        }
        if (!*p) break;
    }
    return y + lineH;
}

// ---- Configuration-tab category blocks (INTERFACE / LAYOUT), lifted verbatim from draw()s tab_==0 body
//      (byte-identical). Placed before draw() so the ROW_BAND/ROW_NEXT macros (config_rows.h) are in scope. ----
void ConfigPage::draw_interface_category(u32 dev, Font* fo, const MouseState* mo, bool click,
                                         float& ry, int& ri, float e,
                                         float bandX, float bandW, float coX, float ctrlW,
                                         float hdrX, float hdrW) {
        // ===== category : INTERFACE (this config menu's own look) =====
        const float blk3 = ry;
        if (catOpen_[3]) cat_panel(dev, hdrX, ry, hdrW, catH_[3]);   // solid menu card behind the OPEN section (last frame's height)
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Interface", "Interface"), catOpen_[3])) catOpen_[3] = !catOpen_[3];
        ROW_NEXT(42.0f)
        if (catOpen_[3]) {
        // Font : the config-menu font (also the HUD's default text face ; per-element faces override it under Advanced > Typography)
        { ROW_BAND(52.0f)
          int gf = ui_config().text[0][TE_UI].face; if (gf < 0 || gf >= ui_font_count()) gf = 0;
          if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Font", "Police"), ui_font_label(gf))) {
              gf = wrap(gf + d, ui_font_count());
              ui_config().text[0][TE_UI].face = gf;   // the config menu font (changes live)
              ui_config().fontFace = gf;              // and the HUD default text face
              save_ui_config(); } }
        ROW_NEXT(52.0f)
        // (The old "Cursor" toggle was removed : the config/edit overlay now fully captures the mouse, so AioHud's
        //  own pointer is always drawn and the game's native cursor no longer competes -- the option did nothing.)
        // Hide key (End) : HOLD = the HUD is hidden only while End is held (peek) ; TOGGLE = one press hides, the next shows.
        { ROW_BAND(48.0f)
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Hide key", "Touche masquer"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            const bool tog = ui_config().hidePeekMode != 0;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, tog ? tr("Toggle", "Bascule") : tr("Hold", "Maintien"), tog)) {
                ui_config().hidePeekMode = tog ? 0 : 1; save_ui_config(); }
        }
        ROW_NEXT(48.0f)
        // Custom accent : a toggle that swaps the style presets + nuance chart for the free HSV picker
        // (uiAccent != 0 = custom). Shown FIRST : a custom accent overrides style/colour in apply_ui_theme, so the
        // style selector + chart below are only shown when Custom is OFF (else they'd sit there inert).
        { ROW_BAND(48.0f)
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Custom colour", "Couleur perso"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            const bool on = ui_config().uiAccent != 0;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, on ? tr("On", "Oui") : tr("Off", "Non"), on)) {
                ui_config().uiAccent = on ? 0u : (theme_accent(ui_config().uiStyle, ui_config().uiColor) | 0xFF000000u); save_ui_config(); }
            ROW_NEXT(48.0f)
        }
        if (ui_config().uiAccent != 0) {
            CFG_COLOR_PICKER(&ui_config().uiAccent)
        } else {
            // Colour style : a family of accents (Neon / Matte / Medieval / Heroic / Pastel)
            { ROW_BAND(52.0f)
                const int st = (ui_config().uiStyle < 0 || ui_config().uiStyle >= STYLE_N) ? 0 : ui_config().uiStyle;
                const char* nm = (ui_config().lang == 1) ? STYLES[st].fr : STYLES[st].en;
                if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Colour style", "Style couleur"), nm)) {
                    ui_config().uiStyle = wrap(st + d, STYLE_N);
                    if (ui_config().uiColor >= style_swatch_count(ui_config().uiStyle)) ui_config().uiColor = 0;
                    save_ui_config(); }
            }
            ROW_NEXT(52.0f)
            // Colour : a real NUANCE CHART -- one hue per column, lightness rows top(light)->bottom(deep). Click to pick.
            {
                const int st = (ui_config().uiStyle < 0 || ui_config().uiStyle >= STYLE_N) ? 0 : ui_config().uiStyle;
                const ThemeStyle& S = STYLES[st];
                const int COLS = S.n, nrows = NUANCE_ROWS, total = COLS * nrows;
                const float sw2 = snap(22.0f), sg = snap(7.0f);
                const float gridH = nrows * sw2 + (nrows - 1) * sg, slotH = gridH + snap(20.0f);
                ROW_BAND(slotH) (void)yo;
                fo->begin(dev);
                fo->draw_lc(dev, coX + snap(4.0f), ry + slotH * 0.5f, tr("Colour", "Couleur"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
                const float gridW = COLS * sw2 + (COLS - 1) * sg;
                const float gx = coX + ctrlW - gridW, gy = ry + (slotH - gridH) * 0.5f;
                for (int k = 0; k < total; ++k) {
                    const int col = k % COLS, row = k / COLS;
                    const u32 c = nuance(S.col[col], row);               // column = hue, row = lightness
                    const float xk = gx + col * (sw2 + sg), yk = gy + row * (sw2 + sg);
                    const bool sel = (ui_config().uiColor == k);
                    if (sel) { cs_add(dev); rrect_glow(dev, xk, yk, sw2, sw2, snap(6.0f), (c & 0x00FFFFFF) | 0x80000000, snap(6.0f)); cs(dev); }
                    rrect_fill(dev, xk, yk, sw2, sw2, snap(6.0f), c, shade(c, -0.28f));
                    outline(dev, xk, yk, sw2, sw2, sel ? 0xFFFFFFFF : C_BORDER);
                    if (inrect(mo, xk, yk, sw2, sw2) && click) { ui_config().uiColor = k; save_ui_config(); }
                }
                ROW_NEXT(slotH)
            }
        }
        }   // end category Interface (catOpen_[3])
        catH_[3] = catOpen_[3] ? (ry - blk3) : 0.0f;   // measure Interface block -> next frame's card
        ry += snap(10.0f);                             // gap between category cards
}
void ConfigPage::draw_layout_category(u32 dev, Font* fo, const MouseState* mo, bool click,
                                      float& ry, int& ri, float e,
                                      float bandX, float bandW, float coX, float ctrlW,
                                      float hdrX, float hdrW) {
        // ===== category : LAYOUT (placement + the shared window skin) =====
        const float blk2 = ry;
        if (catOpen_[2]) cat_panel(dev, hdrX, ry, hdrW, catH_[2]);
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Layout", "Disposition"), catOpen_[2])) catOpen_[2] = !catOpen_[2];
        ROW_NEXT(42.0f)
        if (catOpen_[2]) {
        // Placement moved to the top-level "Edit Layout" tab (drag/resize + Zones/Rules) ; here : a pointer + global reset.
        { ROW_BAND(44.0f)
            const float ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + snap(19.0f), tr("Move & resize boxes: the \"Edit Layout\" tab (top)", "D\xC3\xA9placer & redimensionner : onglet \"\xC3\x89""diter dispo\" (en haut)"), snap(13.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        }
        ROW_NEXT(44.0f)
        { ROW_BAND(56.0f)
            const float bh = snap(34.0f), bw = snap(168.0f);
            const float ty = ry + yo + (snap(40.0f) - bh) * 0.5f;   // 34px button -> recentre in the band
            const float defX = coX + ctrlW - bw;
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + bh * 0.5f, tr("Reset all", "Tout r\xC3\xA9initialiser"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            if (push_btn(dev, fo, mo, click, CTRL_ID, defX, ty, bw, bh, tr("Default (all)", "D\xC3\xA9""faut (tout)"), 1)) reset_ui_config();
        }
        ROW_NEXT(56.0f)
        }   // end category Layout
        catH_[2] = catOpen_[2] ? (ry - blk2) : 0.0f;
}

void ConfigPage::draw(const Frame& f, float sw, float sh) {
    pvOn_ = false;   // live-preview anchor : off unless we reach the Configuration tab below
    helpPlayer_ = false;   // off unless the Help tab reaches the visible Player-box sample this frame
    if (!open_) return;
    apply_ui_theme(ui_config().uiStyle, ui_config().uiColor);   // derive the accent family from the chosen style+colour (before any chrome draws)
    strncpy(activeProf_, active_profile_name(), sizeof(activeProf_) - 1); activeProf_[sizeof(activeProf_) - 1] = 0; profSynced_ = true;   // always reflect the live active profile (startup + auto-load on job change)
    u32 dev = f.dev;
    // the whole config interface is drawn in the Interface text-element face (default Verdana)
    const TextStyle& tsUI = ui_config().text[0][TE_UI];   // the interface (menu) font lives in the Party group
    const char* uiFace = tsUI.face > 0 ? ui_font_face(tsUI.face) : "Verdana";
    Font* fo = (f.fonts ? f.fonts->get(uiFace, tsUI.bold ? 700 : 600, tsUI.italic) : f.font);
    if (fo) fo->ensure(dev);
    if (!fo || !fo->ready()) fo = f.font;
    if (!fo || !fo->ready() || sw <= 0 || sh <= 0) return;
    fo->set_upper(tsUI.upper);   // Interface element : UPPERCASE the whole menu font (reset at the end so it never leaks)
    const MouseState* mo = f.mouse;
    const bool click = mo && mo->clicked;

    // --- frame clock (shared by both modes) : drives the open fade + every ease() spring ---
    float dt = (lastT_ < 0.0f) ? 0.016f : (f.t - lastT_);
    if (dt < 0.0f || dt > 0.25f) dt = 0.016f;
    lastT_ = f.t;
    g_dt = dt;
    g_t  = f.t;

    // --- EDIT LAYOUT mode : hide the page (the game + the real boxes show through) and draw only a
    //     floating toolbar. The party/alliance boxes handle their own drag/resize (see party.cpp). ---
    if (ui_config().editLayout) { draw_edit_layout(f, dev, fo, mo, click, sw, sh); return; }

    // --- open animation : fade + a tiny scale-in ---
    anim_ = clampf(anim_ + dt / 0.18f, 0.0f, 1.0f);
    const float e = 1.0f - (1.0f - anim_) * (1.0f - anim_) * (1.0f - anim_);   // ease-out cubic
    g_fade = e;
    const float pulse = 0.5f + 0.5f * sinf(f.t * 3.0f);                        // 0..1 slow pulse

    // ===== BACK LAYER : an OPAQUE page, leaving a HOLE at the live-preview stage so the REAL game shows
    //       through it (transparent preview). The hole is last frame's stage rect (stable ; cleared each
    //       frame and refilled by the preview stage below, so it vanishes when no preview is shown). =====
    const float hx = pvStageX_, hy = pvStageY_, hw = pvStageW_, hh = pvStageH_;
    pvStageW_ = 0.0f;
    const u32 BG = 0xFF0E131C;
    if (hw > 1.0f && hh > 1.0f) {
        flat(dev, 0, 0, sw, hy, BG);                                  // top strip above the hole
        flat(dev, 0, hy + hh, sw, sh - (hy + hh), BG);                // bottom strip below
        flat(dev, 0, hy, hx, hh, BG);                                 // left strip
        flat(dev, hx + hw, hy, sw - (hx + hw), hh, BG);               // right strip
    } else {
        flat(dev, 0, 0, sw, sh, BG);                                  // no preview -> full opaque page
    }
    flat(dev, 0, 0, sw, snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse));           // top GOLD hairline (FFXI glint)
    flat(dev, 0, 0, sw, 1, 0x40FFFFFF);                               // crisp top inner highlight
    outline(dev, 0, 0, sw, sh, C_BORDERHI);

    // content inset from the skin border (no second frame -- we draw straight on the skin)
    const float m = snap(30.0f);
    const float ix = m, iy = m, iw = sw - 2 * m;
    const float pageBot = sh - m;

    // ===== HEADER : the AIOHUD wordmark as a GILDED heraldic emblem (heroic-fantasy, not hi-tech) =====
    const float titleSz = snap(34.0f);
    fo->begin(dev);
    const float tw = fo->measure("AIOHUD", titleSz);
    const float gemR = snap(6.0f), gemGap = snap(16.0f);
    const float wx = ix + gemR * 2.0f + gemGap;                                // room for the left lozenge ornament
    const float ty = iy + snap(23.0f);
    const float bandTop = ty - titleSz * 0.62f, bandBot = ty + titleSz * 0.54f;
    const u32 acc = C_ACCENT & 0x00FFFFFF;
    // warm torchlight glow behind the emblem
    cs_add(dev); soft_blob(dev, wx + tw * 0.5f, ty - snap(1.0f), tw * 0.64f, snap(28.0f), ((u32)(46.0f * (0.6f + 0.4f * pulse)) << 24) | acc); cs(dev);
    // gilded wordmark : bright top -> deep bottom = engraved gilt (accent-tinted), extruded for relief
    chrome_text(dev, fo, wx, ty, "AIOHUD", titleSz, tw, C_ACCENTHI, shade(C_ACCENT, -0.5f), bandTop, bandBot);
    shine(dev, wx - snap(4.0f), bandTop, tw + snap(8.0f), bandBot - bandTop, 0.32f, f.t * 0.6f);   // slow gilt gleam
    // flanking heraldic lozenges : dark base (engraved edge) + gilt face + a tiny highlight facet
    const float lgx = ix + gemR, rgx = wx + tw + gemGap - snap(2.0f);
    for (int gi = 0; gi < 2; ++gi) {
        const float gxo = gi ? rgx : lgx;
        gem(dev, gxo, ty, gemR + snap(1.5f), shade(C_ACCENT, -0.6f));
        gem(dev, gxo, ty, gemR, C_GOLD);
        gem(dev, gxo - snap(1.0f), ty - snap(1.5f), gemR * 0.42f, C_ACCENTHI);
    }
    // ornamental gilt rule beneath the wordmark, with lozenge terminals + a centre gem
    { const float uy = bandBot + snap(1.0f), u0 = wx - snap(2.0f), u1 = wx + tw + snap(2.0f), umid = (u0 + u1) * 0.5f;
      cs(dev);
      flat(dev, u0, uy, u1 - u0, snap(1.0f), fa((0x99u << 24) | acc));
      flat(dev, u0 + snap(12.0f), uy + snap(2.0f), (u1 - u0) - snap(24.0f), snap(1.0f), fa((0x3Cu << 24) | acc));   // faint second rule
      gem(dev, umid, uy + snap(1.0f), snap(3.5f), C_GOLDHI);
      gem(dev, u0, uy + snap(0.5f), snap(2.5f), C_GOLD);
      gem(dev, u1, uy + snap(0.5f), snap(2.5f), C_GOLD); }
    // subtitle : the AioHud VERSION (small-caps), to the right of the emblem -- shown on every tab.
    fo->begin(dev); fo->draw_lc(dev, rgx + snap(14.0f), ty + snap(1.0f), "V" AIOHUD_VERSION, snap(15.0f), fa(lerpc(C_ACCENT, C_ACCENTHI, pulse)), fa(C_STROKE), 1.2f);

    // close button (X), top-right -- eased red crossfade + a tiny size bump on hover
    const float cbS = snap(36.0f), cbX = ix + iw - cbS, cbY = iy + snap(2.0f);
    const bool cbHov = inrect(mo, cbX, cbY, cbS, cbS);
    const float ct = ease(1, cbHov ? 1.0f : 0.0f);
    halo(dev, cbX, cbY, cbS, cbS, C_CLOSEHOV, ct * 0.9f);
    rpanel(dev, cbX, cbY, cbS, cbS, snap(cbS * 0.30f), lerpc(C_CTL_T, C_CLOSEHOV, ct), lerpc(C_CTL_B, 0xFFA0303A, ct), lerpc(C_CTL_BR, 0xFFE57078, ct), snap(1.5f));
    fo->begin(dev);
    fo->draw_c(dev, cbX + cbS * 0.5f, cbY + cbS * 0.5f, "X", snap(19.0f), fa(C_TEXT), fa(C_STROKE), 1.3f + 0.3f * ct);   // FIXED size (no per-hover atlas re-bake) ; hover feedback = the halo + panel colour + outline width
    if (cbHov && click) { open_ = false; return; }

    // language toggle (EN | FR), left of the close X -- drawn in the shared header, so it's on EVERY tab
    {
        const float lh = snap(26.0f), segW = snap(34.0f), lw = segW * 2.0f;
        const float lx = cbX - snap(12.0f) - lw, ly = cbY + (cbS - lh) * 0.5f;
        rpanel(dev, lx, ly, lw, lh, snap(7.0f), C_CTL_T, C_CTL_B, C_CTL_BR, snap(1.5f));
        const char* seg[2] = { "EN", "FR" };
        for (int i = 0; i < 2; ++i) {
            const float sx = lx + (float)i * segW;
            const bool on = (ui_config().lang == i);
            const bool hov = inrect(mo, sx, ly, segW, lh);
            if (on) rrect_fill(dev, sx + snap(2.0f), ly + snap(2.0f), segW - snap(4.0f), lh - snap(4.0f), snap(5.0f), lerpc(C_ACCENT, C_ACCENTHI, pulse), C_ACCENT);   // blue = interactive
            fo->begin(dev);
            fo->draw_c(dev, sx + segW * 0.5f, ly + lh * 0.5f, seg[i], snap(13.0f),
                       on ? fa(0xFFFFFFFF) : fa(lerpc(C_DIM, C_TEXT, hov ? 1.0f : 0.0f)), fa(C_STROKE), 1.0f);   // white on blue / dim on dark
            if (hov && click && !on) { ui_config().lang = i; save_ui_config(); }
        }
    }

    // accent divider under the header (animated wipe-in width + a soft glow seat)
    const float divY = iy + snap(50.0f);
    flat(dev, ix, divY, iw, 1, C_BORDER);
    flat(dev, ix, divY, iw * e, snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse));   // gold wipe-in divider

    // ===== TAB STRIP (glass "modules" : gear / profile / help, an accent-lit active pill) =====
    const float tabY = divY + snap(18.0f), tabH = snap(42.0f), tabW = snap(176.0f), tabGap = snap(6.0f);
    const float bodyY = tabY + tabH;
    const float iconS = snap(19.0f), iconGap = snap(9.0f);
    const float stripW = NTABS * tabW + (NTABS - 1) * tabGap;
    // a recessed graphite track the pills sit IN -> depth (the active one lifts out of it)
    rrect_top(dev, ix - snap(3.0f), tabY - snap(2.0f), stripW + snap(6.0f), tabH + snap(2.0f), snap(13.0f), 0x66070B0E, 0x8804070A);
    flat(dev, ix - snap(3.0f), tabY - snap(2.0f), stripW + snap(6.0f), 1, 0x10FFFFFF);   // track top hairline
    float activeX = ix;
    for (int i = 0; i < NTABS; ++i) {
        const float tx = ix + i * (tabW + tabGap);
        const float cxT = tx + tabW * 0.5f, cyT = tabY + tabH * 0.5f;
        const bool active = (i == tab_);
        const bool hover  = inrect(mo, tx, tabY, tabW, tabH);
        if (hover && click) { if (i == 2) { ui_config().editLayout = true; } else { tab_ = i; if (i == 1) profDirty_ = true; nameFocus_ = false; } }   // Edit Layout tab = a launch button into the full-screen drag mode (tab_ stays on the page you came from)
        hov_[i] += (((hover ? 1.0f : 0.0f) - hov_[i]) * clampf(dt * 14.0f, 0.0f, 1.0f));   // eased hover
        if (active) activeX = tx;

        const float tr = snap(10.0f);
        if (active) {
            halo(dev, cxT - snap(2.0f), cyT, tabW * 0.5f, tabH * 0.5f, C_GOLD, 0.35f + 0.2f * pulse);      // accent seat glow
            rrect_top(dev, tx, tabY, tabW, tabH + snap(2.0f), tr, C_TABON_T, C_TABON_B);                  // +2 : bleed into the body
            rrect_top(dev, tx + snap(2.0f), tabY + snap(1.0f), tabW - snap(4.0f), tabH * 0.46f, snap(7.0f), 0x48FFFFFF, 0x06FFFFFF);   // glass top sheen
            flat(dev, tx + snap(11.0f), tabY + snap(1.0f), tabW - snap(22.0f), 1, 0x34FFFFFF);            // crisp top rim light
            shine(dev, tx + snap(3.0f), tabY, tabW - snap(6.0f), tabH * 0.92f, 0.34f + 0.14f * pulse, f.t);// slow glass sweep (always, not just hover)
        } else {
            rrect_top(dev, tx, tabY, tabW, tabH, tr, lerpc(C_TABOFF_T, C_TABHOV_T, hov_[i]), lerpc(C_TABOFF_B, C_TABHOV_B, hov_[i]));
            rrect_top(dev, tx + snap(2.0f), tabY + snap(1.0f), tabW - snap(4.0f), tabH * 0.32f, snap(7.0f), ((u32)(0x22 * (0.35f + 0.65f * hov_[i])) << 24) | 0x00FFFFFF, 0x02FFFFFF);   // faint sheen (grows on hover)
            if (hov_[i] > 0.01f) shine(dev, tx + snap(3.0f), tabY, tabW - snap(6.0f), tabH * 0.92f, hov_[i], f.t);
        }
        // icon + label drawn as ONE centred group
        const u32 fg = lerpc(C_DIM, active ? C_GOLDHI : C_TEXT, active ? 1.0f : hov_[i]);
        const float textW = fo->measure(tab_label(i), snap(15.0f));
        const float groupW = iconS + iconGap + textW;
        const float leftX = cxT - groupW * 0.5f;
        tab_icon(dev, i, leftX + iconS * 0.5f, cyT, iconS, fg, fo, C_GOLD, active ? (0.55f + 0.25f * pulse) : hov_[i] * 0.5f);
        fo->begin(dev);
        fo->draw_c(dev, leftX + iconS + iconGap + textW * 0.5f, cyT, tab_label(i), snap(15.0f), fg, fa(C_STROKE), 1.0f);
    }
    // sliding active-tab indicator (interpolates toward the active tab) + a soft accent glow and a
    // travelling bright glint that rides the bar as it slides between modules.
    if (tabSlide_ < 0.0f) tabSlide_ = activeX;
    tabSlide_ += (activeX - tabSlide_) * clampf(dt * 16.0f, 0.0f, 1.0f);
    const float barCx = tabSlide_ + tabW * 0.5f;
    halo(dev, barCx, bodyY - snap(1.0f), tabW * 0.42f, snap(6.0f), C_GOLD, 0.5f + 0.3f * pulse);
    rrect_fill(dev, tabSlide_ + snap(8.0f), bodyY - snap(3.0f), tabW - snap(16.0f), snap(3.0f), snap(1.5f), lerpc(C_GOLD, C_GOLDHI, pulse), lerpc(C_GOLD, C_GOLDHI, pulse));
    cs_add(dev); soft_blob(dev, barCx, bodyY - snap(1.5f), snap(28.0f), snap(4.0f), (C_GOLDHI & 0x00FFFFFF) | ((u32)(120.0f * (0.5f + 0.5f * pulse) * g_fade) << 24));   // bright travelling glint

    // ===== CONTENT BODY (the tab content surface) =====
    const float bodyH = pageBot - bodyY;
    // The container background is intentionally 100% TRANSPARENT : the nebula shows straight through the
    // big frame. Only the frame edge (border + top inner highlight) is kept. The Live preview draws its
    // OWN opaque backdrop, and each control carries its own faint row band -> nothing floats unreadably.
    flat(dev, ix + 1, bodyY + 1, iw - 2, 1, 0x16FFFFFF);                        // crisp top inner highlight
    outline(dev, ix, bodyY, iw, bodyH, C_BORDERHI);

    if (tab_ == 0) {
        if (profDirty_) { profile_refresh(); profDirty_ = false; }

        // ===== LEFT : MODULES (one settings page per module ; scales to many) =====
        const float sbW = snap(220.0f);
        vg(dev, ix, bodyY, sbW, bodyH, C_SIDEBAR, 0xF0121A27);
        cs_add(dev); soft_blob(dev, ix + sbW * 0.5f, bodyY + snap(2.0f), sbW * 0.62f, bodyH * 0.16f, 0x0A2A4E84);   // faint top glow
        q4(dev, ix + sbW, bodyY, snap(22.0f), bodyH, 0x30000000, 0x00000000, 0x30000000, 0x00000000);              // recessed shadow on the content side
        flat(dev, ix + sbW, bodyY, 1, bodyH, C_BORDER);
        fo->begin(dev);
        fo->draw_lc(dev, ix + snap(20.0f), bodyY + snap(24.0f), "MODULES", snap(12.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.4f);
        if (section_ < 0 || section_ >= MODULE_N) section_ = 0;
        for (int i = 0; i < MODULE_N; ++i) {
            const int sec = (i == 0) ? SEC_INTERFACE : (i - 1);   // display order : Interface FIRST, then the HUD modules (their section indices are unchanged -> dispatch untouched)
            const float rx = ix + snap(10.0f), rw = sbW - snap(20.0f);
            const float ry = bodyY + snap(44.0f) + i * snap(42.0f), rh = snap(36.0f);
            const bool active = (sec == section_), hover = inrect(mo, rx, ry, rw, rh);
            if (hover && click) section_ = sec;
            const float ht = ease(10 + i, (hover || active) ? 1.0f : 0.0f), af = active ? 1.0f : ht;
            if (active)          rrect_fill(dev, rx, ry, rw, rh, snap(9.0f), C_ROWON_T, C_ROWON_B);
            else if (ht > 0.01f) rrect_fill(dev, rx, ry, rw, rh, snap(9.0f), ((u32)(0x24 * ht) << 24) | 0x00FFFFFF, ((u32)(0x12 * ht) << 24) | 0x00FFFFFF);
            if (af > 0.01f) rrect_fill(dev, rx + snap(2.0f), ry + rh * (1.0f - af) * 0.5f, snap(4.0f), rh * af, snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse), lerpc(C_GOLD, C_GOLDHI, pulse));   // gold accent pill
            fo->begin(dev); fo->draw_lc(dev, rx + snap(18.0f), ry + rh * 0.5f, module_label(sec), snap(15.0f), lerpc(C_DIM, C_TEXT, active ? 1.0f : ht), fa(C_STROKE), 1.0f);
        }

        const float coX = ix + sbW + snap(30.0f);
        const float coW = (ix + iw) - coX - snap(26.0f);

        // PROFILE BAR : only its GEOMETRY here (coY/cfgTop need barY/barH) ; the bar itself is drawn AFTER the content
        // (draw_profile_bar) so the scroll overflow can't bleed over it.
        const float barY = bodyY + snap(18.0f), barH = snap(46.0f);

        // ===== two columns below the bar : controls (left) | LIVE PREVIEW (right) =====
        // previewW is PROPORTIONAL (screenW_ is the real backbuffer, not a fixed canvas) with guards
        // so the controls column never collapses on a small resolution.
        const float splitGap = snap(40.0f);
        float previewW = coW * 0.40f;
        const float minCtrl = snap(560.0f);
        if (coW - previewW - splitGap < minCtrl) previewW = coW - splitGap - minCtrl;
        if (previewW < snap(260.0f)) previewW = snap(260.0f);
        const float ctrlW = coW - previewW - splitGap;
        const float coY = barY + barH + snap(26.0f);

        // section title (GOLD) -- underline spans the FULL title width (wipes in)
        fo->begin(dev);
        const float secW = fo->measure(module_label(section_), snap(20.0f));
        fo->draw_lc(dev, coX, coY, module_label(section_), snap(20.0f), fa(C_GOLD), fa(C_STROKE), 1.4f);
        flat(dev, coX, coY + snap(18.0f), secW * e, snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse));

        // LIVE PREVIEW stage : a recessed backdrop in the right column. The HUD draws the REAL
        // party + 2-alliance demo boxes (forced //aio alliance2 demo) on top, anchored bottom-right
        // here -- so the preview is exactly what ships in game (cost box space included).
        {
            const float pvx = coX + ctrlW + splitGap, pvy = coY - snap(2.0f);
            fo->begin(dev); fo->draw_lc(dev, pvx, pvy + snap(7.0f), tr("LIVE PREVIEW", "APERÇU EN DIRECT"), snap(12.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.4f);
            const float stageY = pvy + snap(22.0f), stageH = pageBot - stageY;
            // TRANSPARENT stage : the opaque page bg leaves a HOLE here (recorded below -> punched next frame)
            // so the REAL game shows through and the demo boxes preview exactly as they look in play. Just a
            // subtle frame delineates the window.
            pvStageX_ = pvx; pvStageY_ = stageY; pvStageW_ = previewW; pvStageH_ = stageH;
            drop_shadow(dev, pvx, stageY, previewW, stageH, snap(6.0f), 70);
            rrect_stroke(dev, pvx, stageY, previewW, stageH, snap(12.0f), 0x7AAEC4EEu, snap(1.5f));   // thin window frame
            pvRightX_  = pvx + previewW - snap(18.0f);
            pvBottomY_ = pageBot - snap(14.0f);
            pvCX_      = pvx + previewW * 0.5f;
            pvCY_      = stageY + (pageBot - stageY) * 0.5f;
            { const float ins = snap(10.0f);                     // inset the mini-map rect a touch inside the stage frame
              pvSX_ = pvx + ins; pvSY_ = stageY + ins; pvSW_ = previewW - 2.0f * ins; pvSH_ = (pageBot - stageY) - 2.0f * ins; }
            pvOn_ = true;
        }

        // each control row sits on an alternating band (label<->control tie) + eases in, staggered.
        const float bandX = coX - snap(12.0f), bandW = ctrlW + snap(24.0f);
        float ry = coY + snap(44.0f); int ri = 0;
        // ROW_BAND / ROW_NEXT row-layout macros are hoisted to ui/config_rows.h (shared with the
        // per-module *_config.cpp panels, so they can't drift). They need dev/bandX/bandW/e/ry/ri/anim_
        // in scope -- all present here. #undef'd at the end of this controls section.

        // ---- scrolling viewport : all-open categories can exceed the page height, so scroll the
        // column. Rows are stencil-clipped to [cfgTop, cfgBot] and the mouse is clipped to the same
        // rect -> anything scrolled out of view is neither drawn nor click-through. ----
        const float cfgTop = ry, cfgBot = pageBot;                     // ry == coY + 44 here (first row top)
        if (ui_config().wheel != 0 && mo && mo->x >= bandX && mo->x <= bandX + bandW && mo->y >= cfgTop && mo->y <= cfgBot) {
            cfgScroll_ -= (float)ui_config().wheel * snap(64.0f);
            if (cfgScroll_ < 0.0f) cfgScroll_ = 0.0f;
            if (cfgScroll_ > cfgMaxScroll_) cfgScroll_ = cfgMaxScroll_;   // clamp vs last frame's extent -> no overscroll jump at the bottom
        }
        ui_config().wheel = 0;
        ry -= cfgScroll_;                                              // shift every row up by the scroll
        clip_rect_begin(dev, bandX - snap(2.0f), cfgTop, bandW + snap(4.0f), cfgBot - cfgTop);
        const MouseState* moReal_ = mo;                               // gate the mouse to the viewport
        const bool moIn_ = moReal_ && moReal_->x >= bandX && moReal_->x <= bandX + bandW && moReal_->y >= cfgTop && moReal_->y <= cfgBot;
        {
        const MouseState* mo = moIn_ ? moReal_ : nullptr;             // SHADOWS the outer mo/click for the rows below
        const bool click = mo && mo->clicked;
        const float hdrX = coX - snap(12.0f), hdrW = ctrlW + snap(24.0f);
        // (draw_interface_category is NO LONGER drawn on every page -- it's now its own "Interface" sidebar entry below)
        // ===== category : PARTY / ALLIANCE (a MODULE page : shown only under the Party / Alliance module) =====
        if (section_ == 0) {
            draw_party_config(dev, fo, mo, click, ry, ri, e,
                              bandX, bandW, coX, ctrlW, hdrX, hdrW);
        } else if (section_ == 1) {
            draw_target_config(dev, fo, mo, click, ry, ri, e,
                               bandX, bandW, coX, ctrlW, hdrX, hdrW);
        } else if (section_ == 2) {
            draw_player_config(dev, fo, mo, click, ry, ri, e,
                               bandX, bandW, coX, ctrlW, hdrX, hdrW);
        } else if (section_ == 3) {
            draw_minimap_config(dev, fo, mo, click, ry, ri, e,
                                bandX, bandW, coX, ctrlW, hdrX, hdrW);
        } else if (section_ == 5) {
            draw_sc_config(dev, fo, mo, click, ry, ri, e,
                           bandX, bandW, coX, ctrlW, hdrX, hdrW);
        } else if (section_ == 6) {
            draw_tp_config(dev, fo, mo, click, ry, ri, e,
                           bandX, bandW, coX, ctrlW, hdrX, hdrW);
        } else if (section_ == 7) {
            draw_hl_config(dev, fo, mo, click, ry, ri, e,
                           bandX, bandW, coX, ctrlW, hdrX, hdrW);
        } else if (section_ == 8) {
            draw_pw_config(dev, fo, mo, click, ry, ri, e,
                           bandX, bandW, coX, ctrlW, hdrX, hdrW);
        } else if (section_ == 9) {
            draw_grim_config(dev, fo, mo, click, ry, ri, e,
                             bandX, bandW, coX, ctrlW, hdrX, hdrW);
        } else if (section_ == 10) {
            draw_zt_config(dev, fo, mo, click, ry, ri, e,
                           bandX, bandW, coX, ctrlW, hdrX, hdrW);
        } else if (section_ == 11) {
            draw_tm_config(dev, fo, mo, click, ry, ri, e,
                           bandX, bandW, coX, ctrlW, hdrX, hdrW);
        } else if (section_ == 12) {
            draw_ep_config(dev, fo, mo, click, ry, ri, e,
                           bandX, bandW, coX, ctrlW, hdrX, hdrW);
        } else if (section_ == SEC_INTERFACE) {
            draw_interface_category(dev, fo, mo, click, ry, ri, e,   // the config MENU's own font + accent colour
                                    bandX, bandW, coX, ctrlW, hdrX, hdrW);
        } else {
            draw_ws_config(dev, fo, mo, click, ry, ri, e,
                           bandX, bandW, coX, ctrlW, hdrX, hdrW);
        }
        // (the per-page "Layout" category was removed : placement is the Edit Layout tab, and the global
        //  "Reset all settings" now lives once in the Profile tab.)
        }   // end mouse-clip block (restores the outer mo/click)

        clip_rect_end(dev);
        // Scroll-overflow guard : the viewport clip needs a stencil buffer ; on backbuffers that have none, a tall
        // page's rows overflow UP into the header. Re-paint the opaque page bg over the whole strip between the tab
        // strip and the content, then redraw the PROFILE BAR + section title on top (their click handlers already
        // ran in place before the content, and content clicks are gated to the viewport, so no double-processing).
        flat(dev, bandX - snap(2.0f), bodyY, bandW + snap(4.0f), cfgTop - bodyY, 0xFF0E131Cu);
        flat(dev, bandX - snap(2.0f), bodyY, bandW + snap(4.0f), snap(1.0f), C_BORDERHI);   // the repaint above erased the body frame's TOP border line -> redraw it
        draw_profile_bar(dev, fo, mo, click, coX, coW, bodyY, pulse);
        fo->begin(dev); fo->draw_lc(dev, coX, coY, module_label(section_), snap(20.0f), fa(C_GOLD), fa(C_STROKE), 1.4f);
        flat(dev, coX, coY + snap(18.0f), fo->measure(module_label(section_), snap(20.0f)) * e, snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse));
        // scroll extent (this frame) + a thin scrollbar in the split gap
        {
            const float viewH = cfgBot - cfgTop, contentH = (ry + cfgScroll_) - cfgTop + snap(16.0f);   // ry advanced by every row (+ a little bottom breathing room)
            float maxS = contentH - viewH; if (maxS < 0.0f) maxS = 0.0f;
            cfgMaxScroll_ = maxS;                                                          // remembered for next frame's clamp
            if (cfgScroll_ > maxS) cfgScroll_ = maxS;
            scrollbar_vert(dev, mo, 1 /*options viewport*/, bandX + bandW + snap(4.0f), cfgTop, snap(6.0f), viewH, contentH, cfgScroll_, maxS);
        }

        #undef ROW_BAND
        #undef ROW_NEXT
        g_fade = e;   // restore for the footer
    } else if (tab_ == 1) {
        draw_profile_tab(f, dev, fo, mo, click, ix, iw, bodyY, bodyH, pageBot, pulse, e);
    } else if (tab_ == 4) {
        draw_update_tab(f, dev, fo, mo, click, ix, iw, bodyY, bodyH, pageBot, pulse, e);
    } else if (tab_ == 5) {
        draw_debug_tab(f, dev, fo, mo, click, ix, iw, bodyY, bodyH, pageBot);
    } else {
        draw_help_tab(f, dev, fo, mo, click, ix, iw, bodyY, bodyH, pageBot, pulse);
    }

    fo->set_upper(false);   // clear the Interface UPPERCASE so the shared font atlas doesn't stay forced elsewhere
    draw_ui_cursor(f.dev, mo);   // ALWAYS : the overlay suppresses the OS cursor over the client area, so ours is the only one
}

// Configuration-tab quick profile switcher. Drawn AFTER the module content (over an opaque cover) so a tall page's
// scroll overflow can't bleed over it ; its click handlers run here (content clicks are gated to the viewport, so no
// double-processing). Geometry (barY/barH) is recomputed here AND before the content (for coY/cfgTop) -- same formula.
void ConfigPage::draw_profile_bar(u32 dev, Font* fo, const MouseState* mo, bool click, float coX, float coW, float bodyY, float pulse) {
    const bool dirty = activeProf_[0] && profile_dirty();
    const float barY = bodyY + snap(18.0f), barH = snap(46.0f), barCy = barY + barH * 0.5f;
    drop_shadow(dev, coX, barY, coW, barH, snap(4.0f), 50);
    rpanel(dev, coX, barY, coW, barH, snap(10.0f), 0x55101826, 0x550A111C, C_BORDER, snap(1.5f));
    rrect_fill(dev, coX + snap(4.0f), barY + snap(7.0f), snap(4.0f), barH - snap(14.0f), snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse), lerpc(C_GOLD, C_GOLDHI, pulse));
    fo->begin(dev); fo->draw_lc(dev, coX + snap(18.0f), barCy, tr("PROFILE", "PROFIL"), snap(11.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.2f);
    const int nprof = profile_count();
    int cur = -1; for (int i = 0; i < nprof; ++i) if (activeProf_[0] && strcmp(profile_name(i), activeProf_) == 0) { cur = i; break; }
    const float aS = snap(28.0f), ay = barCy - aS * 0.5f, lx = coX + snap(86.0f);
    if (arrow_btn(dev, fo, mo, click, 400, lx, ay, aS, "<") && nprof > 0) {
        const char* nm = profile_name((cur <= 0) ? nprof - 1 : cur - 1);
        profile_load(nm); record_char_profile(nm); strncpy(activeProf_, nm, 31); activeProf_[31] = 0;
    }
    const float fX = lx + aS + snap(10.0f);
    const float nX = lx + aS + snap(258.0f);
    const float fW = (nX - snap(10.0f)) - fX, fH = snap(30.0f), fY = barCy - fH * 0.5f;
    rpanel(dev, fX, fY, fW, fH, snap(7.0f), 0xE6070B13, 0xE604070D, 0x55355072, snap(1.5f));
    fo->begin(dev);
    if (activeProf_[0]) {
        if (dirty) { cs(dev); disc(dev, fX + snap(13.0f), barCy, snap(3.5f), fa(0xFFFFB454)); }
        fo->begin(dev); fo->draw_c(dev, fX + fW * 0.5f, barCy, activeProf_, snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
    } else {
        fo->draw_c(dev, fX + fW * 0.5f, barCy, tr("(none -- open the Profile tab)", "(aucun -- ouvre l'onglet Profil)"), snap(13.0f), fa(C_DIM), fa(C_STROKE), 1.0f);
    }
    if (arrow_btn(dev, fo, mo, click, 401, nX, ay, aS, ">") && nprof > 0) {
        const char* nm = profile_name((cur < 0 || cur >= nprof - 1) ? 0 : cur + 1);
        profile_load(nm); record_char_profile(nm); strncpy(activeProf_, nm, 31); activeProf_[31] = 0;
    }
    const float bH = snap(30.0f), bY = barCy - bH * 0.5f, saveW = snap(140.0f);
    const float bx = coX + coW - snap(12.0f) - saveW;
    {
        const bool canSave = activeProf_[0] != 0;
        const bool hov = canSave && inrect(mo, bx, bY, saveW, bH);
        const float t = ease(410, hov ? 1.0f : 0.0f);
        const float sr = snap(bH * 0.30f);
        if (dirty) {
            cs_add(dev); rrect_glow(dev, bx, bY, saveW, bH, sr, (C_ACCENT & 0x00FFFFFF) | ((u32)(40.0f + 40.0f * pulse) << 24), snap(7.0f)); cs(dev);
            rpanel(dev, bx, bY, saveW, bH, sr, lerpc(C_CHIP_ON_T, C_ACCENTHI, t), lerpc(C_CHIP_ON_B, C_ACCENT, t), C_ACCENTHI, snap(1.4f));
            flat(dev, bx + sr, bY + snap(1.0f), saveW - 2.0f * sr, 1.0f, 0x40FFFFFF);
            fo->begin(dev); fo->draw_c(dev, bx + saveW * 0.5f, barCy, tr("Save changes", "Enregistrer"), snap(14.0f), fa(C_ONACC), 0, 0.0f);
        } else {
            if (canSave && t > 0.01f) { cs_add(dev); rrect_glow(dev, bx, bY, saveW, bH, sr, (C_ACCENT & 0x00FFFFFF) | ((u32)(44.0f * t) << 24), snap(6.0f)); cs(dev); }
            rpanel(dev, bx, bY, saveW, bH, sr, canSave ? lerpc(0xFF1E252B, 0xFF27313A, t) : 0xFF1A2027, canSave ? lerpc(0xFF161C21, 0xFF1C232A, t) : 0xFF121820, lerpc(C_CTL_BR, C_ACCENTHI, t), snap(1.3f));
            fo->begin(dev); fo->draw_c(dev, bx + saveW * 0.5f, barCy, tr("Saved", "Enregistré"), snap(14.0f), fa(canSave ? C_TEXT : C_MUTE), fa(C_STROKE), 1.0f);
        }
        if (canSave && dirty && hov && click) { profile_save(activeProf_); }
    }
}

// ---- Edit-Layout mode (draw-drawn ZONES + draggable panel + toolbar), lifted verbatim from draw() (byte-identical). ----
void ConfigPage::draw_edit_layout(const Frame& f, u32 dev, Font* fo, const MouseState* mo, bool click, float sw, float sh) {
        g_fade = 1.0f;
        // ZONES : while Rules is OFF (placing boxes) draw each zone FAINTLY so you see the keep-out / allowed
        // areas -- a box is pushed out of any zone that forbids it (see guide_push_out). Rules ON draws them
        // interactively further below.
        if (!editShowLines_) {
            static const u32 ZC[8] = { 0xFFFF6E6E,0xFFFF9E50,0xFFEFD24A,0xFF7ED86A,0xFF4AC8E0,0xFF6E8CFF,0xFFC090FF,0xFFFF8AD8 };
            float zx[GUIDE_GROUPS_MAX], zy[GUIDE_GROUPS_MAX], zw[GUIDE_GROUPS_MAX], zh[GUIDE_GROUPS_MAX]; int zg[GUIDE_GROUPS_MAX];
            const int nz = guide_zones(sw, sh, zx, zy, zw, zh, zg, GUIDE_GROUPS_MAX);
            for (int i = 0; i < nz; ++i) {
                const GuideGroup& g = ui_config().guideGroup[zg[i]];
                const bool openZone = g.allow[ZPERM_PARTY] || g.allow[ZPERM_ALLIANCE] || g.allow[ZPERM_HUB] || g.allow[ZPERM_TARGET];
                const u32 col = ZC[zg[i] % 8];
                flat(dev, zx[i], zy[i], zw[i], zh[i], (col & 0x00FFFFFF) | (openZone ? 0x28000000 : 0x40000000));   // fill (forbidden = a touch denser)
                outline(dev, zx[i], zy[i], zw[i], zh[i], col);
                char zl[28]; if (g.name[0]) sprintf(zl, "%s", g.name); else sprintf(zl, tr("Zone %d", "Zone %d"), zg[i] + 1);
                fo->begin(dev); fo->draw_lc(dev, zx[i] + snap(6.0f), zy[i] + snap(10.0f), zl, snap(11.0f), col, C_STROKE, 1.2f);
            }
        }
        // RULES mode : the HUD is hidden (hud.cpp) so only the guide ZONES + the toolbar show. Party 1p-6p and
        // Alliance 1/2 are real zones now (role-tagged) -- there is no separate reference-line handle system.
        if (editShowLines_) {
        UiConfig& C = ui_config();
        // ZONES panel geometry (auto-sized) computed FIRST so EVERY draggable element under it (party-ref
        // handles, zone handles) can ignore clicks that land on the panel -- else one click hits both.
        const char* pTitle = tr("ZONES  (drag on screen to draw)", "ZONES  (dessiner à l'écran)");
        const char* pHint  = tr("Drag on empty space to draw a zone.", "Glissez sur l'écran pour dessiner une zone.");
        const float tbh = snap(28.0f), rowH = snap(22.0f), stride = rowH + snap(3.0f);
        const bool  renaming = (editZoneName_ >= 0 && editZoneName_ < C.guideGroupCount);
        const bool  zoneSel = (groupSel_ >= 0 && groupSel_ < C.guideGroupCount);
        const float actionsH = renaming ? snap(44.0f) : (zoneSel ? snap(104.0f) : snap(26.0f));   // zone : Rename/Delete + permissions header + chips + keep-out note
        const int   visRows = C.guideGroupCount < 12 ? C.guideGroupCount : 12;
        const float listH = (C.guideGroupCount > 0) ? visRows * stride : 0.0f;
        const float newBtnRow = snap(38.0f);
        float pw = fo->measure(pTitle, snap(11.0f)) + snap(28.0f);
        { const float hw2 = fo->measure(pHint, snap(11.0f)) + snap(24.0f); if (hw2 > pw) pw = hw2; }
        if (pw < snap(200.0f)) pw = snap(200.0f); if (pw > snap(420.0f)) pw = snap(420.0f); pw = snap(pw);
        const float ph = snap(tbh + newBtnRow + listH + snap(6.0f) + actionsH + snap(8.0f));
        float pxp = (C.zonePanelX >= 0.0f) ? snap(C.zonePanelX * sw) : snap(sw - pw - 20.0f);
        float pyp = (C.zonePanelY >= 0.0f) ? snap(C.zonePanelY * sh) : snap(80.0f);
        if (pxp > sw - pw) pxp = snap(sw - pw); if (pxp < 0.0f) pxp = 0.0f;
        if (pyp > sh - ph) pyp = snap(sh - ph); if (pyp < 0.0f) pyp = 0.0f;
        const bool overPanel = inrect(mo, pxp, pyp, pw, ph);

        // ===== USER-DRAWN ZONES : drag on empty space to draw a rectangle (creates a zone). Move it by its
        // body, resize it by its corners, name it, set which boxes may sit on it. A box is pushed OUT of any
        // zone that forbids it (guide_push_out). Rectangular by construction. The panel is draggable. =====
        {
            const bool rClick = click && editConfirm_ == 0;
            static const u32 GRP_COL[8] = { 0xFFFF6E6E,0xFFFF9E50,0xFFEFD24A,0xFF7ED86A,0xFF4AC8E0,0xFF6E8CFF,0xFFC090FF,0xFFFF8AD8 };

            // inline RENAME : mirror the keyboard field into the zone's name each frame ; finish on OK/blur.
            if (editZoneName_ >= 0 && editZoneName_ < C.guideGroupCount) {
                strncpy(C.guideGroup[editZoneName_].name, nameBuf_, 18); C.guideGroup[editZoneName_].name[18] = 0;
                if (kbCommit_ || !nameFocus_) { kbCommit_ = false; editZoneName_ = -1; nameFocus_ = false; save_ui_config(); }
            }

            // on-screen HOW-TO banner (top-centre) so the drag-to-draw gesture is discoverable without reading the panel.
            {
                const char* bn = tr("Drag on an empty area to draw a zone   -   move by its body, resize by its corners",
                                    "Glissez sur une zone vide pour dessiner   -   déplacez par le corps, coins pour redimensionner");
                const float bnw = fo->measure(bn, snap(13.0f)) + snap(30.0f), bnx = snap((sw - bnw) * 0.5f), bny = snap(sh * 0.12f), bnh = snap(30.0f);
                drop_shadow(dev, bnx, bny, bnw, bnh, snap(4.0f), 50);
                vg(dev, bnx, bny, bnw, bnh, 0xE0202B3C, 0xE0141C28);
                flat(dev, bnx, bny, bnw, 1, 0x55FFFFFF); outline(dev, bnx, bny, bnw, bnh, C_BORDERHI);
                fo->begin(dev); fo->draw_c(dev, sw * 0.5f, bny + bnh * 0.5f, bn, snap(13.0f), C_TEXT, C_STROKE, 1.0f);
            }

            // ---- draw + edit each ZONE rectangle on screen ----
            const float HS = snap(9.0f);   // corner-handle half-size
            static int grabZone = -1, grabMode = 0;   // grabMode : 0 move ; 1..4 = TL/TR/BL/BR corner resize
            static float gzOffX = 0.0f, gzOffY = 0.0f, grabPX = 0.0f, grabPY = 0.0f; static bool grabMoved = false;
            if (!(mo && mo->down)) { if (grabZone >= 0 && grabMoved) save_ui_config(); grabZone = -1; grabMoved = false; }
            for (int i = 0; i < C.guideGroupCount; ++i) {
                GuideGroup& z = C.guideGroup[i];
                const float rx = snap(z.x * sw), ry = snap(z.y * sh), rw = snap(z.w * sw), rh = snap(z.h * sh);
                const bool sel = (groupSel_ == i);
                const u32  col = GRP_COL[i % 8];
                const bool openZone = z.allow[0] || z.allow[1] || z.allow[2] || z.allow[3];
                flat(dev, rx, ry, rw, rh, (col & 0x00FFFFFF) | (openZone ? 0x40000000 : 0x60000000));   // less transparent fill
                outline(dev, rx, ry, rw, rh, sel ? 0xFFFFE59E : col);
                if (sel) outline(dev, rx + snap(1.0f), ry + snap(1.0f), rw - snap(2.0f), rh - snap(2.0f), (col & 0x00FFFFFF) | 0x88000000);
                if (sel) {   // corner handles for the selected zone
                    const float cxs[4] = { rx, rx + rw, rx, rx + rw }, cys[4] = { ry, ry, ry + rh, ry + rh };
                    for (int cci = 0; cci < 4; ++cci) { vg(dev, cxs[cci] - HS, cys[cci] - HS, 2 * HS, 2 * HS, 0xFFFFE59E, 0xFFD9B24A); outline(dev, cxs[cci] - HS, cys[cci] - HS, 2 * HS, 2 * HS, C_STROKE); }
                }
                char zl[28]; if (z.name[0]) sprintf(zl, "%s", z.name); else sprintf(zl, tr("Zone %d", "Zone %d"), i + 1);
                fo->begin(dev); fo->draw_lc(dev, rx + snap(6.0f), ry + snap(11.0f), zl, snap(11.0f), sel ? 0xFFFFFFFF : col, C_STROKE, 1.2f);
            }
            // start a grab on a FRESH press : selected zone's corner -> body of any zone -> else rubber-band draw.
            static bool zPrevDown = false; const bool freshPress = mo && mo->down && !zPrevDown; zPrevDown = mo && mo->down;
            if (freshPress && grabZone < 0 && !zoneDrawing_ && editConfirm_ == 0 && !overPanel) {
                bool got = false;
                if (groupSel_ >= 0 && groupSel_ < C.guideGroupCount) {
                    GuideGroup& z = C.guideGroup[groupSel_];
                    const float rx = z.x * sw, ry = z.y * sh, rw = z.w * sw, rh = z.h * sh;
                    const float cxs[4] = { rx, rx + rw, rx, rx + rw }, cys[4] = { ry, ry, ry + rh, ry + rh };
                    for (int cci = 0; cci < 4 && !got; ++cci) if (inrect(mo, cxs[cci] - HS, cys[cci] - HS, 2 * HS, 2 * HS)) { grabZone = groupSel_; grabMode = 1 + cci; grabPX = mo->x; grabPY = mo->y; grabMoved = false; got = true; }
                }
                if (!got) for (int i = C.guideGroupCount - 1; i >= 0 && !got; --i) {
                    GuideGroup& z = C.guideGroup[i];
                    if (inrect(mo, z.x * sw, z.y * sh, z.w * sw, z.h * sh)) { grabZone = i; grabMode = 0; groupSel_ = i; gzOffX = mo->x - z.x * sw; gzOffY = mo->y - z.y * sh; grabPX = mo->x; grabPY = mo->y; grabMoved = false; got = true; }
                }
                if (!got) { zoneDrawing_ = true; zoneDrawX_ = mo->x; zoneDrawY_ = mo->y; }
            }
            if (grabZone >= 0 && grabZone < C.guideGroupCount && mo && !grabMoved) {   // 4px dead-zone : a plain click only SELECTS
                const float dx = mo->x - grabPX, dy = mo->y - grabPY; if (dx * dx + dy * dy > 16.0f) grabMoved = true;
            }
            if (grabZone >= 0 && grabZone < C.guideGroupCount && mo && grabMoved) {   // apply an active move / corner resize
                GuideGroup& z = C.guideGroup[grabZone];
                if (grabMode == 0) { z.x = clampf((mo->x - gzOffX) / sw, 0.0f, 1.0f - z.w); z.y = clampf((mo->y - gzOffY) / sh, 0.0f, 1.0f - z.h); }
                else {
                    float x0 = z.x, y0 = z.y, x1 = z.x + z.w, y1 = z.y + z.h;
                    const float mx = clampf(mo->x / sw, 0.0f, 1.0f), my = clampf(mo->y / sh, 0.0f, 1.0f);
                    if (grabMode == 1) { x0 = mx; y0 = my; } else if (grabMode == 2) { x1 = mx; y0 = my; } else if (grabMode == 3) { x0 = mx; y1 = my; } else { x1 = mx; y1 = my; }
                    if (x1 < x0) { float t = x0; x0 = x1; x1 = t; grabMode = (grabMode == 1) ? 2 : (grabMode == 2) ? 1 : (grabMode == 3) ? 4 : 3; }
                    if (y1 < y0) { float t = y0; y0 = y1; y1 = t; grabMode = (grabMode <= 2) ? grabMode + 2 : grabMode - 2; }
                    z.x = x0; z.y = y0; z.w = (x1 - x0 < 0.01f) ? 0.01f : (x1 - x0); z.h = (y1 - y0 < 0.01f) ? 0.01f : (y1 - y0);
                }
            }
            if (zoneDrawing_ && mo) {   // rubber-band : show the pending rect ; on release create the zone
                const float x0 = zoneDrawX_ < mo->x ? zoneDrawX_ : mo->x, y0 = zoneDrawY_ < mo->y ? zoneDrawY_ : mo->y;
                const float x1 = zoneDrawX_ > mo->x ? zoneDrawX_ : mo->x, y1 = zoneDrawY_ > mo->y ? zoneDrawY_ : mo->y;
                if (mo->down) {
                    flat(dev, x0, y0, x1 - x0, y1 - y0, 0x3340C0FF); outline(dev, x0, y0, x1 - x0, y1 - y0, 0xFFBFE0FF);
                    char d[28]; sprintf(d, tr("New zone  %dx%d", "Zone  %dx%d"), (int)(x1 - x0), (int)(y1 - y0));
                    fo->begin(dev); fo->draw_c(dev, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, d, snap(12.0f), 0xFFFFFFFF, C_STROKE, 1.4f);
                }
                else {
                    zoneDrawing_ = false;
                    if ((x1 - x0) > snap(16.0f) && (y1 - y0) > snap(16.0f) && C.guideGroupCount < GUIDE_GROUPS_MAX) {
                        GuideGroup z; z.x = x0 / sw; z.y = y0 / sh; z.w = (x1 - x0) / sw; z.h = (y1 - y0) / sh;
                        C.guideGroup[C.guideGroupCount] = z; groupSel_ = C.guideGroupCount++; save_ui_config();
                    }
                }
            }

            // ---- draggable PANEL : move it FIRST (so background + content share one position), then draw ----
            static float panDX = 0.0f, panDY = 0.0f; static int grabPan = 0;
            const bool tbHov = inrect(mo, pxp, pyp, pw, tbh);
            if (mo && mo->down && !grabPan && tbHov && editConfirm_ == 0 && grabZone < 0 && !zoneDrawing_) { grabPan = 1; panDX = mo->x - pxp; panDY = mo->y - pyp; }
            if (!(mo && mo->down)) { if (grabPan) save_ui_config(); grabPan = 0; }   // persist the panel position on drop
            if (grabPan && mo) {
                pxp = snap(mo->x - panDX); pyp = snap(mo->y - panDY);
                if (pxp > sw - pw) pxp = snap(sw - pw); if (pxp < 0.0f) pxp = 0.0f;
                if (pyp > sh - ph) pyp = snap(sh - ph); if (pyp < 0.0f) pyp = 0.0f;
                C.zonePanelX = pxp / sw; C.zonePanelY = pyp / sh;   // store the CLAMPED position -> stays == what's drawn
            }
            drop_shadow(dev, pxp, pyp, pw, ph, snap(6.0f), 74);
            vg(dev, pxp, pyp, pw, ph, 0xF01A2624, 0xF0111A18);
            outline(dev, pxp, pyp, pw, ph, C_BORDERHI);
            vg(dev, pxp, pyp, pw, tbh, (tbHov || grabPan) ? 0xF02C3A50 : 0xF0243044, 0xF01A2434);
            flat(dev, pxp, pyp, pw, 1, 0x55FFFFFF);
            fo->begin(dev); fo->draw_lc(dev, pxp + snap(12.0f), pyp + tbh * 0.5f, pTitle, snap(11.0f), C_GOLD, C_STROKE, 1.0f);

            const float px0 = pxp + snap(10.0f), pxw = pw - snap(20.0f);
            // + Zone (plain) | + Party (6 party-size zones, role 1..6) | + Ally (2 alliance zones, role 7/8).
            const float nbY = pyp + tbh + snap(6.0f), nbH = snap(24.0f), nbT = (pxw - snap(12.0f)) / 3.0f;
            const float Lx = (C.partyRefX[0] >= 0.0f ? C.partyRefX[0] : 0.42f), Rx = (C.partyRefX[1] >= 0.0f ? C.partyRefX[1] : 0.56f);
            const float x0f = Lx < Rx ? Lx : Rx, wf = Lx < Rx ? (Rx - Lx) : (Lx - Rx);
            if (push_btn(dev, fo, mo, rClick, CTRL_ID, px0, nbY, nbT, nbH, tr("+ Zone", "+ Zone"), 0) && C.guideGroupCount < GUIDE_GROUPS_MAX) {
                GuideGroup z; z.x = 0.42f; z.y = 0.42f; z.w = 0.16f; z.h = 0.14f; C.guideGroup[C.guideGroupCount] = z; groupSel_ = C.guideGroupCount++; save_ui_config();
            }
            if (push_btn(dev, fo, mo, rClick, CTRL_ID, px0 + nbT + snap(6.0f), nbY, nbT, nbH, tr("+ Party", "+ Party"), 0)) {
                const float By = (C.partyBottomY >= 0.0f ? C.partyBottomY : 0.95f);
                bool made = false;
                for (int i = 0; i < 6 && C.guideGroupCount < GUIDE_GROUPS_MAX; ++i) {   // create any MISSING count 1p..6p
                    bool has = false; for (int g = 0; g < C.guideGroupCount; ++g) if (C.guideGroup[g].role == i + 1) has = true;
                    if (has) continue;
                    // the TOP is specific per count ; use the stored line if it's a valid top (above the bottom),
                    // else rebuild it -- interpolate from the neighbours (3p<->5p for 4p), or fall back to spacing.
                    float top = C.partyRef[i];
                    if (!(top >= 0.0f && top < By - 0.03f)) {
                        // rebuild : interpolate from the neighbour COUNT zones (role i and i+2, i.e. counts i and i+2)
                        // -- their CURRENT tops -- falling back to partyRef, then to default spacing.
                        float lo = -1.0f, hi = -1.0f;
                        for (int g = 0; g < C.guideGroupCount; ++g) {
                            if (C.guideGroup[g].role == i)          lo = C.guideGroup[g].y;
                            else if (C.guideGroup[g].role == i + 2) hi = C.guideGroup[g].y;
                        }
                        if (lo < 0.0f && i > 0 && C.partyRef[i - 1] >= 0.0f && C.partyRef[i - 1] < By) lo = C.partyRef[i - 1];
                        if (hi < 0.0f && i < 5 && C.partyRef[i + 1] >= 0.0f && C.partyRef[i + 1] < By) hi = C.partyRef[i + 1];
                        if (lo >= 0.0f && hi >= 0.0f) top = (lo + hi) * 0.5f;
                        else if (lo >= 0.0f)          top = lo - 0.015f;
                        else if (hi >= 0.0f)          top = hi + 0.015f;
                        else                          top = 0.90f - 0.0145f * (float)i;
                    }
                    if (top >= By) top = By - 0.05f; if (top < 0.0f) top = 0.0f;
                    GuideGroup z; z.x = x0f; z.y = top; z.w = wf; z.h = By - top; z.role = i + 1; z.allow[ZPERM_PARTY] = true;
                    sprintf(z.name, tr("Party %dp", "Groupe %dp"), i + 1);
                    C.guideGroup[C.guideGroupCount++] = z; made = true;
                }
                if (made) { groupSel_ = C.guideGroupCount - 1; save_ui_config(); }
            }
            if (push_btn(dev, fo, mo, rClick, CTRL_ID, px0 + 2 * nbT + snap(12.0f), nbY, nbT, nbH, tr("+ Ally", "+ Ally"), 0)) {
                const float adef[4] = { 0.34f, 0.42f, 0.48f, 0.56f };   // A1 top/bottom, A2 top/bottom (defaults)
                bool made = false;
                for (int a = 0; a < 2 && C.guideGroupCount < GUIDE_GROUPS_MAX; ++a) {   // role 7 = Alliance 1, role 8 = Alliance 2
                    const int rl = 7 + a;
                    bool has = false; for (int g = 0; g < C.guideGroupCount; ++g) if (C.guideGroup[g].role == rl) has = true;
                    if (has) continue;
                    float top = (C.allyRefY[a * 2] >= 0.0f ? C.allyRefY[a * 2] : adef[a * 2]);
                    float bot = (C.allyRefY[a * 2 + 1] >= 0.0f ? C.allyRefY[a * 2 + 1] : adef[a * 2 + 1]);
                    if (bot < top) { const float t = top; top = bot; bot = t; }
                    if (bot - top < 0.03f) bot = top + 0.03f;
                    GuideGroup z; z.x = x0f; z.y = top; z.w = wf; z.h = bot - top; z.role = rl; z.allow[ZPERM_ALLIANCE] = true;
                    sprintf(z.name, tr("Alliance %d", "Alliance %d"), a + 1);
                    C.guideGroup[C.guideGroupCount++] = z; made = true;
                }
                if (made) { groupSel_ = C.guideGroupCount - 1; save_ui_config(); }
            }
            const float listTop = pyp + tbh + newBtnRow, listBot = pyp + ph - actionsH, visH = listBot - listTop;
            const float total = C.guideGroupCount * stride, maxScroll = (total > visH) ? (total - visH) : 0.0f;
            const bool  sbVisible = maxScroll > 0.0f;
            const float listRight = px0 + pxw - (sbVisible ? snap(10.0f) : 0.0f);
            if (overPanel && ui_config().wheel != 0) { guideScroll_ -= (float)ui_config().wheel * stride * 3.0f; ui_config().wheel = 0; }
            if (guideScroll_ < 0.0f) guideScroll_ = 0.0f; if (guideScroll_ > maxScroll) guideScroll_ = maxScroll;
            static int dragRow = -1; static float dragStartY = 0.0f, dragOff = 0.0f; static bool dragMoved = false;
            const float rw2 = listRight - px0;
            float ly = listTop - guideScroll_;
            for (int i = 0; i < C.guideGroupCount; ++i, ly += stride) {
                if (ly < listTop || ly + rowH > listBot) continue;
                if (dragRow == i && dragMoved) { vg(dev, px0, ly, rw2, rowH, 0xF0111A18, 0xF00B1210); outline(dev, px0, ly, rw2, rowH, C_BORDER); continue; }   // gap where the dragged row was
                GuideGroup& z = C.guideGroup[i]; const bool sel = (groupSel_ == i);
                const bool hov = inrect(mo, px0, ly, rw2, rowH);
                vg(dev, px0, ly, rw2, rowH, sel ? 0xFF2C5AA0 : (hov ? 0xF02A3548 : 0xF01E2838), sel ? 0xFF20447E : 0xF0161E2C);
                outline(dev, px0, ly, rw2, rowH, sel ? C_ACCENTHI : C_BORDER);
                flat(dev, px0 + snap(4.0f), ly + rowH * 0.5f - snap(5.0f), snap(6.0f), snap(10.0f), GRP_COL[i % 8]);
                char zl[28]; if (z.name[0]) sprintf(zl, "%s", z.name); else sprintf(zl, tr("Zone %d", "Zone %d"), i + 1);
                fo->begin(dev); fo->draw_lc(dev, px0 + snap(16.0f), ly + rowH * 0.5f, zl, snap(11.0f), sel ? 0xFFFFFFFF : C_TEXT, C_STROKE, 1.0f);
                if (hov && rClick && dragRow < 0 && editConfirm_ == 0) { dragRow = i; dragStartY = mo->y; dragOff = mo->y - ly; dragMoved = false; groupSel_ = i; }   // press : select ; a drag then reorders
            }
            // DRAG-TO-REORDER : the picked row follows the mouse, a gold line shows the drop slot, release moves it.
            if (dragRow >= 0 && mo) {
                if (mo->down) {
                    if (fabsf(mo->y - dragStartY) > 4.0f) dragMoved = true;
                    if (dragMoved && dragRow < C.guideGroupCount) {
                        int tgt = (int)((mo->y - (listTop - guideScroll_)) / stride); if (tgt < 0) tgt = 0; if (tgt >= C.guideGroupCount) tgt = C.guideGroupCount - 1;
                        flat(dev, px0, listTop - guideScroll_ + tgt * stride - snap(1.0f), rw2, snap(2.0f), C_ACCENTHI);   // drop indicator
                        float fy = mo->y - dragOff; if (fy < listTop) fy = listTop; if (fy + rowH > listBot) fy = listBot - rowH;   // floating row (clamped to the list)
                        GuideGroup& z = C.guideGroup[dragRow];
                        vg(dev, px0, fy, rw2, rowH, 0xFF3A6FC0, 0xFF244A88); outline(dev, px0, fy, rw2, rowH, C_ACCENTHI);
                        flat(dev, px0 + snap(4.0f), fy + rowH * 0.5f - snap(5.0f), snap(6.0f), snap(10.0f), GRP_COL[dragRow % 8]);
                        char zl[28]; if (z.name[0]) sprintf(zl, "%s", z.name); else sprintf(zl, tr("Zone %d", "Zone %d"), dragRow + 1);
                        fo->begin(dev); fo->draw_lc(dev, px0 + snap(16.0f), fy + rowH * 0.5f, zl, snap(11.0f), 0xFFFFFFFF, C_STROKE, 1.0f);
                    }
                } else {
                    if (dragMoved && dragRow < C.guideGroupCount) {
                        int tgt = (int)((mo->y - (listTop - guideScroll_)) / stride); if (tgt < 0) tgt = 0; if (tgt >= C.guideGroupCount) tgt = C.guideGroupCount - 1;
                        if (tgt != dragRow) {
                            GuideGroup tmp = C.guideGroup[dragRow];
                            if (tgt > dragRow) for (int k = dragRow; k < tgt; ++k) C.guideGroup[k] = C.guideGroup[k + 1];
                            else               for (int k = dragRow; k > tgt; --k) C.guideGroup[k] = C.guideGroup[k - 1];
                            C.guideGroup[tgt] = tmp; groupSel_ = tgt; save_ui_config();
                        }
                    }
                    dragRow = -1; dragMoved = false;
                }
            }
            if (sbVisible) {
                const float sbw = snap(6.0f), sbx = px0 + pxw - sbw;
                float thumbH = visH * visH / total; if (thumbH < snap(24.0f)) thumbH = snap(24.0f);
                const float thumbY = listTop + ((maxScroll > 0.0f) ? guideScroll_ / maxScroll : 0.0f) * (visH - thumbH);
                flat(dev, sbx, listTop, sbw, visH, 0x50101720);
                static int grabSB = 0; static float sbOff = 0.0f;
                const bool thHov = inrect(mo, sbx - snap(2.0f), thumbY, sbw + snap(4.0f), thumbH);
                if (mo && mo->down && !grabSB && thHov && editConfirm_ == 0) { grabSB = 1; sbOff = mo->y - thumbY; }
                if (!(mo && mo->down)) grabSB = 0;
                if (grabSB && mo) { float t = (visH - thumbH > 0.0f) ? (mo->y - sbOff - listTop) / (visH - thumbH) : 0.0f; t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); guideScroll_ = t * maxScroll; }
                vg(dev, sbx, thumbY, sbw, thumbH, (thHov || grabSB) ? 0xFF5A8FE0 : 0xFF3A506E, (thHov || grabSB) ? 0xFF3A6FC0 : 0xFF283A52);
                outline(dev, sbx, thumbY, sbw, thumbH, 0x80FFFFFF);
            }

            float ay = listBot + snap(8.0f);
            if (renaming) {   // real inline TEXT FIELD (caret + Left/Right/Home/End/Delete via the key hook) + OK button -- no Enter needed
                const float fH = snap(30.0f), okW = snap(58.0f), fGap = snap(8.0f), fW = pxw - okW - fGap;
                rpanel(dev, px0, ay, fW, fH, snap(6.0f), 0xE6080C14, 0xE605080F, C_ACCENT, snap(1.5f));
                const float txY = ay + fH * 0.5f, txX = px0 + snap(10.0f);
                fo->begin(dev);
                if (nameLen_ > 0) fo->draw_lc(dev, txX, txY, nameBuf_, snap(13.0f), C_TEXT, C_STROKE, 1.0f);
                else              fo->draw_lc(dev, txX, txY, tr("Zone name...", "Nom de la zone..."), snap(12.0f), C_MUTE, C_STROKE, 1.0f);
                if (sinf(f.t * 5.0f) > 0.0f) {   // blinking caret AT the cursor index
                    int cn = nameCur_ < 0 ? 0 : (nameCur_ > nameLen_ ? nameLen_ : nameCur_);
                    char pre[32]; memcpy(pre, nameBuf_, cn); pre[cn] = 0;
                    const float cxx = txX + (cn > 0 ? fo->measure(pre, snap(13.0f)) : 0.0f) + snap(1.0f);
                    flat(dev, cxx, ay + snap(7.0f), snap(2.0f), fH - snap(14.0f), C_ACCENTHI);
                }
                if (push_btn(dev, fo, mo, rClick, CTRL_ID, px0 + fW + fGap, ay, okW, fH, tr("OK", "OK"), 0)) { editZoneName_ = -1; nameFocus_ = false; save_ui_config(); }
            } else if (groupSel_ >= 0 && groupSel_ < C.guideGroupCount) {   // selected ZONE : Rename / Delete + permissions
                GuideGroup& z = C.guideGroup[groupSel_];
                const float halfg = (pxw - snap(8.0f)) * 0.5f;
                if (push_btn(dev, fo, mo, rClick, CTRL_ID, px0, ay, halfg, snap(24.0f), tr("Rename", "Renom."), 0)) {
                    editZoneName_ = groupSel_; nameFocus_ = true;
                    strncpy(nameBuf_, z.name, sizeof(nameBuf_) - 1); nameBuf_[sizeof(nameBuf_) - 1] = 0; nameLen_ = (int)strlen(nameBuf_); nameCur_ = nameLen_;
                }
                if (push_btn(dev, fo, mo, rClick, CTRL_ID, px0 + halfg + snap(8.0f), ay, halfg, snap(24.0f), tr("Delete", "Suppr."), 1)) {
                    for (int k = groupSel_; k < C.guideGroupCount - 1; ++k) C.guideGroup[k] = C.guideGroup[k + 1];
                    --C.guideGroupCount; groupSel_ = -1; editZoneName_ = -1; nameFocus_ = false; save_ui_config();
                }
                ay += snap(30.0f);
                // header : make the chips unambiguous -> ON (green) = that box MAY sit over this zone.
                fo->begin(dev);
                fo->draw_lc(dev, px0, ay + snap(7.0f), tr("Boxes allowed to overlap this zone :", "Cadres autorisés à passer dessus :"), snap(10.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.0f);
                ay += snap(19.0f);
                const char* pl[ZPERM_COUNT] = { tr("Party", "Groupe"), tr("Alliance", "Alliance"), tr("Hub", "Hub"), tr("Target", "Cible") };
                const float cgap = snap(6.0f), cwp = (pxw - cgap * (float)(ZPERM_COUNT - 1)) / (float)ZPERM_COUNT;
                for (int k = 0; k < ZPERM_COUNT && groupSel_ >= 0 && groupSel_ < C.guideGroupCount; ++k) {
                    const float cxp = px0 + k * (cwp + cgap); const bool on = z.allow[k];
                    const bool hov = inrect(mo, cxp, ay, cwp, snap(24.0f));
                    vg(dev, cxp, ay, cwp, snap(24.0f), on ? 0xFF2E7A44 : (hov ? 0xF04A2530 : 0xF0301A22), on ? 0xFF1F5730 : 0xF0221016);   // ON = green (allowed), OFF = faint red (blocked)
                    outline(dev, cxp, ay, cwp, snap(24.0f), on ? 0xFF5ADC8A : 0xFF8A4450);
                    fo->begin(dev); fo->draw_c(dev, cxp + cwp * 0.5f, ay + snap(12.0f), pl[k], snap(10.0f), on ? 0xFFFFFFFF : 0xFFC98A90, C_STROKE, 1.0f);
                    if (hov && rClick) { z.allow[k] = !z.allow[k]; save_ui_config(); }
                }
                ay += snap(28.0f);
                const bool anyAllow = z.allow[0] || z.allow[1] || z.allow[2] || z.allow[3];
                fo->begin(dev);
                fo->draw_lc(dev, px0, ay + snap(6.0f), anyAllow ? tr("Others are pushed out.", "Les autres sont repoussés.") : tr("Keep-out : every box is pushed out.", "Zone interdite : tout cadre est repoussé."), snap(10.0f), anyAllow ? C_MUTE : 0xFFE79098, C_STROKE, 1.0f);
            } else {
                fo->begin(dev); fo->draw_lc(dev, px0, ay + snap(12.0f), pHint, snap(11.0f), C_MUTE, C_STROKE, 1.0f);
            }
        }
        }   // end if (editShowLines_)
        // The toolbar HIDES while a box is being dragged (it sits at the top and would otherwise mask boxes
        // dragged up there) -> it reappears the moment the click is released.
        if (!edit_drag_busy()) {
        const float bw = snap(820.0f), bh = snap(62.0f), bx = snap((sw - bw) * 0.5f), by = snap(22.0f);
        const float pop = ease(900, 1.0f, 16.0f);                                   // subtle slide-in
        const float byA = by - (1.0f - pop) * snap(10.0f);
        edit_set_ui_blocker(bx - snap(4.0f), byA - snap(2.0f), bw + snap(8.0f), bh + snap(12.0f), f.t);   // TOP priority : boxes under the toolbar don't grab (so Done/Default/Rules always click)
        shadow_down(dev, bx - snap(4.0f), byA + bh, bw + snap(8.0f), snap(10.0f), 0x55000000);
        vg(dev, bx, byA, bw, bh, 0xF0202B3C, 0xF0141C28);
        flat(dev, bx, byA, bw, 1, 0x55FFFFFF);                                       // top sheen
        outline(dev, bx, byA, bw, bh, C_BORDERHI);
        fo->begin(dev);
        // ROW 1 : title (left) + the action buttons (right).  ROW 2 : the full legend, centred, clear of the buttons.
        fo->draw_lc(dev, bx + snap(18.0f), byA + snap(19.0f), tr("EDIT LAYOUT", "ÉDITION"), snap(15.0f), C_TEXT, C_STROKE, 1.2f);
        fo->draw_c(dev, bx + bw * 0.5f, byA + snap(47.0f),
                   tr("Wheel = size  \xC2\xB7  Ctrl = lock Vertical  \xC2\xB7  Shift = lock Horizontal  \xC2\xB7  Ctrl+Shift = free placement",
                      "Molette = taille  \xC2\xB7  Ctrl = bloque Vertical  \xC2\xB7  Shift = bloque Horizontal  \xC2\xB7  Ctrl+Shift = placement libre"),
                   snap(12.0f), C_MUTE, C_STROKE, 1.0f);
        const bool tbClick = click && editConfirm_ == 0;   // while the confirm modal is up the toolbar is inert
        const float bh2 = snap(28.0f), dby = byA + snap(7.0f);   // buttons on ROW 1 ; the legend sits on ROW 2 below them
        const float db = snap(80.0f),  dbx = bx + bw - db - snap(10.0f);            // Done (far right)
        const float rb = snap(90.0f),  rbx = dbx - rb - snap(8.0f);                 // Default
        const float sb = snap(120.0f), sbx = rbx - sb - snap(8.0f);                 // Clear lines
        const float lb = snap(104.0f), lbx = sbx - lb - snap(8.0f);                 // Rules on/off toggle
        // RULES toggle : show / hide the reference lines. While ON the HUD boxes are hidden (hud.cpp) so only
        // the rules + this toolbar show -- align each line onto the game's native window, then toggle OFF to
        // go back to dragging/resizing the boxes.
        {
            const bool hov = inrect(mo, lbx, dby, lb, bh2);
            const float t = ease(904, (editShowLines_ || hov) ? 1.0f : 0.0f);
            halo_rect(dev, lbx, dby, lb, bh2, C_ACCENT, t * 0.8f);
            vg(dev, lbx, dby, lb, bh2, lerpc(0xFF2A3548, 0xFF3A82E0, t), lerpc(0xFF1D2738, 0xFF2A61B6, t));
            outline(dev, lbx, dby, lb, bh2, lerpc(C_BORDERHI, C_ACCENTHI, t));
            fo->begin(dev); fo->draw_c(dev, lbx + lb * 0.5f, dby + bh2 * 0.5f,
                                       editShowLines_ ? tr("Rules: ON", "Règles: ON") : tr("Rules: OFF", "Règles: OFF"),
                                       snap(13.0f), C_TEXT, C_STROKE, 1.0f);
            if (hov && tbClick) editShowLines_ = !editShowLines_;
        }
        // Clear lines : wipe every reference line back to default -> asks to confirm first.
        bool refSet = ui_config().partyBottomY >= 0.0f || ui_config().partyRefX[0] >= 0.0f || ui_config().partyRefX[1] >= 0.0f;
        for (int i = 0; i < 6; ++i) if (ui_config().partyRef[i] >= 0.0f) refSet = true;
        for (int i = 0; i < 4; ++i) if (ui_config().allyRefY[i] >= 0.0f) refSet = true;
        {
            const bool shv = inrect(mo, sbx, dby, sb, bh2) && refSet;
            const float t = ease(901, shv ? 1.0f : 0.0f);
            halo_rect(dev, sbx, dby, sb, bh2, C_ACCENT, t * 0.8f);
            vg(dev, sbx, dby, sb, bh2, lerpc(0xFF2A3548, 0xFF3A82E0, t), lerpc(0xFF1D2738, 0xFF2A61B6, t));
            outline(dev, sbx, dby, sb, bh2, lerpc(C_BORDERHI, C_ACCENTHI, t));
            fo->begin(dev); fo->draw_c(dev, sbx + sb * 0.5f, dby + bh2 * 0.5f, tr("Clear lines", "Effacer lignes"), snap(13.0f), refSet ? C_TEXT : C_MUTE, C_STROKE, 1.0f);
            if (shv && tbClick) editConfirm_ = 1;   // -> confirmation modal
        }
        if (push_btn(dev, fo, mo, tbClick, CTRL_ID, rbx, dby, rb, bh2, tr("Default", "Défaut"), 1)) editConfirm_ = 2;   // -> confirm
        if (push_btn(dev, fo, mo, tbClick, CTRL_ID, dbx, dby, db, bh2, tr("Done", "Terminé"), 0)) { ui_config().editLayout = false; editShowLines_ = false; editZoneName_ = -1; nameFocus_ = false; groupSel_ = -1; save_ui_config(); }
        }   // end if (!edit_drag_busy()) -> toolbar hidden while dragging

        // CONFIRMATION dialog for the destructive actions (Clear lines / Default), drawn on top and capturing input.
        if (editConfirm_ != 0) {
            flat(dev, 0.0f, 0.0f, sw, sh, 0x99000000);                              // dim the screen behind the dialog
            const float mw = snap(440.0f), mh = snap(170.0f), mx = snap((sw - mw) * 0.5f), my = snap((sh - mh) * 0.5f);
            shadow_down(dev, mx - snap(4.0f), my + mh, mw + snap(8.0f), snap(12.0f), 0x66000000);
            vg(dev, mx, my, mw, mh, 0xF0202B3C, 0xF0141C28);
            flat(dev, mx, my, mw, 1, 0x55FFFFFF);                                    // top sheen
            outline(dev, mx, my, mw, mh, C_BORDERHI);
            fo->begin(dev); fo->draw_c(dev, mx + mw * 0.5f, my + snap(34.0f), tr("Please confirm", "Confirmation"), snap(13.0f), C_GOLD, C_STROKE, 1.2f);
            const char* msg = (editConfirm_ == 1) ? tr("Clear all reference lines?", "Effacer toutes les lignes de repère ?")
                                                  : tr("Reset all boxes to default?", "Réinitialiser toutes les boîtes ?");
            fo->begin(dev); fo->draw_c(dev, mx + mw * 0.5f, my + snap(74.0f), msg, snap(15.0f), C_TEXT, C_STROKE, 1.0f);
            const float pbw = snap(160.0f), pbh = snap(34.0f), pby = my + mh - pbh - snap(20.0f);
            const float clx = mx + snap(30.0f), crx = mx + mw - pbw - snap(30.0f);
            if (push_btn(dev, fo, mo, click, CTRL_ID, clx, pby, pbw, pbh, tr("Cancel", "Annuler"), 0)) editConfirm_ = 0;
            if (push_btn(dev, fo, mo, click, CTRL_ID, crx, pby, pbw, pbh, tr("Confirm", "Confirmer"), 1)) {
                if (editConfirm_ == 1) {   // clear every reference line
                    for (int i = 0; i < 6; ++i) ui_config().partyRef[i] = -1.0f;
                    for (int i = 0; i < 4; ++i) ui_config().allyRefY[i] = -1.0f;
                    ui_config().partyBottomY = -1.0f; ui_config().partyRefX[0] = ui_config().partyRefX[1] = -1.0f;
                    save_ui_config();
                } else if (editConfirm_ == 2) {   // reset every box position + size
                    reset_boxes();
                }
                editConfirm_ = 0;
            }
        }
        draw_ui_cursor(dev, mo);   // ALWAYS, same reason as the config page : ours is the only pointer while editing
}

// ---- Profile tab (tab_ == 1 branch), lifted verbatim from draw() (byte-identical). ----
void ConfigPage::draw_profile_tab(const Frame& f, u32 dev, Font* fo, const MouseState* mo, bool click,
                                  float ix, float iw, float bodyY, float bodyH, float pageBot, float pulse, float e) {
        ui_config().wheel = 0;
        if (profDirty_) { profile_refresh(); profDirty_ = false; }
        bool commit = kbCommit_; kbCommit_ = false;
        const char* charName = (f.game && f.game->inGame && f.game->me.name[0]) ? f.game->me.name : 0;
        const int   nprof = profile_count();
        const bool  dirty = activeProf_[0] && profile_dirty();

        // ===== LEFT RAIL : the current character + one-click saves =====
        const float sbW = snap(290.0f);
        vg(dev, ix, bodyY, sbW, bodyH, C_SIDEBAR, 0xF0121A27);
        cs_add(dev); soft_blob(dev, ix + sbW * 0.5f, bodyY + snap(2.0f), sbW * 0.62f, bodyH * 0.16f, 0x0A2A4E84);   // faint top glow
        q4(dev, ix + sbW, bodyY, snap(22.0f), bodyH, 0x30000000, 0x00000000, 0x30000000, 0x00000000);              // recessed shadow on the content side
        flat(dev, ix + sbW, bodyY, 1, bodyH, C_BORDER);
        const float cx0 = ix + snap(20.0f), cw0 = sbW - snap(40.0f);
        fo->begin(dev); fo->draw_lc(dev, cx0, bodyY + snap(24.0f), tr("CHARACTER", "PERSONNAGE"), snap(12.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.4f);

        float ry0 = bodyY + snap(40.0f); const float ccH = snap(98.0f);
        drop_shadow(dev, cx0, ry0, cw0, ccH, snap(4.0f), 50);
        rpanel(dev, cx0, ry0, cw0, ccH, snap(11.0f), 0x55101826, 0x550A111C, C_BORDER, snap(1.5f));
        rrect_fill(dev, cx0 + snap(4.0f), ry0 + snap(9.0f), snap(4.0f), ccH - snap(18.0f), snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse), lerpc(C_GOLD, C_GOLDHI, pulse));
        const float avR = snap(22.0f), avX = cx0 + snap(34.0f), avY = ry0 + snap(34.0f);
        cs(dev); disc(dev, avX, avY, avR, fa(0xFF1E8A82)); disc(dev, avX, avY, avR - snap(2.0f), fa(0xFF123E3A));
        if (charName) { char ini[2] = { (char)(charName[0] >= 'a' && charName[0] <= 'z' ? charName[0] - 32 : charName[0]), 0 };
            fo->begin(dev); fo->draw_cc(dev, avX, avY, ini, snap(22.0f), fa(C_GOLDHI), fa(C_STROKE), 1.6f); }
        else { fo->begin(dev); fo->draw_cc(dev, avX, avY, "?", snap(22.0f), fa(C_MUTE), fa(C_STROKE), 1.4f); }
        const float nlx = avX + avR + snap(14.0f);
        fo->begin(dev);
        fo->draw_lc(dev, nlx, ry0 + snap(26.0f), charName ? charName : tr("(not logged in)", "(non connecté)"), snap(17.0f), fa(charName ? C_TEXT : C_MUTE), fa(C_STROKE), 1.2f);
        char actl[80]; if (activeProf_[0]) _snprintf(actl, sizeof(actl), tr("Active : %s", "Actif : %s"), activeProf_); else strcpy(actl, tr("Active : none", "Actif : aucun"));
        fo->draw_lc(dev, nlx, ry0 + snap(50.0f), actl, snap(12.0f), fa(C_DIM), fa(C_STROKE), 1.0f);
        if (dirty) { cs(dev); disc(dev, nlx + snap(3.0f), ry0 + snap(70.0f), snap(3.5f), fa(0xFFFFB454));
            fo->begin(dev); fo->draw_lc(dev, nlx + snap(12.0f), ry0 + snap(70.0f), tr("unsaved changes", "modifs non enregistrées"), snap(11.0f), fa(0xFFFFB454), fa(C_STROKE), 1.0f); }

        // quick-save buttons
        ry0 += ccH + snap(18.0f);
        fo->begin(dev); fo->draw_lc(dev, cx0, ry0, tr("QUICK SAVE", "SAUVEGARDE RAPIDE"), snap(11.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.2f);
        ry0 += snap(16.0f); const float qbH = snap(40.0f);
        { char defn[64]; const bool haveDef = profile_default_name(defn, sizeof(defn));   // "Name Main/Sub" -> the auto-loading profile for this character/job
          char lbl[96]; if (haveDef) _snprintf(lbl, sizeof(lbl), tr("Save for %s", "Sauver pour %s"), defn); else strcpy(lbl, tr("Save for character", "Sauver pour le perso"));
          if (push_btn(dev, fo, mo, click, CTRL_ID, cx0, ry0, cw0, qbH, lbl, 0) && haveDef) {
              profile_save(defn); record_char_profile(defn); strncpy(activeProf_, defn, sizeof(activeProf_) - 1); activeProf_[sizeof(activeProf_) - 1] = 0;
              nameBuf_[0] = 0; nameLen_ = 0; nameCur_ = 0; nameFocus_ = false; profDirty_ = true; } }
        ry0 += qbH + snap(10.0f);
        if (push_btn(dev, fo, mo, click, CTRL_ID, cx0, ry0, cw0, qbH, tr("Save as Default", "Sauver comme Défaut"), 0)) {
            profile_save("Default"); record_char_profile("Default"); strcpy(activeProf_, "Default");
            nameBuf_[0] = 0; nameLen_ = 0; nameCur_ = 0; nameFocus_ = false; profDirty_ = true; }
        ry0 += qbH + snap(18.0f);
        fo->begin(dev); fo->draw_lc(dev, cx0, ry0, tr("A profile saves every setting.", "Un profil garde tous les réglages."), snap(11.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        fo->draw_lc(dev, cx0, ry0 + snap(16.0f), tr("Quick-save binds it to this character.", "La sauvegarde rapide le lie au perso."), snap(11.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        // Reset everything to factory defaults (was the per-module "Default (all)" button ; global -> lives here once).
        ry0 += snap(42.0f);
        fo->begin(dev); fo->draw_lc(dev, cx0, ry0, tr("RESET", "R\xC3\x89INITIALISER"), snap(11.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.2f);
        ry0 += snap(16.0f);
        if (push_btn(dev, fo, mo, click && !profResetConfirm_, CTRL_ID, cx0, ry0, cw0, qbH, tr("Reset all settings", "Tout r\xC3\xA9initialiser"), 1)) profResetConfirm_ = true;

        // ===== RIGHT : create / rename + the saved library =====
        const float pX = ix + sbW + snap(34.0f), pW = (ix + iw) - pX - snap(30.0f);
        float py = bodyY + snap(26.0f);
        fo->begin(dev);
        const char* ptit = tr("Profiles", "Profils");
        const float ptW = fo->measure(ptit, snap(22.0f));
        fo->draw_lc(dev, pX, py, ptit, snap(22.0f), fa(C_GOLD), fa(C_STROKE), 1.4f);
        flat(dev, pX, py + snap(22.0f), ptW * e, snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse));
        fo->begin(dev);   // re-bind the glyph atlas : the underline flat() above left a colour-only state
        fo->draw_lc(dev, pX, py + snap(44.0f), tr("Load to switch instantly. Type a name to create a new profile, or an existing name to overwrite it.", "Charge pour basculer instantanément. Tape un nom pour créer un profil, ou un nom existant pour le remplacer."), snap(13.0f), fa(C_DIM), fa(C_STROKE), 1.0f);

        // create / rename field (rounded recessed) + action button
        py += snap(66.0f);
        fo->begin(dev); fo->draw_lc(dev, pX, py, tr("NEW PROFILE", "NOUVEAU PROFIL"), snap(11.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.2f);
        py += snap(16.0f);
        const float fH = snap(42.0f), btnW = snap(148.0f), fGap = snap(12.0f), fW = pW - btnW - fGap;
        const bool fldHov = inrect(mo, pX, py, fW, fH);
        if (click) { const bool was = nameFocus_; nameFocus_ = fldHov; if (fldHov && !was) nameCur_ = nameLen_; }
        const float ft = ease(700, nameFocus_ ? 1.0f : 0.0f);
        halo_rect(dev, pX, py, fW, fH, C_ACCENT, ft * 0.6f);
        rpanel(dev, pX, py, fW, fH, snap(8.0f), 0xE6080C14, 0xE605080F, lerpc(C_CTL_BR, C_ACCENT, ft), snap(1.5f));
        const float txY = py + fH * 0.5f, txX = pX + snap(15.0f);
        fo->begin(dev);
        if (nameLen_ > 0) fo->draw_lc(dev, txX, txY, nameBuf_, snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
        else              fo->draw_lc(dev, txX, txY, tr("Type a profile name...", "Tape un nom de profil..."), snap(14.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        if (nameFocus_ && sinf(f.t * 5.0f) > 0.0f) {              // blinking caret AT the cursor index
            int cn = nameCur_ < 0 ? 0 : (nameCur_ > nameLen_ ? nameLen_ : nameCur_);
            char pre[32]; memcpy(pre, nameBuf_, cn); pre[cn] = 0;
            const float cxx = txX + (cn > 0 ? fo->measure(pre, snap(15.0f)) : 0.0f) + snap(1.0f);
            flat(dev, cxx, py + snap(11.0f), snap(2.0f), fH - snap(22.0f), C_ACCENTHI); }
        const bool canSave = nameLen_ > 0, exists = canSave && profile_exists(nameBuf_);
        const char* lbl = exists ? tr("Overwrite", "Remplacer") : tr("Create", "Créer");   // always non-destructive : never auto-renames the active one
        const bool doSave = (push_btn(dev, fo, mo, click, CTRL_ID, pX + fW + fGap, py, btnW, fH, lbl, 0) || commit) && canSave;
        if (doSave) {
            profile_save(nameBuf_); record_char_profile(nameBuf_);
            strncpy(activeProf_, nameBuf_, sizeof(activeProf_) - 1); activeProf_[sizeof(activeProf_) - 1] = 0;
            nameBuf_[0] = 0; nameLen_ = 0; nameCur_ = 0; nameFocus_ = false;   // clear the field -> ready for the NEXT new profile
            profDirty_ = true;
        }

        // saved library
        py += fH + snap(24.0f);
        fo->begin(dev); char hdr[48]; sprintf(hdr, tr("SAVED  (%d)", "ENREGISTRÉS  (%d)"), nprof);
        fo->draw_lc(dev, pX, py, hdr, snap(12.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.2f);
        py += snap(24.0f);
        const float rowH = snap(50.0f), rGap = snap(9.0f);
        if (nprof == 0) {
            rpanel(dev, pX, py, pW, snap(62.0f), snap(11.0f), 0x2A101826, 0x2A0A111C, C_BORDER, snap(1.5f));
            fo->begin(dev); fo->draw_c(dev, pX + pW * 0.5f, py + snap(31.0f), tr("No profiles yet -- quick-save one, or type a name and Create.", "Aucun profil -- fais une sauvegarde rapide, ou tape un nom et Créer."), snap(14.0f), fa(C_DIM), fa(C_STROKE), 1.0f);
        }
        const int maxRows = (int)((pageBot - py) / (rowH + rGap));
        const int cnLen = charName ? (int)strlen(charName) : 0;
        for (int i = 0; i < nprof && i < maxRows; ++i) {
            const char* nm = profile_name(i);
            const bool active = activeProf_[0] && strcmp(nm, activeProf_) == 0;
            const bool isDef  = strcmp(nm, "Default") == 0;
            const bool isChar = charName && (strcmp(nm, charName) == 0 || (strncmp(nm, charName, cnLen) == 0 && (nm[cnLen] == ' ' || nm[cnLen] == '/')));   // "Name Main-Sub" (space) or a legacy "Name/..." profile
            const float ap = stagger(anim_, i); g_fade = e * ap;
            const float ry = py + i * (rowH + rGap) + (1.0f - ap) * snap(12.0f);
            const bool rowHov = inrect(mo, pX, ry, pW, rowH);
            const float rt = ease(320 + i, rowHov ? 1.0f : 0.0f);
            rpanel(dev, pX, ry, pW, rowH, snap(11.0f),
                   lerpc(active ? 0x55203A66 : 0x26141C28, active ? 0x55295082 : 0x33223A5C, rt),
                   lerpc(active ? 0x55172C4E : 0x260E141E, active ? 0x55203F6E : 0x331A2A44, rt),
                   active ? C_GOLD : lerpc(C_BORDER, C_BORDERHI, rt), snap(1.5f));
            rrect_fill(dev, pX + snap(4.0f), ry + snap(9.0f), snap(4.0f), rowH - snap(18.0f), snap(2.0f), lerpc(C_GOLD, C_GOLDHI, active ? 1.0f : pulse), lerpc(C_GOLD, C_GOLDHI, active ? 1.0f : pulse));
            fo->begin(dev); fo->draw_lc(dev, pX + snap(20.0f), ry + rowH * 0.5f, nm, snap(16.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            float bxa = pX + snap(20.0f) + fo->measure(nm, snap(16.0f)) + snap(12.0f);
            const float bcy = ry + rowH * 0.5f;
            if (active) bxa += badge(dev, fo, bxa, bcy, tr("ACTIVE", "ACTIF"),   0xFF5FD37E) + snap(6.0f);   // green
            if (isDef)  bxa += badge(dev, fo, bxa, bcy, tr("DEFAULT", "DÉFAUT"), C_GOLDHI)   + snap(6.0f);   // gold
            if (isChar) bxa += badge(dev, fo, bxa, bcy, charName,                0xFF74BCFF)  + snap(6.0f);   // blue
            const float lbw = snap(84.0f), dbw = snap(84.0f), bgap = snap(8.0f), bH = snap(32.0f);
            const float dX = pX + pW - dbw - snap(10.0f), lX = dX - bgap - lbw, bY = ry + (rowH - bH) * 0.5f;
            if (push_btn(dev, fo, mo, click, 760 + i * 2, lX, bY, lbw, bH, tr("Load", "Charger"), 0)) {
                profile_load(nm); record_char_profile(nm);
                strncpy(activeProf_, nm, sizeof(activeProf_) - 1); activeProf_[sizeof(activeProf_) - 1] = 0;
                nameBuf_[0] = 0; nameLen_ = 0; nameCur_ = 0; nameFocus_ = false;   // keep the create field free for a NEW profile
            }
            if (push_btn(dev, fo, mo, click, 761 + i * 2, dX, bY, dbw, bH, tr("Delete", "Supprimer"), 1)) {
                if (active) { activeProf_[0] = 0; nameBuf_[0] = 0; nameLen_ = 0; nameCur_ = 0; }
                profile_delete(nm); profDirty_ = true;
            }
        }
        g_fade = e;

        // CONFIRMATION : "Reset all settings" factory-resets the live config -> confirm first (profiles are kept).
        if (profResetConfirm_) {
            flat(dev, ix, bodyY, iw, bodyH, 0x99000000);                            // dim the profile body behind the dialog
            const float mw = snap(460.0f), mh = snap(184.0f);
            const float mx = ix + (iw - mw) * 0.5f, my = bodyY + (bodyH - mh) * 0.5f;
            shadow_down(dev, mx - snap(4.0f), my + mh, mw + snap(8.0f), snap(12.0f), 0x66000000);
            vg(dev, mx, my, mw, mh, 0xF0202B3C, 0xF0141C28);
            flat(dev, mx, my, mw, 1, 0x55FFFFFF);
            outline(dev, mx, my, mw, mh, C_BORDERHI);
            fo->begin(dev); fo->draw_c(dev, mx + mw * 0.5f, my + snap(34.0f), tr("Please confirm", "Confirmation"), snap(13.0f), C_GOLD, C_STROKE, 1.2f);
            fo->begin(dev); fo->draw_c(dev, mx + mw * 0.5f, my + snap(72.0f), tr("Reset ALL settings to factory defaults?", "Tout r\xC3\xA9initialiser aux valeurs d'usine ?"), snap(15.0f), C_TEXT, C_STROKE, 1.0f);
            fo->draw_c(dev, mx + mw * 0.5f, my + snap(96.0f), tr("Your saved profiles are kept.", "Tes profils sauvegard\xC3\xA9s sont conserv\xC3\xA9s."), snap(12.0f), C_MUTE, C_STROKE, 1.0f);
            const float pbw = snap(170.0f), pbh = snap(34.0f), pby = my + mh - pbh - snap(20.0f);
            const float clx = mx + snap(30.0f), crx = mx + mw - pbw - snap(30.0f);
            if (push_btn(dev, fo, mo, click, CTRL_ID, clx, pby, pbw, pbh, tr("Cancel", "Annuler"), 0)) profResetConfirm_ = false;
            if (push_btn(dev, fo, mo, click, CTRL_ID, crx, pby, pbw, pbh, tr("Reset all", "Tout r\xC3\xA9init."), 1)) { reset_ui_config(); profResetConfirm_ = false; profDirty_ = true; }
        }
}

// ---- Help tab (tab_ == 2 branch), lifted verbatim from draw() (byte-identical). ----
void ConfigPage::draw_help_tab(const Frame& f, u32 dev, Font* fo, const MouseState* mo, bool click,
                               float ix, float iw, float bodyY, float bodyH, float pageBot, float pulse) {
        // ===== HELP tab : a left menu of MODULES + the selected module's reference (scrollable) =====
        const float sbW = snap(220.0f);
        vg(dev, ix, bodyY, sbW, bodyH, C_SIDEBAR, 0xF0121A27);
        cs_add(dev); soft_blob(dev, ix + sbW * 0.5f, bodyY + snap(2.0f), sbW * 0.62f, bodyH * 0.16f, 0x0A2A4E84);   // faint top glow
        q4(dev, ix + sbW, bodyY, snap(22.0f), bodyH, 0x30000000, 0x00000000, 0x30000000, 0x00000000);              // recessed shadow on the content side
        flat(dev, ix + sbW, bodyY, 1, bodyH, C_BORDER);
        fo->begin(dev);
        fo->draw_lc(dev, ix + snap(20.0f), bodyY + snap(24.0f), "MODULES", snap(12.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.4f);
        for (int i = 0; i < HELP_MODULE_N; ++i) {
            const float rx = ix + snap(10.0f), rw = sbW - snap(20.0f);
            const float ry = bodyY + snap(44.0f) + i * snap(42.0f), rh = snap(36.0f);
            const bool active = (i == helpSel_), hover = inrect(mo, rx, ry, rw, rh);
            if (hover && click && i != helpSel_) { helpSel_ = i; helpScroll_ = 0.0f; }
            const float ht = ease(200 + i, (hover || active) ? 1.0f : 0.0f);
            if (active) vg(dev, rx, ry, rw, rh, C_ROWON_T, C_ROWON_B);
            else if (ht > 0.01f) flat(dev, rx, ry, rw, rh, (0x22FFFFFF & 0x00FFFFFF) | ((u32)(0x22 * ht) << 24));
            flat(dev, rx, ry, snap(3.0f), rh * (active ? 1.0f : ht), lerpc(C_GOLD, C_GOLDHI, pulse));
            fo->begin(dev);
            fo->draw_lc(dev, rx + snap(16.0f), ry + rh * 0.5f, tr(HELP_MODULES[i].en, HELP_MODULES[i].fr), snap(15.0f),
                        lerpc(C_DIM, C_TEXT, active ? 1.0f : ht), fa(C_STROKE), 1.0f);
        }

        // content : the selected module's help, scrollable
        if (helpSel_ < 0 || helpSel_ >= HELP_MODULE_N) helpSel_ = 0;
        const HelpModule& mod = HELP_MODULES[helpSel_];
        const float hx = ix + sbW + snap(30.0f);
        float hw = (ix + iw) - hx - snap(40.0f);
        // Prose is capped for READABILITY -- a 2000px line is miserable to read. Live SAMPLES are not prose :
        // they are boxes laid side by side, and inheriting the reading cap made them wrap while the panel still
        // had room to the right. Keep both widths : hw for text, hwWide for sample grids.
        const float hwWide = hw;
        const float hwMax = snap(1180.0f); if (hw > hwMax) hw = hwMax;   // cap line length for readability
        const float top = bodyY + snap(8.0f), bot = pageBot - snap(4.0f);
        if (ui_config().wheel != 0) {   // clamp against LAST frame's limit immediately -> no overscroll bounce at the bottom
            helpScroll_ -= (float)ui_config().wheel * snap(40.0f); ui_config().wheel = 0;
            if (helpScroll_ < 0.0f) helpScroll_ = 0.0f; if (helpScroll_ > helpMaxScroll_) helpScroll_ = helpMaxScroll_;
        }
        const float natTop = top + snap(16.0f), lh = snap(22.0f), bsz = snap(14.0f), hsz = snap(18.0f);
        float y = natTop - helpScroll_;
        for (int i = 0; i < mod.count; ++i) {
            const HelpItem& it = mod.items[i];
            const char* txt = (ui_config().lang == 1) ? it.fr : it.en;   // active-language text for this item
            if (it.kind == 0) {                       // heading + accent underline
                y += snap(16.0f);
                if (y >= top && y + lh + snap(3.0f) <= bot) {   // STRICT clip (incl. the underline) -> never spills onto the tabs / footer
                    fo->begin(dev); fo->draw_lc(dev, hx, y + lh * 0.5f, txt, hsz, fa(C_GOLD), fa(C_STROKE), 1.3f);
                    const float ulW = fo->measure(txt, hsz);   // underline spans the FULL title width
                    flat(dev, hx, y + lh + snap(1.0f), ulW, snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse));
                }
                y += lh + snap(8.0f);
            } else if (it.kind == 2) {                // bullet
                if (y >= top && y + lh <= bot) flat(dev, hx + snap(6.0f), y + lh * 0.5f - snap(2.0f), snap(4.0f), snap(4.0f), fa(C_ACCENT));
                y = draw_wrapped(dev, fo, hx + snap(20.0f), y, hw - snap(20.0f), top, bot, txt, bsz, C_DIM, lh);
                y += snap(3.0f);
            } else if (it.kind == 10) {               // LIVE sample : the distance readout sweeping 0.00 -> 30.00
                const float rh2 = snap(28.0f), bw = snap(96.0f);
                if (y >= top && y + rh2 <= bot) {
                    const float d = fmodf(f.t * 4.0f, 30.0f);
                    const u32 dc = (d < 10.0f) ? 0xFF5AA2FF : (d < 20.8f) ? 0xFFEFD24A : 0xFFFF6E6E;   // blue / yellow / red
                    vg(dev, hx, y, bw, rh2, 0xF01E2838, 0xF0141C24); outline(dev, hx, y, bw, rh2, C_BORDER);
                    char db[16]; sprintf(db, "%05.2f", d);
                    fo->begin(dev); fo->draw_c(dev, hx + bw * 0.5f, y + rh2 * 0.5f, db, snap(16.0f), fa(dc), fa(C_STROKE), 1.3f);
                    fo->draw_lc(dev, hx + bw + snap(14.0f), y + rh2 * 0.5f, txt, bsz, fa(C_DIM), fa(C_STROKE), 1.0f);
                }
                y += rh2 + snap(10.0f);
            } else if (it.kind == 11) {               // LIVE sample : the three leader / QM dots with their game terms
                const u32 dcol[3] = { 0xFFFFFFFF, 0xFFFFEF3F, 0xFF42D98A };
                const char* dlab[3] = { "Alliance Leader", "Party Leader", "Quartermaster" };
                const float rh2 = snap(22.0f);
                for (int k = 0; k < 3; ++k) {
                    if (y >= top && y + rh2 <= bot) {
                        cs(dev); disc(dev, hx + snap(9.0f), y + rh2 * 0.5f, snap(4.5f), dcol[k]);   // reset to the colour-quad state first (a prior paragraph left the font texture bound -> the dot came out black)
                        fo->begin(dev); fo->draw_lc(dev, hx + snap(24.0f), y + rh2 * 0.5f, dlab[k], bsz, fa(C_TEXT), fa(C_STROKE), 1.0f);
                    }
                    y += rh2;
                }
                y += snap(8.0f);
            } else if (it.kind == 12) {               // LIVE sample : the REAL HP gauge sweeping full -> empty (colour + critical blink)
                const float gh = snap(22.0f), gw = snap(210.0f), rh2 = gh + snap(12.0f);
                if (y >= top && y + rh2 <= bot) {
                    const float hp = 0.5f + 0.5f * sinf(f.t * 0.55f);   // 0..1
                    const u32 hc = (hp >= 0.5f) ? lerpc(0xFFEFD24A, 0xFF5ADC5A, (hp - 0.5f) / 0.5f) : lerpc(0xFFFF4646, 0xFFEFD24A, hp / 0.5f);
                    party_gauge(dev, hx + snap(4.0f), y + snap(5.0f), gw, gh, hp * 100.0f, hc, f.t, 0.0f, hp <= 0.25f ? 1.0f : 0.0f, 0, ui_config().gaugeStyle[0]);
                    fo->begin(dev); fo->draw_lc(dev, hx + gw + snap(24.0f), y + rh2 * 0.5f, txt, bsz, fa(C_DIM), fa(C_STROKE), 1.0f);
                }
                y += rh2 + snap(8.0f);
            } else if (it.kind == 13) {               // LIVE sample : the REAL MP (blue) and TP (glows past 1000) gauges
                const float gh = snap(22.0f), gw = snap(130.0f), gap = snap(16.0f), rh2 = gh + snap(12.0f);
                if (y >= top && y + rh2 <= bot) {
                    const float mp = 0.45f + 0.35f * sinf(f.t * 0.4f + 1.0f);
                    party_gauge(dev, hx + snap(4.0f), y + snap(5.0f), gw, gh, mp * 100.0f, 0xFF4F9DFF, f.t, 0.0f, 0.0f, 1, ui_config().gaugeStyle[0]);
                    const float tpf = 0.5f + 0.5f * sinf(f.t * 0.5f);   // 0..1 = 0..3000 TP
                    const bool ready = tpf >= (1000.0f / 3000.0f);
                    party_gauge(dev, hx + snap(4.0f) + gw + gap, y + snap(5.0f), gw, gh, tpf * 100.0f, ready ? 0xFFFF7AE8 : 0xFF7A5C8E, f.t, ready ? 1.0f : 0.0f, 0.0f, 2, ui_config().gaugeStyle[0]);
                    fo->begin(dev); fo->draw_lc(dev, hx + snap(4.0f) + 2 * gw + gap + snap(22.0f), y + rh2 * 0.5f, txt, bsz, fa(C_DIM), fa(C_STROKE), 1.0f);
                }
                y += rh2 + snap(8.0f);
            } else if (it.kind == 14) {               // LIVE sample : the REAL selection hand (bobbing) + its highlight bar with a glass sweep, gold (main) / blue (sub) / red (locked)
                const float rh2 = snap(44.0f), hand = snap(44.0f), bx2 = hx + hand + snap(8.0f), bw = snap(200.0f);
                for (int t2 = 0; t2 < 3; ++t2) {
                    if (y >= top && y + rh2 <= bot) {
                        // the EXACT party selection frame (gold glass + sweep + rims for main, ocean-blue for sub, red when locked)
                        party_selframe(dev, bx2, y, bw, rh2, f.t, 1.0f, t2);
                        const float bob = party_cursor_bob(f.t, hand);                       // same horizontal bob as the in-game cursor
                        party_cursor(dev, helpCursorTex_, hx + hand * 0.5f + bob, y + rh2 * 0.5f, hand, t2);
                        const char* cl = (t2 == 0) ? (ui_config().lang == 1 ? "Cible principale, curseur blanc" : "Main target, white hand")
                                       : (t2 == 1) ? (ui_config().lang == 1 ? "Sous-cible, curseur bleu"      : "Sub-target, blue hand")
                                                   : (ui_config().lang == 1 ? "Cible verrouillée, curseur rouge" : "Locked target, red hand");
                        fo->begin(dev); fo->draw_lc(dev, bx2 + bw + snap(14.0f), y + rh2 * 0.5f, cl, bsz, fa(C_DIM), fa(C_STROKE), 1.0f);
                    }
                    y += rh2 + snap(4.0f);
                }
                y += snap(6.0f);
            } else if (it.kind == 20) {               // LIVE sample : the Target HP bar with the "white damage" trail
                const float gh = snap(22.0f), gw = snap(210.0f), rh2 = gh + snap(12.0f);
                if (y >= top && y + rh2 <= bot) {
                    target_help_hpbar(dev, hx + snap(4.0f), y + snap(5.0f), gw, gh, f.t);
                    fo->begin(dev); fo->draw_lc(dev, hx + gw + snap(24.0f), y + rh2 * 0.5f, txt, bsz, fa(C_DIM), fa(C_STROKE), 1.0f);
                }
                y += rh2 + snap(8.0f);
            } else if (it.kind == 21 || it.kind == 22) {   // LIVE sample : the range gauge vs a MONSTER (21) / a PLAYER-NPC (22)
                const float gh = snap(22.0f), gw = snap(250.0f), rh2 = gh + snap(34.0f);   // extra height : the staggered zone labels sit below
                if (y >= top && y + rh2 <= bot) {
                    target_help_range(dev, fo, hx + snap(4.0f), y + snap(5.0f), gw, gh, it.kind == 21, f.t);
                    fo->begin(dev); fo->draw_lc(dev, hx + gw + snap(24.0f), y + snap(5.0f) + gh * 0.5f, txt, bsz, fa(C_DIM), fa(C_STROKE), 1.0f);
                }
                y += rh2 + snap(8.0f);
            } else if (it.kind == 23) {               // LIVE sample : the REAL speed readout (Speed icon + NN%) -- green on a player, red on a monster
                const float rh2 = snap(26.0f), gap = snap(96.0f);
                if (!tgtTexTried_) { target_help_textures(dev, tgtBuffTex_, tgtThTex_); tgtTexTried_ = true; }
                if (y >= top && y + rh2 <= bot) {
                    const int v = 6 + (int)(9.0f * (0.5f + 0.5f * sinf(f.t * 0.9f)));   // +6..+15
                    const float cy = y + rh2 * 0.5f;
                    target_help_speed(dev, fo, tgtBuffTex_, hx + snap(4.0f),       cy, v, true,  true);   // player : green = good
                    target_help_speed(dev, fo, tgtBuffTex_, hx + snap(4.0f) + gap, cy, v, false, true);   // monster : red = dangerous
                    fo->begin(dev); fo->draw_lc(dev, hx + snap(4.0f) + 2.0f * gap, cy, txt, bsz, fa(C_DIM), fa(C_STROKE), 1.0f);
                }
                y += rh2 + snap(10.0f);
            } else if (it.kind == 24) {               // LIVE sample : the Treasure Hunter coffer + tier
                const float sz = snap(30.0f), rh2 = sz + snap(6.0f);
                if (!tgtTexTried_) { target_help_textures(dev, tgtBuffTex_, tgtThTex_); tgtTexTried_ = true; }
                if (y >= top && y + rh2 <= bot) {
                    target_help_th(dev, fo, tgtThTex_, hx + snap(4.0f), y, sz, f.t);
                    fo->begin(dev); fo->draw_lc(dev, hx + sz + snap(46.0f), y + sz * 0.5f, txt, bsz, fa(C_DIM), fa(C_STROKE), 1.0f);
                }
                y += rh2 + snap(8.0f);
            } else if (it.kind == 25) {               // LIVE sample : a row of real debuff icons + live timers
                const float cell = snap(28.0f), gap = snap(8.0f); const int nd = 5;
                const float rh2 = cell + snap(24.0f), rowW = nd * (cell + gap);
                if (!tgtTexTried_) { target_help_textures(dev, tgtBuffTex_, tgtThTex_); tgtTexTried_ = true; }
                if (y >= top && y + rh2 <= bot) {
                    target_help_debuffs(dev, fo, tgtBuffTex_, hx + snap(4.0f), y, cell, nd, f.t);
                    fo->begin(dev); fo->draw_lc(dev, hx + rowW + snap(20.0f), y + cell * 0.5f, txt, bsz, fa(C_DIM), fa(C_STROKE), 1.0f);
                }
                y += rh2 + snap(8.0f);
            } else if (it.kind == 30) {               // LIVE sample : the minimap in YOUR shape/frame/zoom -- the REAL zone map + player pin + real markers
                const float r = snap(110.0f), rh2 = 2.0f * r + snap(10.0f);
                if (!mmTried_) { minimap_help_textures(dev, mmMkPlayer_, mmMkMob_, mmElem_); mmTried_ = true; }
                // Own our copy of the live zone map (loaded straight from the ROM DAT, exactly like the widget) so the
                // radar sample shows the REAL map even when the floating Minimap is hidden / hasn't drawn this frame.
                // Reload only when the zone's map file-id changes ; remember it even on failure (no per-frame retry).
                if (f.game && f.game->map.valid && f.game->map.fileId != 0 &&
                    (f.game->map.fileId != mmMapFileId_ || mmMapTex_ == 0)) {   // (re)load ; retry every frame until it sticks
                    if (mmMapTex_) { release_texture(mmMapTex_); mmMapTex_ = 0; }
                    u32* mpx = 0; int mw = 0, mh = 0;
                    if (load_zone_map(f.game->map.fileId, mpx, mw, mh)) {
                        mmMapTex_ = make_texture_argb_mip(dev, mw, mh, mpx); free_map_image(mpx);
                        mmMapFileId_ = f.game->map.fileId;                        // remember only on SUCCESS -> a transient failure retries
                    }
                }
                if (y >= top && y + rh2 <= bot) {
                    cs(dev);   // reset to the colour-quad state first (a prior paragraph left the font texture bound -> the lens would draw black)
                    minimap_help_disc(dev, f, fo, mmMkPlayer_, mmMkMob_, mmMapTex_, hx + snap(4.0f) + r, y + snap(4.0f) + r, r, f.t);
                    cs(dev);
                    fo->begin(dev); fo->draw_lc(dev, hx + snap(4.0f) + 2.0f * r + snap(24.0f), y + rh2 * 0.5f, txt, bsz, fa(C_DIM), fa(C_STROKE), 1.0f);
                }
                y += rh2 + snap(8.0f);
            } else if (it.kind == 31) {               // LIVE sample : the marker legend (real dot / arrow / pin + its meaning), one row each
                const float rowH = snap(24.0f);
                if (!mmTried_) { minimap_help_textures(dev, mmMkPlayer_, mmMkMob_, mmElem_); mmTried_ = true; }
                cs(dev);
                y = minimap_help_legend(dev, fo, mmMkPlayer_, mmMkMob_, hx + snap(6.0f), y, rowH, top, bot, ui_config().lang);
                y += snap(8.0f);
            } else if (it.kind == 32) {               // LIVE sample : the moon, swept New -> Full -> New, with its live phase name
                const float r = snap(15.0f), rh2 = 2.0f * r + snap(8.0f);
                if (y >= top && y + rh2 <= bot) {
                    cs(dev);
                    const char* ph = minimap_help_moon(dev, mmMoonTex_, mmMoonKey_, hx + snap(4.0f) + r, y + snap(4.0f) + r, r, f.t, ui_config().lang);
                    cs(dev);
                    fo->begin(dev);
                    const float lx = hx + snap(4.0f) + 2.0f * r + snap(20.0f), lcy = y + rh2 * 0.5f;
                    fo->draw_lc(dev, lx, lcy, ph, bsz, fa(C_GOLD), fa(C_STROKE), 1.0f);
                    fo->draw_lc(dev, lx + fo->measure(ph, bsz) + snap(10.0f), lcy, txt, bsz, fa(C_DIM), fa(C_STROKE), 1.0f);
                }
                y += rh2 + snap(8.0f);
            } else if (it.kind == 33) {               // LIVE sample : the eight elemental-day icons, the active one cycling + its name
                const float rh2 = snap(30.0f), rowW = snap(250.0f);
                if (!mmTried_) { minimap_help_textures(dev, mmMkPlayer_, mmMkMob_, mmElem_); mmTried_ = true; }
                if (y >= top && y + rh2 <= bot) {
                    cs(dev);
                    const char* dn = minimap_help_day(dev, fo, mmElem_, hx + snap(4.0f), y + rh2 * 0.5f, f.t, ui_config().lang);
                    cs(dev);
                    fo->begin(dev);
                    const float lx = hx + snap(4.0f) + rowW + snap(16.0f), lcy = y + rh2 * 0.5f;
                    fo->draw_lc(dev, lx, lcy, dn, bsz, fa(C_GOLD), fa(C_STROKE), 1.0f);
                    fo->draw_lc(dev, lx + fo->measure(dn, bsz) + snap(10.0f), lcy, txt, bsz, fa(C_DIM), fa(C_STROKE), 1.0f);
                }
                y += rh2 + snap(8.0f);
            } else if (it.kind == 40) {               // LIVE sample : the Hate List in YOUR config -- the REAL box in preview mode (a few fiole rows)
                const float rh2 = snap(118.0f);
                if (y >= top && y + rh2 <= bot) {
                    cs(dev);
                    hatelist_help_box(f, hx + hw * 0.5f, y + rh2 * 0.5f, 1.0f);
                    cs(dev);
                }
                y += rh2 + snap(8.0f);
            } else if (it.kind == 41) {               // LIVE sample : the PointWatch box in YOUR config -- as BIG as fits the help width
                float s = 1.4f, bh = 0.0f;
                pointwatch_help_fit(f, hw * 0.9f, 1.7f, s, bh);   // moderate : compact sample, capped so it doesn't fill the width   // largest scale that fits (capped) -> a prominent sample
                const float rh2 = bh + snap(14.0f);
                if (y >= top && y + rh2 <= bot) {
                    cs(dev);
                    pointwatch_help_box(f, hx + hw * 0.5f, y + rh2 * 0.5f, s);
                    cs(dev);
                }
                y += rh2 + snap(8.0f);
            } else if (it.kind == 42) {               // LIVE sample : the Timers box in YOUR config -- as BIG as fits the help width
                float s = 1.4f, bh = 0.0f;
                timers_help_fit(f, hw * 0.9f, 1.4f, s, bh);
                const float rh2 = bh + snap(14.0f);
                if (y >= top && y + rh2 <= bot) {
                    cs(dev);
                    timers_help_box(f, hx + hw * 0.5f, y + rh2 * 0.5f, s);
                    cs(dev);
                }
                y += rh2 + snap(8.0f);
            } else if (it.kind == 43) {               // LIVE sample : the Skillchains box in YOUR config -- as BIG as fits the help width
                float s = 1.4f, bh = 0.0f;
                skillchains_help_fit(f, hw * 0.9f, 1.4f, s, bh);
                const float rh2 = bh + snap(14.0f);
                if (y >= top && y + rh2 <= bot) {
                    cs(dev);
                    skillchains_help_box(f, hx + hw * 0.5f, y + rh2 * 0.5f, s);
                    cs(dev);
                }
                y += rh2 + snap(8.0f);
            } else if (it.kind == 44) {               // LIVE sample : the Treasure Pool box in YOUR config -- as BIG as fits the help width
                float s = 1.4f, bh = 0.0f;
                treasure_help_fit(f, hw * 0.9f, 1.4f, s, bh);
                const float rh2 = bh + snap(14.0f);
                if (y >= top && y + rh2 <= bot) {
                    cs(dev);
                    treasure_help_box(f, hx + hw * 0.5f, y + rh2 * 0.5f, s);
                    cs(dev);
                }
                y += rh2 + snap(8.0f);
            } else if (it.kind == 45) {               // LIVE sample : ONE small example of EACH content variant, side by side (each box carries its own title header)
                static const int ZV[6] = { 0, 1, 2, 3, 4, 5 };   // Dynamis / Abyssea / Omen / Nyzul / Odyssey / Limbus
                const float sc = 1.05f, colGap = snap(16.0f);   // small : several fit per row (scale factor, not a pixel value)
                float gx = hx, rowTop = y, rowMaxH = 0.0f;
                const float gw = hwWide;   // sample grid : the panel's real width, not the prose cap
                for (int zi = 0; zi < 6; ++zi) {
                    float bw1 = 0.0f, bh1 = 0.0f; zonetracker_help_measure(f, ZV[zi], bw1, bh1);
                    const float bw = bw1 * sc, bh = bh1 * sc;
                    const bool wrapped = (gx > hx && gx + bw > hx + gw);
                    if (wrapped) { rowTop += rowMaxH + snap(14.0f); gx = hx; rowMaxH = 0.0f; }   // wrap
                    if (rowTop >= top && rowTop + bh <= bot) { cs(dev); zonetracker_help_box(f, gx + bw * 0.5f, rowTop + bh * 0.5f, sc, ZV[zi]); cs(dev); }
                    gx += bw + colGap;
                    if (bh > rowMaxH) rowMaxH = bh;
                }
                y = rowTop + rowMaxH + snap(10.0f);
            } else if (it.kind == 46) {               // LIVE sample : the WHOLE Player Hub (identity / vitals / buffs / gil / speed / equipment) -- the Hud draws the demo box in this slot (it owns the widget)
                const float rh2 = snap(300.0f);
                if (y >= top && y + rh2 <= bot) {
                    helpPlayer_ = true;
                    helpPlayerCx_ = hx + hw * 0.5f; helpPlayerCy_ = y + rh2 * 0.5f; helpPlayerW_ = hw - snap(16.0f); helpPlayerH_ = rh2 - snap(16.0f);
                }
                y += rh2 + snap(8.0f);
            } else if (it.kind == 47) {               // LIVE sample : the EmpyPop stack. Its chloris chain is NARROW + TALL,
                float s = 1.0f, bh = 0.0f;            // so a width-only fit over-scales the height -> also clamp to the height left.
                empypop_help_fit(f, hw * 0.7f, 1.0f, s, bh);
                const float maxH = (bot - y) - snap(28.0f);
                if (bh > maxH && bh > 1.0f) { s *= maxH / bh; bh = maxH; }   // shrink to fit the remaining panel height
                if (s < 0.5f) s = 0.5f;
                const float rh2 = bh + snap(14.0f);
                if (y >= top && y + rh2 <= bot) {
                    cs(dev);
                    empypop_help_box(f, hx + hw * 0.5f, y + rh2 * 0.5f, s);
                    cs(dev);
                }
                y += rh2 + snap(8.0f);
            } else {                                  // paragraph
                y = draw_wrapped(dev, fo, hx, y, hw, top, bot, txt, bsz, C_DIM, lh);
                y += snap(10.0f);
            }
        }
        // scroll clamp (next frame) + a thin scrollbar on the right
        const float viewH = bot - natTop, contentH = (y + helpScroll_) - natTop;
        float maxScroll = contentH - viewH; if (maxScroll < 0.0f) maxScroll = 0.0f;
        helpMaxScroll_ = maxScroll;   // remember for next frame's wheel clamp (kills the overscroll bounce)
        if (helpScroll_ < 0.0f) helpScroll_ = 0.0f; if (helpScroll_ > maxScroll) helpScroll_ = maxScroll;
        scrollbar_vert(dev, mo, 2 /*Help*/, ix + iw - snap(8.0f), natTop, snap(6.0f), viewH, contentH, helpScroll_, maxScroll);
}

#include "ui/config_changelog.h"   // CL_<ver> groups + RELEASES[] (data only -- see the header)

// ---- Update tab (tab_ == 4) : version + release check + one-click update (top), then a "What's new" changelog
//      (below, scrollable when the release is big). ----
void ConfigPage::draw_update_tab(const Frame& f, u32 dev, Font* fo, const MouseState* mo, bool click,
                                 float ix, float iw, float bodyY, float bodyH, float pageBot, float pulse, float e) {
    (void)bodyH; (void)pulse; (void)e;
    char ver[64] = { 0 };
    const int  st  = aio_update_check_status(ver, sizeof(ver));   // 0 checking, 1 up-to-date, 2 available, 3 error
    const char* cur = aio_version_string();

    // update card, anchored at the TOP (the changelog fills the space below it)
    const float cw = snap(560.0f), ch = snap(250.0f);
    const float cx = ix + (iw - cw) * 0.5f, cy = bodyY + snap(6.0f);
    rrect_fill(dev, cx, cy, cw, ch, snap(14.0f), 0xE0121A27, 0xF00A0F18);
    cs_add(dev); soft_blob(dev, cx + cw * 0.5f, cy + snap(4.0f), cw * 0.5f, ch * 0.14f, 0x142A4E84); cs(dev);
    outline(dev, cx, cy, cw, ch, C_BORDERHI);
    const float midX = cx + cw * 0.5f;

    fo->begin(dev);
    fo->draw_c(dev, midX, cy + snap(46.0f), tr("AioHud Update", "Mise \xC3\xA0 jour AioHud"), snap(24.0f), fa(C_GOLDHI), fa(C_STROKE), 1.4f);
    char inst[96]; _snprintf(inst, sizeof(inst), "%s : V%s", tr("Installed", "Install\xC3\xA9""e"), cur); inst[sizeof(inst) - 1] = 0;
    fo->begin(dev); fo->draw_c(dev, midX, cy + snap(98.0f), inst, snap(16.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);

    // ---- an update requested from this tab is IN FLIGHT (or just finished) -> show live progress instead of the
    //      check status, so the user knows something is happening before the addon //unloads + //reloads the HUD. ----
    char pmsg[96] = { 0 };
    const int prog = aio_update_progress(pmsg, sizeof(pmsg));   // 0 idle, 1 working, 2 up-to-date, 3 updated, 4 error
    if (prog != 0) {
        char line[128]; u32 pcol; const char* pbtn = 0; int pbtnKind = 0;
        if (prog == 1) {   // working : animated label + indeterminate bar, NO button (can't re-trigger mid-update)
            int nd = ((int)(f.t * 2.0f)) % 4; char dots[4] = { 0 }; for (int i = 0; i < nd; ++i) dots[i] = '.';
            _snprintf(line, sizeof(line), "%s%s", tr("Updating", "Mise \xC3\xA0 jour"), dots); pcol = C_GOLDHI;
        } else if (prog == 2) { _snprintf(line, sizeof(line), "%s", tr("Already up to date", "D\xC3\xA9j\xC3\xA0 \xC3\xA0 jour")); pcol = 0xFF6BE06Bu; pbtn = tr("Check again", "Rev\xC3\xA9rifier"); }
        else if (prog == 3)   { _snprintf(line, sizeof(line), "%s v%s", tr("Updated", "Mis \xC3\xA0 jour"), pmsg);             pcol = 0xFF6BE06Bu; pbtn = tr("Done", "OK"); }
        else                  { _snprintf(line, sizeof(line), "%s", tr("Update failed", "\xC3\x89""chec de la mise \xC3\xA0 jour")); pcol = 0xFFF06060u; pbtn = tr("Retry", "R\xC3\xA9""essayer"); pbtnKind = 1; }
        line[sizeof(line) - 1] = 0;
        fo->begin(dev); fo->draw_c(dev, midX, cy + snap(146.0f), line, snap(18.0f), fa(pcol), fa(C_STROKE), 1.1f);
        if (prog == 4 && pmsg[0]) { fo->begin(dev); fo->draw_c(dev, midX, cy + snap(170.0f), pmsg, snap(12.0f), fa(C_DIM), fa(C_STROKE), 1.0f); }

        if (prog == 1) {
            // indeterminate progress bar : a track + a highlight segment sliding across, looped on f.t.
            const float trackW = snap(360.0f), trackH = snap(10.0f);
            const float tx = midX - trackW * 0.5f, ty = cy + snap(192.0f);
            rrect_fill(dev, tx, ty, trackW, trackH, trackH * 0.5f, 0xFF0E1622, 0xFF0A0F18);
            const float segW = trackW * 0.30f;
            float ph = f.t * 0.7f; ph -= (float)(int)ph;                 // 0..1 loop
            const float sx = tx - segW + ph * (trackW + segW);
            const float x0 = sx < tx ? tx : sx, x1 = (sx + segW) > (tx + trackW) ? (tx + trackW) : (sx + segW);
            if (x1 > x0) rrect_fill(dev, x0, ty, x1 - x0, trackH, trackH * 0.5f, 0xFFF2C24Bu, 0xFFCE9A2Cu);
            fo->begin(dev); fo->draw_c(dev, midX, ty + snap(34.0f),
                tr("The HUD will reload in a moment -- your settings are kept.",
                   "L'interface va se recharger dans un instant -- tes r\xC3\xA9glages sont gard\xC3\xA9s."),
                snap(12.0f), fa(C_DIM), fa(C_STROKE), 1.0f);
        } else if (pbtn) {
            const float bw = snap(230.0f), bh = snap(46.0f), bx = midX - bw * 0.5f, by = cy + snap(196.0f);
            if (push_btn(dev, fo, mo, click, CTRL_ID, bx, by, bw, bh, pbtn, pbtnKind)) {
                if (prog == 4) aio_update_request();          // retry the full update
                else { aio_update_clear(); aio_update_spawn_check(); }   // back to the normal check flow
            }
        }
        return;   // progress UI replaces the check status + button for this frame
    }

    char status[96]; u32 col; const char* btn; bool doUpdate = false;
    if (st == 2)      { col = C_GOLDHI;    _snprintf(status, sizeof(status), "%s : v%s", tr("Update available", "Mise \xC3\xA0 jour dispo"), ver); btn = tr("Update now", "Mettre \xC3\xA0 jour"); doUpdate = true; }
    else if (st == 1) { col = 0xFF6BE06Bu; _snprintf(status, sizeof(status), "%s", tr("Up to date", "\xC3\x80 jour"));                              btn = tr("Check again", "Rev\xC3\xA9rifier"); }
    else if (st == 3) { col = 0xFFF06060u; _snprintf(status, sizeof(status), "%s", tr("Check failed (network?)", "\xC3\x89""chec (r\xC3\xA9seau ?)")); btn = tr("Retry", "R\xC3\xA9""essayer"); }
    else              { col = C_DIM;       _snprintf(status, sizeof(status), "%s", tr("Checking...", "V\xC3\xA9rification..."));                    btn = tr("Check now", "V\xC3\xA9rifier"); }
    status[sizeof(status) - 1] = 0;
    fo->begin(dev); fo->draw_c(dev, midX, cy + snap(146.0f), status, snap(18.0f), fa(col), fa(C_STROKE), 1.1f);

    const float bw = snap(230.0f), bh = snap(46.0f), bx = midX - bw * 0.5f, by = cy + snap(188.0f);
    if (push_btn(dev, fo, mo, click, CTRL_ID, bx, by, bw, bh, btn, doUpdate ? 1 : 0)) {
        if (doUpdate) aio_update_request();    // -> the AioUpdate addon runs the full no-window update
        else          aio_update_spawn_check();// re-run the check
    }

    // ---- "What's new" changelog, below the card : what THIS version fixes / adds, in plain language. Scrolls
    //      when the release is big (D3D8 stencil clip + wheel, same pattern as the Configuration tab). ----
    const float clX = cx, clW = cw;
    const float clTop = cy + ch + snap(14.0f);
    char wn[80]; _snprintf(wn, sizeof(wn), "%s V%s", tr("What's new in", "Nouveaut\xC3\xA9s de la"), cur); wn[sizeof(wn) - 1] = 0;
    fo->begin(dev); fo->draw_lc(dev, clX + snap(2.0f), clTop + snap(11.0f), wn, snap(15.0f), fa(C_GOLDHI), fa(C_STROKE), 1.2f);
    const float listTop = clTop + snap(30.0f), listBot = pageBot - snap(6.0f);
    if (listBot > listTop + snap(20.0f)) {                                   // only if there's vertical room below
        if (ui_config().wheel != 0 && mo && mo->x >= clX && mo->x <= clX + clW && mo->y >= listTop && mo->y <= listBot) {
            updScroll_ -= (float)ui_config().wheel * snap(48.0f);
            if (updScroll_ < 0.0f) updScroll_ = 0.0f;
            if (updScroll_ > updMaxScroll_) updScroll_ = updMaxScroll_;      // clamp vs last frame's extent -> no overscroll
            ui_config().wheel = 0;
        }
        clip_rect_begin(dev, clX - snap(2.0f), listTop, clW + snap(12.0f), listBot - listTop);
        const float lh = snap(17.0f), tsz = snap(13.0f);
        const float hdrH = snap(30.0f), hdrGap = snap(4.0f);
        float y = listTop - updScroll_;
        // grouped changelog : a clickable version header per release (newest first) ; its change lines show when open.
        // relOpen_ sized >= RELEASES_N in config_page.h -- keep them in sync.
        for (int r = 0; r < RELEASES_N && r < (int)(sizeof(relOpen_) / sizeof(relOpen_[0])); ++r) {
            char vbuf[24]; _snprintf(vbuf, sizeof(vbuf), "v%s", RELEASES[r].version); vbuf[sizeof(vbuf) - 1] = 0;
            // cat_header isn't line-clipped like the entries (draw_wrapped) and the stencil viewport clip can be a
            // no-op on a stencil-less backbuffer -> draw the version header ONLY when it's FULLY inside the viewport,
            // else a scrolled-up header spilled over the update card above (and the footer below).
            const bool onScreen = (snap(y) >= listTop) && (snap(y) + hdrH <= listBot);
            if (onScreen && cat_header(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, r), clX + snap(2.0f), snap(y), clW - snap(4.0f), vbuf, relOpen_[r]))
                relOpen_[r] = !relOpen_[r];
            y += hdrH + hdrGap;
            if (relOpen_[r]) {
                for (int i = 0; i < RELEASES[r].n; ++i) {
                    cs(dev); rrect_fill(dev, clX + snap(9.0f), snap(y + lh * 0.5f - 2.0f), snap(4.0f), snap(4.0f), snap(2.0f), fa(C_GOLDHI), fa(C_GOLDHI));   // gold bullet
                    const char* txt = (ui_config().lang == 1) ? RELEASES[r].lines[i].fr : RELEASES[r].lines[i].en;
                    y = draw_wrapped(dev, fo, clX + snap(22.0f), y, clW - snap(30.0f), listTop, listBot, txt, tsz, C_TEXT, lh);
                    y += snap(7.0f);                                         // gap between entries
                }
                y += snap(6.0f);                                            // extra gap after an open release
            }
        }
        clip_rect_end(dev);
        // this frame's content extent -> the wheel clamp + a thin scrollbar when it overflows
        const float viewH = listBot - listTop, contentH = (y + updScroll_) - listTop + snap(6.0f);
        float maxS = contentH - viewH; if (maxS < 0.0f) maxS = 0.0f;
        updMaxScroll_ = maxS;
        if (updScroll_ > maxS) updScroll_ = maxS;
        scrollbar_vert(dev, mo, 3 /*Update changelog*/, clX + clW + snap(4.0f), listTop, snap(6.0f), viewH, contentH, updScroll_, maxS);
    }
}

// ---- Debug tab (tab_ == 5) : the known-bugs / planned-work tracker, in-game. Collapsible sections, scrollable,
//      same stencil-clip + wheel pattern as the Update changelog. Data in config_debug.h (mirrors Debug.txt). ----
#include "ui/config_debug.h"   // DEBUG_SECTIONS[] (data only)

void ConfigPage::draw_debug_tab(const Frame& f, u32 dev, Font* fo, const MouseState* mo, bool click,
                                float ix, float iw, float bodyY, float bodyH, float pageBot) {
    (void)bodyH;
    const float clW = snap(560.0f), clX = ix + (iw - clW) * 0.5f;
    const float titleY = bodyY + snap(12.0f);
    fo->begin(dev);
    fo->draw_c(dev, clX + clW * 0.5f, titleY + snap(6.0f), tr("Known bugs & planned work", "Bugs connus & \xC3\xA0 faire"), snap(22.0f), fa(C_GOLDHI), fa(C_STROKE), 1.4f);
    fo->begin(dev);
    fo->draw_c(dev, clX + clW * 0.5f, titleY + snap(30.0f), tr("Fixed items are listed per version in the Update tab.", "Les \xC3\xA9l\xC3\xA9ments corrig\xC3\xA9s sont list\xC3\xA9s par version dans l'onglet Mise \xC3\xA0 jour."), snap(12.0f), fa(C_DIM), fa(C_STROKE), 1.0f);

    const float listTop = titleY + snap(48.0f), listBot = pageBot - snap(6.0f);
    if (listBot <= listTop + snap(20.0f)) return;
    if (ui_config().wheel != 0 && mo && mo->x >= clX && mo->x <= clX + clW && mo->y >= listTop && mo->y <= listBot) {
        dbgScroll_ -= (float)ui_config().wheel * snap(48.0f);
        if (dbgScroll_ < 0.0f) dbgScroll_ = 0.0f;
        if (dbgScroll_ > dbgMaxScroll_) dbgScroll_ = dbgMaxScroll_;
        ui_config().wheel = 0;
    }
    clip_rect_begin(dev, clX - snap(2.0f), listTop, clW + snap(12.0f), listBot - listTop);
    const float lh = snap(17.0f), tsz = snap(13.0f), hdrH = snap(30.0f), hdrGap = snap(4.0f);
    float y = listTop - dbgScroll_;
    for (int r = 0; r < DEBUG_SECTIONS_N && r < (int)(sizeof(dbgOpen_) / sizeof(dbgOpen_[0])); ++r) {
        const char* title = (ui_config().lang == 1) ? DEBUG_SECTIONS[r].titleFr : DEBUG_SECTIONS[r].titleEn;
        const bool onScreen = (snap(y) >= listTop) && (snap(y) + hdrH <= listBot);
        if (onScreen && cat_header(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, r), clX + snap(2.0f), snap(y), clW - snap(4.0f), title, dbgOpen_[r]))
            dbgOpen_[r] = !dbgOpen_[r];
        y += hdrH + hdrGap;
        if (dbgOpen_[r]) {
            for (int i = 0; i < DEBUG_SECTIONS[r].n; ++i) {
                cs(dev); rrect_fill(dev, clX + snap(9.0f), snap(y + lh * 0.5f - 2.0f), snap(4.0f), snap(4.0f), snap(2.0f), fa(C_GOLDHI), fa(C_GOLDHI));   // gold bullet
                const char* txt = (ui_config().lang == 1) ? DEBUG_SECTIONS[r].lines[i].fr : DEBUG_SECTIONS[r].lines[i].en;
                y = draw_wrapped(dev, fo, clX + snap(22.0f), y, clW - snap(30.0f), listTop, listBot, txt, tsz, C_TEXT, lh);
                y += snap(7.0f);
            }
            y += snap(6.0f);
        }
    }
    clip_rect_end(dev);
    const float viewH = listBot - listTop, contentH = (y + dbgScroll_) - listTop + snap(6.0f);
    float maxS = contentH - viewH; if (maxS < 0.0f) maxS = 0.0f;
    dbgMaxScroll_ = maxS;
    if (dbgScroll_ > maxS) dbgScroll_ = maxS;
    scrollbar_vert(dev, mo, 4 /*Debug*/, clX + clW + snap(4.0f), listTop, snap(6.0f), viewH, contentH, dbgScroll_, maxS);
    (void)f;
}

} // namespace aio
