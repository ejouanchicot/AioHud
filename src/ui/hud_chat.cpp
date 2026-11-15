// hud_chat.cpp -- the Chat module : AioHUD's own log, as TWO boxes on the current theme.
//   box 0 = CHAT   : talk (say / shout / yell / tell / party / linkshells / unity / emote / assist), NPC, system
//   box 1 = BATTLE : the combat log
// Header : text TABS with a sliding accent underline (a count badge on a tab that received lines), the box's
// category on the right, a soft light line under it. Body : lines word-wrapped, coloured by channel and by the
// client's own colour codes, every coloured run on ONE baseline ; new lines slide in from the bottom, the wheel
// scrolls smoothly, the top edge fades ; scrolled up, a pill counts the new messages below and brings you back.
// Placed / resized like every box : drag + wheel in //aio edit, plus a corner grip for its width and line count.
// Data : model/chat_log.h. Clicks and the wheel over a box never reach the game (aiohud.cpp, UiConfig::chHit).
#include "ui/hud.h"
#include "ui/hud_internal.h"
#include "ui/text_style.h"
#include "ui/box_style.h"
#include "ui/config_controls.h"   // tr(), clip_rect_begin / end
#include "model/ui_config.h"
#include "model/chat_log.h"
#include "windower_debug.h"
#include "model/native_ui.h"   // native_input_read : the client's input text, drawn in the chat box
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include "gfx/font.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <windows.h>

