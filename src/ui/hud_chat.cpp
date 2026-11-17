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
#include "gfx/texture.h"
#include "ui/tex_retry.h"     // bounded lazy load of the tab icon strip (rule 10)
#include "model/paths.h"      // plugin_path
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
    if (idx == L.atIdx) return ui_config().chAtColor | 0xFF000000u;          // the auto-translated words : the player's colour (Chat -> Text)
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
enum TabIcon { TI_ALL, TI_PARTY, TI_LS, TI_TELL, TI_SAY, TI_SYS, TI_SWORDS, TI_ME };
struct ChatTab { const char* en; const char* fr; unsigned mask; bool me; int icon; };
#define CB(c) (1u << (c))
static const unsigned ALL_TALK = CB(CH_SAY) | CB(CH_SHOUT) | CB(CH_YELL) | CB(CH_TELL) | CB(CH_PARTY) | CB(CH_LS1) | CB(CH_LS2) |
                                 CB(CH_UNITY) | CB(CH_EMOTE) | CB(CH_ASSIST) | CB(CH_SYSTEM) | CB(CH_NPC) | CB(CH_OTHER);
static const ChatTab TABS_CHAT[] = {
    { "Chat",   "Discussion",        ALL_TALK, false, TI_ALL },   // the "all" tab carries the box's name (no title beside it)
    { "Party",  "Groupe",            CB(CH_PARTY) | CB(CH_ASSIST), false, TI_PARTY },
    { "Linkshell", "Linkshell",      CB(CH_LS1) | CB(CH_LS2), false, TI_LS },
    { "Tell",   "Tell",              CB(CH_TELL), false, TI_TELL },
    { "Say",    "Dire",              CB(CH_SAY) | CB(CH_SHOUT) | CB(CH_YELL) | CB(CH_EMOTE) | CB(CH_UNITY), false, TI_SAY },
    { "System", "Syst\xC3\xA8me",    CB(CH_SYSTEM) | CB(CH_NPC) | CB(CH_OTHER), false, TI_SYS },
};
static const ChatTab TABS_BATTLE[] = {
    { "Battle", "Combat", CB(CH_BATTLE), false, TI_SWORDS },
    { "Me",     "Moi",    CB(CH_BATTLE), true,  TI_ME },
};
#undef CB
static const int TAB_N[2] = { (int)(sizeof(TABS_CHAT) / sizeof(TABS_CHAT[0])), (int)(sizeof(TABS_BATTLE) / sizeof(TABS_BATTLE[0])) };
static const ChatTab& tab_def(int box, int t) { return box == 0 ? TABS_CHAT[t] : TABS_BATTLE[t]; }
// The game's EXPANDED log tabs, by its category id (native_fulllog().tab ; the user named them, 2026-11-16). X cycles
// them in FULL_ORDER. Names are the game's own words (both languages). Windows 1 = what the Discussion box shows,
// Windows 2 = the battle log ; Assist E / J are one channel here.
#define CB(c) (1u << (c))
static const ChatTab FULL_TABS[11] = {
    { "Windows 1",   "Windows 1",   0, false, TI_ALL },      // mask : TABS_CHAT[0]'s, filled in full_tab()
    { "Party",       "Party",       CB(CH_PARTY), false, TI_PARTY },
    { "Linkshell",   "Linkshell",   CB(CH_LS1), false, TI_LS },
    { "Say",         "Say",         CB(CH_SAY) | CB(CH_EMOTE), false, TI_SAY },
    { "Tell",        "Tell",        CB(CH_TELL), false, TI_TELL },
    { "Shout",       "Shout",       CB(CH_SHOUT) | CB(CH_YELL), false, TI_SAY },
    { "Unity",       "Unity",       CB(CH_UNITY), false, TI_SAY },
    { "Linkshell 2", "Linkshell 2", CB(CH_LS2), false, TI_LS },
    { "Windows 2",   "Windows 2",   CB(CH_BATTLE), false, TI_SWORDS },
    { "Assist J",    "Assist J",    CB(CH_ASSIST), false, TI_PARTY },
    { "Assist E",    "Assist E",    CB(CH_ASSIST), false, TI_PARTY },
};
#undef CB
static const int FULL_ORDER[11] = { 0, 8, 3, 4, 1, 2, 7, 10, 9, 6, 5 };
static ChatTab full_tab(int id) {
    if (id < 0 || id > 10) id = 0;
    ChatTab t = FULL_TABS[id]; if (id == 0) t.mask = TABS_CHAT[0].mask;
    return t;
}

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
    // behaviour (fade / collapse when idle)
    unsigned lastAct = 0;           // GetTickCount of the last line for THIS box
    unsigned actSeq = 0;            // newest seq already checked for activity
    float    vis = 1.0f;            // drawn opacity (eased)
    float    foc = 0.0f;            // the game's small log window holds the focus here (eased 0..1) : opaque + our pointer
    float    fold = 0.0f;           // 0 = open .. 1 = collapsed to its header + one line (eased)
    float    full = 0.0f;           // 0 .. 1 : unfolded like the game's EXPANDED log (fulllog open), eased
    float    rx = 0, ry = 0, rw = 0, rh = 0;   // last frame's rect : the hover test
    float    bodyEase = -1.0f;      // the body height drawn (eased toward what the content needs ; -1 = first frame)
    const Font* advFont = 0; float advSize = 0.0f; bool advExact = false; float adv[256];   // advance cache by CODEPOINT 0..255 (face + size) ; wider ones : cw_at asks the font
    const Font* advBFont = 0; float advBSize = 0.0f; bool advBExact = false; float advB[256];   // the same for the BOLD auto-translate words
    int      grip = 0;              // token for the shared drag lock
    // text selection (a drag in the body ; copied on release) and the link under the mouse
    bool     selOn = false, selDrag = false, selMoved = false;
    unsigned selASeq = 0, selFSeq = 0; int selAIdx = 0, selFIdx = 0;
    float    downX = 0.0f, downY = 0.0f;
    char     downUrl[256] = "";     // the link the press landed on (a click without a drag offers it)
    unsigned linkSeq = 0; int linkA = -1;   // the link under the mouse (underlined)
    unsigned toastMs = 0; float toastX = 0.0f, toastY = 0.0f; int toastKind = 0;   // 1 copied, 2 link copied, 3 nothing copied
};
static ChatBoxState g_cb[2];
static float g_pvSizeU[2][2] = { { 0.0f, 0.0f }, { 0.0f, 0.0f } };   // a PREVIEW box's size / its scale (chat_preview_size)
static float g_liveRect[2][4]; static unsigned g_liveMs[2] = { 0, 0 };   // the LIVE box's rect (previews never write it)
bool chat_box_rect(int box, float& x, float& y, float& w, float& h) {
    if (box < 0 || box > 1 || !ui_config().chShow[box] || !g_liveMs[box] || GetTickCount() - g_liveMs[box] > 500u) return false;
    x = g_liveRect[box][0]; y = g_liveRect[box][1]; w = g_liveRect[box][2]; h = g_liveRect[box][3];
    return w > 0.0f && h > 0.0f;
}

