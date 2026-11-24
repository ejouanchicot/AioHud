// chat_log.cpp -- see chat_log.h. Text thread -> ring -> main thread -> decoded history.
#include "model/chat_log.h"
#include "windower_debug.h"
#include "model/paths.h"          // plugin_path : Windower's res folder
#include <windows.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>

namespace aio {

// an item quoted with auto-translate : the id -> name table (itemnames_gen.cpp, defined once), reached through
// party_state_empypop.cpp
const char* chat_item_name(unsigned id);

// ---------------- the text-thread ring (single producer = text_in, single consumer = render6) ----------------
// Same contract as queue_game_text (party_state_zonetracker.cpp) : aligned 32-bit head/tail are atomic on x86, the
// producer publishes head LAST, so the consumer never sees a half-copied slot. Full = the line is dropped, COUNTED.
static const int Q_N = 256, Q_LEN = 512;
static char          g_qText[Q_N][Q_LEN];
static int           g_qMode[Q_N];
static volatile long g_qHead = 0;     // text thread only
static volatile long g_qTail = 0;     // main thread only
static volatile long g_qDropped = 0;  // text thread only (read for the report)

void chat_queue(const char* s, int mode) {
    if (!s) return;
    const long head = g_qHead;
    if (head - g_qTail >= Q_N) { g_qDropped = g_qDropped + 1; return; }
    const int slot = (int)(head & (Q_N - 1));
    int i = 0; for (; i < Q_LEN - 1 && s[i]; ++i) g_qText[slot][i] = s[i];
    g_qText[slot][i] = 0;
    g_qMode[slot] = mode;
    g_qHead = head + 1;
}

// ---------------- channel ----------------
// Chat channels : Windower res/chat.lua (incoming / outgoing ids). The COMBAT log is not listed there : its modes
// are the battle message colours, 20..120 on the live client -- PROVISIONAL, to be confirmed by //aio chatmodes.
int chat_classify(int m) {
    switch (m) {
        case 1: case 9:     return CH_SAY;
        case 2: case 10:    return CH_SHOUT;
        case 3: case 11:    return CH_YELL;
        case 4: case 12:    return CH_TELL;
        case 5: case 13:    return CH_PARTY;
        case 6: case 14:    return CH_LS1;
        case 213: case 214: return CH_LS2;
        case 211: case 212: return CH_UNITY;
        case 7: case 15:    return CH_EMOTE;
        case 219: case 220: case 221: case 222: return CH_ASSIST;
        case 150: case 151: case 152: case 142: case 144: return CH_NPC;
        default: break;
    }
    if (m >= 19 && m <= 120) return CH_BATTLE;
    return CH_SYSTEM;
}
bool chat_is_battle(int chan) { return chan == CH_BATTLE; }

// ---------------- auto-translate phrases (Windower res/auto_translates.lua) ----------------
// Loaded on first need, from the Windower install the player runs (always current, nothing baked into the DLL) :
// "[256] = {id=256,en="Greetings",ja=...}," -- the id is (category << 8) | index, the two bytes the client puts in
// its FD .. FD sequence. Bounded retry (rule 10) : 5 tries, 10 s apart, success and failure logged.
static char     g_atPool[96 * 1024];
static unsigned g_atOff[16384];          // id -> offset + 1 in the pool (0 = unknown id)
static int      g_atState = 0;           // 0 = not loaded yet, 1 = loaded, -1 = gave up (logged)
static int      g_atTries = 0; static unsigned g_atNext = 0;

static bool at_load() {
    // plugin_dir() is plugins\AioHud (the runtime folder) : Windower's res is TWO levels up. The one-level form is a
    // fallback for a DLL run from plugins\ itself. (2026-11-15 : one level only -> "table not found", every phrase {...})
    char path[300]; FILE* f = 0;
    static const char* REL[2] = { "..\\..\\res\\auto_translates.lua", "..\\res\\auto_translates.lua" };
    for (int k = 0; k < 2 && !f; ++k) { plugin_path(path, sizeof(path), REL[k]); f = fopen(path, "rb"); }
    if (!f) { windower::debug::log("CHAT auto-translate table not found (%s) -- try %d", path, g_atTries); return false; }
    fseek(f, 0, SEEK_END); const long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 4 * 1024 * 1024) { fclose(f); windower::debug::log("CHAT auto-translate table : odd size %ld", n); return false; }
    char* buf = (char*)malloc((size_t)n + 1);            // load time only, freed below -- never per frame
    if (!buf) { fclose(f); return false; }
    const size_t got = fread(buf, 1, (size_t)n, f); fclose(f); buf[got] = 0;
    memset(g_atOff, 0, sizeof(g_atOff));
    unsigned used = 0; int count = 0;
    for (char* q = buf; (q = strstr(q, "[")) != 0; ) {
        ++q; if (*q < '0' || *q > '9') continue;
        const unsigned id = (unsigned)strtoul(q, &q, 10);
        if (strncmp(q, "] = {", 5)) continue;
        char* e = strstr(q, "en=\""); char* nl = strchr(q, '\n');
        if (!e || (nl && e > nl)) continue;
        e += 4;
        if (id >= 16384 || used + 2 >= sizeof(g_atPool)) { q = e; continue; }
        const unsigned start = used;
        while (*e && *e != '"' && used + 1 < sizeof(g_atPool)) {
            char c = *e++;
            if (c == '\\' && *e) c = *e++;   // an escaped quote or backslash
            if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7F) g_atPool[used++] = c;
        }
        g_atPool[used++] = 0;
        g_atOff[id] = start + 1; ++count;
        q = e;
    }
    free(buf);
    windower::debug::log("CHAT auto-translate table loaded : %d phrases (%u bytes) from %s", count, used, path);
    return count > 0;
}
static const char* at_phrase(unsigned id) {
    if (g_atState == 0) {
        const unsigned now = GetTickCount();
        if (g_atTries == 0 || (int)(now - g_atNext) >= 0) {
            ++g_atTries; g_atNext = now + 10000;
            if (at_load()) g_atState = 1;
            else if (g_atTries >= 5) { g_atState = -1; windower::debug::log("CHAT auto-translate : gave up after %d tries -- phrases show as {...}", g_atTries); }
        }
    }
    if (g_atState != 1 || id >= 16384 || !g_atOff[id]) return 0;
    return g_atPool + g_atOff[id] - 1;
}