namespace aio {

// ---- channel colours (readable on a dark panel) ----
static const u32 CHAN_COL[CH_COUNT] = {
    0xFFE9EEF5u,   // say
    0xFFFFB46Eu,   // shout
    0xFFFF8F6Bu,   // yell
    0xFFF2A0DAu,   // tell
    0xFF7FD3FFu,   // party
    0xFF98E38Au,   // linkshell 1
    0xFFC8EE88u,   // linkshell 2
    0xFFFFD66Eu,   // unity
    0xFFC6B6F2u,   // emote
    0xFFA9DCFFu,   // assist
    0xFFE4D6A6u,   // system
    0xFFDCE5F0u,   // NPC
    0xFFC5CFDDu,   // battle
    0xFFB8C2D0u,   // other
};
// a run's colour : the CLIENT's colour for that code (chat_log resolved it from the game's own tables), opaque ;
// unknown (0) -> the line's colour. Faint colours are lifted so they stay readable on a dark panel.
static u32 code_col(const ChatLine& L, unsigned char idx, u32 base) {
    if (idx == 0 || idx > 8) return base;
    u32 c = L.pal[idx - 1]; if (!c) return base;
    c |= 0xFF000000u;
    const int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255, mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (mx < 110) { const float k = 110.0f / (float)(mx ? mx : 1); return 0xFF000000u | ((u32)(r * k) << 16) | ((u32)(g * k) << 8) | (u32)(b * k); }
    return c;
}
static inline u32 with_alpha(u32 c, float a) {   // scale a colour's alpha (row fades)
    if (a >= 1.0f) return c; if (a <= 0.0f) return c & 0x00FFFFFFu;
    return (c & 0x00FFFFFFu) | ((u32)((float)(c >> 24) * a + 0.5f) << 24);
}
static inline u32 mix(u32 a, u32 b, float t) {    // RGB blend, alpha of a
    const int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255, br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    return (a & 0xFF000000u) | ((u32)(ar + (br - ar) * t) << 16) | ((u32)(ag + (bg - ag) * t) << 8) | (u32)(ab + (bb - ab) * t);
}

// ---- tabs : a mask of channels (+ "mentions me" for the battle log) ----
struct ChatTab { const char* en; const char* fr; unsigned mask; bool me; };
#define CB(c) (1u << (c))
static const unsigned ALL_TALK = CB(CH_SAY) | CB(CH_SHOUT) | CB(CH_YELL) | CB(CH_TELL) | CB(CH_PARTY) | CB(CH_LS1) | CB(CH_LS2) |
                                 CB(CH_UNITY) | CB(CH_EMOTE) | CB(CH_ASSIST) | CB(CH_SYSTEM) | CB(CH_NPC) | CB(CH_OTHER);
static const ChatTab TABS_CHAT[] = {
    { "All",    "Tout",              ALL_TALK, false },
    { "Party",  "Groupe",            CB(CH_PARTY) | CB(CH_ASSIST), false },
    { "LS",     "LS",                CB(CH_LS1) | CB(CH_LS2), false },
    { "Tell",   "Tell",              CB(CH_TELL), false },
    { "Say",    "Dire",              CB(CH_SAY) | CB(CH_SHOUT) | CB(CH_YELL) | CB(CH_EMOTE) | CB(CH_UNITY), false },
    { "System", "Syst\xC3\xA8me",    CB(CH_SYSTEM) | CB(CH_NPC) | CB(CH_OTHER), false },
};
static const ChatTab TABS_BATTLE[] = {
    { "All", "Tout", CB(CH_BATTLE), false },
    { "Me",  "Moi",  CB(CH_BATTLE), true  },
};
#undef CB
static const int TAB_N[2] = { (int)(sizeof(TABS_CHAT) / sizeof(TABS_CHAT[0])), (int)(sizeof(TABS_BATTLE) / sizeof(TABS_BATTLE[0])) };
static const ChatTab& tab_def(int box, int t) { return box == 0 ? TABS_CHAT[t] : TABS_BATTLE[t]; }

// ---- per-box runtime state (never saved) ----
struct ChatBoxState {
    int      scroll = 0;            // TARGET : whole rows scrolled up from the newest
    float    vpos = 0.0f;           // DRAWN scroll (rows, fractional) -- eases toward `scroll`
    unsigned lastSeq = 0;           // newest seq already accounted for (new rows slide in / count below)
    int      lastTab = -1;
    unsigned lastMs = 0;
    int      newBelow = 0;          // messages that arrived in this tab while scrolled up
    unsigned scanned[8] = {0};      // per tab : lines up to this seq already counted for its badge
    int      unread[8] = {0};       // per tab : messages that arrived since it was last shown
    bool     init = false;
    float    ulX = -1.0f, ulW = 0.0f;   // the active tab's underline, eased (box-relative px)
    float    hov[8] = {0};          // per tab : eased hover 0..1
    EditBox  eb;                    // //aio edit drag
    bool     resizing = false;      // corner grip held
    float    inA = 0.0f;            // the input field's open amount (eased 0..1)
    float    inScroll = 0.0f;       // its horizontal scroll (px) -- keeps the caret in view
    int      inFail = 0;            // consecutive unreadable frames (3 -> fall back to the docked client line)
    const Font* advFont = 0; float advSize = 0.0f; float adv[256];   // per-byte advance cache (face + size) ; >= 0xC0 = Latin-1
    int      grip = 0;              // token for the shared drag lock
};
static ChatBoxState g_cb[2];

static bool mentions(const char* s, const char* me) {
    if (me && me[0] && strstr(s, me)) return true;
    return !strncmp(s, "You ", 4) || !strncmp(s, "Your ", 5);
}
// "mentions me" is asked of the same lines every frame (the Me tab, its badge) : answered ONCE per line, keyed by
// its seq (a slot is reused only after CHAT_HIST newer lines) and by the name (a character change re-asks).
static unsigned      g_meSeq[CHAT_HIST];
static unsigned char g_meVal[CHAT_HIST];
static unsigned      g_meName = 0;
static unsigned name_hash(const char* s) { unsigned h = 2166136261u; for (; s && *s; ++s) h = (h ^ (unsigned char)*s) * 16777619u; return h; }
static bool line_in_tab(const ChatLine& L, const ChatTab& t, const char* me) {
    if (!((1u << L.chan) & t.mask)) return false;
    if (!t.me) return true;
    // samples carry seq 1..8 : seq <= 16 is never cached (a live line that low just pays the direct test once)
    const unsigned slot = L.seq & (CHAT_HIST - 1);
    if (L.seq > 16u && g_meSeq[slot] == L.seq) return g_meVal[slot] != 0;
    const bool v = mentions(L.text, me);
    if (L.seq > 16u) { g_meSeq[slot] = L.seq; g_meVal[slot] = v ? 1 : 0; }
    return v;
}

// ---- sample lines (config preview, //aio edit with an empty log) : fictional names only ----
static ChatLine g_sample[2][8]; static int g_sampleN[2] = { 0, 0 };
static void sample_add(int box, int chan, const char* s, unsigned short hhmm, const char* hl = 0, unsigned char hlCol = 0) {
    if (g_sampleN[box] >= 8) return;
    ChatLine& L = g_sample[box][g_sampleN[box]++];
    memset(&L, 0, sizeof(L)); L.seq = (unsigned)g_sampleN[box]; L.chan = (unsigned char)chan; L.hhmm = hhmm;
    int n = 0; for (; s[n] && n < CHAT_TEXT - 1; ++n) L.text[n] = s[n]; L.text[n] = 0; L.len = (unsigned char)n;
    if (hl) {   // one highlighted run, in a fixed sample colour
        static const u32 SAMPLE_COL[3] = { 0xFF8EE88Eu, 0xFFFFE08Au, 0xFFFF9C8Cu };
        L.pal[0] = SAMPLE_COL[hlCol % 3];
        const char* at = strstr(L.text, hl); if (at) for (int k = (int)(at - L.text), e = k + (int)strlen(hl); k < e; ++k) L.col[k] = 1;
    }
}
static void build_samples() {
    if (g_sampleN[0]) return;
    sample_add(0, CH_SYSTEM, "Welcome to Vana'diel.", 2100);
    sample_add(0, CH_PARTY,  "(PlayerName1) Ready when you are!", 2101);
    sample_add(0, CH_LS1,    "<PlayerName2> Anyone up for a run tonight?", 2102);
    sample_add(0, CH_TELL,   "PlayerName3>> Thanks for the help earlier :)", 2103);
    sample_add(0, CH_SYSTEM, "PlayerName1 obtains a Riftcinder.", 2104, "Riftcinder", 2);
    sample_add(0, CH_NPC,    "Shop Clerk : Welcome! Take your time looking around.", 2105);
    sample_add(0, CH_PARTY,  "(PlayerName2) Pulling in 5, buffs up please.", 2106);
    sample_add(1, CH_BATTLE, "PlayerName1 hits the Apex Bat for 1234 points of damage.", 2110, "Apex Bat", 0x80 | CH_BATTLE);
    sample_add(1, CH_BATTLE, "The Apex Bat misses PlayerName2.", 2110, "Apex Bat", 0x80 | CH_BATTLE);
    sample_add(1, CH_BATTLE, "PlayerName1 uses Savage Blade.", 2111, "Savage Blade", 6);
    sample_add(1, CH_BATTLE, "Skillchain: Light. The Apex Bat takes 15420 points of damage.", 2111, "Skillchain: Light.", 6);
    sample_add(1, CH_BATTLE, "PlayerName3 casts Cure V. PlayerName1 recovers 1080 HP.", 2112);
    sample_add(1, CH_BATTLE, "PlayerName1 defeats the Apex Bat.", 2112, "Apex Bat", 0x80 | CH_BATTLE);
}

// the history the box draws : the live log, or the samples (preview / an empty log while editing)
struct ChatSrc { bool sample; int box; int n() const { return sample ? g_sampleN[box] : chat_count(); }
                 const ChatLine* at(int i) const { return sample ? &g_sample[box][i] : chat_line(i); } };

void chat_hit_clear() {
    UiConfig& c = ui_config();
    for (int b = 0; b < 2; ++b) for (int k = 0; k < 4; ++k) c.chHit[b][k] = 0.0f;
}

// Word-wrap one history line into spans [sa, sb) that fit `avail` px (a word longer than a row breaks where it must).
static int wrap_line(const ChatLine& L, const float* adv, float avail, short* sa, short* sb, int cap) {
    int ns = 0, a = 0;
    while (a < L.len && ns < cap) {
        float w = 0.0f; int b = a, lastSp = -1;
        while (b < L.len) {
            const unsigned char ch = (unsigned char)L.text[b];
            const float cw = adv[ch];
            if (w + cw > avail && b > a) break;
            if (ch == ' ') lastSp = b;
            w += cw; ++b;
        }
        if (b < L.len && lastSp > a) b = lastSp + 1;                       // break after the last space that fits
        sa[ns] = (short)a; sb[ns] = (short)b; ++ns; a = b;
    }
    return ns;
}

// The client's input text (native_input_read) -> printable chars + colours + caret index. 7F FF = caret, 7F FE lead,
// 7F 69 .. 7F 64 = an auto-translate word (shown in braces), 7F 66 = the Tab candidate (dimmed), 85 xx = accents.
// A leading channel command (/p, /l, /t Name ...) takes that channel's colour.
struct InLine { char t[0x170]; u32 c[0x170]; int n; int caret; };
static void input_decode(const char* raw, int rn, InLine& o) {
    o.n = 0; o.caret = -1;
    bool at = false, cand = false;
    for (int i = 0; i < rn && o.n < 0x16F; ++i) {
        const unsigned char ch = (unsigned char)raw[i];
        if (ch == 0x7F && i + 1 < rn) {
            const unsigned char m = (unsigned char)raw[++i];
            if (m == 0xFF) o.caret = o.n;
            else if (m == 0x69) { at = true; o.t[o.n] = '{'; o.c[o.n++] = 0xFF8EE88Eu; }
            else if (m == 0x64) { if (at) { o.t[o.n] = '}'; o.c[o.n++] = 0xFF8EE88Eu; } at = false; cand = false; }
            else if (m == 0x66) cand = !cand;
            continue;
        }
        unsigned char out = ch;
        if (ch == 0x85 && i + 1 < rn && (unsigned char)raw[i + 1] >= 0x9F && (unsigned char)raw[i + 1] <= 0xDE) { out = (unsigned char)((unsigned char)raw[++i] + 0x21); }
        else if (((ch >= 0x81 && ch <= 0x9F) || (ch >= 0xE0 && ch <= 0xFC)) && i + 1 < rn) { ++i; out = '?'; }
        else if (ch < 0x20 || ch == 0x7F) continue;
        else if (ch >= 0x80 && ch < 0xC0) out = '?';
        o.t[o.n] = (char)out; o.c[o.n++] = at ? 0xFF8EE88Eu : (cand ? 0xFF8A96A8u : 0xFFF2F5F9u);
    }
    // the getter ends with the caret then ONE space (every live sample) : that space is not the player's
    if (o.caret == o.n - 1 && o.n > 0 && o.t[o.n - 1] == ' ') --o.n;
    if (o.caret < 0 || o.caret > o.n) o.caret = o.n;
    o.t[o.n] = 0;
    // channel command at the start : its colour on the command (and on the name, for a tell)
    static const struct { const char* cmd; int chan; } PFX[] = {
        { "/p ", CH_PARTY }, { "/party ", CH_PARTY }, { "/l2 ", CH_LS2 }, { "/linkshell2 ", CH_LS2 }, { "/l ", CH_LS1 },
        { "/linkshell ", CH_LS1 }, { "/t ", CH_TELL }, { "/tell ", CH_TELL }, { "/sh ", CH_SHOUT }, { "/shout ", CH_SHOUT },
        { "/s ", CH_SAY }, { "/say ", CH_SAY }, { "/y ", CH_YELL }, { "/yell ", CH_YELL }, { "/u ", CH_UNITY },
        { "/unity ", CH_UNITY }, { "/em ", CH_EMOTE }, { "/emote ", CH_EMOTE } };
    for (int k = 0; k < (int)(sizeof(PFX) / sizeof(PFX[0])); ++k) {
        const int L = (int)strlen(PFX[k].cmd);
        if (o.n >= L && !_strnicmp(o.t, PFX[k].cmd, L)) {
            int e = L;
            if (PFX[k].chan == CH_TELL) { while (e < o.n && o.t[e] != ' ') ++e; }   // + the name
            for (int j = 0; j < e; ++j) o.c[j] = CHAN_COL[PFX[k].chan];
            break;
        }
    }
}

// The input line's channel (handler+0x2C) -> our channel. Measured live 2026-11-15 : the order the values were first
// seen matched the order the player picked them (Say Tell Party LS LS2 Unity Shout) -> 2 say, 4 tell, 5 party,
// 6 linkshell, 12 linkshell 2, 11 unity, 3 shout. Any other value is logged the first time it is seen.
static int input_mode_chan(int m) {
    static unsigned char seen[32];
    if (m >= 0 && m < 32 && !seen[m]) { seen[m] = 1; windower::debug::log("CHATINPUT channel value %d seen", m); }
    switch (m) {
        case 2: return CH_SAY;   case 3: return CH_SHOUT; case 4: return CH_TELL;  case 5: return CH_PARTY;
        case 6: return CH_LS1;   case 11: return CH_UNITY; case 12: return CH_LS2;
        default: return -1;
    }
}
static const char* chan_label(int ch) {
    switch (ch) {
        case CH_SAY: return tr("Say", "Dire");       case CH_SHOUT: return tr("Shout", "Crier");  case CH_TELL: return "Tell";
        case CH_PARTY: return tr("Party", "Groupe"); case CH_LS1: return "LS";                     case CH_LS2: return "LS2";
        case CH_UNITY: return "Unity";               default: return 0;
    }
}

// one wrapped row of a history line : chars [a, b), `first` = the message's first row (carries the time stamp)
struct ChatRow { const ChatLine* L; short a, b; bool first; };

void chat_box_draw(const Frame& f, int box, bool preview, bool interactive, float ovX, float ovY, float ovS) {
    UiConfig& C = ui_config();
    if (box < 0 || box > 1 || !C.chShow[box]) return;
    if (!f.font && !f.fonts) return;
    const u32 dev = f.dev;
    const bool editing = C.editLayout && !preview;
    ChatBoxState& st = g_cb[box];
    build_samples();
    const ChatSrc src = { preview || (editing && chat_count() == 0), box };
    if (!st.init) { st.init = true; for (int t = 0; t < 8; ++t) st.scanned[t] = chat_newest_seq(); st.lastSeq = chat_newest_seq(); }

    // ---- time step (eases are frame-rate independent) ----
    const unsigned nowMs = GetTickCount();
    float dt = st.lastMs ? (float)(nowMs - st.lastMs) / 1000.0f : 0.0f; st.lastMs = nowMs;
    if (dt > 0.1f) dt = 0.1f;
    const float kFast = 1.0f - expf(-dt * 18.0f), kSoft = 1.0f - expf(-dt * 12.0f);

    // ---- metrics ----
    float scl = C.chScale[box]; if (scl < 0.5f) scl = 0.5f; if (scl > 2.0f) scl = 2.0f;
    const float S = (ovS > 0.0f) ? ovS : (f.screenH / 1000.0f) * scl;
    const TextStyle& TS = C.chText[box];
    Font* fo = te_font(f, TS); if (!fo) return;
    const float tz = te_sz(TS, 15.0f) * S, tow = te_ow(TS, 1.1f) * S;
    const float rowH = snap(tz * 1.38f);
    const float pad = snap(10.0f * S), headH = snap(26.0f * S), headGap = snap(6.0f * S);
    const float tabZ = 13.5f * S, titleZ = 11.0f * S, badgeZ = 9.5f * S;
    int lines = C.chLines[box]; if (lines < 3) lines = 3; if (lines > 40) lines = 40;
    const float boxW = snap((float)C.chW[box] * S);
    const float bodyY0 = pad + headH + headGap;
    // ---- the client's input line, drawn HERE (UiConfig::chDock == 2) : read it the way the client's draw does ----
    static char s_inRaw[0x170]; static InLine s_in; static int s_inMode = -1;
    int inN = -1;
    if (box == 0 && !preview && C.chDock == 2) {
        inN = native_input_read(s_inRaw, 0x168, &s_inMode);
        if (inN == -2) { if (++st.inFail >= 3) C.chInputOk = 0; }      // 3 frames unreadable : the client's own line comes back
        else { st.inFail = 0; if (inN >= 0) C.chInputOk = 1; }
        if (inN >= 0) input_decode(s_inRaw, inN, s_in);
    }
    const bool typing = inN >= 0;
    st.inA += ((typing ? 1.0f : 0.0f) - st.inA) * kSoft; if (st.inA < 0.003f) st.inA = 0.0f; if (st.inA > 0.997f) st.inA = 1.0f;
    const float inH = snap(rowH + 12.0f * S);
    const float boxH = snap(bodyY0 + lines * rowH + pad * 0.8f + inH * st.inA);

    // ---- position ----
    float px, py;
    if (ovS > 0.0f) { px = snap(ovX - boxW * 0.5f); py = snap(ovY - boxH * 0.5f); }
    else            { px = snap(C.chX[box] * f.screenW); py = snap(C.chY[box] * f.screenH); }
    const MouseState* m = f.mouse;
    if (editing) {
        // the CORNER GRIP first : it takes the shared drag lock, so box_edit cannot grab the same click
        const float g = 16.0f * S, gx = px + boxW - g, gy = py + boxH - g;
        const bool overGrip = m && m->x >= gx && m->x < px + boxW && m->y >= gy && m->y < py + boxH;
        if (m && m->clicked && overGrip && !st.resizing && edit_drag_grab(&st.grip)) st.resizing = true;
        if (st.resizing) {
            if (m && m->down) {
                int w = (int)((m->x - px) / S + 0.5f); C.chW[box] = w < 200 ? 200 : (w > 1600 ? 1600 : w);
                int ln = (int)((m->y - py - bodyY0 - pad * 0.8f) / rowH + 0.5f); C.chLines[box] = ln < 3 ? 3 : (ln > 40 ? 40 : ln);
            } else { st.resizing = false; edit_drag_release(&st.grip); save_ui_config(); }
        }
        if (!st.resizing) box_edit(f, st.eb, EDITBOX_CHAT + box, px, py, boxW, boxH, C.chScale[box], C.chX[box], C.chY[box], 0);
    } else if (ovS <= 0.0f) box_on_screen(f, px, py, boxW, boxH);

    // the rect the mouse callback tests (live boxes only)
    if (!preview && f.screenW > 0.0f && f.screenH > 0.0f) {
        C.chHit[box][0] = px / f.screenW; C.chHit[box][1] = py / f.screenH;
        C.chHit[box][2] = boxW / f.screenW; C.chHit[box][3] = boxH / f.screenH;
        if (box == 0 && !editing) {   // the game's input line docks under this box (native_ui.cpp) ; not while dragging
            C.chDockRect[0] = px; C.chDockRect[1] = py; C.chDockRect[2] = boxW; C.chDockRect[3] = boxH;
            C.chDockRect[4] = f.screenW; C.chDockRect[5] = f.screenH;
        }
    }

    // ---- chrome ----
    dColorQuadState(dev);
    draw_themed_box(dev, f.skin, px, py, boxW, boxH, C.chBox[box], 1.0f, S);
    const u32 accent = box_style_border_color(dev, f.skin, C.chBox[box]) | 0xFF000000u;
    const u32 accentLite = mix(accent, 0xFFFFFFFFu, 0.35f);

    const bool canClick = interactive && !editing && !preview && m && m->focused;
    const char* me = (f.game && f.game->me.name[0]) ? f.game->me.name : "";
    { const unsigned h = name_hash(me); if (h != g_meName) { g_meName = h; memset(g_meSeq, 0, sizeof(g_meSeq)); } }
    const int nTabs = TAB_N[box];
    int tab = C.chTab[box]; if (tab < 0 || tab >= nTabs) tab = 0;
    const unsigned newest = src.sample ? 0u : chat_newest_seq();

    // ---- badges : count each new message ONCE for every tab it belongs to (continuation lines don't count) ----
    if (!src.sample) {
        for (int t = 0; t < nTabs; ++t) {
            const ChatTab& T = tab_def(box, t);
            for (int i = src.n() - 1; i >= 0; --i) {
                const ChatLine* L = src.at(i); if (!L || L->seq <= st.scanned[t]) break;
                // a badge counts what the CURRENT tab does not show : on "All", nothing is unseen
                if (!L->cont && t != tab && line_in_tab(*L, T, me) && !line_in_tab(*L, tab_def(box, tab), me)) ++st.unread[t];
            }
            st.scanned[t] = newest;
        }
    }

    // ---- header : category on the right ----
    const float headY = py + pad, headCY = headY + headH * 0.5f;
    const char* title = (box == 0) ? tr("CHAT", "DISCUSSION") : tr("BATTLE", "COMBAT");
    const float titleW = fo->measure(title, titleZ);
    {   // just the word, quiet : no ornament beside it
        const float tx = px + boxW - pad - titleW;
        fo->begin(dev);
        fo->draw_lc(dev, tx, headCY - 1.0f * S, title, titleZ, 0xFF7C889Au, 0xC0000000u, tow * 0.8f);
    }

    // ---- header : tabs (text + sliding underline + count badge) ----
    const float tabsRight = px + boxW - pad - titleW - 20.0f * S;
    float tx = px + pad;
    float actX = -1.0f, actW = 0.0f;
    for (int t = 0; t < nTabs; ++t) {
        const ChatTab& T = tab_def(box, t);
        const char* lbl = tr(T.en, T.fr);
        const float lw = fo->measure(lbl, tabZ);
        char bt[8] = ""; float bw = 0.0f; const float bh = snap(14.0f * S);
        if (t != tab && st.unread[t] > 0) {
            if (st.unread[t] > 99) lstrcpynA(bt, "99+", sizeof(bt)); else _snprintf(bt, sizeof(bt), "%d", st.unread[t]);
            bt[sizeof(bt) - 1] = 0;
            bw = snap(fo->measure(bt, badgeZ) + 8.0f * S); if (bw < bh) bw = bh;
        }
        const float tw = lw + (bw > 0.0f ? bw + 5.0f * S : 0.0f);
        if (tx + tw > tabsRight) break;                                     // a narrow box shows the tabs that fit
        const bool active = t == tab;
        const bool hover = canClick && m->x >= tx - 6.0f * S && m->x < tx + tw + 6.0f * S && m->y >= headY && m->y < headY + headH;
        if (hover && m->clicked && !active) { C.chTab[box] = t; tab = t; st.unread[t] = 0; save_ui_config(); }
        st.hov[t] += ((hover ? 1.0f : 0.0f) - st.hov[t]) * kFast;
        const u32 txt = (t == tab) ? 0xFFFFFFFFu : mix(0xFF8794A8u, 0xFFDDE3EBu, st.hov[t]);
        fo->begin(dev);
        fo->draw_lc(dev, tx, headCY - 1.0f * S, lbl, tabZ, txt, 0xD0000000u, tow);
        if (bw > 0.0f) {                                                    // the count badge
            const float bx = snap(tx + lw + 5.0f * S), by = snap(headCY - 1.0f * S - bh * 0.5f);
            dColorQuadState(dev);
            rrect_glow(dev, bx, by, bw, bh, bh * 0.5f, (accent & 0x00FFFFFFu) | 0x50000000u, 3.0f * S);
            rrect(dev, bx, by, bw, bh, bh * 0.5f, accentLite, accent);
            fo->begin(dev);
            fo->draw_cc(dev, bx + bw * 0.5f, by + bh * 0.5f, bt, badgeZ, 0xFFFFFFFFu, 0x90000000u, tow * 0.7f);
        }
        if (t == tab) { actX = tx - px; actW = lw; }
        tx += tw + 16.0f * S;
    }
    // the light line under the header, and the active tab's underline sliding to it
    const float lineY = snap(headY + headH + headGap * 0.5f);
    dColorQuadState(dev);
    hbar_soft(dev, px + pad, lineY, boxW - 2.0f * pad, 1.0f, accent, 0x60, 0.18f);
    if (actX >= 0.0f) {
        if (st.ulX < 0.0f) { st.ulX = actX; st.ulW = actW; }
        st.ulX += (actX - st.ulX) * kSoft; st.ulW += (actW - st.ulW) * kSoft;
        const float ux = snap(px + st.ulX), uw = snap(st.ulW), uh = snap(2.0f * S) < 2.0f ? 2.0f : snap(2.0f * S);
        rrect_glow(dev, ux, lineY - uh * 0.5f, uw, uh, uh * 0.5f, (accent & 0x00FFFFFFu) | 0x70000000u, 4.0f * S);
        rrect(dev, ux, lineY - uh * 0.5f, uw, uh, uh * 0.5f, accentLite, accent, 1.0f);
    }
    if (!src.sample) st.unread[tab] = 0;

    // ---- advance table for this font + size : wrapping and run placement without a measure() per char ----
    if (st.advFont != fo || st.advSize != tz) {                           // rebuilt only when the face / size changes
        st.advFont = fo; st.advSize = tz;
        char u[4] = { 0, 0, 0, 0 };
        for (int ch = 0; ch < 256; ++ch) {
            if (ch < 0x80) { u[0] = (char)(ch < 32 ? ' ' : ch); u[1] = 0; }
            else if (ch >= 0xC0) { u[0] = (char)(0xC0 | (ch >> 6)); u[1] = (char)(0x80 | (ch & 0x3F)); u[2] = 0; }   // Latin-1 -> UTF-8
            else { u[0] = '?'; u[1] = 0; }
            st.adv[ch] = fo->measure(u, tz);
        }
    }
    const float* adv = st.adv;
    char stampFmt[12] = "00:00";
    const float stampW = C.chStamp[box] ? fo->measure("00:00", tz * 0.86f) + 9.0f * S : 0.0f;
    const float avail = boxW - 2.0f * pad - 2.0f * S - stampW;            // wrapped rows align under the text, after the stamp
    const ChatTab& T = tab_def(box, tab);

    // ---- scrolling : st.scroll = the TARGET (whole rows up from the newest), st.vpos = what is drawn ----
    if (st.lastTab != tab) { st.lastTab = tab; st.scroll = 0; st.vpos = 0.0f; st.newBelow = 0; st.lastSeq = newest; }
    if (!src.sample && newest != st.lastSeq) {
        // rows the new lines add to THIS tab : at the bottom they slide in from below ; scrolled up, the view holds
        int add = 0, msgs = 0; short sa[24], sb[24];
        for (int i = src.n() - 1; i >= 0; --i) {
            const ChatLine* L = src.at(i); if (!L || L->seq <= st.lastSeq) break;
            if (line_in_tab(*L, T, me)) { add += wrap_line(*L, adv, avail, sa, sb, 24); if (!L->cont) ++msgs; }
        }
        st.lastSeq = newest;
        if (add > 0) {
            st.vpos += (float)add;
            if (st.scroll > 0) { st.scroll += add; st.newBelow += msgs; }
        }
        if (st.vpos > (float)(st.scroll + lines * 2)) st.vpos = (float)(st.scroll + lines * 2);   // a burst : don't crawl
    }
    if (!preview) {
        if (C.chWheel[box] != 0) { st.scroll += C.chWheel[box] * 3; C.chWheel[box] = 0; }
        if (st.scroll < 0) st.scroll = 0;
        if (st.scroll == 0) st.newBelow = 0;
    } else { st.scroll = 0; st.vpos = 0.0f; }
    st.vpos += ((float)st.scroll - st.vpos) * kSoft;
    if (fabsf((float)st.scroll - st.vpos) < 0.01f) st.vpos = (float)st.scroll;

    // ---- collect the visible rows, newest first (one extra : the row sliding through the top) ----
    const int want = lines + 1;
    ChatRow rows[48]; int nr = 0;
    int skip = (int)st.vpos; const float frac = st.vpos - (float)skip;
    int skipLeft = st.scroll;                                               // to clamp the TARGET to the history
    for (int i = src.n() - 1; i >= 0 && (nr < want || skipLeft > 0); --i) {
        const ChatLine* L = src.at(i); if (!L || !line_in_tab(*L, T, me)) continue;
        short sa[24], sb[24]; const int ns = wrap_line(*L, adv, avail, sa, sb, 24);
        for (int k = ns - 1; k >= 0; --k) {
            if (skipLeft > 0) --skipLeft;
            if (skip > 0) { --skip; continue; }
            if (nr < want) { ChatRow& R = rows[nr++]; R.L = L; R.a = sa[k]; R.b = sb[k]; R.first = (k == 0) && !L->cont; }
        }
    }
    if (skipLeft > 0 && !preview) { st.scroll -= skipLeft; if (st.scroll < 0) st.scroll = 0; }   // past the oldest line

    // ---- draw the rows, bottom up : ONE baseline for every coloured run, top edge fading, clipped to the body ----
    const float bodyTop = py + bodyY0, bodyH = lines * rowH, bodyBot = bodyTop + bodyH;
    const float x0 = px + pad + 1.0f * S;
    float refT = 0.0f, refB = 0.0f; fo->ink_v("Hg", tz, refT, refB);
    const float yOff = (refT + refB) * 0.5f;                                // cell top -> visual centre, the same for every run
    float sT = 0.0f, sB = 0.0f; fo->ink_v("00:00", tz * 0.86f, sT, sB);
    const float sOff = (sT + sB) * 0.5f;
    float cy = bodyTop + (lines - 0.5f) * rowH + frac * rowH;
    const float fadeH = rowH * 1.1f;
    char buf[CHAT_TEXT * 2 + 1];
    clip_rect_begin(dev, px + 2.0f * S, bodyTop, boxW - 4.0f * S, bodyH);
    fo->begin(dev);
    if (nr == 0)
        fo->draw_cc(dev, px + boxW * 0.5f, bodyTop + bodyH * 0.5f, tr("No messages yet", "Aucun message pour l'instant"), tz * 0.92f, 0xFF6E7A8Cu, 0xC0000000u, tow);
    for (int r = 0; r < nr; ++r, cy -= rowH) {
        const ChatRow& R = rows[r]; const ChatLine& L = *R.L;
        const float rTop = cy - rowH * 0.5f;
        float a = (rTop - bodyTop) / fadeH + 0.35f; if (a > 1.0f) a = 1.0f;   // the top edge fades out
        const float aBot = (bodyBot - rTop) / rowH; if (aBot < a) a = aBot;    // a row sliding in fades in
        if (a <= 0.02f) continue;
        const u32 base = te_col(TS, L.modeArgb ? (L.modeArgb | 0xFF000000u) : CHAN_COL[L.chan < CH_COUNT ? L.chan : CH_OTHER]);
        const u32 stroke = with_alpha(0xE0000000u, a);
        if (C.chStamp[box] && R.first) {
            _snprintf(stampFmt, sizeof(stampFmt), "%02u:%02u", L.hhmm / 100, L.hhmm % 100); stampFmt[sizeof(stampFmt) - 1] = 0;
            fo->draw(dev, x0, cy - sOff, stampFmt, tz * 0.86f, with_alpha(0xFF5F6B7Eu, a), stroke, tow * 0.8f);
        }
        float x = x0 + stampW;
        int i = R.a;                                                        // runs of one colour, placed by the advance sum
        while (i < R.b) {
            const unsigned char cc = L.col[i]; int j = i, n = 0; float w = 0.0f;
            while (j < R.b && L.col[j] == cc && n < CHAT_TEXT * 2 - 2) {
                const unsigned char ch = (unsigned char)L.text[j++]; w += adv[ch];
                if (ch >= 0xC0) { buf[n++] = (char)(0xC0 | (ch >> 6)); buf[n++] = (char)(0x80 | (ch & 0x3F)); }   // Latin-1 -> UTF-8
                else buf[n++] = (char)ch;
            }
            buf[n] = 0;
            if (n) fo->draw(dev, x, cy - yOff, buf, tz, with_alpha(code_col(L, cc, base), a), stroke, tow);
            x += w; i = j;
        }
    }
    clip_rect_end(dev);

    // ---- the input field (the client's text, live) ----
    if (st.inA > 0.0f) {
        const float a = st.inA;
        const float fx = px + pad, fw = boxW - 2.0f * pad, fh = inH - 6.0f * S;
        const float fy = bodyBot + 3.0f * S + (1.0f - a) * 6.0f * S;
        dColorQuadState(dev);
        rrect(dev, snap(fx), snap(fy), snap(fw), snap(fh), 6.0f * S, with_alpha(0x8C0A0D12u, a), with_alpha(0x8C10141Bu, a));
        rrect_stroke(dev, snap(fx), snap(fy), snap(fw), snap(fh), 6.0f * S, with_alpha((accent & 0x00FFFFFFu) | 0x90000000u, a), 1.0f);
        // the channel the line will speak in : a chip on the left, in that channel's colour
        const int inCh = input_mode_chan(s_inMode); const char* chLbl = inCh >= 0 ? chan_label(inCh) : 0;
        float chipW = 0.0f;
        if (chLbl) {
            const float cz = 11.5f * S, chh = snap(fh - 8.0f * S); chipW = snap(fo->measure(chLbl, cz) + 14.0f * S);
            const float cxp = snap(fx + 5.0f * S), cyp = snap(fy + (fh - chh) * 0.5f);
            const u32 cc = CHAN_COL[inCh];
            dColorQuadState(dev);
            rrect(dev, cxp, cyp, chipW, chh, chh * 0.5f, with_alpha((cc & 0x00FFFFFFu) | 0x38000000u, a), with_alpha((cc & 0x00FFFFFFu) | 0x24000000u, a));
            rrect_stroke(dev, cxp, cyp, chipW, chh, chh * 0.5f, with_alpha((cc & 0x00FFFFFFu) | 0x90000000u, a), 1.0f);
            fo->begin(dev);
            fo->draw_cc(dev, cxp + chipW * 0.5f, cyp + chh * 0.5f, chLbl, cz, with_alpha(cc, a), with_alpha(0xC0000000u, a), tow * 0.8f);
            chipW += 6.0f * S;
        }
        const float tx0 = fx + 9.0f * S + chipW, tcy = fy + fh * 0.5f, tw = fw - 18.0f * S - chipW;
        const InLine& L = s_in;
        float caretX = 0.0f; for (int i = 0; i < L.caret && i < L.n; ++i) caretX += adv[(unsigned char)L.t[i]];
        if (caretX - st.inScroll > tw - 4.0f * S) st.inScroll = caretX - (tw - 4.0f * S);   // keep the caret in view
        if (caretX - st.inScroll < 0.0f) st.inScroll = caretX;
        if (st.inScroll < 0.0f) st.inScroll = 0.0f;
        clip_rect_begin(dev, tx0 - 1.0f * S, fy, tw + 2.0f * S, fh);
        fo->begin(dev);
        if (typing && L.n == 0)
            fo->draw(dev, tx0, tcy - yOff, tr("Type a message...", "Ã""cris un message..."), tz, with_alpha(0xFF5F6B7Eu, a), with_alpha(0xC0000000u, a), tow);
        float x = tx0 - st.inScroll;
        char ub[0x170 * 2 + 1];
        for (int i = 0; i < L.n; ) {                                        // runs of one colour, Latin-1 -> UTF-8
            const u32 col = L.c[i]; int j = i, k = 0; float w = 0.0f;
            while (j < L.n && L.c[j] == col) { const unsigned char ch = (unsigned char)L.t[j++]; w += adv[ch];
                if (ch >= 0xC0) { ub[k++] = (char)(0xC0 | (ch >> 6)); ub[k++] = (char)(0x80 | (ch & 0x3F)); } else ub[k++] = (char)ch; }
            ub[k] = 0;
            if (x + w > tx0 - 2.0f && x < tx0 + tw + 2.0f) fo->draw(dev, x, tcy - yOff, ub, tz, with_alpha(col, a), with_alpha(0xD0000000u, a), tow);
            x += w; i = j;
        }
        clip_rect_end(dev);
        if (typing && ((GetTickCount() / 530u) & 1u) == 0) {               // the caret, blinking like the client's
            const float cx = snap(tx0 - st.inScroll + caretX), ch = snap(tz * 1.05f);
            dColorQuadState(dev);
            rrect(dev, cx, snap(tcy - ch * 0.5f), snap(2.0f * S) < 2.0f ? 2.0f : snap(2.0f * S), ch, 0.0f, accentLite, accentLite, 0.0f);
        }
    } else st.inScroll = 0.0f;

    // ---- scrolled up : a pill counts what arrived below and brings the box back to the newest line ----
    if (st.scroll > 0 && !preview) {
        char lb[48];
        if (st.newBelow > 0) _snprintf(lb, sizeof(lb), st.newBelow == 1 ? tr("1 new message", "1 nouveau message") : tr("%d new messages", "%d nouveaux messages"), st.newBelow);
        else lstrcpynA(lb, tr("Back to latest", "Revenir en bas"), sizeof(lb));
        lb[sizeof(lb) - 1] = 0;
        const float pz = 12.0f * S, ph = snap(22.0f * S), chev = 7.0f * S;
        const float pw = snap(fo->measure(lb, pz) + chev + 26.0f * S);
        const float pxx = snap(px + (boxW - pw) * 0.5f), pyy = snap(bodyBot - ph - 4.0f * S);
        const bool hov = canClick && m->x >= pxx && m->x < pxx + pw && m->y >= pyy && m->y < pyy + ph;
        if (hov && m->clicked) { st.scroll = 0; st.newBelow = 0; }
        dColorQuadState(dev);
        rrect_glow(dev, pxx, pyy, pw, ph, ph * 0.5f, (accent & 0x00FFFFFFu) | (hov ? 0x90000000u : 0x60000000u), 6.0f * S);
        rrect(dev, pxx, pyy, pw, ph, ph * 0.5f, hov ? accentLite : mix(accent, 0xFFFFFFFFu, 0.15f), mix(accent, 0xFF000000u, 0.25f));
        const float cx0 = pxx + 12.0f * S, ccy = pyy + ph * 0.5f + 1.0f * S;   // the chevron : pointing down
        fill_tri(dev, cx0, ccy - chev * 0.35f, cx0 + chev, ccy - chev * 0.35f, cx0 + chev * 0.5f, ccy + chev * 0.35f, 0xFFFFFFFFu);
        fo->begin(dev);
        fo->draw_lc(dev, cx0 + chev + 7.0f * S, pyy + ph * 0.5f, lb, pz, 0xFFFFFFFFu, 0x90000000u, tow * 0.8f);
    }

    // ---- //aio edit : the corner grip (three diagonal strokes) ----
    if (editing) {
        const float ex = px + boxW - 5.0f * S, ey = py + boxH - 5.0f * S;
        dColorQuadState(dev);
        for (int k = 1; k <= 3; ++k) {
            const float o = (float)k * 4.0f * S;
            seg_soft(dev, ex - o, ey, ex, ey - o, 1.4f * S, st.resizing ? accentLite : accent);
        }
    }
}

} // namespace aio