// Where AioHUD's menus open (the action menu, the NPC windows) : on the CHAT, wherever the player put it (2026-11-17 :
// "toujours le chat de gauche ... le joueur peut mettre ses chats ou il veut"). The two boxes STACKED (their x ranges
// overlap) : anchored on the HIGHER one, the group spans both. SIDE BY SIDE : the LEFT one alone. One box : it.
// the input field drawn last (screen px : x, y, w, h, the caret's x) -- hud_actmenu's auto-translate picker sits on it
static float s_inRect[6] = { 0, 0, 0, 0, 0, 0 }; static DWORD s_inRectMs = 0;
bool chat_input_rect(float& x, float& y, float& w, float& h, float& caretX, float& wordX) {
    if (!s_inRectMs || GetTickCount() - s_inRectMs > 200u) return false;
    x = s_inRect[0]; y = s_inRect[1]; w = s_inRect[2]; h = s_inRect[3]; caretX = s_inRect[4]; wordX = s_inRect[5]; return true;
}
bool chat_anchor(float& ax, float& aw, float& top, float& bottom) {
    float r[2][4]; bool on[2];
    for (int b = 0; b < 2; ++b) on[b] = chat_box_rect(b, r[b][0], r[b][1], r[b][2], r[b][3]);
    int a = on[0] ? 0 : (on[1] ? 1 : -1);
    if (a < 0) return false;
    top = r[a][1]; bottom = r[a][1] + r[a][3];
    if (on[0] && on[1]) {
        const bool stacked = r[0][0] < r[1][0] + r[1][2] && r[1][0] < r[0][0] + r[0][2];
        if (stacked) {
            a = r[0][1] <= r[1][1] ? 0 : 1;
            top = r[0][1] < r[1][1] ? r[0][1] : r[1][1];
            const float b0 = r[0][1] + r[0][3], b1 = r[1][1] + r[1][3]; bottom = b0 > b1 ? b0 : b1;
        } else {
            a = r[0][0] <= r[1][0] ? 0 : 1;
            top = r[a][1]; bottom = r[a][1] + r[a][3];
        }
    }
    ax = r[a][0]; aw = r[a][2];
    return true;
}
// a bw x bh block against that anchor : ABOVE the group when it fits, else BELOW it ; always inside the screen
bool place_on_chat(float bw, float bh, float gap, float screenW, float screenH, float& bx, float& by) {
    float ax, aw, top, bottom;
    if (!chat_anchor(ax, aw, top, bottom)) return false;
    (void)aw;
    bx = ax;
    by = (top - gap - bh >= 4.0f) ? top - gap - bh : bottom + gap;
    if (bx + bw > screenW - 4.0f) bx = screenW - 4.0f - bw;
    if (bx < 4.0f) bx = 4.0f;
    if (by + bh > screenH - 4.0f) by = screenH - 4.0f - bh;
    if (by < 4.0f) by = 4.0f;
    bx = snap(bx); by = snap(by);
    return true;
}

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
// a BANNED speaker's line (chat_banned) : asked once per line (keyed by its seq), again when the list changes. Your own
// tells to them stay (chTellOut : the name is the one you wrote to).
static unsigned      g_banSeq[CHAT_HIST];
static unsigned char g_banVal[CHAT_HIST];
static unsigned      g_banVer = 0;
static bool line_banned(const ChatLine& L) {
    if (!L.chSpeaker[0] || L.chTellOut || chat_ban_count() == 0) return false;
    if (chat_ban_version() != g_banVer) { g_banVer = chat_ban_version(); memset(g_banSeq, 0, sizeof(g_banSeq)); }
    const int k = (int)(L.seq & (CHAT_HIST - 1));
    if (L.seq > 16 && g_banSeq[k] == L.seq) return g_banVal[k] != 0;
    const bool b = chat_banned(L.chSpeaker);
    if (L.seq > 16) { g_banSeq[k] = L.seq; g_banVal[k] = b ? 1 : 0; }
    return b;
}
static bool line_in_tab(const ChatLine& L, const ChatTab& t, const char* me) {
    if (!((1u << L.chan) & t.mask)) return false;
    if (line_banned(L)) return false;
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
    chat_parse_speaker(L);                                                   // the same badge + name as a live line
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
// ---- the line's PREFIX : a channel badge, then the speaker's name (the client's "(Name)" "<Name>" "Name>>" are gone) ----
static const char* chan_badge(const ChatLine& L) {
    switch (L.chan) {
        case CH_PARTY: return tr("PARTY", "GROUPE");  case CH_SAY: return tr("SAY", "DIRE");      case CH_SHOUT: return tr("SHOUT", "CRIER");
        case CH_YELL: return tr("YELL", "HURLER");    case CH_UNITY: return "UNITY";              case CH_ASSIST: return "ASSIST";
        case CH_NPC: return tr("NPC", "PNJ");         case CH_EMOTE: return "EMOTE";
        case CH_TELL: return L.chTellOut ? tr("TELL TO", "TELL \xC3\x80") : "TELL";
        case CH_LS1: case CH_LS2: return L.chLsIdx == 2 ? "LS2" : "LS1";
        default: return 0;                                                  // system, battle : no badge
    }
}
// ---- the history is UTF-8 (2026-11-17, Japanese) : every loop below steps by CHARACTER, never by byte ----
// adv[] = the advance of codepoints 0..255 (the font's own atlas) ; a wider one is measured by the font (its shared
// wide pages cache it). s_wFo / s_wTz : the font + size the table was built for, set with it.
static Font* s_wFo = 0; static float s_wTz = 0.0f;
// the auto-translated words are BOLD (2026-11-17) : their own font and advance table, set with the regular ones
static Font* s_wFoB = 0; static const float* s_advB = 0;
static inline int u8len(unsigned char b) { return b < 0x80 ? 1 : (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 1; }
// the character at t[i] : its advance, byte length `k`, codepoint `cp` ; `bold` = an auto-translated word's
static inline float cw_at(const float* adv, const char* t, int i, int len, int& k, unsigned& cp, bool bold = false) {
    const float* A = (bold && s_advB) ? s_advB : adv;
    Font* F = (bold && s_wFoB) ? s_wFoB : s_wFo;
    const unsigned char b = (unsigned char)t[i];
    k = u8len(b); if (i + k > len) k = 1;
    if (k == 1) { cp = b; return A[b]; }
    cp = b & (0x3Fu >> (k - 1));
    for (int q = 1; q < k; ++q) cp = (cp << 6) | ((unsigned char)t[i + q] & 0x3Fu);
    if (cp < 256) return A[cp];
    char u[5]; for (int q = 0; q < k; ++q) u[q] = t[i + q]; u[k] = 0;
    return F ? F->measure(u, s_wTz) : A['?'];
}
static inline bool at_bold(const ChatLine& L, int i) { return L.atIdx && L.col[i] == L.atIdx; }
// what punct_pre sees of a character : itself when Latin-1, else a neutral non-letter
static inline unsigned char punct_c(unsigned cp) { return cp < 256 ? (unsigned char)cp : 1; }
// the advance table : codepoint -> px (0..255, every one as its UTF-8 form)
static void build_adv(float* adv, Font* fo, float tz) {
    char u[4] = { 0, 0, 0, 0 };
    for (int ch = 0; ch < 256; ++ch) {
        if (ch < 0x80) { u[0] = (char)(ch < 32 ? ' ' : ch); u[1] = 0; }
        else { u[0] = (char)(0xC0 | (ch >> 6)); u[1] = (char)(0x80 | (ch & 0x3F)); u[2] = 0; }
        adv[ch] = fo->measure(u, tz);
    }
}

struct LinePrefix { const char* badge; float badgeW, nameW, w; };
static void line_prefix(const ChatLine& L, Font* fo, float bz, const float* adv, float S, LinePrefix& p) {
    p.badge = L.cont ? 0 : chan_badge(L); p.badgeW = 0.0f; p.nameW = 0.0f; p.w = 0.0f;
    if (p.badge) { p.badgeW = snap(fo->measure(p.badge, bz) + 10.0f * S); p.w += p.badgeW + 6.0f * S; }
    if (!L.cont && L.chSpeaker[0]) {
        const int n = (int)strlen(L.chSpeaker);
        for (int i = 0; i < n; ) { int k; unsigned cp; p.nameW += cw_at(adv, L.chSpeaker, i, n, k, cp); i += k; }
        p.w += p.nameW + 7.0f * S;
    }
}

// the reading space before punctuation (font.h punct_pre), the same step for wrapping, run widths and drawing
static inline float chat_punct_gap(float tz) { return tz * 0.16f; }
static int wrap_line(const ChatLine& L, const float* adv, float gap, float avail, short* sa, short* sb, int cap, float firstCut = 0.0f) {
    int ns = 0, a = L.cont ? 0 : L.chBodyAt; if (a > L.len) a = L.len;
    if (a >= L.len) { sa[0] = (short)a; sb[0] = (short)a; return 1; }    // a speaker with an empty message : one row
    while (a < L.len && ns < cap) {
        float w = 0.0f; int b = a, lastSp = -1; unsigned char prev = 0;
        const float av = ns == 0 ? avail - firstCut : avail;
        while (b < L.len) {
            int k; unsigned cp; float cw = cw_at(adv, L.text, b, L.len, k, cp, at_bold(L, b));
            const unsigned char c8 = punct_c(cp);
            cw += b > a ? punct_pre(prev, c8, gap) : 0.0f;
            if (w + cw > av && b > a) break;                               // a row breaks BETWEEN characters (Japanese has no spaces)
            if (cp == ' ') lastSp = b;
            w += cw; b += k; prev = c8;
        }
        if (b < L.len && lastSp > a) b = lastSp + 1;                       // break after the last space that fits
        sa[ns] = (short)a; sb[ns] = (short)b; ++ns; a = b;
    }
    return ns;
}

// ---- text selection, links, the clipboard (2026-11-17 : "un chat moderne : copier, coller des liens, exporter") ----
// The rows drawn this frame (body text only : x0 = where the message starts), for the mouse : a drag in the body selects
// (anchor / focus = line seq + byte index), the release copies it ; a click on a link offers to open / copy it.
struct RowHit { const ChatLine* L; short a, b; float x0, cy; };
static RowHit g_rowHit[2][152]; static int g_rowHitN[2] = { 0, 0 };
// links : http:// https:// www. at a word start, up to a space / a non-ASCII byte ; trailing punctuation left out
static int find_links(const ChatLine& L, short* la, short* lb, int cap) {
    int n = 0;
    for (int i = 0; i < L.len && n < cap; ++i) {
        if (i > 0) { const char p = L.text[i - 1]; if (!(p == ' ' || p == '(' || p == '<' || p == '[' || p == '"' || p == '\'' || p == '{' || p == '>')) continue; }
        int pre = 0;
        if (i + 8 <= L.len && !_strnicmp(L.text + i, "https://", 8)) pre = 8;
        else if (i + 7 <= L.len && !_strnicmp(L.text + i, "http://", 7)) pre = 7;
        else if (i + 4 <= L.len && !_strnicmp(L.text + i, "www.", 4)) pre = 4;
        if (!pre) continue;
        int e = i + pre;
        while (e < L.len) { const unsigned char c = (unsigned char)L.text[e]; if (c <= 0x20 || c >= 0x7F || c == '"' || c == '<' || c == '>' || c == '{' || c == '}') break; ++e; }
        while (e > i + pre && strchr(".,;:!?)]'", L.text[e - 1])) --e;
        bool dot = false; for (int q = i + pre; q < e; ++q) if (L.text[q] == '.') dot = true;
        if (e - i - pre >= 3 && (dot || pre != 4)) { la[n] = (short)i; lb[n] = (short)e; ++n; }
        i = e > i ? e - 1 : i;
    }
    return n;
}
// the byte index under x on a drawn row, and the x of an index -- the same advances as the draw
static int row_index_at(const RowHit& R, float mx, const float* adv, float gap) {
    const ChatLine& L = *R.L; float x = R.x0; unsigned char lastc = 0;
    for (int j = R.a; j < R.b; ) {
        int k; unsigned cp; float cw = cw_at(adv, L.text, j, L.len, k, cp, at_bold(L, j));
        const unsigned char c8 = punct_c(cp); cw += punct_pre(j > R.a ? lastc : 0, c8, gap);
        if (mx < x + cw * 0.5f) return j;
        x += cw; j += k; lastc = c8;
    }
    return R.b;
}
static float row_x_at(const ChatLine& L, int a, int b, float x0, int idx, const float* adv, float gap) {
    float x = x0; unsigned char lastc = 0;
    for (int j = a; j < b && j < idx; ) {
        int k; unsigned cp; float cw = cw_at(adv, L.text, j, L.len, k, cp, at_bold(L, j));
        const unsigned char c8 = punct_c(cp); cw += punct_pre(j > a ? lastc : 0, c8, gap);
        x += cw; j += k; lastc = c8;
    }
    return x;
}
// UTF-8 -> the clipboard (CF_UNICODETEXT) ; false when the clipboard is busy
static bool clip_put_utf8(const char* s, int n) {
    if (n <= 0) return false;
    const int wn = MultiByteToWideChar(CP_UTF8, 0, s, n, 0, 0); if (wn <= 0) return false;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)(wn + 1) * sizeof(wchar_t)); if (!g) return false;
    wchar_t* w = (wchar_t*)GlobalLock(g); if (!w) { GlobalFree(g); return false; }
    MultiByteToWideChar(CP_UTF8, 0, s, n, w, wn); w[wn] = 0; GlobalUnlock(g);
    if (!OpenClipboard(0)) { GlobalFree(g); return false; }
    EmptyClipboard();
    const bool ok = SetClipboardData(CF_UNICODETEXT, g) != 0;
    CloseClipboard();
    if (!ok) GlobalFree(g);
    return ok;
}
// a link opened by the shell, on its own thread (ShellExecute may take a while : never the game's frame). shell32 /
// ole32 are bound at run time (the DLL links user32 / kernel32 / gdi32 only).
static DWORD WINAPI url_thread(LPVOID p) {
    typedef HINSTANCE (WINAPI *SeFn)(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT);
    typedef HRESULT (WINAPI *CiFn)(LPVOID, DWORD); typedef void (WINAPI *CuFn)();
    HMODULE ole = LoadLibraryA("ole32.dll"), sh = LoadLibraryA("shell32.dll");
    CiFn ci = ole ? (CiFn)GetProcAddress(ole, "CoInitializeEx") : 0; CuFn cu = ole ? (CuFn)GetProcAddress(ole, "CoUninitialize") : 0;
    const bool co = ci && SUCCEEDED(ci(0, 0x2 | 0x4));            // COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE
    SeFn se = sh ? (SeFn)GetProcAddress(sh, "ShellExecuteW") : 0;
    const INT_PTR r = se ? (INT_PTR)se(0, L"open", (const wchar_t*)p, 0, 0, SW_SHOWNORMAL) : 0;
    if (r <= 32) windower::debug::log("CHAT link : the shell could not open it (%d)", (int)r);
    if (co && cu) cu();
    HeapFree(GetProcessHeap(), 0, p);
    return 0;
}
static bool open_url(const char* u) {
    char full[300];
    if (!_strnicmp(u, "www.", 4)) _snprintf(full, sizeof(full), "http://%s", u); else lstrcpynA(full, u, sizeof(full));
    full[sizeof(full) - 1] = 0;
    if (_strnicmp(full, "http://", 7) && _strnicmp(full, "https://", 8)) return false;   // a web page, nothing else
    for (const char* q = full; *q; ++q) if ((unsigned char)*q <= 0x20 || (unsigned char)*q >= 0x7F) return false;
    wchar_t* w = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, 300 * sizeof(wchar_t)); if (!w) return false;
    MultiByteToWideChar(CP_UTF8, 0, full, -1, w, 300); w[299] = 0;
    HANDLE t = CreateThread(0, 0, url_thread, w, 0, 0);
    if (!t) { HeapFree(GetProcessHeap(), 0, w); return false; }
    CloseHandle(t); windower::debug::log("CHAT link opened : %.120s", full);
    return true;
}
// the link popup (one for both boxes) : the link, [Open] [Copy]
static struct { bool on; int box; char url[256]; float x, y; unsigned ms; } g_lp = { false, 0, "", 0, 0, 0 };
// the LINE menu (a right-click on a line) : copy the line / the message / the name / its link
static struct { bool on; int box; unsigned seq; float x, y; char name[20]; char url[256]; } g_cm = { false, 0, 0, 0, 0, "", "" };
// a whole message from its first history line (its continuations after it) ; body : without the speaker's prefix
static int message_text(unsigned seq, bool body, char* out, int cap) {
    int o = 0; bool in = false;
    for (int i = 0; i < chat_count(); ++i) {
        const ChatLine* L = chat_line(i); if (!L) continue;
        if (!in) { if (L->seq != seq) continue; in = true; }
        else if (!L->cont) break;
        const int a = (body && !L->cont) ? L->chBodyAt : 0; if (L->len <= a) continue;
        if (o && o < cap - 2) { out[o++] = ' '; }
        const int n = L->len - a < cap - 1 - o ? L->len - a : cap - 1 - o; if (n <= 0) break;
        memcpy(out + o, L->text + a, (size_t)n); o += n;
    }
    out[o > 0 ? o : 0] = 0; return o;
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
            else if (m == 0x69) { at = true; o.t[o.n] = '{'; o.c[o.n++] = 0xFF4CD964u; }   // the client's green open
            else if (m == 0x64) { if (at) { o.t[o.n] = '}'; o.c[o.n++] = 0xFFE5534Bu; } at = false; cand = false; }   // the client's red close
            else if (m == 0x66) cand = !cand;
            continue;
        }
        // UTF-8, like the history (2026-11-17) : the accents, Japanese (code page 932), half-width katakana
        unsigned cp = ch;
        if (ch == 0x85 && i + 1 < rn && (unsigned char)raw[i + 1] >= 0x9F && (unsigned char)raw[i + 1] <= 0xDE) cp = (unsigned)(unsigned char)raw[++i] + 0x21u;
        else if (((ch >= 0x81 && ch <= 0x9F) || (ch >= 0xE0 && ch <= 0xFC)) && i + 1 < rn) {
            const char pr[2] = { (char)ch, raw[i + 1] }; wchar_t w = 0; ++i;
            cp = (MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, pr, 2, &w, 1) == 1 && w >= 0x20) ? (unsigned)w : (unsigned)'?';
        }
        else if (ch >= 0xA1 && ch <= 0xDF) cp = 0xFF61u + (unsigned)(ch - 0xA1);
        else if (ch < 0x20 || ch == 0x7F) continue;
        else if (ch >= 0x80) cp = '?';
        char b[3]; int k = 0;
        if (cp < 0x80) b[k++] = (char)cp;
        else if (cp < 0x800) { b[k++] = (char)(0xC0 | (cp >> 6)); b[k++] = (char)(0x80 | (cp & 0x3F)); }
        else { b[k++] = (char)(0xE0 | (cp >> 12)); b[k++] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[k++] = (char)(0x80 | (cp & 0x3F)); }
        if (o.n + k > 0x16F) break;
        const u32 cc = at ? (ui_config().chAtColor | 0xFF000000u) : (cand ? 0xFF8A96A8u : 0xFFF2F5F9u);   // auto-translate words : the player's colour
        for (int q = 0; q < k; ++q) { o.t[o.n] = b[q]; o.c[o.n++] = cc; }
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