// ---------------- decoding ----------------
// Shift-JIS glyphs the English client actually prints in its log (arrows in battle lines, full-width punctuation)
static const char* sjis_glyph(unsigned char a, unsigned char b) {
    if (a == 0x81) {
        switch (b) {
            case 0x40: return " ";   case 0x41: return ",";  case 0x42: return ".";  case 0x45: return ".";
            case 0x46: return ":";   case 0x47: return ";";  case 0x48: return "?";  case 0x49: return "!";
            case 0x5B: return "-";   case 0x5C: return "-";  case 0x5D: return "-";  case 0x60: return "~";
            case 0x69: return "(";   case 0x6A: return ")";  case 0x6D: return "[";  case 0x6E: return "]";
            case 0x6F: return "{";   case 0x70: return "}";  case 0x7B: return "+";  case 0x7C: return "-";
            case 0x7E: return "x";   case 0x81: return "=";  case 0x83: return "<";  case 0x84: return ">";
            case 0x93: return "%";   case 0x94: return "#";  case 0x95: return "&";  case 0x96: return "*";
            case 0x97: return "@";   case 0x99: return "*";  case 0x9A: return "*";  case 0x9B: return "o";
            case 0x9C: return "o";   case 0x9E: return "*";  case 0xA0: return "#";  case 0xA2: return "^";
            case 0xA8: return "->";  case 0xA9: return "<-"; case 0xAA: return "^";  case 0xAB: return "v";
            default: break;
        }
    }
    // the client's own quotation marks around a name ("the challenge 87 B2 Spoils (Seals). 87 B3", captured 2026-11-16)
    if (a == 0x87 && (b == 0xB2 || b == 0xB3)) return "\"";
    return "?";
}

// set by chat_decode when a byte had no known meaning (printed '?') : chat_drain then logs that line's raw bytes
static bool g_decUnknown = false;

