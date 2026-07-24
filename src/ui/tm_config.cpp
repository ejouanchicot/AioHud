// tm_config.cpp -- the Timers module's settings panel (//aio config -> "Timers").
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids.
// Reuses catOpen_ [6] (Display) / [5] (Text) -- fine, module sections are mutually exclusive.
#include "ui/config_page.h"
#include "ui/config_controls.h"
#include "ui/config_rows.h"
#include "model/ui_config.h"
#include "model/party_state.h"       // self_main_job : default the track checklist to the current job
#include "model/game_mem.h"          // read_player : current main/sub job + levels (level-gate the checklist)
#include "model/job_track_gen.h"     // per-job trackable buff/recast entries + categories
#include "model/paths.h"             // plugin_path : locate the job-icon atlas
#include "gfx/texture.h"             // load_raw_texture : the job-icon atlas
#include <cstring>
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

void timers_reset();   // hud_timers.cpp : flush live buff/recast timers + focus alerts (also //aio timers reset)

void ConfigPage::draw_tm_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                float& ry, int& ri, float e,
                                float bandX, float bandW, float coX, float ctrlW,
                                float hdrX, float hdrW) {
    UiConfig& c = ui_config();
    const char* MODE[3] = { tr("Icon", "Ic\xC3\xB4ne"), tr("Name", "Nom"), tr("Both", "Les deux") };
    const char* SRC[4]  = { tr("Mine only", "Moi seul"), tr("Mine + players", "Moi + joueurs"), tr("Mine + trusts", "Moi + trusts"), tr("All", "Tout") };   // Duration buff-source filter (tmBuffSrc)

    // ===== sub-section : DISPLAY =====
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6])) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (catOpen_[6]) {
        #define TM_TOGGLE(UID, LABEL, FIELD) { ROW_BAND(48.0f) row_toggle(dev, fo, mo, click, UID, coX, ry + yo, ctrlW, LABEL, &(FIELD)); } ROW_NEXT(48.0f)
        TM_TOGGLE(CTRL_ID, tr("Show", "Afficher"), c.tmShow)
        { ROW_BAND(46.0f)   // Size (canonical : right after Show, before the box appearance)
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.tmScale * 100.0f + 0.5f));
            float v01 = (c.tmScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.tmScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.tmBox);   // Box / Transparency / Theme / Hue / Luminosity
        TM_TOGGLE(CTRL_ID, tr("Show titles", "Afficher les titres"), c.tmTitle)
        #undef TM_TOGGLE
        { ROW_BAND(48.0f)   // Layout : fused (one box) vs separate (two draggable boxes)
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Layout", "Disposition"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(128.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.tmMerged ? tr("Fused", "Fusionn\xC3\xA9") : tr("Separate", "S\xC3\xA9par\xC3\xA9"), c.tmMerged != 0)) { c.tmMerged = !c.tmMerged; save_ui_config(); }
        } ROW_NEXT(48.0f)
        { ROW_BAND(46.0f)   // Max per column
            const float lo = 1.0f, hi = 50.0f; char b[16]; sprintf(b, "%d", c.tmMax);
            float v01 = ((float)c.tmMax - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Max per column", "Max par colonne"), b, &v01)) { int v = (int)(lo + v01 * (hi - lo) + 0.5f); c.tmMax = v < 1 ? 1 : (v > 50 ? 50 : v); }
        } ROW_NEXT(46.0f)
        { ROW_BAND(46.0f)   // Icon size (Duration buff icons)
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.tmIconScale * 100.0f + 0.5f));
            float v01 = (c.tmIconScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Icon size", "Taille ic\xC3\xB4nes"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.tmIconScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        { ROW_BAND(46.0f)   // Row spacing : vertical gap between each timer line
            const float lo = 0.60f, hi = 3.00f; char b[16]; sprintf(b, "%d%%", (int)(c.tmRowGap * 100.0f + 0.5f));
            float v01 = (c.tmRowGap - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Row spacing", "Espacement lignes"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.tmRowGap = v < lo ? lo : (v > hi ? hi : v); }   // no save_ui_config() here : row_slider persists on RELEASE, saving per drag-frame rewrote the whole config file at 60 Hz
        } ROW_NEXT(46.0f)
        { ROW_BAND(52.0f)   // Duration display : Icon / Name / Both
            int m = (c.tmDurMode < 0 || c.tmDurMode > 2) ? 0 : c.tmDurMode;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Duration: show", "Duration : afficher"), MODE[m])) { c.tmDurMode = wrap(m + d, 3); save_ui_config(); }
        } ROW_NEXT(52.0f)
        { ROW_BAND(52.0f)   // Buff source : whose buffs on YOU to show -- mine only / + other players / + trusts / all
            int s = (c.tmBuffSrc < 0 || c.tmBuffSrc > 3) ? TMSRC_ALL : c.tmBuffSrc;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Buff source", "Source des buffs"), SRC[s])) { c.tmBuffSrc = wrap(s + d, 4); c.tmOthers = (c.tmBuffSrc != TMSRC_MINE); save_ui_config(); }
        } ROW_NEXT(52.0f)
        { ROW_BAND(48.0f)   // Buffs on allies : show a buff YOU cast on another player (person name + ESTIMATED timer)
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("My buffs on allies", "Mes buffs sur alli\xC3\xA9s"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.tmMine ? tr("On", "Oui") : tr("Off", "Non"), c.tmMine != 0)) { c.tmMine = !c.tmMine; save_ui_config(); }
        } ROW_NEXT(48.0f)
        if (c.tmMine) { ROW_BAND(48.0f)   // Ally-buff layout : GROUP same-spell into "(AoE N)" or one row PER ally (single-target
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);   //   Haste/Protect spread ; real AoE like Protectra / SCH Accession is grouped either way)
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Single-target on allies", "Monocible sur alli\xC3\xA9s"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(140.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.tmAllyGroup ? tr("Grouped", "Group\xC3\xA9s") : tr("Per person", "Par personne"), c.tmAllyGroup != 0)) { c.tmAllyGroup = !c.tmAllyGroup; save_ui_config(); }
        } ROW_NEXT(48.0f)
    }   // end Display

    // ===== sub-section : ALERTS (SP blink + focus-alert timings) =====
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Alerts", "Alertes"), catOpen_[7])) catOpen_[7] = !catOpen_[7];
    ROW_NEXT(42.0f)
    if (catOpen_[7]) {
        { ROW_BAND(48.0f)   // SP alert : SP1/SP2 buffs (all jobs) blink hard in their last minute (Soul Voice -> Nitro window)
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("SP last-min alert", "Alerte SP derni\xC3\xA8re min"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.tmSpAlert ? tr("On", "Oui") : tr("Off", "Non"), c.tmSpAlert != 0)) { c.tmSpAlert = !c.tmSpAlert; save_ui_config(); }
        } ROW_NEXT(48.0f)
        { ROW_BAND(46.0f)   // Focus WARN : a "Hidden + focus" buff surfaces when it drops below this many seconds
            const float lo = 10.0f, hi = 300.0f; char b[16]; sprintf(b, "%ds", c.tmFocusWarn);
            float v01 = ((float)c.tmFocusWarn - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Alert under", "Alerter sous"), b, &v01)) { int v = (int)(lo + v01 * (hi - lo) + 0.5f); v = (v / 5) * 5; c.tmFocusWarn = v < 10 ? 10 : (v > 300 ? 300 : v); }
        } ROW_NEXT(46.0f)
        { ROW_BAND(46.0f)   // Focus HOLD : how long a lost "Hidden + focus" alert stays before it clears itself
            const float lo = 5.0f, hi = 300.0f; char b[16]; sprintf(b, "%ds", c.tmFocusHold);
            float v01 = ((float)c.tmFocusHold - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Alert duration", "Dur\xC3\xA9""e de l'alerte"), b, &v01)) { int v = (int)(lo + v01 * (hi - lo) + 0.5f); v = (v / 5) * 5; c.tmFocusHold = v < 5 ? 5 : (v > 300 ? 300 : v); }
        } ROW_NEXT(46.0f)
        { ROW_BAND(38.0f)   // hint
            const float ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + snap(16.0f), tr("These tune the \"+ alert\" states (when they blink / appear).", "R\xC3\xA8gle les \xC3\xA9tats \"+ alerte\" (quand ils clignotent / apparaissent)."), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        } ROW_NEXT(38.0f)
        { ROW_BAND(52.0f)   // Reset : flush live buff/recast timers + focus "OUT" alerts (clears a stuck row). Same as //aio timers reset.
            const float bh = snap(34.0f), bw = snap(150.0f), ty = ry + yo + (snap(40.0f) - bh) * 0.5f; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + bh * 0.5f, tr("Reset timers", "R\xC3\xA9initialiser timers"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            if (push_btn(dev, fo, mo, click, CTRL_ID, coX + ctrlW - bw, ty, bw, bh, tr("Reset now", "R\xC3\xA9initialiser"), 1)) timers_reset();
        } ROW_NEXT(52.0f)
    }   // end Alerts

    // ===== sub-section : TEXT =====
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5])) catOpen_[5] = !catOpen_[5];
    ROW_NEXT(42.0f)
    if (catOpen_[5]) {
        { ROW_BAND(52.0f)   // element selector
            const char* TLBL[TM_TE_COUNT] = { tr("Title", "Titre"), tr("Name", "Nom"), tr("Timer", "Timer") };
            int te = (cfgTmTextElem_ < 0 || cfgTmTextElem_ >= TM_TE_COUNT) ? 0 : cfgTmTextElem_;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), TLBL[te])) { cfgTmTextElem_ = wrap(te + d, TM_TE_COUNT); }
        }
        ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                        c.tmText[(cfgTmTextElem_ < 0 || cfgTmTextElem_ >= TM_TE_COUNT) ? 0 : cfgTmTextElem_]);
    }   // end Text

    // ===== sub-section : BUFF FILTER (job-agnostic checklist of buffs to show, grouped by magic family) =====
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Buff filter", "Filtre de buffs"), trkSecOpen_)) trkSecOpen_ = !trkSecOpen_;
    ROW_NEXT(42.0f)
    if (trkSecOpen_) {
        { ROW_BAND(58.0f)   // LEGEND : one click on a spell's dot cycles it through these 4 states (dots match the checklist below)
            const float ty = ry + yo, lx = coX + snap(6.0f), lr = snap(4.0f), colW = ctrlW * 0.5f;
            struct LG { int focus, off; const char* en; const char* fr; };
            static const LG lg[4] = {
                { 0, 0, "Show",         "Afficher" },               // always visible, no alert
                { 1, 0, "Show + alert", "Afficher + alerte" },      // visible + blinks when low + OUT if lost
                { 0, 1, "Hide",         "Masquer" },                // never shown
                { 1, 1, "Hide + alert", "Masquer + alerte" },       // hidden ; appears + blinks when low + OUT if lost
            };
            for (int i = 0; i < 4; ++i) {                                                     // PASS 1 : all dots (quads) -- keep quads and text apart or the font binding breaks
                const float dcx = lx + ((i & 1) ? colW : 0.0f) + lr;
                const float cyy = ty + ((i >> 1) ? snap(42.0f) : snap(25.0f));
                if (lg[i].focus && !lg[i].off)      { gem(dev, dcx, cyy, lr + snap(1.0f), 0xFFFF7043u); gem(dev, dcx, cyy, lr - snap(3.0f), 0xFFFFE0C0u); }   // Focus : solid orange-red + light core
                else if (lg[i].focus && lg[i].off)  { gem(dev, dcx, cyy, lr + snap(1.0f), 0xFFFF7043u); gem(dev, dcx, cyy, lr - snap(2.0f), 0xFF12181Cu); }   // Unfollow-Focus : orange-red ring
                else if (!lg[i].off)                gem(dev, dcx, cyy, lr, C_ACCENTHI);                                                                        // Follow : solid accent
                else                                { gem(dev, dcx, cyy, lr, 0xFF3A4650u); gem(dev, dcx, cyy, lr - snap(2.0f), 0xFF12181Cu); }                 // Unfollow : hollow ring
            }
            fo->begin(dev);                                                                  // PASS 2 : all text
            fo->draw_lc(dev, lx, ty + snap(8.0f), tr("Click the dot to cycle :", "Clic sur la pastille pour changer :"), snap(11.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            for (int i = 0; i < 4; ++i) {
                const float dcx = lx + ((i & 1) ? colW : 0.0f) + lr;
                const float cyy = ty + ((i >> 1) ? snap(42.0f) : snap(25.0f));
                fo->draw_lc(dev, dcx + lr + snap(6.0f), cyy, tr(lg[i].en, lg[i].fr), snap(11.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
            }
        } ROW_NEXT(58.0f)
        { ROW_BAND(32.0f)   // "Collapse all" : fold every expanded family + JA sub-job in one click
            const float bw = snap(120.0f), bh = snap(24.0f), bx = coX + ctrlW - bw, by = ry + yo + (snap(28.0f) - bh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx, by, bw, bh, tr("Collapse all", "Tout replier"), false)) {
                for (int cc = 0; cc < TC_COUNT; ++cc) trkCatOpen_[cc] = false;
                for (int jj = 0; jj < 24; ++jj) jaJobOpen_[jj] = false;
            }
        } ROW_NEXT(32.0f)
        // Job-agnostic filter : one GLOBAL state per buff STATUS (shared across every job, stored per profile via tmBuffOff).
        // HIDDEN if the raw status key is set ; FOCUSED if TM_KEY_FOCUS|status is. Direct, no recast/ally/mirror logic.
        #define BF_OFF(st)      ( c.tm_buff_off((unsigned)(st)) )
        #define BF_FOFF(st)     ( c.tm_buff_off(UiConfig::TM_KEY_FOCUS | (unsigned)(st)) )
        #define BF_SET(st, OFF) c.tm_buff_set((unsigned)(st), (OFF))
        #define BF_FSET(st, ON) c.tm_buff_set(UiConfig::TM_KEY_FOCUS | (unsigned)(st), (ON))
        // ---- MASONRY : flat families flow into TWO INDEPENDENT half-width columns, each with its OWN vertical
        //      cursor (yL / yR). Every family lands in the column that is currently SHORTER (ties -> left) and that
        //      column OWNS the whole family : an OPEN family renders its checklist half-width directly under its
        //      header, extending ONLY that column. TC_JA is the exception -- it spans FULL width, so both cursors
        //      sync to a common baseline around it (see the sync-in / sync-out below). The global ry is NOT touched
        //      per-family ; it is dropped to the taller column once the whole grid is laid out. ----
        const float gutter = snap(12.0f), cellW = (hdrW - gutter) * 0.5f;
        const float colX[2] = { hdrX, hdrX + cellW + gutter };
        const float chipW = snap(84.0f), chipH = snap(28.0f);
        float yL = ry, yR = ry;     // per-column cursors, both starting at the grid top
        int   riL = ri, riR = ri;   // per-column zebra/stagger index (the global ri would alternate wrong across two side-by-side columns)
        int   flatN = 0;            // running index of rendered flat families -> STABLE column by parity (never reflows on toggle)
        for (int cat = 0; cat < TC_COUNT; ++cat) {
            // ---- SPECIAL CASE : Job Abilities are grouped BY JOB (the per-job tables), not flat from BUFF_FAM.
            //      The player's current main job comes FIRST, gold-tinted + expanded ; every other job with a
            //      status-bearing JA follows under a collapsible sub-header. State is still GLOBAL (keyed by
            //      .status), so the same 4-state dots / BF_* macros as the flat categories apply. ----
            if (cat == TC_JA) {
                // Job Abilities is FULL WIDTH : sync BOTH column cursors to a common baseline (the taller one) and
                // render from there on the GLOBAL ry/ri, exactly as before. sync-out below rejoins the columns.
                { const float base = (yL > yR) ? yL : yR; ry = base; ri = (riL > riR) ? riL : riR; }
                const int mainJob = party().self_main_job();   // 1..23, 0 if unknown -> its sub-section is gold + open by default
                // gather the UNIQUE JA statuses across every job (global filter state) for the header count + group chip
                unsigned jaSt[512]; int jaN = 0, catState = -2;
                for (int j = 1; j <= 23; ++j) {
                    int njt = 0; const JobBuff* jt = job_track((unsigned)j, njt);
                    for (int k = 0; k < njt && jaN < 512; ++k) if (jt[k].cat == TC_JA && jt[k].status > 0) {
                        const unsigned st = jt[k].status; bool dup = false;
                        for (int q = 0; q < jaN; ++q) if (jaSt[q] == st) { dup = true; break; }
                        if (!dup) jaSt[jaN++] = st;
                    }
                }
                if (!jaN) continue;
                for (int q = 0; q < jaN; ++q) { const int s = (BF_OFF(jaSt[q]) ? 2 : 0) | (BF_FOFF(jaSt[q]) ? 1 : 0); catState = (catState == -2) ? s : (catState == s ? s : -1); }
                // ---- category header (collapsible) + GROUP state chip : cycles EVERY shown JA status at once ----
                char hl[56]; _snprintf(hl, sizeof(hl), "%s (%d)", tr(TRACK_CAT_EN[cat], TRACK_CAT_FR[cat]), jaN); hl[sizeof(hl) - 1] = 0;
                const float gchipW = snap(96.0f), hbX = hdrX + snap(14.0f);
                if (cat_header(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, cat), hbX, ry, hdrW - snap(14.0f) - gchipW - snap(6.0f), hl, trkCatOpen_[cat])) trkCatOpen_[cat] = !trkCatOpen_[cat];
                {
                    static const char* const SN_EN[4] = { "Show", "Show+al", "Hide", "Hide+al" };
                    static const char* const SN_FR[4] = { "Afficher", "Aff+al", "Masquer", "Masq+al" };
                    const char* glbl = (catState < 0) ? tr("Mixed", "Mixte") : tr(SN_EN[catState], SN_FR[catState]);
                    const bool lit = (catState == 1 || catState == 3);
                    if (toggle_chip(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, cat), hdrX + hdrW - gchipW, ry + snap(4.0f), gchipW, snap(30.0f), glbl, lit)) {
                        const int next = (catState < 0) ? 0 : (catState + 1) % 4;
                        for (int q = 0; q < jaN; ++q) { BF_SET(jaSt[q], (next & 2) != 0); BF_FSET(jaSt[q], (next & 1) != 0); }
                        save_ui_config();
                    }
                }
                ROW_NEXT(40.0f)
                if (trkCatOpen_[cat]) {
                    if (mainJob >= 1 && mainJob <= 23) jaJobOpen_[mainJob] = true;   // player's own JAs always visible (forced open ; gold path ignores this anyway)
                    int order[24], on = 0;                                           // draw ORDER : current main job first, then the rest
                    if (mainJob >= 1 && mainJob <= 23) order[on++] = mainJob;
                    for (int j = 1; j <= 23; ++j) if (j != mainJob) order[on++] = j;
                    for (int oi = 0; oi < on; ++oi) {
                        const int j = order[oi];
                        int njt = 0; const JobBuff* jt = job_track((unsigned)j, njt);
                        unsigned jst[64]; const char* jnm[64]; int jm = 0;           // this job's status-bearing JAs (deduped within the job)
                        for (int k = 0; k < njt && jm < 64; ++k) if (jt[k].cat == TC_JA && jt[k].status > 0) {
                            const unsigned st = jt[k].status; bool dup = false;
                            for (int q = 0; q < jm; ++q) if (jst[q] == st) { dup = true; break; }
                            if (!dup) { jst[jm] = st; jnm[jm] = jt[k].name; ++jm; }
                        }
                        if (!jm) continue;
                        const bool gold = (j == mainJob);                            // current main job : gold + always expanded
                        const bool jopen = gold ? true : jaJobOpen_[j];
                        const u32 baseCol = gold ? C_GOLDHI : C_TEXT;
                        // ---- job SUB-HEADER : an indented row [caret] ABBR (hover band + click-to-collapse ; the forced-open main job ignores the click) ----
                        { ROW_BAND(26.0f)
                            const float ty = ry + yo, rh = snap(24.0f), sx = coX + snap(14.0f), swj = ctrlW - snap(14.0f);
                            const bool hov = inrect(mo, sx, ty, swj, rh);
                            if (hov) flat(dev, sx, ty, swj, rh, gold ? 0x18FFD24Au : 0x14FFFFFFu);
                            if (hov && click && !gold) jaJobOpen_[j] = !jaJobOpen_[j];   // collapse state is in-memory only, like trkCatOpen_ (no save)
                            const float gx = sx + snap(9.0f), gy = ty + rh * 0.5f, s = snap(3.5f); const u32 cc = fa(gold ? C_GOLDHI : C_ACCENTHI);
                            if (jopen) { const float d[6] = { gx - s, gy - s * 0.55f,  gx + s, gy - s * 0.55f,  gx, gy + s * 0.85f }; fill_poly_aa(dev, d, 3, cc); }   // down caret (open)
                            else       { const float d[6] = { gx - s * 0.55f, gy - s,  gx - s * 0.55f, gy + s,  gx + s * 0.85f, gy }; fill_poly_aa(dev, d, 3, cc); }   // right caret (collapsed)
                            fo->begin(dev);
                            fo->draw_lc(dev, gx + snap(13.0f), gy, job_abbr(j), snap(13.0f), fa(baseCol), fa(C_STROKE), 1.2f);
                        } ROW_NEXT(26.0f)
                        // ---- this job's JA checklist : the SAME column-major grid + 4-state dots as the flat categories ----
                        if (jopen) {
                            ry += snap(4.0f);   // breathing room between the job sub-header and its first ability
                            const float avail = ctrlW - snap(32.0f), x0 = coX + snap(32.0f);   // one indent deeper than a flat category
                            const int cols = (avail > snap(560.0f)) ? 3 : (avail > snap(300.0f)) ? 2 : 1;
                            const float colW = avail / cols;
                            const int rpc = (jm + cols - 1) / cols;
                            for (int r = 0; r < rpc; ++r) {
                                ROW_BAND(21.0f)
                                const float ty = ry + yo, rh = snap(21.0f), cbs = snap(13.0f);
                                for (int col = 0; col < cols; ++col) {   // sub-pass A : QUADS (hover band + dots) + click
                                    const int k = col * rpc + r; if (k >= jm) continue;
                                    const unsigned st = jst[k]; const bool off = BF_OFF(st), focus = BF_FOFF(st);
                                    const float cx = x0 + col * colW, cby = ty + (rh - cbs) * 0.5f;
                                    if (inrect(mo, cx - snap(2.0f), ty, colW - snap(4.0f), rh)) {
                                        flat(dev, cx - snap(2.0f), ty, colW - snap(4.0f), rh, 0x18FFFFFFu);
                                        if (click) {
                                            if (!off && !focus) BF_FSET(st, true);                              // Follow -> Follow+focus
                                            else if (!off && focus) { BF_SET(st, true); BF_FSET(st, false); }   // Follow+focus -> Hidden
                                            else if (off && !focus) BF_FSET(st, true);                          // Hidden -> Hidden+focus
                                            else { BF_SET(st, false); BF_FSET(st, false); }                     // Hidden+focus -> Follow
                                            save_ui_config();
                                        }
                                    }
                                    const float dcx = cx + cbs * 0.5f, dcy = cby + cbs * 0.5f, dr = cbs * 0.44f;
                                    if (focus && !off)      { gem(dev, dcx, dcy, dr + snap(1.0f), 0xFFFF7043u); gem(dev, dcx, dcy, dr - snap(3.0f), 0xFFFFE0C0u); }
                                    else if (focus && off)  { gem(dev, dcx, dcy, dr + snap(1.0f), 0xFFFF7043u); gem(dev, dcx, dcy, dr - snap(2.0f), 0xFF12181Cu); }
                                    else if (!off)          gem(dev, dcx, dcy, dr, C_ACCENTHI);
                                    else                    { gem(dev, dcx, dcy, dr, 0xFF3A4650u); gem(dev, dcx, dcy, dr - snap(2.0f), 0xFF12181Cu); }
                                }
                                fo->begin(dev);   // sub-pass B : LABELS (font re-bound after the quads)
                                for (int col = 0; col < cols; ++col) {
                                    const int k = col * rpc + r; if (k >= jm) continue;
                                    const unsigned st = jst[k]; const bool off = BF_OFF(st), focus = BF_FOFF(st);
                                    const float cx = x0 + col * colW;
                                    fo->draw_lc(dev, cx + cbs + snap(5.0f), ty + rh * 0.5f, jnm[k], snap(12.0f), fa(focus ? 0xFFFFB78Au : (off ? C_MUTE : baseCol)), fa(C_STROKE), 1.0f);
                                }
                                ROW_NEXT(21.0f)
                            }
                        }
                    }
                }
                yL = yR = ry; riL = riR = ri;   // sync-out : both columns resume together BELOW the full-width JA block
                continue;
            }
            int cn = 0, catState = -2;   // group state : -2 none yet, -1 mixed, 0 Follow, 1 Follow+focus, 2 Hidden, 3 Hidden+focus
            for (int i = 0; i < BUFF_FAM_N; ++i) if (BUFF_FAM[i].cat == cat) {
                ++cn;
                const int s = (BF_OFF(BUFF_FAM[i].status) ? 2 : 0) | (BF_FOFF(BUFF_FAM[i].status) ? 1 : 0);
                catState = (catState == -2) ? s : (catState == s ? s : -1);
            }
            if (!cn) continue;
            // group state chip label + lit flag (shared by the header cell and its group chip)
            char hl[56]; _snprintf(hl, sizeof(hl), "%s (%d)", tr(TRACK_CAT_EN[cat], TRACK_CAT_FR[cat]), cn); hl[sizeof(hl) - 1] = 0;
            static const char* const SN_EN[4] = { "Show", "Show+al", "Hide", "Hide+al" };
            static const char* const SN_FR[4] = { "Afficher", "Aff+al", "Masquer", "Masq+al" };
            const char* glbl = (catState < 0) ? tr("Mixed", "Mixte") : tr(SN_EN[catState], SN_FR[catState]);
            const bool lit = (catState == 1 || catState == 3);   // a focus state -> highlight
            // ---- STABLE column : a family's column is fixed by its ORDER PARITY, not the running heights -- so expanding
            //      one family NEVER makes another jump columns (the masonry reflow the user hit). That column owns it. ----
            const int   ci = (flatN++ & 1);
            float&      yc = ci ? yR : yL;
            int&        rc = ci ? riR : riL;
            const float cx = colX[ci];
            const bool  open = trkCatOpen_[cat];
            // ---- compact header cell : [caret] Name (count) on the left, GROUP state chip on the cell's right edge.
            //      The cell is half-width whether the family is open or closed ; only the caret + the checklist differ. ----
            row_band(dev, cx, yc, cellW, snap(40.0f), (rc & 1) != 0, 0.0f);
            {
                const float ap = stagger(anim_, rc); g_fade = e * ap;
                const float yoff = (1.0f - ap) * snap(10.0f);
                const float hy = yc + yoff + snap(4.0f), chy = yc + yoff + (snap(40.0f) - chipH) * 0.5f;
                const float hw = cellW - chipW - snap(6.0f);
                if (cat_header(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, cat), cx, hy, hw, hl, open)) trkCatOpen_[cat] = !trkCatOpen_[cat];
                // group chip on the cell's right edge (distinct source line -> distinct CTRL_ID -> its own spring)
                if (toggle_chip(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, cat), cx + cellW - chipW, chy, chipW, chipH, glbl, lit)) {
                    const int next = (catState < 0) ? 0 : (catState + 1) % 4;   // mixed -> Follow, else advance
                    for (int i = 0; i < BUFF_FAM_N; ++i) if (BUFF_FAM[i].cat == cat) { BF_SET(BUFF_FAM[i].status, (next & 2) != 0); BF_FSET(BUFF_FAM[i].status, (next & 1) != 0); }
                    save_ui_config();
                }
            }
            yc += snap(40.0f); ++rc;
            // ---- OPEN : the buff checklist renders HALF-WIDTH directly under the header, extending only THIS column.
            //      Drawn at an EXPLICIT (x = cx, y = yc, w = cellW) and advancing the column cursor by hand -- NEVER via
            //      ROW_NEXT (that moves the GLOBAL ry and would cross-wire the two columns). The 1/2/3-column rule now
            //      keys off cellW, so a half-width family uses fewer inner columns. Same 4-state dots / BF_* / cycle. ----
            if (open) {
                yc += snap(6.0f);   // breathing room between the category bar and its first entry (else the top item touches the bar)
                int idx[256], m = 0;
                for (int i = 0; i < BUFF_FAM_N && m < 256; ++i) if (BUFF_FAM[i].cat == cat) idx[m++] = i;
                const float avail = cellW - snap(20.0f), x0 = cx + snap(14.0f);
                const int cols = (avail > snap(560.0f)) ? 3 : (avail > snap(300.0f)) ? 2 : 1;
                const float colW = avail / cols;
                const int rpc = (m + cols - 1) / cols;   // rows per column (column-major : reads down each column)
                for (int r = 0; r < rpc; ++r) {
                    const float ap = stagger(anim_, rc); g_fade = e * ap;
                    const float yo = (1.0f - ap) * snap(14.0f) + (snap(21.0f) - snap(40.0f)) * 0.5f;   // same content-centring as ROW_BAND(21)
                    row_band(dev, cx, yc, cellW, snap(21.0f), (rc & 1) != 0, 0.0f);
                    const float ty = yc + yo, rh = snap(21.0f), cbs = snap(13.0f);
                    // --- sub-pass A : QUADS (hover band + dots) + click handling ---
                    for (int col = 0; col < cols; ++col) {
                        const int k = col * rpc + r; if (k >= m) continue;
                        const unsigned st = BUFF_FAM[idx[k]].status; const bool off = BF_OFF(st);
                        // 4 states via 2 bits (hidden?, focus?). Click cycles in the legend order :
                        // Follow -> Follow+focus -> Hidden -> Hidden+focus -> Follow.
                        const bool focus = BF_FOFF(st);
                        const float dx = x0 + col * colW, cby = ty + (rh - cbs) * 0.5f;
                        if (inrect(mo, dx - snap(2.0f), ty, colW - snap(4.0f), rh)) {
                            flat(dev, dx - snap(2.0f), ty, colW - snap(4.0f), rh, 0x18FFFFFFu);
                            if (click) {
                                if (!off && !focus) BF_FSET(st, true);                          // Follow -> Follow+focus
                                else if (!off && focus) { BF_SET(st, true); BF_FSET(st, false); }   // Follow+focus -> Hidden
                                else if (off && !focus) BF_FSET(st, true);                       // Hidden -> Hidden+focus
                                else { BF_SET(st, false); BF_FSET(st, false); }                  // Hidden+focus -> Follow
                                save_ui_config();
                            }
                        }
                        const float dcx = dx + cbs * 0.5f, dcy = cby + cbs * 0.5f, dr = cbs * 0.44f;
                        if (focus && !off)      { gem(dev, dcx, dcy, dr + snap(1.0f), 0xFFFF7043u); gem(dev, dcx, dcy, dr - snap(3.0f), 0xFFFFE0C0u); }   // Focus : solid orange-red + light core
                        else if (focus && off)  { gem(dev, dcx, dcy, dr + snap(1.0f), 0xFFFF7043u); gem(dev, dcx, dcy, dr - snap(2.0f), 0xFF12181Cu); }   // Unfollow-Focus : orange-red RING
                        else if (!off)          gem(dev, dcx, dcy, dr, C_ACCENTHI);                                                                        // Follow : solid accent
                        else                    { gem(dev, dcx, dcy, dr, 0xFF3A4650u); gem(dev, dcx, dcy, dr - snap(2.0f), 0xFF12181Cu); }                 // Unfollow : hollow ring
                    }
                    // --- sub-pass B : LABELS (font must be re-bound AFTER the quads above) ---
                    fo->begin(dev);
                    for (int col = 0; col < cols; ++col) {
                        const int k = col * rpc + r; if (k >= m) continue;
                        const unsigned st = BUFF_FAM[idx[k]].status; const bool off = BF_OFF(st);
                        const bool focus = BF_FOFF(st);
                        const float dx = x0 + col * colW;
                        fo->draw_lc(dev, dx + cbs + snap(5.0f), ty + rh * 0.5f, BUFF_FAM[idx[k]].name, snap(12.0f), fa(focus ? 0xFFFFB78Au : (off ? C_MUTE : C_TEXT)), fa(C_STROKE), 1.0f);
                    }
                    yc += snap(21.0f); ++rc;
                }
                yc += snap(6.0f);   // a little breathing room beneath an expanded family
            }
        }
        // ---- grid laid out : drop the GLOBAL cursor to the TALLER column so every section BELOW renders correctly ----
        ry = (yL > yR) ? yL : yR;
        ri = (riL > riR) ? riL : riR;
        #undef BF_OFF
        #undef BF_FOFF
        #undef BF_SET
        #undef BF_FSET
    }   // end Buff filter

    ry += snap(10.0f);
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