// ---- tab icons, drawn with the HUD's own primitives (no texture) : one glyph per channel family ----
static u32 icon_col(int icon) {
    switch (icon) {
        case TI_PARTY: return CHAN_COL[CH_PARTY]; case TI_LS: return CHAN_COL[CH_LS1]; case TI_TELL: return CHAN_COL[CH_TELL];
        case TI_SAY: return CHAN_COL[CH_SAY];     case TI_SYS: return CHAN_COL[CH_SYSTEM]; case TI_SWORDS: return 0xFFFF9C6Eu;
        case TI_ME: return 0xFF9FD8FFu;           default: return 0xFFDDE4EDu;
    }
}
static void ring(u32 dev, float cx, float cy, float r, float bt, u32 c) { rrect_stroke(dev, cx - r, cy - r, 2.0f * r, 2.0f * r, r, c, bt); }
// one little person : a head and rounded shoulders (party icon)
static void person(u32 dev, float cx, float cy, float h, u32 c) {
    const float hr = h * 0.20f;                                              // head radius
    disc(dev, cx, cy - h * 0.22f, hr, c);
    const float bw = h * 0.62f, bh = h * 0.34f;                             // shoulders : a rounded top, flat bottom
    rrect_tb(dev, cx - bw * 0.5f, cy + h * 0.04f, bw, bh, bw * 0.48f, h * 0.06f, c, c, 1.0f);
}
// ---- the mother-of-pearl tab icons (assets/chat_icons.raw, scripts/gen_chat_icons.py ; 2026-11-16) ----
// A 512x64 strip, one 64 px WHITE icon per TabIcon (TI_ALL's cell empty : it keeps its drawn icon), tinted with the
// tab's colour. Owned here : forgotten on a device change, released at //unload (rule 4) ; a miss is retried (rule 10).
static u32 g_chatIcons = 0; static TexRetry g_chatIconsR;
static const char* CHAT_ICONS_PATH() { static char b[260]; if (!b[0]) plugin_path(b, 260, "assets\\chat_icons.raw"); return b; }
void chat_icons_forget()  { g_chatIcons = 0; g_chatIconsR = TexRetry{}; }
void chat_icons_dispose() { if (g_chatIcons) release_texture(g_chatIcons); chat_icons_forget(); }
// true when drawn from the strip ; false -> the caller draws the vector icon instead
static bool draw_tab_pearl(u32 dev, int icon, float cx, float cy, float z, u32 c) {
    if (icon == TI_ALL) return false;
    const u32 tex = ensure_raw_tex_mip(dev, g_chatIcons, g_chatIconsR, CHAT_ICONS_PATH(), 512, 64);
    if (!tex) return false;
    const float s = snap(z * 1.18f), u0 = (float)icon / 8.0f, u1 = (float)(icon + 1) / 8.0f;   // the glow needs a little room
    dTexQuadState(dev, tex, true);
    tquad(dev, snap(cx - s * 0.5f), snap(cy - s * 0.5f), s, s, u0, u1, 0.0f, 1.0f, c, c);
    dColorQuadState(dev);                                                    // the untextured pipeline for what follows
    return true;
}

