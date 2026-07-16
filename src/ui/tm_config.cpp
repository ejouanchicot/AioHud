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
        #define TM_TOGGLE(UID, LABEL, FIELD)                                                                          \
            { ROW_BAND(48.0f) const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);                           \
              fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, LABEL, snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);\
              const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;\
              if (toggle_chip(dev, fo, mo, click, UID, bx2, bty, bbw, bbh, (FIELD) ? tr("On", "Oui") : tr("Off", "Non"), (FIELD) != 0)) { FIELD = !(FIELD); save_ui_config(); } } ROW_NEXT(48.0f)
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
        { ROW_BAND(44.0f)   // placement hint
            const float ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + snap(19.0f),
                        c.tmMerged ? tr("Position: drag it in //aio edit", "Position : d\xC3\xA9place-le dans //aio edit")
                                   : tr("Separate: drag Duration & Recast independently in //aio edit", "S\xC3\xA9par\xC3\xA9 : bouge Duration et Recast ind\xC3\xA9pendamment dans //aio edit"),
                        snap(13.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        } ROW_NEXT(44.0f)
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
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Focus: warn under", "Focus : alerte sous"), b, &v01)) { int v = (int)(lo + v01 * (hi - lo) + 0.5f); v = (v / 5) * 5; c.tmFocusWarn = v < 10 ? 10 : (v > 300 ? 300 : v); }
        } ROW_NEXT(46.0f)
        { ROW_BAND(46.0f)   // Focus HOLD : how long a lost "Hidden + focus" alert stays before it clears itself
            const float lo = 5.0f, hi = 300.0f; char b[16]; sprintf(b, "%ds", c.tmFocusHold);
            float v01 = ((float)c.tmFocusHold - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Focus: hold alert", "Focus : garder alerte"), b, &v01)) { int v = (int)(lo + v01 * (hi - lo) + 0.5f); v = (v / 5) * 5; c.tmFocusHold = v < 5 ? 5 : (v > 300 ? 300 : v); }
        } ROW_NEXT(46.0f)
        { ROW_BAND(38.0f)   // hint
            const float ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + snap(16.0f), tr("These tune \"Hidden + focus\" tracked spells.", "R\xC3\xA8gle les sorts suivis en \"Masqu\xC3\xA9 + focus\"."), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
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

    // ===== sub-section : TRACK PER JOB (checklist of buffs/recasts to show, per job) =====
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Track per job", "Suivi par job"), trkSecOpen_)) trkSecOpen_ = !trkSecOpen_;
    ROW_NEXT(42.0f)
    if (trkSecOpen_) {
        static const char* const JAB[24] = { "-","WAR","MNK","WHM","BLM","RDM","THF","PLD","DRK","BST","BRD","RNG","SAM",
                                             "NIN","DRG","SMN","BLU","COR","PUP","DNC","SCH","GEO","RUN","MON" };
        PlayerInfo me; const bool haveMe = read_player(me);
        const int curMain = haveMe ? me.mjob : 0, curSub = haveMe ? me.sjob : 0;
        const int curMlvl = haveMe ? me.mlvl : 99, curSlvl = haveMe ? me.slvl : 49;
        int job = (trkJob_ >= 1 && trkJob_ <= 22) ? trkJob_ : (curMain >= 1 && curMain <= 22 ? curMain : 5);   // MAIN
        int sub = (trkSub_ >= 0) ? trkSub_ : (curSub >= 1 && curSub <= 22 ? curSub : 0);                        // SUB (0 = none)
        if (job < 1 || job > 22) job = 5;
        if (sub < 0 || sub > 22) sub = 0;
        // ---- MAIN + SUB job pickers : a grid of NAME buttons. Selected = lit panel + ring ; a gold corner dot marks
        //      your LIVE job ; the Sub grid leads with a '-' (none). Section title is CENTERED + underlined. ----
        for (int which = 0; which < 2; ++which) {
            { ROW_BAND(26.0f) fo->begin(dev);
              const char* t = which == 0 ? tr("MAIN JOB", "JOB PRINCIPAL") : tr("SUB JOB", "SOUS-JOB");
              const float tcx = coX + ctrlW * 0.5f, tw = fo->measure(t, snap(15.0f));
              fo->draw_c(dev, tcx, ry + yo + snap(13.0f), t, snap(15.0f), fa(C_GOLD), fa(C_STROKE), 1.3f);
              flat(dev, tcx - tw * 0.5f, ry + yo + snap(23.0f), tw, snap(1.5f), C_GOLD);   // gold underline
            } ROW_NEXT(32.0f)   // + a gap between the title and its job list
            const int perRow = 11; const float gap = snap(3.0f), cw = (ctrlW - (perRow - 1) * gap) / perRow, ch = snap(23.0f);
            const int first = (which == 1) ? 0 : 1;   // the Sub grid leads with 'None' (job 0)
            const int total = 22 - first + 1;
            for (int r = 0; r * perRow < total; ++r) {
                ROW_BAND(26.0f)
                // sub-pass A : panels + selection ring + None-dash + live dot + click (colour quads)
                for (int col = 0; col < perRow; ++col) {
                    const int gi = r * perRow + col; if (gi >= total) break;
                    const int jv = first + gi;
                    const bool seld = (which == 0) ? (jv == job) : (jv == sub);
                    const bool live = (which == 0) ? (jv == curMain) : (jv == curSub);
                    const float cx = coX + col * (cw + gap), cy = ry + yo;
                    const bool hov = inrect(mo, cx, cy, cw, ch);
                    rrect_fill(dev, cx, cy, cw, ch, snap(4.0f), seld ? C_ACCENTHI : (hov ? 0xFF2C363Eu : 0xFF1B2228u), seld ? C_ACCENT : (hov ? 0xFF20292Fu : 0xFF141A1Fu));
                    if (seld) outline(dev, cx, cy, cw, ch, 0xFFEAFBF9u);
                    if (jv == 0) flat(dev, cx + cw * 0.5f - snap(5.0f), cy + ch * 0.5f - snap(1.0f), snap(10.0f), snap(2.0f), C_MUTE);   // 'None'
                    if (live) flat(dev, cx + snap(3.0f), cy + ch - snap(3.5f), cw - snap(6.0f), snap(3.0f), 0xFFFFB300u);                // live-job : a fixed AMBER underline (visible on ANY theme, incl. a white/bright selected panel)
                    if (hov && click) { if (which == 0) { trkJob_ = jv; job = jv; } else { trkSub_ = jv; sub = jv; } }
                }
                // sub-pass B : the job NAMES (font ; dark text on the bright SELECTED panel, light otherwise)
                fo->begin(dev);
                for (int col = 0; col < perRow; ++col) {
                    const int gi = r * perRow + col; if (gi >= total) break;
                    const int jv = first + gi; if (jv == 0) continue;
                    const bool seld = (which == 0) ? (jv == job) : (jv == sub);
                    const bool live = (which == 0) ? (jv == curMain) : (jv == curSub);
                    const float cx = coX + col * (cw + gap), cy = ry + yo;
                    const u32 tc = seld ? 0xFF0B1014u : (live ? 0xFFFFC94Du : C_TEXT);   // live (unselected) job name in amber
                    fo->draw_c(dev, cx + cw * 0.5f, cy + ch * 0.5f, JAB[jv], snap(11.0f), fa(tc), fa(seld ? 0x66FFFFFFu : C_STROKE), 1.0f);
                }
                ROW_NEXT(26.0f)
            }
            ry += snap(9.0f);   // space between the Main and Sub sections (and after Sub, before the scope selector)
        }
        { ROW_BAND(52.0f)   // SCOPE : Self (buffs on you + your recasts) vs Allies (buffs YOU cast on others)
            const char* SC[2] = { tr("Self", "Soi"), tr("Allies", "Alli\xC3\xA9s") };
            int sc = (trkScope_ == 1) ? 1 : 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Track on", "Suivre sur"), SC[sc])) trkScope_ = wrap(sc + d, 2);
        } ROW_NEXT(52.0f)
        { ROW_BAND(58.0f)   // LEGEND : one click on a spell's dot cycles it through these 4 states (dots match the checklist below)
            const float ty = ry + yo, lx = coX + snap(6.0f), lr = snap(4.0f), colW = ctrlW * 0.5f;
            struct LG { int focus, off; const char* en; const char* fr; };
            static const LG lg[4] = {
                { 0, 0, "Tracked",         "Suivi" },              // shown
                { 1, 0, "Tracked + focus", "Suivi + focus" },      // shown + red alert if lost
                { 0, 1, "Hidden",          "Masqu\xC3\xA9" },          // hidden
                { 1, 1, "Hidden + focus",  "Masqu\xC3\xA9 + focus" },  // hidden + alert if < 1 min or lost
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
        const bool ally = (trkScope_ == 1);
        // scope-aware helpers. Self : status-key (buff) + recast-key. Allies : only buffs, keyed TM_KEY_ALLY|status.
        // ALLIES scope drops what you can't put on another player : recast-only cooldowns, Job Abilities, and the
        // self-only enhancing families (Gains, Spikes, Enspells). Self scope shows everything.
        #define ENT_APPLIES(E) ( ally ? ((E).status != 0 && (E).cat != TC_JA && (E).cat != TC_GAIN && (E).cat != TC_SPIKES && (E).cat != TC_ENSPELL) : true )
        // The checkbox/focus state keys on the entry's UNIQUE recast (distinct per spell) so distinct spells that merely
        // SHARE a buff status (BLU Cocoon / Reactor Cool = Defense Boost) don't toggle together. Recast-less entries
        // fall back to their status. (The runtime hud reads the STATUS key ; entFocusSet mirrors it below.)
        #define ENT_OFF(E) ( ally ? ((E).status && c.tm_track_off(job, UiConfig::TM_KEY_ALLY | (E).status)) \
                                  : ((E).recast ? c.tm_track_off(job, UiConfig::TM_KEY_RECAST + (E).recast) \
                                                : ((E).status && c.tm_track_off(job, (E).status))) )
        // Self routes through entHiddenSet (defined below, after the entry list is built) so the shared STATUS mirror =
        // AND over same-status entries ; ally keys purely on status.
        #define ENT_SET(E, OFF) do { if (ally) { if ((E).status) c.tm_track_set(job, UiConfig::TM_KEY_ALLY | (E).status, (OFF)); } \
                                     else entHiddenSet((E), (OFF)); } while (0)
        // FOCUS read : per-entry (unique) so shared-status spells show independent dots. Self : FOCUS|recast (else FOCUS|status). Allies : FOCUS|ALLY|status.
        #define ENT_FOFF(E) ( ally ? ((E).status && c.tm_track_off(job, UiConfig::TM_KEY_FOCUS | UiConfig::TM_KEY_ALLY | (E).status)) \
                                   : ((E).recast ? c.tm_track_off(job, UiConfig::TM_KEY_FOCUS | (UiConfig::TM_KEY_RECAST + (E).recast)) \
                                                 : ((E).status && c.tm_track_off(job, UiConfig::TM_KEY_FOCUS | (E).status))) )
        // ---- build the checklist : MAIN entries (level <= your main level) + SUB entries (level <= sub cap = half
        //      main), deduped. Off-job selections show the full job (main cap 99 / sub cap 49). ----
        const int mainLvl = (job == curMain) ? curMlvl : 99;
        const int subCap  = (sub == curSub && job == curMain) ? curSlvl : (mainLvl / 2);
        static JobBuff comb[768]; int n = 0;
        auto addJob = [&](int jj, int lvlCap) {
            if (jj < 1 || jj > 22) return;
            int nn = 0; const JobBuff* a = job_track((unsigned)jj, nn);
            for (int i = 0; i < nn && n < 768; ++i) {
                if (a[i].level > lvlCap) continue;
                bool dup = false;
                for (int q = 0; q < n; ++q) if (comb[q].status == a[i].status && comb[q].recast == a[i].recast && !strcmp(comb[q].name, a[i].name)) { dup = true; break; }
                if (!dup) comb[n++] = a[i];
            }
        };
        addJob(job, mainLvl);
        if (sub && sub != job) addJob(sub, subCap);
        const JobBuff* jb = comb;
        // Set/clear an entry's FOCUS flag. Writes the per-entry (unique) key AND re-mirrors the STATUS key that the
        // runtime hud reads = OR over every listed entry sharing this status (so un-focusing one of two same-status
        // spells doesn't silence the other at runtime).
        auto entFocusSet = [&](const JobBuff& E, bool on) {
            if (ally) { if (E.status) c.tm_track_set(job, UiConfig::TM_KEY_FOCUS | UiConfig::TM_KEY_ALLY | E.status, on); return; }
            if (E.recast)      c.tm_track_set(job, UiConfig::TM_KEY_FOCUS | (UiConfig::TM_KEY_RECAST + E.recast), on);
            else if (E.status) c.tm_track_set(job, UiConfig::TM_KEY_FOCUS | E.status, on);
            if (E.status) {
                bool any = false;
                for (int q = 0; q < n && !any; ++q) if (jb[q].status == E.status)
                    any = jb[q].recast ? c.tm_track_off(job, UiConfig::TM_KEY_FOCUS | (UiConfig::TM_KEY_RECAST + jb[q].recast))
                                       : c.tm_track_off(job, UiConfig::TM_KEY_FOCUS | jb[q].status);
                c.tm_track_set(job, UiConfig::TM_KEY_FOCUS | E.status, any);   // status mirror for the runtime monitor
            }
        };
        // Set/clear an entry's HIDDEN flag (self scope). Writes the per-entry recast key AND re-mirrors the STATUS key the
        // runtime reads = AND over every listed entry sharing this status : the shared-status icon hides only when ALL its
        // spells are hidden, so hiding ONE of two same-status spells (BLU Cocoon / Reactor Cool = Defense Boost) no longer
        // hides the other's icon at runtime. ENT_SET (self) routes here.
        auto entHiddenSet = [&](const JobBuff& E, bool off) {
            if (E.recast) c.tm_track_set(job, UiConfig::TM_KEY_RECAST + E.recast, off);
            if (E.status) {
                bool all = true;
                for (int q = 0; q < n && all; ++q) if (jb[q].status == E.status) {
                    const bool h = jb[q].recast ? c.tm_track_off(job, UiConfig::TM_KEY_RECAST + jb[q].recast)
                                                : (&jb[q] == &E ? off : c.tm_track_off(job, E.status));   // recast-less entry's own state = the value being set
                    if (!h) all = false;
                }
                c.tm_track_set(job, E.status, all);   // runtime hides the icon only when ALL same-status spells are hidden
            }
        };
        if (n <= 0) { ROW_BAND(40.0f) fo->begin(dev);
            fo->draw_lc(dev, coX + snap(8.0f), ry + yo + snap(18.0f), tr("No trackable buffs/recasts here.", "Rien \xC3\xA0 suivre ici."), snap(13.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
            ROW_NEXT(40.0f)
        }
        for (int cat = 0; cat < TC_COUNT; ++cat) {
            int cn = 0, catState = -2;   // group state : -2 none yet, -1 mixed, 0 Follow, 1 Follow+focus, 2 Hidden, 3 Hidden+focus
            for (int i = 0; i < n; ++i) if (jb[i].cat == cat && ENT_APPLIES(jb[i])) {
                ++cn;
                const int s = (ENT_OFF(jb[i]) ? 2 : 0) | (ENT_FOFF(jb[i]) ? 1 : 0);
                catState = (catState == -2) ? s : (catState == s ? s : -1);
            }
            if (!cn) continue;
            // ---- category header (collapsible) + a GROUP state chip : one click sets the WHOLE category to the next
            //      of the 4 states (Follow -> Follow+focus -> Hidden -> Hidden+focus), same cycle as a single dot ----
            char hl[56]; _snprintf(hl, sizeof(hl), "%s (%d)", tr(TRACK_CAT_EN[cat], TRACK_CAT_FR[cat]), cn); hl[sizeof(hl) - 1] = 0;
            const float gchipW = snap(96.0f), hbX = hdrX + snap(14.0f);
            if (cat_header(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, cat), hbX, ry, hdrW - snap(14.0f) - gchipW - snap(6.0f), hl, trkCatOpen_[cat])) trkCatOpen_[cat] = !trkCatOpen_[cat];
            {
                static const char* const SN_EN[4] = { "Tracked", "Track+foc", "Hidden", "Hid+foc" };
                static const char* const SN_FR[4] = { "Suivi", "Suivi+f", "Masqu\xC3\xA9", "Masq+f" };
                const char* glbl = (catState < 0) ? tr("Mixed", "Mixte") : tr(SN_EN[catState], SN_FR[catState]);
                const bool lit = (catState == 1 || catState == 3);   // a focus state -> highlight
                if (toggle_chip(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, cat), hdrX + hdrW - gchipW, ry + snap(4.0f), gchipW, snap(30.0f), glbl, lit)) {
                    const int next = (catState < 0) ? 0 : (catState + 1) % 4;   // mixed -> Follow, else advance
                    for (int i = 0; i < n; ++i) if (jb[i].cat == cat && ENT_APPLIES(jb[i])) { ENT_SET(jb[i], (next & 2) != 0); entFocusSet(jb[i], (next & 1) != 0); }
                    save_ui_config();
                }
            }
            ROW_NEXT(40.0f)
            // ---- entries : compact chips in a wrapping grid, split into two sub-groups : buffs (duration + recast)
            //      then recast-only. The split labels only show when the category actually has both kinds. ----
            if (trkCatOpen_[cat]) {
                int nBuf = 0, nRec = 0;
                for (int i = 0; i < n; ++i) if (jb[i].cat == cat && ENT_APPLIES(jb[i])) { if (jb[i].status) ++nBuf; else ++nRec; }
                const bool split = (nBuf > 0 && nRec > 0);
                const float avail = ctrlW - snap(18.0f), x0 = coX + snap(18.0f);
                const int cols = (avail > snap(560.0f)) ? 3 : (avail > snap(300.0f)) ? 2 : 1;
                const float colW = avail / cols;
                for (int pass = 0; pass < 2; ++pass) {   // 0 = buffs (duration + recast) ; 1 = recast-only
                    int idx[256], m = 0;
                    for (int i = 0; i < n && m < 256; ++i) if (jb[i].cat == cat && ENT_APPLIES(jb[i]) && ((jb[i].status != 0) == (pass == 0))) idx[m++] = i;
                    if (!m) continue;
                    if (split) { ROW_BAND(20.0f) fo->begin(dev);
                        fo->draw_lc(dev, x0, ry + yo + snap(10.0f),
                                    (pass == 0) ? tr("Duration + recast", "Dur\xC3\xA9""e + recast") : tr("Recast only", "Recast seul"),
                                    snap(11.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
                        ROW_NEXT(20.0f)
                    }
                    // CHECKLIST : aligned rows of [checkbox] Name, laid COLUMN-major (reads down each column). Filled
                    // box = tracked, empty box = hidden. Far more scannable than a cloud of pill-buttons.
                    const int rpc = (m + cols - 1) / cols;   // rows per column
                    for (int r = 0; r < rpc; ++r) {
                        ROW_BAND(21.0f)
                        const float ty = ry + yo, rh = snap(21.0f), cbs = snap(13.0f);
                        // --- sub-pass A : QUADS (hover band + checkboxes) + click handling ---
                        for (int col = 0; col < cols; ++col) {
                            const int k = col * rpc + r; if (k >= m) continue;   // column-major
                            const JobBuff& en = jb[idx[k]]; const bool off = ENT_OFF(en);
                            // 4 states (buff entries) via 2 bits (hidden?, focus?). Click cycles in the SAME order as the
                            // legend : Follow -> Follow+focus -> Hidden -> Hidden+focus -> Follow.
                            //   Follow+focus = shown + permanent red alert when lost ; Hidden+focus = hidden but pops an
                            //   alert when < 1 min left OR lost (60s).
                            const bool focusable = (en.status != 0);
                            const bool focus = focusable && ENT_FOFF(en);
                            const float cx = x0 + col * colW, cby = ty + (rh - cbs) * 0.5f;
                            if (inrect(mo, cx - snap(2.0f), ty, colW - snap(4.0f), rh)) {
                                flat(dev, cx - snap(2.0f), ty, colW - snap(4.0f), rh, 0x18FFFFFFu);
                                if (click) {
                                    if (!focusable) ENT_SET(en, !off);                                       // 2-state (recast-only)
                                    else if (!off && !focus) entFocusSet(en, true);                          // Follow -> Follow+focus
                                    else if (!off && focus) { ENT_SET(en, true); entFocusSet(en, false); }   // Follow+focus -> Hidden
                                    else if (off && !focus) entFocusSet(en, true);                           // Hidden -> Hidden+focus
                                    else { ENT_SET(en, false); entFocusSet(en, false); }                     // Hidden+focus -> Follow
                                    save_ui_config();
                                }
                            }
                            const float dcx = cx + cbs * 0.5f, dcy = cby + cbs * 0.5f, dr = cbs * 0.44f;
                            if (focus && !off)      { gem(dev, dcx, dcy, dr + snap(1.0f), 0xFFFF7043u); gem(dev, dcx, dcy, dr - snap(3.0f), 0xFFFFE0C0u); }   // Focus : solid orange-red + light core
                            else if (focus && off)  { gem(dev, dcx, dcy, dr + snap(1.0f), 0xFFFF7043u); gem(dev, dcx, dcy, dr - snap(2.0f), 0xFF12181Cu); }   // Unfollow-Focus : orange-red RING
                            else if (!off)          gem(dev, dcx, dcy, dr, C_ACCENTHI);                                                                        // Follow : solid accent
                            else                    { gem(dev, dcx, dcy, dr, 0xFF3A4650u); gem(dev, dcx, dcy, dr - snap(2.0f), 0xFF12181Cu); }                 // Unfollow : hollow ring
                        }
                        // --- sub-pass B : LABELS (font must be re-bound AFTER the quads above) ---
                        fo->begin(dev);
                        for (int col = 0; col < cols; ++col) {
                            const int k = col * rpc + r; if (k >= m) continue;
                            const JobBuff& en = jb[idx[k]]; const bool off = ENT_OFF(en);
                            const bool focus = en.status && ENT_FOFF(en);
                            const float cx = x0 + col * colW;
                            fo->draw_lc(dev, cx + cbs + snap(5.0f), ty + rh * 0.5f, en.name, snap(12.0f), fa(focus ? 0xFFFFB78Au : (off ? C_MUTE : C_TEXT)), fa(C_STROKE), 1.0f);
                        }
                        ROW_NEXT(21.0f)
                    }
                }
            }
        }
        #undef ENT_APPLIES
        #undef ENT_OFF
        #undef ENT_SET
    }   // end Track per job

    ry += snap(10.0f);
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