int chat_decode(const char* raw, char out[][CHAT_TEXT], unsigned short col[][CHAT_TEXT], int lens[], int maxLines) {
    g_decUnknown = false;
    if (!raw || maxLines < 1) return 0;
    const unsigned char* s = (const unsigned char*)raw;
    int ln = 0, n = 0; unsigned short cur = 0; bool inAt = false;   // inside the client's own auto-translate brackets
    bool full = false;                       // this line had no room for a whole character : the rest of it is dropped
    bool atWords = false;                    // inside an auto-translate : the words take CHAT_AT_TEXT
    auto put = [&](char ch) { if (!full && n < CHAT_TEXT - 1) { out[ln][n] = ch; col[ln][n] = (inAt || atWords) ? CHAT_AT_TEXT : cur; ++n; } };
    // one CODEPOINT as UTF-8 (the history is UTF-8 since 2026-11-17) : all its bytes or none, every byte the same colour
    auto put_cp = [&](unsigned cp) {
        char b[4]; int k = 0;
        if (cp < 0x80) b[k++] = (char)cp;
        else if (cp < 0x800) { b[k++] = (char)(0xC0 | (cp >> 6)); b[k++] = (char)(0x80 | (cp & 0x3F)); }
        else { b[k++] = (char)(0xE0 | (cp >> 12)); b[k++] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[k++] = (char)(0x80 | (cp & 0x3F)); }
        // no room for the whole character : this LINE ends here (never half a character) ; the message's next lines
        // (after a 07 / CR / LF) still come -- review 2026-11-17 : `n = CHAT_TEXT` used to end the whole message and leave
        // lens[] at a previous message's value
        if (n + k > CHAT_TEXT - 1) { full = true; return; }
        for (int q = 0; q < k; ++q) put(b[q]);
    };
    // an auto-translate BRACKET, coloured as the client draws them : the opening one green, the closing one red
    // (2026-11-17 : "{Hello!} {Waktza rostrum}" read as plain braces) -- codes CHAT_AT_OPEN / CHAT_AT_CLOSE
    auto put_at = [&](bool open) {           // written directly : its own code, whatever surrounds it
        if (!full && n < CHAT_TEXT - 1) { out[ln][n] = open ? '{' : '}'; col[ln][n] = open ? CHAT_AT_OPEN : CHAT_AT_CLOSE; ++n; }
    };
    auto brk = [&]() {                       // close the current line ; an empty one is not kept
        full = false; inAt = false;          // an unclosed bracket never colours the next line
        if (n == 0) return;
        out[ln][n] = 0; lens[ln] = n;
        if (ln + 1 < maxLines) { ++ln; n = 0; }
        else n = CHAT_TEXT;                  // out of lines : ignore the rest
    };
    for (int i = 0; s[i]; ++i) {
        const unsigned char c = s[i];
        if (n >= CHAT_TEXT) break;
        if (c == 0x1E) { if (!s[i + 1]) break; cur = (s[i + 1] == 1) ? 0 : (unsigned short)(0x100 | s[i + 1]); ++i; continue; }
        if (c == 0x1F) { if (!s[i + 1]) break; cur = (unsigned short)(0x200 | s[i + 1]); ++i; continue; }
        if (c == 0x7F) {                     // the client's own marker lengths (FUN_05469997)
            const unsigned char m = s[i + 1]; int len = 1;
            if (m == '1' || m == '2' || m == '3' || m == '7' || m == 0xFB || m == 0xFC) len = 2;
            else if (m == '4' || m == '5' || m == '6') len = 3;
            else if (m == '8') len = 4;
            for (int k = 1; k < len && s[i + 1]; ++k) ++i;
            continue;
        }
        if (c == 0xEF && (s[i + 1] == 0x27 || s[i + 1] == 0x28)) { inAt = s[i + 1] == 0x27; put_at(inAt); ++i; continue; }
        if (c == 0xFD) {                     // auto-translate : FD type lang id_hi id_lo FD
            int j = i + 1; while (j < i + 8 && s[j] && s[j] != 0xFD) ++j;
            if (s[j] == 0xFD) {
                const char* ph = 0;
                if (j == i + 5) {
                    const unsigned type = s[i + 1], id = ((unsigned)s[i + 3] << 8) | s[i + 4];
                    if (type == 0x02 || type == 0x01) ph = at_phrase(id);        // a phrase of the dictionary
                    else if (type == 0x07)            ph = chat_item_name(id);        // an item
                }
                if (!ph || !*ph) { ph = "..."; g_decUnknown = true; }
                if (!inAt) put_at(true);
                atWords = true;
                for (const char* g = ph; *g; ++g) if ((unsigned char)*g >= 0x20 && (unsigned char)*g < 0x7F) put(*g);
                atWords = false;
                if (!inAt) put_at(false);
                i = j; continue;
            }
        }
        if (c == 0x07 || c == '\n' || c == '\r') { brk(); cur = 0; continue; }
        if (c < 0x20) continue;
        if (c < 0x7F) { put((char)c); continue; }
        // U+FFFD (UTF-8 EF BF BD), the "unknown character" mark : battlemod's arrow reaches the log as TWO of them
        // ("[Tetsouo] 608 hit EF BF BD EF BF BD Vampire Leech", captured 2026-11-15) -> "->" ; a lone one is dropped.
        if (c == 0xEF && s[i + 1] == 0xBF && s[i + 2] == 0xBD) {
            int run = 0; while (s[i] == 0xEF && s[i + 1] == 0xBF && s[i + 2] == 0xBD) { ++run; i += 3; }
            --i;                             // the loop's ++i
            if (run >= 2) { put('-'); put('>'); }
            continue;
        }
        const unsigned char t = s[i + 1];
        if (c == 0x81 && t == 0x3F) { put(' '); ++i; continue; }   // battlemod's space inside a mob name (81 3F)
        // the client's accented Latin letters : 85 xx, xx = Latin-1 - 0x21 ("envoy\xE9" arrived as 85 C8, 2026-11-15)
        if (c == 0x85 && t >= 0x9F && t <= 0xDE) { put_cp((unsigned)t + 0x21u); ++i; continue; }
        if (((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) && ((t >= 0x40 && t <= 0x7E) || (t >= 0x80 && t <= 0xFC))) {   // Shift-JIS pair
            // the English log's own glyphs (arrows, full-width punctuation) stay ASCII ; any other character -- Japanese
            // from a player (2026-11-17 : "que des ??? c'est du JP") -- is the real one : Windows' code page 932
            const char* g = sjis_glyph(c, t);
            if (!(g[0] == '?' && !g[1])) { for (; *g; ++g) put(*g); ++i; continue; }
            const char pr[2] = { (char)c, (char)t }; wchar_t w = 0;
            if (MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, pr, 2, &w, 1) == 1 && w >= 0x80) put_cp((unsigned)w);
            else { g_decUnknown = true; put('?'); }
            ++i; continue;
        }
        if (c >= 0xA1 && c <= 0xDF) { put_cp(0xFF61u + (unsigned)(c - 0xA1)); continue; }   // a half-width katakana (one byte)
        g_decUnknown = true;                 // a lone high byte : not a Shift-JIS pair (the trail would be < 0x40 or 0x7F)
        put('?');
    }
    if (n > 0 && n < CHAT_TEXT) { out[ln][n] = 0; lens[ln] = n; return ln + 1; }
    if (n >= CHAT_TEXT) { out[ln][CHAT_TEXT - 1] = 0; if (lens[ln] <= 0 || lens[ln] > CHAT_TEXT - 1) lens[ln] = CHAT_TEXT - 1; return ln + 1; }
    return ln;                               // the last line was empty
}