static void draw_tab_icon(u32 dev, int icon, float cx, float cy, float z, float S, u32 c) {
    const float t = 1.5f * S;                                                // stroke
    const u32 ink = (c & 0xFF000000u) | 0x000000u;                           // a dark cut-out, same opacity as the icon
    switch (icon) {
    case TI_ALL:     // a stack of lines : everything
        rrect(dev, cx - z * 0.42f, cy - z * 0.30f, z * 0.84f, t, t * 0.5f, c, c, 1.0f);
        rrect(dev, cx - z * 0.42f, cy - t * 0.5f,  z * 0.60f, t, t * 0.5f, c, c, 1.0f);
        rrect(dev, cx - z * 0.42f, cy + z * 0.30f - t, z * 0.74f, t, t * 0.5f, c, c, 1.0f);
        break;
    case TI_PARTY: { // three people : the two behind, smaller and dimmer ; the one in front cut out from them
        const u32 back = (((c >> 24) * 3 / 5) << 24) | (c & 0x00FFFFFFu);
        person(dev, cx - z * 0.30f, cy + z * 0.02f, z * 0.78f, back);
        person(dev, cx + z * 0.30f, cy + z * 0.02f, z * 0.78f, back);
        person(dev, cx, cy + z * 0.06f, z * 1.04f, (((ink >> 24) * 3 / 4) << 24));   // outline : the front one stands out
        person(dev, cx, cy + z * 0.06f, z * 0.92f, c);
        break; }
    case TI_LS: {    // a linkpearl : halo, a darker rim, the lit body, a highlight and a glint
        const float r = z * 0.36f;
        const u32 a = c & 0xFF000000u, rgb = c & 0x00FFFFFFu;
        const u32 rim = a | ((((rgb >> 16) & 255) * 11 / 20) << 16) | ((((rgb >> 8) & 255) * 11 / 20) << 8) | ((rgb & 255) * 11 / 20);
        disc_glow(dev, cx, cy, r * 0.95f, (((a >> 24) / 4) << 24) | rgb, 3.0f * S);
        disc(dev, cx, cy, r, rim);
        disc(dev, cx - r * 0.10f, cy - r * 0.10f, r * 0.80f, c);
        disc(dev, cx - r * 0.34f, cy - r * 0.36f, r * 0.24f, (((a >> 24) * 7 / 8) << 24) | 0xFFFFFFu);
        disc(dev, cx + r * 0.32f, cy + r * 0.30f, r * 0.09f, (((a >> 24) / 2) << 24) | 0xFFFFFFu);
        break; }
    case TI_TELL: {  // a sealed letter, filled, with its flap
        const float w = z * 0.88f, h = z * 0.62f, x = cx - w * 0.5f, y = cy - h * 0.5f;
        rrect(dev, x, y, w, h, 2.0f * S, c, c, 1.0f);
        const float fl[6] = { x + 1.0f * S, y + 1.0f * S, x + w - 1.0f * S, y + 1.0f * S, cx, cy + h * 0.12f };
        fill_poly_aa(dev, fl, 3, (((ink >> 24) * 3 / 5) << 24));               // the flap, as a shadow
        seg_soft(dev, x + 1.5f * S, y + 1.5f * S, cx, cy + h * 0.10f, t, (c & 0x00FFFFFFu) | (c & 0xFF000000u));
        seg_soft(dev, x + w - 1.5f * S, y + 1.5f * S, cx, cy + h * 0.10f, t, (c & 0x00FFFFFFu) | (c & 0xFF000000u));
        break; }
    case TI_SAY: {   // a speech bubble with three dots : someone talking nearby
        const float w = z * 0.90f, h = z * 0.60f, x = cx - w * 0.5f, y = cy - z * 0.40f;
        rrect_stroke(dev, x, y, w, h, h * 0.45f, c, t);
        const float xy[6] = { x + w * 0.22f, y + h - t * 0.5f, x + w * 0.46f, y + h - t * 0.5f, x + w * 0.14f, y + h + z * 0.24f };
        fill_poly_aa(dev, xy, 3, c);
        for (int k = -1; k <= 1; ++k) disc(dev, cx + k * w * 0.22f, y + h * 0.5f, z * 0.055f, c);
        break; }
    case TI_SYS: {   // a cog : the game itself
        const float ro = z * 0.42f, ri = z * 0.30f;
        for (int k = 0; k < 8; ++k) {                                       // eight teeth
            const float a0 = 6.2831853f * (float)k / 8.0f, hw = 0.20f;
            const float ca = cosf(a0), sa = sinf(a0), ca1 = cosf(a0 - hw), sa1 = sinf(a0 - hw), ca2 = cosf(a0 + hw), sa2 = sinf(a0 + hw);
            const float xy[8] = { cx + ca1 * ri, cy + sa1 * ri, cx + ca1 * ro, cy + sa1 * ro, cx + ca2 * ro, cy + sa2 * ro, cx + ca2 * ri, cy + sa2 * ri };
            (void)ca; (void)sa;
            fill_poly_aa(dev, xy, 4, c);
        }
        disc(dev, cx, cy, ri, c);
        disc(dev, cx, cy, ri * 0.42f, (((ink >> 24) * 4 / 5) << 24));         // the hub
        break; }
    case TI_SWORDS:  // crossed blades : the fight
        seg_soft(dev, cx - z * 0.36f, cy + z * 0.36f, cx + z * 0.36f, cy - z * 0.36f, t * 1.4f, c);
        seg_soft(dev, cx + z * 0.36f, cy + z * 0.36f, cx - z * 0.36f, cy - z * 0.36f, t * 1.4f, c);
        seg_soft(dev, cx - z * 0.38f, cy + z * 0.14f, cx - z * 0.14f, cy + z * 0.38f, t * 1.4f, c);   // guards
        seg_soft(dev, cx + z * 0.38f, cy + z * 0.14f, cx + z * 0.14f, cy + z * 0.38f, t * 1.4f, c);
        break;
    case TI_ME:      // a reticle : what concerns you
        ring(dev, cx, cy, z * 0.28f, t, c);
        disc(dev, cx, cy, z * 0.07f, c);
        rrect(dev, cx - t * 0.5f, cy - z * 0.48f, t, z * 0.14f, 0.0f, c, c, 1.0f);
        rrect(dev, cx - t * 0.5f, cy + z * 0.34f, t, z * 0.14f, 0.0f, c, c, 1.0f);
        rrect(dev, cx - z * 0.48f, cy - t * 0.5f, z * 0.14f, t, 0.0f, c, c, 1.0f);
        rrect(dev, cx + z * 0.34f, cy - t * 0.5f, z * 0.14f, t, 0.0f, c, c, 1.0f);
        break;
    default: break;
    }
}

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
    // the auto-translated words : the same face, BOLD (one step bolder when the text already is)
    Font* fb = f.fonts ? f.fonts->get(TS.face > 0 ? ui_font_face(TS.face) : 0, TS.bold ? 900 : 700, TS.italic) : 0;
    if (!fb) fb = fo;
    // its advance table follows the regular one ; refreshed once more when that size's atlas exists (exact widths)
    auto adv_bold = [&](float z) {
        if (st.advBFont != fb || st.advBSize != z) { st.advBFont = fb; st.advBSize = z; st.advBExact = fb->baked(z); build_adv(st.advB, fb, z); }
        else if (!st.advBExact && fb->baked(z)) { st.advBExact = true; build_adv(st.advB, fb, z); }
        s_wFoB = fb; s_advB = st.advB;
    };
    const float tz = te_sz(TS, 15.0f) * S, tow = te_ow(TS, 1.1f) * S;
    // DENSITY (compact / normal / airy) : the padding, the header's height, the gap under it, the line spacing
    static const float D_PAD[3] = { 7.0f, 10.0f, 13.0f }, D_HEAD[3] = { 24.0f, 28.0f, 32.0f }, D_GAP[3] = { 3.0f, 6.0f, 8.0f }, D_ROW[3] = { 1.30f, 1.40f, 1.50f };
    const int dn = (C.chDensity[box] >= 0 && C.chDensity[box] <= 2) ? C.chDensity[box] : 0;
    const float rowH = snap(tz * D_ROW[dn]);
    float hsc = C.chHeadScale[box]; if (hsc < 0.6f) hsc = 0.6f; if (hsc > 1.6f) hsc = 1.6f;   // the header's own size
    const float pad = snap(D_PAD[dn] * S), headH = snap(D_HEAD[dn] * S * hsc), headGap = snap(D_GAP[dn] * S);
    const float tabZ = 13.5f * S * hsc, titleZ = 11.0f * S, badgeZ = 9.5f * S * hsc;
    const float bz = tz * 0.62f;                                             // the channel badge's text size
    // HEIGHT is the box's own (px at S) : the font decides how many lines fit, never the box size. A config from before
    // carries a line count only -> its height is taken ONCE from it, with today's metrics, so nothing moves.
    if (C.chH[box] <= 0) {
        int ln = C.chLines[box]; if (ln < 3) ln = 3; if (ln > 40) ln = 40;
        C.chH[box] = (int)((pad + headH + headGap + ln * rowH + pad * 0.8f) / S + 0.5f);
        if (!preview) save_ui_config();
    }
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

    // ---- behaviour : always shown / fades when idle / collapses when idle ----
    // Idle = no new line for THIS box for chIdle seconds, nobody hovering it, not typing, not scrolled back. The
    // fade-out is slow and the wake-up quick. A collapsed box keeps its BOTTOM edge (the chat lives low on screen).
    const int beh = (preview || editing) ? 0 : C.chBehave[box];
    if (!src.sample) {
        const unsigned nw = chat_newest_seq();
        if (nw != st.actSeq) {
            for (int i = src.n() - 1; i >= 0; --i) {
                const ChatLine* L = src.at(i); if (!L || L->seq <= st.actSeq) break;
                if ((box == 1) == chat_is_battle(L->chan)) { st.lastAct = nowMs; break; }
            }
            st.actSeq = nw;
        }
    }
    if (!st.lastAct) st.lastAct = nowMs;
    const MouseState* mh = f.mouse;
    const bool hoverBox = mh && mh->focused && st.rw > 0.0f && mh->x >= st.rx && mh->x < st.rx + st.rw && mh->y >= st.ry && mh->y < st.ry + st.rh;
    // fade and collapse are independent, each on its own delay (e.g. collapse after 8 s, fade out after 20 s)
    // the game's EXPANDED log is open : this box unfolds in its place (its window is parked with "Chat log")
    const NativeFullLog& FL = native_fulllog();
    const bool fullOn = box == 0 && !preview && !editing && FL.open && (C.nuHide & NU_CHAT);   // only in place of the game's (hidden) log
    const ChatTab fullT = full_tab(FL.tab);
    st.full += ((fullOn ? 1.0f : 0.0f) - st.full) * kSoft; if (fabsf((fullOn ? 1.0f : 0.0f) - st.full) < 0.003f) st.full = fullOn ? 1.0f : 0.0f;
    // the game's SMALL log window is selected (before X / Enter unfolds it) : the client turns it opaque with a little
    // arrow on its left. Ours : the box goes opaque, with a soft halo and a light rim (2026-11-16).
    const bool focOn = !preview && !editing && !fullOn && FL.logFocus == box && (C.nuHide & NU_CHAT);
    st.foc += ((focOn ? 1.0f : 0.0f) - st.foc) * kFast; if (fabsf((focOn ? 1.0f : 0.0f) - st.foc) < 0.003f) st.foc = focOn ? 1.0f : 0.0f;
    const bool awake = typing || hoverBox || st.scroll > 0 || fullOn || focOn;
    const int idleMs = (int)(nowMs - st.lastAct);
    const bool folded = (beh & 2) && !awake && idleMs >= C.chIdle[box] * 1000;
    const bool faded  = (beh & 1) && !awake && idleMs >= C.chFadeIdle[box] * 1000;
    // the game's expanded log open : the Discussion box unfolds in its place, the Battle box steps aside (2026-11-16)
    const bool asideForFull = box == 1 && !preview && !editing && FL.open && (C.nuHide & NU_CHAT);
    const float tVis = asideForFull ? 0.0f : (faded ? (float)C.chFadeTo[box] / 100.0f : 1.0f);
    const float tFold = folded ? 1.0f : 0.0f;
    const float kSlow = 1.0f - expf(-dt * 3.5f);
    st.vis += (tVis - st.vis) * ((tVis < st.vis && !asideForFull) ? kSlow : kFast);
    if (fabsf(tVis - st.vis) < 0.004f) st.vis = tVis;
    if (asideForFull) st.vis = 0.0f;                                         // the log unfolds : the other box is gone AT ONCE (no fade lag)
    st.fold += (tFold - st.fold) * kSoft;
    if (fabsf(tFold - st.fold) < 0.004f) st.fold = tFold;
    const float A = st.vis;                                                  // every colour below is multiplied by it
    if (asideForFull && A <= 0.0f) return;                                  // stepped aside : nothing drawn, nothing hit
    // HEIGHT : chMaxLines lines (the text size scales it), or the fixed chH when chMaxLines is 0 ("all")
    float bodyPx = (float)C.chH[box] * S - bodyY0 - pad * 0.8f; if (bodyPx < rowH) bodyPx = rowH;
    const float bodyMax = C.chMaxLines[box] > 0 ? (float)C.chMaxLines[box] * rowH : bodyPx;
    // The box GROWS WITH ITS CONTENT : as many rows as the open tab has (wrapped), up to the max -- one line, one row.
    // Counted from the newest back, stopping at the max (cheap). Edit layout and the preview show the full size.
    float bodyOpen = bodyMax;
    bool overflow = true;                                                    // more history than the box shows -> fade its top edge
    // COLLAPSE, line by line : a line stays chIdle seconds after it ARRIVED, then leaves -- down to chFoldLines rows.
    // FIXED box (default) : the box keeps its size and the old lines FADE OUT in place. Fitting box : it shrinks.
    // Hover, typing or scrolling back shows everything again.
    const bool collapsing = (beh & 2) && !awake;
    const unsigned keepMs = (unsigned)C.chIdle[box] * 1000u;
    const bool fitBox = C.chFit[box] == 1;
    if (!editing && !preview && fitBox) {
        if (st.advFont != fo || st.advSize != tz || !st.advExact) {           // the same per-char advance cache as below
            st.advFont = fo; st.advSize = tz; st.advExact = fo->baked(tz);
            build_adv(st.adv, fo, tz);
        }
        s_wFo = fo; s_wTz = tz; adv_bold(tz);
        int tb = C.chTab[box]; if (tb < 0 || tb >= TAB_N[box]) tb = 0;
        const ChatTab& TP = tab_def(box, tb);
        const char* meP = (f.game && f.game->me.name[0]) ? f.game->me.name : "";
        const float stampP = C.chStamp[box] ? fo->measure("00:00", tz * 0.86f) + 9.0f * S : 0.0f;
        const float availP = (float)C.chW[box] * S - 2.0f * pad - 2.0f * S - stampP;
        const int cap = (int)(bodyMax / rowH + 0.999f);
        int have = 0, fresh = 0; bool aged = false; short sa[24], sb[24];
        for (int i = src.n() - 1; i >= 0 && have < cap; --i) {
            const ChatLine* L = src.at(i); if (!L || !line_in_tab(*L, TP, meP)) continue;
            LinePrefix lp; line_prefix(*L, fo, tz * 0.62f, st.adv, S, lp);
            const int rws = wrap_line(*L, st.adv, chat_punct_gap(tz), availP, sa, sb, 24, lp.w);
            have += rws;
            if (!aged && nowMs - L->arrivedTick < keepMs) fresh += rws; else aged = true;   // newest first : the fresh ones are on top of the list
        }
        int rowsShown = have;
        if (collapsing) { int keep = C.chFoldLines[box]; if (fresh > keep) keep = fresh; if (keep < rowsShown) rowsShown = keep; }
        float want = (float)(rowsShown < 1 ? 1 : rowsShown) * rowH; if (want > bodyMax) want = bodyMax;
        overflow = (float)have * rowH > bodyMax + 0.5f || st.scroll > 0;
        if (st.bodyEase < 0.0f) st.bodyEase = want;
        st.bodyEase += (want - st.bodyEase) * kSoft;
        if (fabsf(want - st.bodyEase) < 0.3f) st.bodyEase = want;
        bodyOpen = st.bodyEase;
    }
    const float lines = bodyOpen / rowH;                                     // how many lines fit (fractional : the top one fades)
    float bodyFold = (float)C.chFoldLines[box] * rowH; if (bodyFold > bodyOpen) bodyFold = bodyOpen; if (bodyFold < rowH) bodyFold = rowH;
    (void)bodyFold;
    float bodyNow = bodyOpen;                                                // (collapse is per line now : in bodyOpen)
    if (st.full > 0.0f && f.screenH > 0.0f) {       // unfolded : all the room on its growing side (a margin kept), at most 75 %
        const float edge = C.chY[box] * f.screenH, chrome = bodyY0 + pad * 0.8f + inH * st.inA + 12.0f * S;
        float room = (C.chGrow[box] != 1) ? edge - chrome : f.screenH - edge - chrome;
        if (room > f.screenH * 0.75f) room = f.screenH * 0.75f;
        if (room > bodyOpen) bodyNow = snap(bodyOpen + (room - bodyOpen) * st.full);
    }
    const float linesVis = bodyNow / rowH;
    const float boxH = snap(bodyY0 + bodyNow + pad * 0.8f + inH * st.inA);
    // WHICH EDGE STAYS : chY is that edge (0 = bottom : the box grows upward ; 1 = top : it grows downward). A config
    // from before kept chY as the top of a chH-high frame whose bottom the box sat on : take that bottom, once.
    if (C.chGrow[box] < 0 && !preview && f.screenH > 0.0f) {
        C.chY[box] = (C.chY[box] * f.screenH + bodyY0 + bodyPx + pad * 0.8f) / f.screenH;
        C.chGrow[box] = 0; save_ui_config();
    }
    const bool growUp = C.chGrow[box] != 1;

    // ---- position ----
    float px, py;
    if (ovS > 0.0f) { px = snap(ovX - boxW * 0.5f); py = snap(ovY - boxH * 0.5f); }
    else            { px = snap(C.chX[box] * f.screenW); py = snap(C.chY[box] * f.screenH - (growUp ? boxH : 0.0f)); }
    if (preview && S > 0.0f) { g_pvSizeU[box][0] = boxW / S; g_pvSizeU[box][1] = boxH / S; }   // the config preview stacks the boxes : last frame's size
    const MouseState* m = f.mouse;
    if (editing) {
        // the CORNER GRIP first : it takes the shared drag lock, so box_edit cannot grab the same click
        // the CORNER GRIP sits on the MOVING edge (top-right when the box grows upward) : width + lines (or height)
        const float g = 16.0f * S, gx = px + boxW - g, gy = growUp ? py : py + boxH - g;
        const bool overGrip = m && m->x >= gx && m->x < px + boxW && m->y >= gy && m->y < gy + g;
        if (m && m->clicked && overGrip && !st.resizing && edit_drag_grab(&st.grip)) st.resizing = true;
        if (st.resizing) {
            if (m && m->down) {
                int w = (int)((m->x - px) / S + 0.5f); C.chW[box] = w < 200 ? 200 : (w > 1600 ? 1600 : w);
                const float span = growUp ? (py + boxH - m->y) : (m->y - py);    // the new box height, from the fixed edge
                if (C.chMaxLines[box] > 0) { int ln = (int)((span - bodyY0 - pad * 0.8f) / rowH + 0.5f); C.chMaxLines[box] = ln < 1 ? 1 : (ln > 40 ? 40 : ln); }
                else { int hh = (int)(span / S + 0.5f); C.chH[box] = hh < 80 ? 80 : (hh > 1400 ? 1400 : hh); }
            } else { st.resizing = false; edit_drag_release(&st.grip); save_ui_config(); }
        }
        if (!st.resizing) {   // box_edit stores the TOP : turn it back into the fixed edge (and save once the drop is done)
            float topY = py / (f.screenH > 0.0f ? f.screenH : 1.0f); const float y0 = C.chY[box];
            box_edit(f, st.eb, EDITBOX_CHAT + box, px, py, boxW, boxH, C.chScale[box], C.chX[box], topY, 0);
            C.chY[box] = growUp ? topY + boxH / f.screenH : topY;
            if (C.chY[box] != y0 && !st.eb.dragging) save_ui_config();
        }
    } else if (ovS <= 0.0f) box_on_screen(f, px, py, boxW, boxH);

    st.rx = px; st.ry = py; st.rw = boxW; st.rh = boxH;                    // next frame's hover test
    if (!preview && !editing && A > 0.05f) { g_liveRect[box][0] = px; g_liveRect[box][1] = py; g_liveRect[box][2] = boxW; g_liveRect[box][3] = boxH; g_liveMs[box] = nowMs; }
    if (!preview && f.screenH > 0.0f) { C.chEdge[box][0] = py / f.screenH; C.chEdge[box][1] = (py + boxH) / f.screenH; }
    // the rect the mouse callback tests (live boxes only) -- a box faded near nothing no longer eats clicks
    if (!preview && f.screenW > 0.0f && f.screenH > 0.0f && A > 0.5f) {
        C.chHit[box][0] = px / f.screenW; C.chHit[box][1] = py / f.screenH;
        C.chHit[box][2] = boxW / f.screenW; C.chHit[box][3] = boxH / f.screenH;
    }
    if (!preview && f.screenW > 0.0f && f.screenH > 0.0f) {
        if (box == 0 && !editing) {   // the game's input line docks under this box (native_ui.cpp) ; not while dragging
            C.chDockRect[0] = px; C.chDockRect[1] = py; C.chDockRect[2] = boxW; C.chDockRect[3] = boxH;
            C.chDockRect[4] = f.screenW; C.chDockRect[5] = f.screenH;
        }
    }

    // ---- chrome ----
    dColorQuadState(dev);
    if (A <= 0.003f && !editing) return;                                  // fully faded : nothing to draw
    const u32 accent = box_style_border_color(dev, f.skin, C.chBox[box]) | 0xFF000000u;
    const u32 accentLite = mix(accent, 0xFFFFFFFFu, 0.35f);
    // FOCUSED (the game's log window is selected) : a soft halo of the frame's own colour BEHIND the box, breathing
    // slowly, the box turned opaque, and a fine light rim on its edge -- subtle, but readable at a glance (2026-11-16 :
    // "une surbrillance du chat, quelque chose de subtil mais visible" ; a pointer said nothing).
    const float fA = A * st.foc, breath = 0.80f + 0.20f * sinf(f.t * 2.4f);
    if (fA > 0.0f) {
        rrect_glow(dev, px, py, boxW, boxH, 6.0f * S, with_alpha(mix(accentLite, 0xFFFFFFFFu, 0.15f) & 0xC8FFFFFFu, fA * breath), 11.0f * S);
        dColorQuadState(dev);
    }
    draw_themed_box(dev, f.skin, px, py, boxW, boxH, C.chBox[box], A, S);
    if (fA > 0.0f) {
        if (C.chBox[box].on) {                     // the same frame again at full opacity, over the translucent one
            BoxStyle op = C.chBox[box]; op.alpha = 1.0f; op.alphaOwn = 1;
            draw_themed_box(dev, f.skin, px, py, boxW, boxH, op, fA, S);
            dColorQuadState(dev);
        } else {                                   // no frame at all : a plain dark panel
            rrect(dev, px, py, boxW, boxH, 6.0f * S, with_alpha(0xF0141A24u, fA), with_alpha(0xF00A0E14u, fA));
        }
        // the WHOLE box lit : a wash of the frame's colour over its surface (lighter at the top, like light falling on it)
        rrect(dev, px + 1.0f * S, py + 1.0f * S, boxW - 2.0f * S, boxH - 2.0f * S, 6.0f * S,
              with_alpha(accentLite & 0x48FFFFFFu, fA * breath), with_alpha(accent & 0x20FFFFFFu, fA * breath));
        rrect_stroke(dev, px + 0.5f * S, py + 0.5f * S, boxW - 1.0f * S, boxH - 1.0f * S, 6.0f * S, with_alpha(mix(accentLite, 0xFFFFFFFFu, 0.3f), fA * breath), 1.5f * S);
    }

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

    // ---- header : the tabs only -- the first one ("Chat" / "Battle") IS the box's name ----
    const float headY = py + pad, headCY = headY + headH * 0.5f; (void)headCY; (void)headY;
    (void)titleZ;

    // ---- header : tabs (text + sliding underline + count badge) ----
    // the tabs start INSIDE the header band, with room for the open tab's pill (+ its glow) on either side
    const float pillPad = 6.0f * S, bandX = px + pad * 0.5f, bandW = boxW - pad;
    const float tabsRight = bandX + bandW - pillPad - 3.0f * S;
    const float tx0 = bandX + pillPad + 3.0f * S;
    float tx = tx0;
    float actX = -1.0f, actW = 0.0f;
    // The header is a BAND a shade lighter than the body (no rule line) ; the open tab sits on a pill that slides to it,
    // drawn first with last frame's eased place so the icons and the label sit on top. EVERYTHING in it is centred on
    // the band's own middle (bandCY), and the icons are as big as the pill allows (2026-11-16 : "les icônes le plus
    // gros possible sans déformer le header ni dépasser", "bien centré horizontalement et verticalement").
    const float bandY = py + pad * 0.45f, bandH = headH + pad * 0.55f + headGap * 0.4f, bandCY = bandY + bandH * 0.5f;
    const float ph = snap(bandH - 4.0f * S), bandPh = ph;                    // the pill : the band less a small margin
    const float iz = snap((ph - 1.0f * S) / 1.18f);                          // icon box : its drawn size (x1.18) fills the pill
    dColorQuadState(dev);
    rrect(dev, snap(bandX), snap(bandY), snap(bandW), snap(bandH), 7.0f * S, with_alpha(0x16FFFFFFu, A), with_alpha(0x08FFFFFFu, A));
    if (st.ulX >= 0.0f) {
        const float ph = fullOn ? snap(bandH - 8.0f * S) : bandPh;          // unfolded : the size of the tab capsules
        const float pyy = snap(bandCY - ph * 0.5f);
        float pxl = px + st.ulX - pillPad, pw = st.ulW + 2.0f * pillPad;
        if (pxl < bandX + 1.0f * S) { pw -= bandX + 1.0f * S - pxl; pxl = bandX + 1.0f * S; }                 // never past the band
        if (pxl + pw > bandX + bandW - 1.0f * S) pw = bandX + bandW - 1.0f * S - pxl;
        pxl = snap(pxl); pw = snap(pw);
        rrect_glow(dev, pxl, pyy, pw, ph, ph * 0.5f, with_alpha((accent & 0x00FFFFFFu) | 0x30000000u, A), 3.0f * S);
        rrect(dev, pxl, pyy, pw, ph, ph * 0.5f, with_alpha((accent & 0x00FFFFFFu) | 0x3C000000u, A), with_alpha((accent & 0x00FFFFFFu) | 0x22000000u, A));
        rrect_stroke(dev, pxl, pyy, pw, ph, ph * 0.5f, with_alpha((accent & 0x00FFFFFFu) | 0x80000000u, A), 1.0f);
    }
    // FIXED SLOTS : every tab keeps its place whatever is open -- only the pill moves. With room, every tab shows its
    // icon AND name, plus a reserved spot for its unread count (so a count appearing shifts nothing) ; too narrow,
    // every tab is an icon only. The count sits AFTER the name, never under the pill of a neighbour.
    // FIXED SLOTS, left to right : every tab keeps its place whatever is open -- only the pill moves.
    const float minGap = (dn == 0 ? 12.0f : 15.0f) * S, cntSlot = snap(fo->measure("99", badgeZ) + 12.0f * S);
    const float availW = tabsRight - tx0;
    float lws[8]; float need = 0.0f;
    for (int t = 0; t < nTabs; ++t) { lws[t] = fo->measure(tr(tab_def(box, t).en, tab_def(box, t).fr), tabZ); need += iz + 6.0f * S + lws[t] + cntSlot + (t ? minGap : 0.0f); }
    const int tst = C.chTabStyle[box];                                       // 0 auto, 1 icon + name, 2 icon, 3 name
    const bool named = tst == 1 || tst == 3 || (tst == 0 && need <= availW);
    const bool iconed = tst != 3;
    const float izU = iconed ? iz : 0.0f, icGap = (iconed && named) ? 6.0f * S : 0.0f;
    // the tabs that fit, and the even gap around them (the count's spot rides in the gap after its name)
    int nFit = 0; float sumW = 0.0f;
    for (int t = 0; t < (fullOn ? 0 : nTabs); ++t) {
        const float tw = named ? izU + icGap + lws[t] : izU;
        const float add = tw + (named ? cntSlot : 0.0f) + (nFit ? minGap : 0.0f);
        if (sumW + add > availW && nFit) break;
        sumW += add; ++nFit;
    }
    tx = tx0;                                                                // left to right, packed (the user, 2026-11-16 : not spread)
    for (int t = 0; t < nFit; ++t) {
        const ChatTab& T = tab_def(box, t);
        const bool active = t == tab;
        const char* lbl = tr(T.en, T.fr);
        const float tw = named ? izU + icGap + lws[t] : izU;                 // the part the pill covers
        tx = snap(tx);
        const float slot = named ? tw + cntSlot : izU + 8.0f * S;              // the tab's whole fixed place (its count's spot included)
        const bool hover = canClick && m->x >= tx - 5.0f * S && m->x < tx + slot && m->y >= bandY && m->y < bandY + bandH;
        if (hover && m->clicked && !active) { C.chTab[box] = t; tab = t; st.unread[t] = 0; save_ui_config(); }
        st.hov[t] += ((hover ? 1.0f : 0.0f) - st.hov[t]) * kFast;
        const u32 ic = icon_col(T.icon);
        const u32 icc = active ? ic : mix(mix(ic, 0xFF6A7586u, 0.55f), ic, st.hov[t]);   // idle tabs : the colour, muted
        dColorQuadState(dev);
        if (iconed) {
            if (active) disc_glow(dev, tx + iz * 0.5f, bandCY, iz * 0.30f, with_alpha((ic & 0x00FFFFFFu) | 0x30000000u, A), 6.0f * S);
            if (!draw_tab_pearl(dev, T.icon, tx + iz * 0.5f, bandCY, iz, with_alpha(icc, A)))
                draw_tab_icon(dev, T.icon, tx + iz * 0.5f, bandCY, iz * 0.85f, S, with_alpha(icc, A));
        }
        if (named) {
            const u32 lc = active ? 0xFFFFFFFFu : mix(0xFF8794A8u, 0xFFDDE3EBu, st.hov[t]);
            fo->begin(dev);
            fo->draw_lc(dev, tx + izU + icGap, bandCY, lbl, tabZ, with_alpha(lc, A), with_alpha(0xD0000000u, A), tow);
        }
        if (!active && st.unread[t] > 0) {                                  // the count : after the name (or on the icon, icons-only)
            char bt[8]; if (st.unread[t] > 99) lstrcpynA(bt, "99+", sizeof(bt)); else _snprintf(bt, sizeof(bt), "%d", st.unread[t]);
            bt[sizeof(bt) - 1] = 0;
            const float bh = snap(13.0f * S); float bw = snap(fo->measure(bt, badgeZ) + 8.0f * S); if (bw < bh) bw = bh;
            const float bx = named ? snap(tx + tw + 5.0f * S) : snap(tx + iz - bw * 0.45f);
            const float by = named ? snap(bandCY - bh * 0.5f) : snap(bandCY - iz * 0.5f - bh * 0.35f);
            dColorQuadState(dev);
            rrect_glow(dev, bx, by, bw, bh, bh * 0.5f, with_alpha((accent & 0x00FFFFFFu) | 0x50000000u, A), 3.0f * S);
            rrect(dev, bx, by, bw, bh, bh * 0.5f, with_alpha(accentLite, A), with_alpha(accent, A));
            fo->begin(dev);
            fo->draw_cc(dev, bx + bw * 0.5f, by + bh * 0.5f, bt, badgeZ, with_alpha(0xFFFFFFFFu, A), with_alpha(0x90000000u, A), tow * 0.7f);
        }
        if (active) { actX = tx - px; actW = tw; }
        tx += slot + minGap;
    }
    if (fullOn) {   // unfolded : EVERY tab of the game's expanded log by NAME (no icons), in X's order -- one capsule each, the
                    // sliding pill on the game's current one ; ONE baseline for all (draw_lc centred each word on its own ink :
                    // "Say" / "Party" sat lower than "Tell", 2026-11-16)
        float lw11[11], sumL = 0.0f;
        for (int k = 0; k < 11; ++k) { const ChatTab& G = FULL_TABS[FULL_ORDER[k]]; lw11[k] = fo->measure(tr(G.en, G.fr), tabZ); sumL += lw11[k]; }
        const float minG = 2.0f * pillPad + 4.0f * S;                        // capsules never touch
        float zF = tabZ, gapF = minGap > minG ? minGap : minG;
        if (sumL + 10.0f * gapF > availW) {                                  // too wide : tighter gaps first, then a smaller text
            gapF = (availW - sumL) / 10.0f;
            if (gapF < minG) { gapF = minG; float k = (availW - 10.0f * gapF) / sumL; if (k < 0.6f) k = 0.6f; zF = tabZ * k; }
        }
        const float kz = zF / tabZ;
        float capT = 0.0f, capB = 0.0f; fo->ink_v("H", zF, capT, capB);
        const float ty = bandCY - (capT + capB) * 0.5f;                     // the cell top that puts every capital on bandCY
        const float chH = snap(ph - 4.0f * S), chY = snap(bandCY - chH * 0.5f);
        tx = tx0;
        for (int k = 0; k < 11; ++k) {
            const int id = FULL_ORDER[k]; const ChatTab& G = FULL_TABS[id];
            const bool active = id == FL.tab;
            const float tw = lw11[k] * kz;
            if (tx + tw > tabsRight + 0.5f) break;
            tx = snap(tx);
            if (!active) {                                                   // the capsule (the open one wears the sliding pill)
                const float cx = snap(tx - pillPad), cw = snap(tw + 2.0f * pillPad);
                dColorQuadState(dev);
                rrect(dev, cx, chY, cw, chH, chH * 0.5f, with_alpha(0x22FFFFFFu, A), with_alpha(0x0CFFFFFFu, A));
                rrect_stroke(dev, cx, chY, cw, chH, chH * 0.5f, with_alpha(0x30C8D6EAu, A), 1.0f);
            }
            fo->begin(dev);
            fo->draw(dev, tx, ty, tr(G.en, G.fr), zF, with_alpha(active ? 0xFFFFFFFFu : 0xFFAAB5C4u, A), with_alpha(0xD0000000u, A), tow);
            if (active) { actX = tx - px; actW = tw; }
            tx += tw + gapF;
        }
    }
    // the pill slides to the open tab (eased ; drawn next frame, under the icons)
    if (actX >= 0.0f) {
        if (st.ulX < 0.0f) { st.ulX = actX; st.ulW = actW; }
        st.ulX += (actX - st.ulX) * kSoft; st.ulW += (actW - st.ulW) * kSoft;
    }
    if (!src.sample) st.unread[tab] = 0;

    // ---- advance table for this font + size : wrapping and run placement without a measure() per char ----
    // rebuilt when the face / size changes -- AND until the size's own atlas exists : measured on a NEIGHBOUR size
    // (the first frames, bake budget) the advances are that size's, scaled, and they were cached for the session ; the
    // text is drawn with the real ones, so every colour run after a long one landed short ("le point est dans le d de
    // blood", "a" glued to an item name -- 2026-11-16, the raw text was clean)
    if (st.advFont != fo || st.advSize != tz || !st.advExact) {
        st.advFont = fo; st.advSize = tz; st.advExact = fo->baked(tz);
        build_adv(st.adv, fo, tz);
    }
    s_wFo = fo; s_wTz = tz;                                               // the wide characters : measured by this font, this size
    adv_bold(tz);                                                         // ... and the bold auto-translate words by its bold twin
    const float* adv = st.adv;
    char stampFmt[12] = "00:00";
    const float stampW = C.chStamp[box] ? fo->measure("00:00", tz * 0.86f) + 9.0f * S : 0.0f;
    const float avail = boxW - 2.0f * pad - 2.0f * S - stampW;            // wrapped rows align under the text, after the stamp
    const ChatTab& T = fullOn ? fullT : tab_def(box, tab);                   // unfolded : the game's tab filters it
    const int tabKey = fullOn ? 100 + FL.tab : tab;

    // ---- scrolling : st.scroll = the TARGET (whole rows up from the newest), st.vpos = what is drawn ----
    if (st.lastTab != tabKey) { st.lastTab = tabKey; st.scroll = 0; st.vpos = 0.0f; st.newBelow = 0; st.lastSeq = newest; }
    if (!src.sample && newest != st.lastSeq) {
        // rows the new lines add to THIS tab : at the bottom they slide in from below ; scrolled up, the view holds
        int add = 0, msgs = 0; short sa[24], sb[24];
        for (int i = src.n() - 1; i >= 0; --i) {
            const ChatLine* L = src.at(i); if (!L || L->seq <= st.lastSeq) break;
            if (line_in_tab(*L, T, me)) { LinePrefix lp; line_prefix(*L, fo, bz, adv, S, lp); add += wrap_line(*L, adv, chat_punct_gap(tz), avail, sa, sb, 24, lp.w); if (!L->cont) ++msgs; }
        }
        st.lastSeq = newest;
        if (add > 0) {
            st.vpos += (float)add;
            if (st.scroll > 0) { st.scroll += add; st.newBelow += msgs; }
        }
        if (st.vpos > (float)st.scroll + lines * 2.0f) st.vpos = (float)st.scroll + lines * 2.0f;   // a burst : don't crawl
    }
    if (!preview) {
        if (C.chWheel[box] != 0) { st.scroll += C.chWheel[box] * 3; C.chWheel[box] = 0; }
        if (fullOn) st.scroll = FL.back;                                     // unfolded : the game's own scroll (its lines back)
        if (st.scroll < 0) st.scroll = 0;
        if (st.scroll == 0) st.newBelow = 0;
    } else { st.scroll = 0; st.vpos = 0.0f; }
    st.vpos += ((float)st.scroll - st.vpos) * kSoft;
    if (fabsf((float)st.scroll - st.vpos) < 0.01f) st.vpos = (float)st.scroll;

    // ---- collect the visible rows, newest first (one extra : the row sliding through the top) ----
    int want = (int)linesVis + 2; if (want > 150) want = 150;            // the unfolded box shows many more rows
    ChatRow rows[152]; int nr = 0;
    int skip = (int)st.vpos; const float frac = st.vpos - (float)skip;
    int skipLeft = st.scroll;                                               // to clamp the TARGET to the history
    for (int i = src.n() - 1; i >= 0 && (nr < want || skipLeft > 0); --i) {
        const ChatLine* L = src.at(i); if (!L || !line_in_tab(*L, T, me)) continue;
        LinePrefix lp; line_prefix(*L, fo, bz, adv, S, lp);
        short sa[24], sb[24]; const int ns = wrap_line(*L, adv, chat_punct_gap(tz), avail, sa, sb, 24, lp.w);
        for (int k = ns - 1; k >= 0; --k) {
            if (skipLeft > 0) --skipLeft;
            if (skip > 0) { --skip; continue; }
            if (nr < want) { ChatRow& R = rows[nr++]; R.L = L; R.a = sa[k]; R.b = sb[k]; R.first = (k == 0) && !L->cont; }
        }
    }
    if (skipLeft > 0 && !preview) { st.scroll -= skipLeft; if (st.scroll < 0) st.scroll = 0; }   // past the oldest line

    // ---- draw the rows, bottom up : ONE baseline for every coloured run, top edge fading, clipped to the body ----
    const float bodyTop = py + bodyY0, bodyH = bodyNow, bodyBot = bodyTop + bodyH;
    const float x0 = px + pad + 1.0f * S;
    float refT = 0.0f, refB = 0.0f; fo->ink_v("Hg", tz, refT, refB);
    const float yOff = (refT + refB) * 0.5f;                                // cell top -> visual centre, the same for every run
    // where the EYE puts the middle of a line : the middle of its capitals (descenders don't count). A badge is centred
    // there, not on cy -- centred on cy it sat visibly low against the text (2026-11-15).
    float capT = 0.0f, capB = 0.0f; fo->ink_v("H", tz, capT, capB);
    const float capMid = (capT + capB) * 0.5f - yOff;                       // offset from cy to the capitals' middle
    float sT = 0.0f, sB = 0.0f; fo->ink_v("00:00", tz * 0.86f, sT, sB);
    const float sOff = (sT + sB) * 0.5f;
    float cy = bodyBot - 0.5f * rowH + frac * rowH;
    const float fadeH = rowH * 1.1f;
    char buf[CHAT_TEXT * 2 + 1];
    const float gapP = chat_punct_gap(tz);
    clip_rect_begin(dev, px + 2.0f * S, bodyTop, boxW - 4.0f * S, bodyH);
    fo->begin(dev);
    int rowHitN = 0;
    if (nr == 0)
        fo->draw_cc(dev, px + boxW * 0.5f, bodyTop + bodyH * 0.5f, tr("No messages yet", "Aucun message pour l'instant"), tz * 0.92f, with_alpha(0xFF6E7A8Cu, A), with_alpha(0xC0000000u, A), tow);
    for (int r = 0; r < nr; ++r, cy -= rowH) {
        const ChatRow& R = rows[r]; const ChatLine& L = *R.L;
        const float rTop = cy - rowH * 0.5f;
        float a = overflow ? (rTop - bodyTop) / fadeH + 0.35f : 1.0f; if (a > 1.0f) a = 1.0f;   // the top edge fades out (only when more is above)
        const float aBot = (bodyBot - rTop) / rowH; if (aBot < a) a = aBot;    // a row sliding in fades in
        if (collapsing && !fitBox && r >= C.chFoldLines[box]) {            // fixed box : an old line fades out in place
            const unsigned age = nowMs - L.arrivedTick;
            if (age > keepMs) { float k = 1.0f - (float)(age - keepMs) / 900.0f; if (k < 0.0f) k = 0.0f; a *= k; }
        }
        a *= A;
        if (a <= 0.02f) continue;
        // talk lines : the name carries the channel colour, the message a light tint of it (easier to read) ;
        // system / battle / NPC lines keep the colour the client gives their mode
        const bool talk = L.chan != CH_SYSTEM && L.chan != CH_BATTLE && L.chan != CH_NPC && L.chan != CH_OTHER;
        const u32 base = te_col(TS, talk ? mix(CHAN_COL[L.chan], 0xFFFFFFFFu, 0.55f)
                                         : (L.modeArgb ? (L.modeArgb | 0xFF000000u) : CHAN_COL[L.chan < CH_COUNT ? L.chan : CH_OTHER]));
        const u32 stroke = with_alpha(0xE0000000u, a);
        if (C.chStamp[box] && R.first) {
            _snprintf(stampFmt, sizeof(stampFmt), "%02u:%02u", L.hhmm / 100, L.hhmm % 100); stampFmt[sizeof(stampFmt) - 1] = 0;
            fo->draw(dev, x0, cy - sOff, stampFmt, tz * 0.86f, with_alpha(0xFF5F6B7Eu, a), stroke, tow * 0.8f);
        }
        float x = x0 + stampW;
        const u32 chanC = CHAN_COL[L.chan < CH_COUNT ? L.chan : CH_OTHER];
        if (R.first) {                                                      // the badge, then the speaker's name
            LinePrefix lp; line_prefix(L, fo, bz, adv, S, lp);
            if (lp.badge) {
                const float bh = snap(rowH * 0.70f), by = snap(cy + capMid - bh * 0.5f), bx = snap(x);
                dColorQuadState(dev);
                rrect(dev, bx, by, lp.badgeW, bh, 3.0f * S, with_alpha((chanC & 0x00FFFFFFu) | 0x3A000000u, a), with_alpha((chanC & 0x00FFFFFFu) | 0x22000000u, a));
                rrect_stroke(dev, bx, by, lp.badgeW, bh, 3.0f * S, with_alpha((chanC & 0x00FFFFFFu) | 0x8C000000u, a), 1.0f);
                fo->begin(dev);
                fo->draw_cc(dev, bx + lp.badgeW * 0.5f, by + bh * 0.5f, lp.badge, bz, with_alpha(chanC, a), with_alpha(0xB0000000u, a), tow * 0.7f);
                x += lp.badgeW + 6.0f * S;
            }
            if (L.chSpeaker[0]) {
                fo->begin(dev);
                fo->draw(dev, x, cy - yOff, L.chSpeaker, tz, with_alpha(chanC, a), stroke, tow);
                x += lp.nameW + 7.0f * S;
            }
        }
        if (!src.sample && rowHitN < 152) { RowHit& H = g_rowHit[box][rowHitN++]; H.L = &L; H.a = R.a; H.b = R.b; H.x0 = x; H.cy = cy; }
        if (st.selOn && !src.sample) {                                      // the selection on this row
            unsigned s0 = st.selASeq, s1 = st.selFSeq; int i0 = st.selAIdx, i1 = st.selFIdx;
            if (s0 > s1 || (s0 == s1 && i0 > i1)) { const unsigned ts = s0; s0 = s1; s1 = ts; const int ti = i0; i0 = i1; i1 = ti; }
            if (L.seq >= s0 && L.seq <= s1) {
                int sa0 = L.seq == s0 ? i0 : 0, sb0 = L.seq == s1 ? i1 : L.len;
                if (sa0 < R.a) sa0 = R.a; if (sb0 > R.b) sb0 = R.b;
                if (sb0 > sa0) {
                    const float xs = row_x_at(L, R.a, R.b, x, sa0, adv, gapP), xe = row_x_at(L, R.a, R.b, x, sb0, adv, gapP);
                    dColorQuadState(dev);
                    // clearly visible (2026-11-17 : "pas assez visible") : a solid bright band, a light rim
                    rrect(dev, snap(xs - 1.0f * S), snap(cy - rowH * 0.5f), snap(xe - xs + 2.0f * S), rowH, 3.0f * S, with_alpha(0xB04D8FE0u, a), with_alpha(0xB03A72C0u, a));
                    rrect_stroke(dev, snap(xs - 1.0f * S), snap(cy - rowH * 0.5f), snap(xe - xs + 2.0f * S), rowH, 3.0f * S, with_alpha(0xC0A8D0FFu, a), 1.0f);
                    fo->begin(dev);
                }
            }
        }
        short lka[4], lkb[4]; const int nlk = find_links(L, lka, lkb, 4);  // the links : light blue, underlined under the mouse
        auto lk_at = [&](int j) -> int { for (int q = 0; q < nlk; ++q) if (j >= lka[q] && j < lkb[q]) return q; return -1; };
        int i = R.a;                                                        // runs of one colour, placed by the advance sum
        unsigned char lastc = 0;                                            // the character before (punct_pre), across runs
        while (i < R.b) {
            const unsigned char cc = L.col[i]; int j = i, n = 0; float w = 0.0f;
            const int li = lk_at(i);
            const bool bold = L.atIdx && cc == L.atIdx;                     // the auto-translated words : bold
            const unsigned char prev0 = lastc;                              // a run after a colour change keeps the rule
            while (j < R.b && L.col[j] == cc && n < CHAT_TEXT * 2 - 6
                   && lk_at(j) == li) {    // whole characters, copied as they are (UTF-8)
                int k; unsigned cp; const float cw = cw_at(adv, L.text, j, L.len, k, cp, bold);
                const unsigned char c8 = punct_c(cp);
                w += cw + punct_pre(j > R.a ? lastc : 0, c8, gapP);
                for (int q = 0; q < k; ++q) buf[n++] = L.text[j + q];
                j += k; lastc = c8;
            }
            buf[n] = 0;
            if (n) (bold ? fb : fo)->draw_punct(dev, x, cy - yOff, buf, tz, with_alpha(li >= 0 ? 0xFF7CC4FFu : code_col(L, cc, base), a), stroke, tow, gapP, prev0);
            if (li >= 0 && st.linkSeq == L.seq && st.linkA == lka[li]) {   // the hovered link : underlined
                dColorQuadState(dev);
                rrect(dev, snap(x), snap(cy - yOff + capB + 1.0f * S), snap(w), snap(1.0f * S) < 1.0f ? 1.0f : snap(1.0f * S), 0.0f, with_alpha(0xFF7CC4FFu, a), with_alpha(0xFF7CC4FFu, a));
                fo->begin(dev);
            }
            x += w; i = j;
        }
    }
    clip_rect_end(dev);
    if (!src.sample) g_rowHitN[box] = rowHitN;

    // ---- the mouse on the text : select (drag), copy (release), a link (click) ----
    {
        const int nh = src.sample ? 0 : g_rowHitN[box];
        const RowHit* RH = g_rowHit[box];
        // nearest drawn row to y : its index under x (above the top row : its start, below the bottom one : its end)
        auto hit = [&](float mx, float my, unsigned& seq, int& idx, bool& onRow) -> bool {
            onRow = false; if (nh <= 0) return false;
            int best = 0; float bd = 1e9f;
            for (int r = 0; r < nh; ++r) { const float d = fabsf(my - RH[r].cy); if (d < bd) { bd = d; best = r; } }
            const RowHit& H = RH[best];
            seq = H.L->seq; idx = row_index_at(H, mx, adv, gapP);
            if (my < H.cy - rowH * 0.5f && best == nh - 1) idx = H.a;
            else if (my > H.cy + rowH * 0.5f && best == 0) idx = H.b;
            onRow = bd <= rowH * 0.5f && mx >= H.x0 && mx < row_x_at(*H.L, H.a, H.b, H.x0, H.b, adv, gapP);
            return true;
        };
        // the link under (mx, my) : its url, the line seq and its start
        auto link_at = [&](float mx, float my, char* url, int cap, unsigned& seq, int& la0) -> bool {
            unsigned s; int idx; bool on;
            if (!hit(mx, my, s, idx, on) || !on) return false;
            for (int r = 0; r < nh; ++r) if (RH[r].L->seq == s && fabsf(my - RH[r].cy) <= rowH * 0.5f) {
                const ChatLine& L = *RH[r].L; short la[4], lb[4]; const int nl = find_links(L, la, lb, 4);
                for (int q = 0; q < nl; ++q) if (idx >= la[q] && idx < lb[q]) {
                    int n = lb[q] - la[q]; if (n > cap - 1) n = cap - 1;
                    memcpy(url, L.text + la[q], (size_t)n); url[n] = 0; seq = s; la0 = la[q]; return true;
                }
            }
            return false;
        };
        const bool inBody = m && m->x >= px && m->x < px + boxW && m->y >= bodyTop && m->y < bodyBot;
        const bool overPill = st.scroll > 0 && m && m->y >= bodyBot - 30.0f * S;
        // the hovered link (underlined next frame)
        st.linkSeq = 0; st.linkA = -1;
        if (canClick && inBody && !st.selMoved) { char u[8]; unsigned s; int la0; if (link_at(m->x, m->y, u, sizeof(u), s, la0)) { st.linkSeq = s; st.linkA = la0; } }
        // the link popup's buttons (it is drawn at the end) : a click there is the popup's
        float lpX = 0, lpY = 0, lpW = 0, lpH = 0, lpB1 = 0, lpB2 = 0, lpBY = 0, lpBW = 0, lpBH = 0;
        const float lpz = 12.0f * S;
        char lpShow[64] = "";
        if (g_lp.on && g_lp.box == box) {
            if (nowMs - g_lp.ms > 10000u) g_lp.on = false;
            int n = (int)strlen(g_lp.url); if (n > 44) { memcpy(lpShow, g_lp.url, 41); lpShow[41] = lpShow[42] = lpShow[43] = '.'; lpShow[44] = 0; } else lstrcpynA(lpShow, g_lp.url, sizeof(lpShow));
            lpBW = snap(fo->measure(tr("Copy", "Copier"), lpz) + 26.0f * S); { const float o = snap(fo->measure(tr("Open", "Ouvrir"), lpz) + 26.0f * S); if (o > lpBW) lpBW = o; }
            lpBH = snap(24.0f * S);
            lpW = snap(fo->measure(lpShow, lpz) + 20.0f * S); if (lpW < 2.0f * lpBW + 30.0f * S) lpW = snap(2.0f * lpBW + 30.0f * S);
            lpH = snap(10.0f * S + lpz * 1.4f + 8.0f * S + lpBH + 10.0f * S);
            lpX = snap(g_lp.x - lpW * 0.5f); if (lpX + lpW > px + boxW - 4.0f * S) lpX = snap(px + boxW - 4.0f * S - lpW); if (lpX < px + 4.0f * S) lpX = snap(px + 4.0f * S);
            lpY = snap(g_lp.y - lpH - 8.0f * S); if (lpY < py + 4.0f * S) lpY = snap(g_lp.y + 14.0f * S);
            lpBY = snap(lpY + lpH - 10.0f * S - lpBH);
            lpB2 = snap(lpX + lpW - 10.0f * S - lpBW); lpB1 = snap(lpB2 - 8.0f * S - lpBW);
        }
        bool eaten = false;
        // ---- the line menu : its geometry (drawn below), its click ----
        const char* CML[5] = { tr("Copy the line", "Copier la ligne"), tr("Copy the message", "Copier le message"), tr("Copy the name", "Copier le nom"), tr("Copy the link", "Copier le lien"),
                               tr("Copy the selection", "Copier la s\xC3\xA9lection") };
        char banLbl[64]; _snprintf(banLbl, sizeof(banLbl), tr("Ban %s", "Bannir %s"), g_cm.name); banLbl[sizeof(banLbl) - 1] = 0;
        auto cm_label = [&](int it) -> const char* { return it == 5 ? banLbl : CML[it]; };
        int cmIt[6]; int cmN = 0; float cmX = 0, cmY = 0, cmW = 0, cmIH = 0, cmH = 0; const float cmz = 12.5f * S;
        if (g_cm.on && g_cm.box == box) {
            if (st.selOn) cmIt[cmN++] = 4;
            cmIt[cmN++] = 0; cmIt[cmN++] = 1; if (g_cm.name[0]) cmIt[cmN++] = 2; if (g_cm.url[0]) cmIt[cmN++] = 3;
            if (g_cm.name[0] && _stricmp(g_cm.name, me)) cmIt[cmN++] = 5;   // never yourself
            cmIH = snap(26.0f * S); for (int q = 0; q < cmN; ++q) { const float w = fo->measure(cm_label(cmIt[q]), cmz) + 28.0f * S; if (w > cmW) cmW = w; }
            cmW = snap(cmW); cmH = snap((float)cmN * cmIH + 8.0f * S);
            cmX = snap(g_cm.x); if (cmX + cmW > px + boxW - 4.0f * S) cmX = snap(px + boxW - 4.0f * S - cmW); if (cmX < px + 4.0f * S) cmX = snap(px + 4.0f * S);
            cmY = snap(g_cm.y); if (cmY + cmH > py + boxH - 4.0f * S) cmY = snap(g_cm.y - cmH); if (cmY < py + 4.0f * S) cmY = snap(py + 4.0f * S);
            if (canClick && (m->clicked || m->rightClicked)) {
                const bool inM = m->x >= cmX && m->x < cmX + cmW && m->y >= cmY + 4.0f * S && m->y < cmY + cmH - 4.0f * S;
                if (inM && m->clicked) {
                    const int it = cmIt[(int)((m->y - cmY - 4.0f * S) / cmIH) < cmN ? (int)((m->y - cmY - 4.0f * S) / cmIH) : cmN - 1];
                    static char tb[2048]; int n = 0;
                    if (it == 4) {                                          // the selection, in the box's order
                        unsigned s0 = st.selASeq, s1 = st.selFSeq; int i0 = st.selAIdx, i1 = st.selFIdx;
                        if (s0 > s1 || (s0 == s1 && i0 > i1)) { const unsigned ts = s0; s0 = s1; s1 = ts; const int ti = i0; i0 = i1; i1 = ti; }
                        static char out[48 * 1024]; int o = 0;
                        for (int i = 0; i < chat_count() && o < (int)sizeof(out) - CHAT_TEXT - 4; ++i) {
                            const ChatLine* L = chat_line(i); if (!L || L->seq < s0 || L->seq > s1 || !line_in_tab(*L, T, me)) continue;
                            int a0 = L->seq == s0 ? i0 : 0; const int b0 = L->seq == s1 ? i1 : L->len;
                            if (a0 == L->chBodyAt && !L->cont) a0 = 0;      // from the message's start : its speaker too, as the client writes it
                            if (b0 <= a0) continue;
                            if (o) { out[o++] = '\r'; out[o++] = '\n'; }
                            memcpy(out + o, L->text + a0, (size_t)(b0 - a0)); o += b0 - a0;
                        }
                        const bool ok = clip_put_utf8(out, o);
                        st.toastMs = nowMs; st.toastX = m->x; st.toastY = m->y; st.toastKind = ok ? 1 : 3;
                        st.selOn = false; g_cm.on = false; eaten = true;
                        goto cm_done;
                    }
                    if (it == 5) {                                          // ban : the speaker's lines leave the boxes
                        const bool ok = chat_ban_add(g_cm.name);
                        st.toastMs = nowMs; st.toastX = m->x; st.toastY = m->y; st.toastKind = ok ? 4 : 5;
                        g_cm.on = false; eaten = true;
                        goto cm_done;
                    }
                    if (it == 0 || it == 1) n = message_text(g_cm.seq, it == 1, tb, sizeof(tb));
                    else if (it == 2) { lstrcpynA(tb, g_cm.name, sizeof(tb)); n = (int)strlen(tb); }
                    else { lstrcpynA(tb, g_cm.url, sizeof(tb)); n = (int)strlen(tb); }
                    const bool ok = n > 0 && clip_put_utf8(tb, n);
                    st.toastMs = nowMs; st.toastX = m->x; st.toastY = m->y; st.toastKind = ok ? (it == 3 ? 2 : 1) : 3;
                }
                g_cm.on = false; eaten = true;                            // any click with the menu up closes it
            }
            cm_done:;
        }
        if (!eaten && canClick && m->rightClicked && inBody && !overPill) {   // a right-click on a line : its menu
            unsigned s; int idx; bool on;
            if (hit(m->x, m->y, s, idx, on)) {
                for (int r = 0; r < nh; ++r) if (RH[r].L->seq == s && fabsf(m->y - RH[r].cy) <= rowH * 0.5f) {
                    unsigned first = s;                                     // a continuation : its message's first line
                    g_cm.name[0] = 0;
                    for (int i = chat_count() - 1; i >= 0; --i) { const ChatLine* L = chat_line(i); if (!L || L->seq > s) continue; if (!L->cont) { first = L->seq; lstrcpynA(g_cm.name, L->chSpeaker, sizeof(g_cm.name)); break; } }
                    g_cm.url[0] = 0; { short la[4], lb[4]; const ChatLine& L = *RH[r].L; const int nl = find_links(L, la, lb, 4);
                      if (nl > 0) { int n = lb[0] - la[0]; if (n > 255) n = 255; memcpy(g_cm.url, L.text + la[0], (size_t)n); g_cm.url[n] = 0; } }
                    g_cm.on = true; g_cm.box = box; g_cm.seq = first; g_cm.x = m->x; g_cm.y = m->y; g_lp.on = false;   // the selection stays : the menu copies it
                    eaten = true; break;
                }
            }
        }
        if (!eaten && canClick && m->clicked && g_lp.on && g_lp.box == box) {
            const bool inPop = m->x >= lpX && m->x < lpX + lpW && m->y >= lpY && m->y < lpY + lpH;
            const bool onB = m->y >= lpBY && m->y < lpBY + lpBH;
            if (inPop && onB && m->x >= lpB1 && m->x < lpB1 + lpBW) { open_url(g_lp.url); g_lp.on = false; }
            else if (inPop && onB && m->x >= lpB2 && m->x < lpB2 + lpBW) {
                const bool ok = clip_put_utf8(g_lp.url, (int)strlen(g_lp.url));
                st.toastMs = nowMs; st.toastX = m->x; st.toastY = m->y; st.toastKind = ok ? 2 : 3; g_lp.on = false;
            }
            else if (!inPop) g_lp.on = false;
            eaten = true;                                                   // a click with the popup up only closes it
        }
        if (!eaten && canClick && m->clicked) {
            if (inBody && !overPill && nh > 0) {                            // a press in the text : a drag may start
                unsigned s; int idx; bool on;
                if (hit(m->x, m->y, s, idx, on)) {
                    st.selDrag = true; st.selMoved = false; st.selOn = false;
                    st.selASeq = st.selFSeq = s; st.selAIdx = st.selFIdx = idx;
                    st.downX = m->x; st.downY = m->y;
                    unsigned ls; int la0; st.downUrl[0] = 0;
                    if (!link_at(m->x, m->y, st.downUrl, sizeof(st.downUrl), ls, la0)) st.downUrl[0] = 0;
                }
            } else st.selOn = false;                                        // a click elsewhere drops the selection
        }
        if (st.selDrag && m) {
            if (m->down) {
                unsigned s; int idx; bool on;
                if (hit(m->x, m->y, s, idx, on)) { st.selFSeq = s; st.selFIdx = idx; }
                if (fabsf(m->x - st.downX) + fabsf(m->y - st.downY) > 4.0f * S) st.selMoved = true;
                st.selOn = st.selMoved && (st.selASeq != st.selFSeq || st.selAIdx != st.selFIdx);
            } else {                                                        // released
                st.selDrag = false;
                if (st.selOn) {
                    // the selection STAYS : copied from the right-click menu only (2026-11-17 : "on veut uniquement sur
                    // clic droit")
                } else if (!st.selMoved && st.downUrl[0]) {                 // a click on a link : open / copy it
                    g_lp.on = true; g_lp.box = box; lstrcpynA(g_lp.url, st.downUrl, sizeof(g_lp.url)); g_lp.x = st.downX; g_lp.y = st.downY; g_lp.ms = nowMs;
                }
                st.selMoved = false;
            }
        }
        // ---- the link popup ----
        if (g_lp.on && g_lp.box == box && lpW > 0.0f) {
            dColorQuadState(dev);
            rrect_glow(dev, lpX, lpY, lpW, lpH, 6.0f * S, with_alpha(0x90000000u, A), 8.0f * S);
            rrect(dev, lpX, lpY, lpW, lpH, 6.0f * S, with_alpha(0xF41A212Cu, A), with_alpha(0xF4111720u, A));
            rrect_stroke(dev, lpX, lpY, lpW, lpH, 6.0f * S, with_alpha((accent & 0x00FFFFFFu) | 0xB0000000u, A), 1.0f);
            const char* BL[2] = { tr("Open", "Ouvrir"), tr("Copy", "Copier") }; const float BX[2] = { lpB1, lpB2 };
            for (int b2 = 0; b2 < 2; ++b2) {
                const bool hv = m && m->x >= BX[b2] && m->x < BX[b2] + lpBW && m->y >= lpBY && m->y < lpBY + lpBH;
                const u32 bc = b2 == 0 ? accent : 0xFF3A4556u;
                rrect(dev, BX[b2], lpBY, lpBW, lpBH, lpBH * 0.5f, with_alpha(hv ? mix(bc, 0xFFFFFFFFu, 0.2f) : bc, A), with_alpha(mix(bc, 0xFF000000u, 0.25f), A));
            }
            fo->begin(dev);
            fo->draw_lc(dev, snap(lpX + 10.0f * S), snap(lpY + 10.0f * S + lpz * 0.7f), lpShow, lpz, with_alpha(0xFF7CC4FFu, A), with_alpha(0xC0000000u, A), tow * 0.8f);
            for (int b2 = 0; b2 < 2; ++b2)
                fo->draw_cc(dev, BX[b2] + lpBW * 0.5f, lpBY + lpBH * 0.5f, BL[b2], lpz, with_alpha(0xFFFFFFFFu, A), with_alpha(0xC0000000u, A), tow * 0.7f);
        }
        // ---- the line menu ----
        if (g_cm.on && g_cm.box == box && cmN > 0) {
            dColorQuadState(dev);
            rrect_glow(dev, cmX, cmY, cmW, cmH, 6.0f * S, with_alpha(0x90000000u, A), 8.0f * S);
            rrect(dev, cmX, cmY, cmW, cmH, 6.0f * S, with_alpha(0xF41A212Cu, A), with_alpha(0xF4111720u, A));
            rrect_stroke(dev, cmX, cmY, cmW, cmH, 6.0f * S, with_alpha((accent & 0x00FFFFFFu) | 0xB0000000u, A), 1.0f);
            for (int q = 0; q < cmN; ++q) {
                const float iy = cmY + 4.0f * S + (float)q * cmIH;
                const bool hv = m && m->x >= cmX && m->x < cmX + cmW && m->y >= iy && m->y < iy + cmIH;
                if (hv) rrect(dev, cmX + 4.0f * S, iy, cmW - 8.0f * S, cmIH, 4.0f * S, with_alpha((accent & 0x00FFFFFFu) | 0x70000000u, A), with_alpha((accent & 0x00FFFFFFu) | 0x50000000u, A));
            }
            fo->begin(dev);
            for (int q = 0; q < cmN; ++q)
                fo->draw_lc(dev, snap(cmX + 14.0f * S), snap(cmY + 4.0f * S + (float)q * cmIH + cmIH * 0.5f), cm_label(cmIt[q]), cmz, with_alpha(cmIt[q] == 3 ? 0xFF7CC4FFu : cmIt[q] == 5 ? 0xFFFF8A80u : 0xFFE8EEF6u, A), with_alpha(0xC0000000u, A), tow * 0.7f);
        }
        // ---- "Copied" : a small pill where the mouse let go, fading ----
        if (st.toastMs && nowMs - st.toastMs < 1400u) {
            const float k = 1.0f - (float)(nowMs - st.toastMs) / 1400.0f, ta = A * (k < 0.3f ? k / 0.3f : 1.0f);
            const char* tl = st.toastKind == 1 ? tr("Copied", "Copi\xC3\xA9") : st.toastKind == 2 ? tr("Link copied", "Lien copi\xC3\xA9")
                           : st.toastKind == 4 ? tr("Banned", "Banni") : st.toastKind == 5 ? tr("Already banned (or list full)", "D\xC3\xA9j\xC3\xA0 banni (ou liste pleine)")
                           : tr("Clipboard busy", "Presse-papiers occup\xC3\xA9");
            const float tzz = 12.0f * S, th = snap(22.0f * S), tw2 = snap(fo->measure(tl, tzz) + 22.0f * S);
            float tx = snap(st.toastX - tw2 * 0.5f), ty = snap(st.toastY - th - 10.0f * S - (1.0f - k) * 8.0f * S);
            if (tx < px) tx = px; if (tx + tw2 > px + boxW) tx = snap(px + boxW - tw2);
            dColorQuadState(dev);
            rrect(dev, tx, ty, tw2, th, th * 0.5f, with_alpha(st.toastKind == 3 ? 0xF0803030u : (accent | 0xFF000000u), ta), with_alpha(mix(st.toastKind == 3 ? 0xF0803030u : accent, 0xFF000000u, 0.3f), ta));
            fo->begin(dev);
            fo->draw_cc(dev, tx + tw2 * 0.5f, ty + th * 0.5f, tl, tzz, with_alpha(0xFFFFFFFFu, ta), with_alpha(0xC0000000u, ta), tow * 0.7f);
        } else st.toastMs = 0;
    }

    // ---- the input field (the client's text, live) ----
    if (st.inA > 0.0f) {
        const float a = st.inA * A;
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
            const float cxp = snap(fx + 5.0f * S), cyp = snap(fy + fh * 0.5f + capMid - chh * 0.5f);   // on the text's capitals
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
        float caretX = 0.0f; for (int i = 0; i < L.caret && i < L.n; ) { int k; unsigned cp; caretX += cw_at(adv, L.t, i, L.n, k, cp); i += k; }
        if (caretX - st.inScroll > tw - 4.0f * S) st.inScroll = caretX - (tw - 4.0f * S);   // keep the caret in view
        if (caretX - st.inScroll < 0.0f) st.inScroll = caretX;
        if (st.inScroll < 0.0f) st.inScroll = 0.0f;
        clip_rect_begin(dev, tx0 - 1.0f * S, fy, tw + 2.0f * S, fh);
        fo->begin(dev);
        if (typing && L.n == 0)
            fo->draw(dev, tx0, tcy - yOff, tr("Type a message...", "\xC3\x89""cris un message..."), tz, with_alpha(0xFF5F6B7Eu, a), with_alpha(0xC0000000u, a), tow);
        float x = tx0 - st.inScroll;
        char ub[0x170 * 2 + 1];
        for (int i = 0; i < L.n; ) {                                        // runs of one colour (UTF-8, whole characters)
            const u32 col = L.c[i]; int j = i, k = 0; float w = 0.0f;
            while (j < L.n && L.c[j] == col) { int cl; unsigned cp; w += cw_at(adv, L.t, j, L.n, cl, cp); for (int q = 0; q < cl; ++q) ub[k++] = L.t[j + q]; j += cl; }
            ub[k] = 0;
            if (x + w > tx0 - 2.0f && x < tx0 + tw + 2.0f) fo->draw(dev, x, tcy - yOff, ub, tz, with_alpha(col, a), with_alpha(0xD0000000u, a), tow);
            x += w; i = j;
        }
        clip_rect_end(dev);
        s_inRect[0] = fx; s_inRect[1] = fy; s_inRect[2] = fw; s_inRect[3] = fh;   // the auto-translate picker opens on it
        s_inRect[4] = tx0 - st.inScroll + caretX; s_inRectMs = GetTickCount();
        // the caret while the auto-translate picker is CLOSED : once open, the client writes the candidate under the
        // selection into the line and its caret runs with it -- ours STAYS until a choice is made, and the picker opens
        // right after it (2026-11-17 : "le curseur doit rester a sa place tant qu'on n'a pas choisi")
        // (a category validated closes and reopens its window : the client's MODE stays on, the same opening)
        const bool fepOpen = native_fep().picking;
        if (!fepOpen) s_inRect[5] = tx0 - st.inScroll + caretX;
        if (typing && ((GetTickCount() / 530u) & 1u) == 0) {               // the caret, blinking like the client's
            const float cx = snap(fepOpen ? s_inRect[5] : tx0 - st.inScroll + caretX), ch = snap(tz * 1.05f);
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
        rrect_glow(dev, pxx, pyy, pw, ph, ph * 0.5f, with_alpha((accent & 0x00FFFFFFu) | (hov ? 0x90000000u : 0x60000000u), A), 6.0f * S);
        rrect(dev, pxx, pyy, pw, ph, ph * 0.5f, with_alpha(hov ? accentLite : mix(accent, 0xFFFFFFFFu, 0.15f), A), with_alpha(mix(accent, 0xFF000000u, 0.25f), A));
        const float cx0 = pxx + 12.0f * S, ccy = pyy + ph * 0.5f + 1.0f * S;   // the chevron : pointing down
        { const float xy[6] = { cx0, ccy - chev * 0.35f, cx0 + chev, ccy - chev * 0.35f, cx0 + chev * 0.5f, ccy + chev * 0.35f };
          fill_poly_aa(dev, xy, 3, with_alpha(0xFFFFFFFFu, A)); }
        fo->begin(dev);
        fo->draw_lc(dev, cx0 + chev + 7.0f * S, pyy + ph * 0.5f, lb, pz, 0xFFFFFFFFu, 0x90000000u, tow * 0.8f);
    }

    // ---- //aio edit : the corner grip (three diagonal strokes) ----
    if (editing) {
        const float ex = px + boxW - 5.0f * S, ey = growUp ? py + 5.0f * S : py + boxH - 5.0f * S, dy = growUp ? 1.0f : -1.0f;
        dColorQuadState(dev);
        for (int k = 1; k <= 3; ++k) {
            const float o = (float)k * 4.0f * S;
            seg_soft(dev, ex - o, ey, ex, ey + dy * o, 1.4f * S, st.resizing ? accentLite : accent);
        }
    }
}

// a preview box's size per unit of scale, as last drawn (0 before its first preview frame) : the config preview and the
// Help sample stack the two boxes, which needs their heights before drawing them
bool chat_preview_size(int box, float& wU, float& hU) {
    if (box < 0 || box > 1 || g_pvSizeU[box][1] <= 0.0f) return false;
    wU = g_pvSizeU[box][0]; hU = g_pvSizeU[box][1]; return true;
}

// The two boxes as SAMPLES, one ABOVE the other (2026-11-17 : "mets-les l'un sur l'autre plutot que cote a cote"),
// centred on (cx, cy) at scale s (the screen-height scale ; each box adds its own Size). `force` : both boxes even when
// one is turned off (the Help shows what the module is) ; else only the shown ones (the config preview is WYSIWYG).
// A box's height is known only once drawn (lines, header, density) : the previous preview frame's, else a guess for the
// first frame. draw = false only measures (outW / outH, in px at s).
void chat_stack_sample(const Frame& f, float cx, float cy, float s, bool force, bool draw, float* outW, float* outH) {
    UiConfig& C = ui_config();
    const float gapU = 14.0f;
    float wU[2] = { 0.0f, 0.0f }, hU[2] = { 0.0f, 0.0f }, maxW = 0.0f, totH = 0.0f; int n = 0;
    for (int b = 0; b < 2; ++b) {
        if (!force && !C.chShow[b]) continue;
        float w1 = 0.0f, h1 = 0.0f;
        if (!chat_preview_size(b, w1, h1)) { w1 = (float)C.chW[b]; h1 = C.chH[b] > 0 ? (float)C.chH[b] : 220.0f; }
        wU[b] = w1 * C.chScale[b]; hU[b] = h1 * C.chScale[b];
        if (wU[b] > maxW) maxW = wU[b];
        totH += hU[b] + (n ? gapU : 0.0f); ++n;
    }
    if (outW) *outW = maxW * s;
    if (outH) *outH = totH * s;
    if (!draw || !n) return;
    float y = cy - totH * s * 0.5f;
    for (int b = 0; b < 2; ++b) {
        if (hU[b] <= 0.0f) continue;
        const int was = C.chShow[b]; C.chShow[b] = 1;       // force : drawn for this sample only
        chat_box_draw(f, b, true, false, cx, y + hU[b] * s * 0.5f, s * C.chScale[b]);
        C.chShow[b] = was;
        y += (hU[b] + gapU) * s;
    }
}
// the Help's sample : both boxes, as big as fits availW (capped at maxScale)
void chat_help_fit(const Frame& f, float availW, float maxScale, float& outScale, float& outH) {
    float w = 0.0f, h = 0.0f; chat_stack_sample(f, 0.0f, 0.0f, 1.0f, true, false, &w, &h);
    float sc = w > 1.0f ? availW / w : maxScale; if (sc > maxScale) sc = maxScale; if (sc < 0.4f) sc = 0.4f;
    outScale = sc; outH = h * sc;
}
void chat_help_box(const Frame& f, float cx, float cy, float s) { chat_stack_sample(f, cx, cy, s, true, true, 0, 0); }

} // namespace aio