// ---------------- the client's colours ----------------
// Found by their CODE (unique AOBs), so a client patch that moves the tables still finds them :
//   FUN_054A35A0 : 0F BF 44 24 04 99 33 C2 2B C2 83 F8 11 7D 08 8B 04 85 [palette]   -> 17 ARGB, "1E n" n < 17
//   FUN_0546B160 : A1 [modes] 33 C9 66 85 C0 74 18 66 8B 54 24 04 66 3B C2            -> {short mode, byte key, pad}
//   FUN_0546B190 : A0 [keys] 33 C9 84 C0 74 1A 0F BF 54 24 04 25 FF 00 00 00 3B C2   -> {byte key, pad[3], ARGB}
// Bounded retry (rule 10) : 5 tries, 10 s apart, the outcome logged once.
static unsigned g_palAt = 0, g_modesAt = 0, g_keysAt = 0;
// unique-AOB search over FFXiMain's image, self-contained (the offline tests link this file alone)
static unsigned aob_once(const unsigned char* P, int n, int wlo, int whi) {
    const unsigned b = (unsigned)GetModuleHandleA("FFXiMain.dll"); if (!b) return 0;
    unsigned e = 0, sz = 0;
    __try { e = *(const unsigned*)(b + 0x3C); sz = *(const unsigned*)(b + e + 0x50); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (!sz || sz > 0x4000000) return 0;
    int hits = 0; unsigned at = 0;
    __try {
        for (const unsigned char* q = (const unsigned char*)b; q + n < (const unsigned char*)(b + sz); ++q) {
            if (q[0] != P[0]) continue;
            bool ok = true;
            for (int i = 1; i < n; ++i) { if (i >= wlo && i < whi) continue; if (q[i] != P[i]) { ok = false; break; } }
            if (ok) { ++hits; at = (unsigned)q; if (hits > 1) break; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return hits == 1 ? at : 0;
}
static int      g_colState = 0, g_colTries = 0; static unsigned g_colNext = 0;
static bool col_resolve() {
    if (g_colState == 1) return true;
    if (g_colState == -1) return false;
    const unsigned now = GetTickCount();
    if (g_colTries && (int)(now - g_colNext) < 0) return false;
    ++g_colTries; g_colNext = now + 10000;
    static const unsigned char PP[] = { 0x0F,0xBF,0x44,0x24,0x04,0x99,0x33,0xC2,0x2B,0xC2,0x83,0xF8,0x11,0x7D,0x08,0x8B,0x04,0x85,0,0,0,0 };
    static const unsigned char PM[] = { 0xA1,0,0,0,0,0x33,0xC9,0x66,0x85,0xC0,0x74,0x18,0x66,0x8B,0x54,0x24,0x04,0x66,0x3B,0xC2 };
    static const unsigned char PK[] = { 0xA0,0,0,0,0,0x33,0xC9,0x84,0xC0,0x74,0x1A,0x0F,0xBF,0x54,0x24,0x04,0x25,0xFF,0x00,0x00,0x00,0x3B,0xC2 };
    const unsigned a = aob_once(PP, sizeof(PP), 18, 22), b = aob_once(PM, sizeof(PM), 1, 5), k = aob_once(PK, sizeof(PK), 1, 5);
    unsigned pa = 0, mo = 0, ke = 0;
    __try { if (a) pa = *(const unsigned*)(a + 18); if (b) mo = *(const unsigned*)(b + 1); if (k) ke = *(const unsigned*)(k + 1); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (pa && mo && ke) {
        g_palAt = pa; g_modesAt = mo; g_keysAt = ke; g_colState = 1;
        windower::debug::log("CHAT client colours found : palette %08X, mode->key %08X, key->colour %08X (try %d)", pa, mo, ke, g_colTries);
        return true;
    }
    if (g_colTries >= 5) { g_colState = -1; windower::debug::log("CHAT client colours NOT found (palette %08X modes %08X keys %08X) -- the boxes keep their own colours", pa, mo, ke); }
    return false;
}
static unsigned col_full(unsigned c) {   // the client's half-intensity ARGB (0x80 = 1.0) -> full
    unsigned a = (c >> 24) * 2, r = ((c >> 16) & 255) * 2, g = ((c >> 8) & 255) * 2, b = (c & 255) * 2;
    if (a > 255) a = 255; if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    return (a << 24) | (r << 16) | (g << 8) | b;
}
static unsigned col_by_key(unsigned key) {
    unsigned out = 0;
    __try {
        for (int i = 0; i < 256; ++i) {
            const unsigned char k = *(const unsigned char*)(g_keysAt + i * 8);
            if (!k) break;
            if (k == key) { out = *(const unsigned*)(g_keysAt + i * 8 + 4); break; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; }
    return out;
}
unsigned chat_game_color(unsigned short code) {
    if (code == CHAT_AT_OPEN)  return 0xFF4CD964u;          // the client's auto-translate brackets : green opens,
    if (code == CHAT_AT_CLOSE) return 0xFFE5534Bu;          // red closes
    if (code == CHAT_AT_TEXT)  return 0xFF9FE8CFu;          // the words between : their own pale sea green
    if (!code || !col_resolve()) return 0;
    const unsigned n = code & 0xFF;
    unsigned raw = 0;
    if ((code & 0x300) == 0x100) {                          // 1E n
        if (n < 17) { __try { raw = *(const unsigned*)(g_palAt + n * 4); } __except (EXCEPTION_EXECUTE_HANDLER) { raw = 0; } }
        else raw = col_by_key(n);
    } else if ((code & 0x300) == 0x200) {                   // 1F n : mode -> key -> colour
        unsigned key = 0;
        __try {
            for (int i = 0; i < 512; ++i) {
                const unsigned short m = *(const unsigned short*)(g_modesAt + i * 4);
                if (!m) break;
                if (m == n) { key = *(const unsigned char*)(g_modesAt + i * 4 + 2); break; }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { key = 0; }
        if (key) raw = col_by_key(key);
    }
    return raw ? col_full(raw) : 0;
}

// ---------------- history ----------------
static ChatLine g_hist[CHAT_HIST];
static int      g_histN = 0;       // lines held (<= CHAT_HIST)
static int      g_histHead = 0;    // next slot to write
static unsigned g_seq = 0;

// //aio chatmodes : every mode seen, how often, its channel and the first line in it
static unsigned g_modeCount[512];
static char     g_modeSample[512][72];

int chat_count() { return g_histN; }

// ---- the ban list ----
static char     g_ban[CHAT_BAN_MAX][20];
static int      g_banN = 0;
static unsigned g_banVer = 1;
// Rule 10 (2026-11-24, the CI guard) : this was a one-shot latch -- a file that EXISTS but could not be opened at
// that moment (an antivirus or an editor holding it) read as "nobody banned" for the whole session, and the next
// ban then rewrote the file with that one name : the list was LOST. Now a missing file is an empty list, an
// unreadable one is retried (BAN_TRIES, 1 s apart, logged), and the list is never saved before it was read.
static bool     g_banLoaded = false;   // rule10-ok: set only by a REAL read or a file that does not exist ; a failed open retries (ban_load)
static int      g_banTries = 0;
static unsigned g_banNext = 0;
static const int BAN_TRIES = 5;
static void ban_path(wchar_t* w, int cap) { plugin_path_w(w, cap, L"data\\chat_bans.txt"); }
// true once the list is known (read, or no file) ; false while a file that exists could not be read yet
static bool ban_load() {
    if (g_banLoaded) return true;
    if (g_banTries >= BAN_TRIES || (g_banTries && (int)(GetTickCount() - g_banNext) < 0)) return false;
    wchar_t p[600]; ban_path(p, 600);
    if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) {
        const DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) { g_banLoaded = true; return true; }   // rule10-ok: no file = nobody banned
    }
    FILE* f = _wfopen(p, L"r");
    if (!f) {
        ++g_banTries; g_banNext = GetTickCount() + 1000;
        windower::debug::log("CHAT bans : the file exists but could not be read (errno %d, try %d/%d)%s", errno, g_banTries, BAN_TRIES,
                             g_banTries >= BAN_TRIES ? " -- given up : no ban applied, the file is left untouched" : " -- retried");
        return false;
    }
    char ln[64];
    while (g_banN < CHAT_BAN_MAX && fgets(ln, sizeof(ln), f)) {
        int n = (int)strlen(ln); while (n && (ln[n - 1] == '\n' || ln[n - 1] == '\r' || ln[n - 1] == ' ')) ln[--n] = 0;
        if (n > 0 && n < 20) { lstrcpynA(g_ban[g_banN++], ln, 20); }
    }
    fclose(f);
    for (int i = 1; i < g_banN; ++i) { char t[20]; memcpy(t, g_ban[i], 20); int j = i; while (j > 0 && _stricmp(g_ban[j - 1], t) > 0) { memcpy(g_ban[j], g_ban[j - 1], 20); --j; } memcpy(g_ban[j], t, 20); }   // alphabetical (a hand-edited file too)
    ++g_banVer;
    g_banLoaded = true;                                  // rule10-ok: after a real read
    windower::debug::log("CHAT bans : %d name(s) loaded%s", g_banN, g_banTries ? " (after a failed read)" : "");
    return true;
}
static void ban_save() {
    wchar_t p[600]; ban_path(p, 600);
    FILE* f = _wfopen(p, L"w");
    if (!f) { windower::debug::log("CHAT bans : cannot write the file (errno %d) -- kept for this session only", errno); return; }
    for (int i = 0; i < g_banN; ++i) fprintf(f, "%s\n", g_ban[i]);
    fclose(f);
}
static int ban_find(const char* name) { for (int i = 0; i < g_banN; ++i) if (!_stricmp(g_ban[i], name)) return i; return -1; }
bool chat_banned(const char* name) { ban_load(); return name && name[0] && ban_find(name) >= 0; }
bool chat_ban_add(const char* name) {
    if (!ban_load()) { windower::debug::log("CHAT bans : '%s' NOT banned -- the ban file could not be read yet (saving now would erase it)", name ? name : ""); return false; }
    if (!name || !name[0] || strlen(name) >= 20 || g_banN >= CHAT_BAN_MAX || ban_find(name) >= 0) return false;
    int at = g_banN; while (at > 0 && _stricmp(g_ban[at - 1], name) > 0) { memcpy(g_ban[at], g_ban[at - 1], 20); --at; }   // alphabetical
    lstrcpynA(g_ban[at], name, 20); ++g_banN; ++g_banVer; ban_save();
    windower::debug::log("CHAT bans : '%s' banned (%d in the list)", name, g_banN);
    return true;
}
bool chat_ban_remove(const char* name) {
    if (!ban_load()) { windower::debug::log("CHAT bans : '%s' NOT unbanned -- the ban file could not be read yet", name ? name : ""); return false; }
    const int k = name ? ban_find(name) : -1; if (k < 0) return false;
    for (int i = k; i + 1 < g_banN; ++i) memcpy(g_ban[i], g_ban[i + 1], 20);
    --g_banN; ++g_banVer; ban_save();
    windower::debug::log("CHAT bans : '%s' unbanned (%d left)", name, g_banN);
    return true;
}
int         chat_ban_count() { ban_load(); return g_banN; }
const char* chat_ban_name(int i) { ban_load(); return i >= 0 && i < g_banN ? g_ban[i] : ""; }
unsigned    chat_ban_version() { ban_load(); return g_banVer; }

int chat_export_txt(bool battle, char* nameOut, int nameCap) {
    static const char* const CH[CH_COUNT] = { "Say", "Shout", "Yell", "Tell", "Party", "LS1", "LS2", "Unity", "Emote", "Assist", "System", "NPC", "Battle", "Other" };
    SYSTEMTIME t; GetLocalTime(&t);
    char rel[96]; _snprintf(rel, sizeof(rel), "exports\\chat_%04u-%02u-%02u_%02u%02u%02u.txt", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond); rel[sizeof(rel) - 1] = 0;
    if (nameOut && nameCap > 0) lstrcpynA(nameOut, rel, nameCap);
    wchar_t dir[600], path[600], wrel[96];
    plugin_path_w(dir, 600, L"exports"); CreateDirectoryW(dir, 0);   // already there : fine
    MultiByteToWideChar(CP_ACP, 0, rel, -1, wrel, 96);
    plugin_path_w(path, 600, wrel);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) { windower::debug::log("CHAT export : cannot create the file (error %u)", (unsigned)GetLastError()); return -1; }
    static char buf[64 * 1024]; int o = 0, n = 0; DWORD wr = 0; bool ok = true;
    memcpy(buf, "\xEF\xBB\xBF", 3); o = 3;                         // the UTF-8 BOM (Notepad reads the Japanese right)
    for (int i = 0; i < g_histN && ok; ++i) {
        const ChatLine* L = chat_line(i); if (!L || !L->seq) continue;
        if (!battle && L->chan == CH_BATTLE) continue;
        if (o > (int)sizeof(buf) - 400) { ok = WriteFile(h, buf, (DWORD)o, &wr, 0) && wr == (DWORD)o; o = 0; }
        if (L->cont) o += _snprintf(buf + o, sizeof(buf) - o, "                  ");
        else o += _snprintf(buf + o, sizeof(buf) - o, "[%02u:%02u] %-8s ", L->hhmm / 100, L->hhmm % 100, CH[L->chan < CH_COUNT ? L->chan : CH_OTHER]);
        memcpy(buf + o, L->text, L->len); o += L->len;
        buf[o++] = '\r'; buf[o++] = '\n'; ++n;
    }
    if (ok && o) ok = WriteFile(h, buf, (DWORD)o, &wr, 0) && wr == (DWORD)o;
    CloseHandle(h);
    if (!ok) { windower::debug::log("CHAT export : write failed (error %u) after %d line(s)", (unsigned)GetLastError(), n); return -1; }
    windower::debug::log("CHAT export : %d line(s) -> %s", n, rel);
    return n;
}
const ChatLine* chat_line(int i) {
    if (i < 0 || i >= g_histN) return 0;
    const int first = (g_histHead - g_histN + CHAT_HIST) & (CHAT_HIST - 1);
    return &g_hist[(first + i) & (CHAT_HIST - 1)];
}
unsigned chat_newest_seq() { return g_seq; }

// decode ONE raw client line and append it to the history. hhmm < 0 = now ; `ago` ms = how long before NOW it
// arrived (0 for a live line ; the client's backlog is old news : the boxes' idle collapse must not treat it as fresh).
static void append_raw(const char* raw, int mode, int hhmm, unsigned ago) {
    static char           dt[8][CHAT_TEXT];
    static unsigned short dc[8][CHAT_TEXT];
    static int            dl[8];
    mode &= 0x1FF;
    const int nl = chat_decode(raw, dt, dc, dl, 8);
    if (g_decUnknown) {                                  // MEASURE what we could not read : the raw bytes, bounded
        static int nRaw = 0;
        if (nRaw < 25) {
            ++nRaw; char hx[3 * 96 + 1]; int k = 0;
            for (int j = 0; j < 96 && raw[j]; ++j) k += _snprintf(hx + k, sizeof(hx) - k, "%02X ", (unsigned char)raw[j]);
            hx[k] = 0;
            windower::debug::log("CHATRAW mode %d | %s | %s", mode, dt[0], hx);
            if (nRaw == 25) windower::debug::log("=== CHATRAW : 25 lines captured, nothing more this session ===");
        }
    }
    if (nl <= 0) return;
    {   // a timestamp addon already put "[hh:mm:ss] " / "[hh:mm] " in front : the boxes have their own stamp
        const char* t0 = dt[0]; int cut = 0;
        if (t0[0] == '[' && t0[1] >= '0' && t0[1] <= '9' && t0[2] >= '0' && t0[2] <= '9' && t0[3] == ':' && t0[4] >= '0' && t0[4] <= '9' && t0[5] >= '0' && t0[5] <= '9') {
            if (t0[6] == ']' && t0[7] == ' ') cut = 8;
            else if (t0[6] == ':' && t0[7] >= '0' && t0[7] <= '9' && t0[8] >= '0' && t0[8] <= '9' && t0[9] == ']' && t0[10] == ' ') cut = 11;
        }
        if (cut && dl[0] > cut) { memmove(dt[0], dt[0] + cut, dl[0] - cut + 1); memmove(dc[0], dc[0] + cut, (dl[0] - cut) * sizeof(dc[0][0])); dl[0] -= cut; }
    }
    SYSTEMTIME st; GetLocalTime(&st);
    if (hhmm < 0) hhmm = st.wHour * 100 + st.wMinute;
    const int chan = chat_classify(mode);
    if (g_modeCount[mode]++ == 0) { int k = 0; for (; k < 71 && dt[0][k]; ++k) g_modeSample[mode][k] = dt[0][k]; g_modeSample[mode][k] = 0; }
    for (int L = 0; L < nl; ++L) {
        ChatLine& h = g_hist[g_histHead];
        h.seq = ++g_seq; h.mode = (unsigned short)mode; h.chan = (unsigned char)chan; h.cont = L > 0 ? 1 : 0;
        h.hhmm = (unsigned short)hhmm;
        h.arrivedTick = GetTickCount() - ago;
        h.chSpeaker[0] = 0; h.chBodyAt = 0; h.chLsIdx = 0; h.chTellOut = 0;
        int len = dl[L]; if (len < 0) len = 0; if (len > CHAT_TEXT - 1) len = CHAT_TEXT - 1;
        memcpy(h.text, dt[L], len); h.text[len] = 0; h.len = (unsigned char)len;
        // the line's colours as the CLIENT gives them : each distinct code once into pal[], col[] = its index
        unsigned short codes[8]; int nc = 0;
        for (int k = 0; k < len; ++k) {
            const unsigned short code = dc[L][k]; unsigned char idx = 0;
            if (code) {
                int f = 0; while (f < nc && codes[f] != code) ++f;
                if (f == nc && nc < 8) { codes[nc] = code; h.pal[nc] = chat_game_color(code); ++nc; }
                if (f < nc) idx = (unsigned char)(f + 1);
            }
            h.col[k] = idx;
        }
        for (int k = nc; k < 8; ++k) h.pal[k] = 0;
        h.atIdx = 0; for (int f = 0; f < nc; ++f) if (codes[f] == CHAT_AT_TEXT) h.atIdx = (unsigned char)(f + 1);
        h.modeArgb = (chan == CH_SYSTEM || chan == CH_BATTLE || chan == CH_NPC || chan == CH_OTHER) ? chat_game_color((unsigned short)(0x200 | (mode & 0xFF))) : 0;
        if (L == 0) chat_parse_speaker(h);                // the speaker sits on the message's FIRST line only
        else { const ChatLine& h0 = g_hist[(g_histHead - L + CHAT_HIST) & (CHAT_HIST - 1)];   // ...and its continuations carry it
               memcpy(h.chSpeaker, h0.chSpeaker, sizeof(h.chSpeaker)); h.chTellOut = h0.chTellOut; }   // (drawn on the first line only : !cont)
        g_histHead = (g_histHead + 1) & (CHAT_HIST - 1);
        if (g_histN < CHAT_HIST) ++g_histN;
    }
}

// the client's own log, replayed at load (chat_backlog.cpp) : held back until it is in, so it lands BEFORE the live lines
static bool g_seedHold = true;
void chat_seed_line(const char* raw, int mode, int hhmm) { append_raw(raw, mode, hhmm, 3600000u); }
void chat_seed_end(bool tookQueued) {
    // the lines queued since load are IN the client's log already (text_in runs before the client stores the line) :
    // the backlog brought them, so drop them here rather than show them twice
    if (tookQueued) g_qTail = g_qHead;
    g_seedHold = false;
}

void chat_seed_restart() {
    g_histN = 0; g_histHead = 0; ++g_seq;   // the boxes key their cache on the newest seq : a bump redraws them empty
    g_seedHold = true;
}

void chat_drain() {
    if (g_seedHold) return;                              // the backlog first (chat_backlog_tick settles it, bounded)
    int budget = Q_N;                                    // never loop forever on a ring the producer keeps filling
    while (g_qTail != g_qHead && budget-- > 0) {
        const int slot = (int)(g_qTail & (Q_N - 1));
        const int mode = g_qMode[slot] & 0x1FF;
        append_raw(g_qText[slot], mode, -1, 0);
        g_qTail = g_qTail + 1;                           // the slot is copied out : hand it back
    }
}

void chat_parse_speaker(ChatLine& h) {
    h.chSpeaker[0] = 0; h.chBodyAt = 0; h.chLsIdx = 0; h.chTellOut = 0;
    const char* t = h.text; const int n = h.len;
    if (h.chan == CH_LS1) h.chLsIdx = 1; else if (h.chan == CH_LS2) h.chLsIdx = 2;
    auto take = [&](int a, int b, int body) {              // speaker = text[a, b) ; the message starts at `body`
        if (b <= a || b - a > 19) return;
        int k = 0; for (int j = a; j < b; ++j) h.chSpeaker[k++] = t[j]; h.chSpeaker[k] = 0;
        while (body < n && t[body] == ' ') ++body;
        h.chBodyAt = (unsigned char)(body > 239 ? 239 : body);
    };
    auto find = [&](const char* needle, int from, int limit) -> int {   // first index of needle in [from, limit)
        const int ln = (int)strlen(needle);
        for (int i = from; i + ln <= n && i < limit; ++i) if (!memcmp(t + i, needle, ln)) return i;
        return -1;
    };
    switch (h.chan) {
        case CH_PARTY:
            if (t[0] == '(') { const int e = find(")", 1, 21); if (e > 0) take(1, e, e + 1); }
            break;
        case CH_LS1: case CH_LS2: {
            int s0 = 0;
            if (t[0] == '[' && (t[1] == '1' || t[1] == '2') && t[2] == ']') { h.chLsIdx = (unsigned char)(t[1] - '0'); s0 = 3; }
            if (t[s0] == '<') { const int e = find(">", s0 + 1, s0 + 21); if (e > 0) take(s0 + 1, e, e + 1); }
            break; }
        case CH_TELL:
            if (t[0] == '>' && t[1] == '>') {                  // you sent it : ">>Name : msg"
                h.chTellOut = 1; int e = 2; while (e < n && t[e] != ' ' && t[e] != ':') ++e;
                int b = e; while (b < n && (t[b] == ' ' || t[b] == ':')) ++b;
                take(2, e, b);
            } else {                                           // you received it : "Name>> msg"
                const int g = find(">>", 1, 21);
                if (g > 0) { bool sp = false; for (int j = 0; j < g; ++j) if (t[j] == ' ') sp = true; if (!sp) take(0, g, g + 2); }
            }
            break;
        case CH_UNITY: case CH_ASSIST:
            if (t[0] == '{') { const int e = find("}", 1, 21); if (e > 0) take(1, e, e + 1); }
            break;
        case CH_SAY: case CH_SHOUT: case CH_YELL: case CH_NPC: {
            const int c = find(" : ", 1, 30);
            if (c > 0) { int e = c; if (h.chan == CH_YELL) { const int br = find("[", 1, c); if (br > 0) e = br; } take(0, e, c + 3); }
            else if (h.chan == CH_YELL) { const int br = find("[", 1, 21), cl = find("]:", 1, 60); if (br > 0 && cl > br) take(0, br, cl + 2); }
            break; }
        default: break;
    }
}

void chat_mode_report() {
    static const char* NAME[CH_COUNT] = { "say", "shout", "yell", "tell", "party", "ls1", "ls2", "unity", "emote", "assist", "system", "npc", "BATTLE", "other" };
    windower::debug::log("=== CHATMODES : modes seen since load (%u lines, %ld dropped by a full ring) ===", g_seq, (long)g_qDropped);
    for (int m = 0; m < 512; ++m)
        if (g_modeCount[m]) windower::debug::log("CHATMODES mode %3d x%-5u -> %-6s | %s", m, g_modeCount[m], NAME[chat_classify(m)], g_modeSample[m]);
}

} // namespace aio
