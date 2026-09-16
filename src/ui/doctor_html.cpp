// doctor_html.cpp -- see doctor_html.h. One self-contained page, written beside AioHud.dll.
//
// NO EXTERNAL ANYTHING. The file is opened from disk, often with the game running and sometimes with no
// network at all: no web font, no CDN, no script. The only outside references are the gear icons, and those
// are the plugin's own, at a path relative to where this file is written.
//
// The layout deliberately mirrors the game's own windows -- the equipment as its 4x4 grid, in ITS order, and
// the attributes as base + what the gear adds, which is how the status menu shows them. A sheet that reads
// like the game is a sheet you can check against the game.
#include "ui/doctor_html.h"
#include "model/party_state.h"
#include "model/gamestate.h"
#include "model/charsheet.h"
#include "model/paths.h"
#include "model/itemnames_gen.h"
#include "model/ui_config.h"
#include "ui/config_controls.h"   // tr(en, fr) : the page follows the config language
#include "gfx/texture.h"   // read_gear_icon_bmp : the plugin decodes its own BMP V4 cache
#include "windower.h"
#include <cstdio>
#include <cstring>

namespace aio {

// Defined in the plugin TU (aiohud.cpp) : the build string the page stamps itself with.
const char* aio_version_string();

namespace {

const char* const JOB_ABBR[CS_JOB_N] = {
    "", "WAR", "MNK", "WHM", "BLM", "RDM", "THF", "PLD", "DRK", "BST", "BRD", "RNG", "SAM",
    "NIN", "DRG", "SMN", "BLU", "COR", "PUP", "DNC", "SCH", "GEO", "RUN"
};
// The game's own Equipment window : four rows of four, in ITS order. The packet order is a different thing,
// and showing that one instead would make every comparison with the game harder than it needs to be.
struct SlotCell { int pkt; const char* label; };
const SlotCell WINDOW[16] = {
    { 0, "main" },  { 1, "sub" },   { 2, "range" }, { 3, "ammo" },
    { 4, "head" },  { 9, "neck" },  {11, "ear 1" }, {12, "ear 2" },
    { 5, "body" },  { 6, "hands" }, {13, "ring 1"}, {14, "ring 2"},
    {15, "back" },  {10, "waist" }, { 7, "legs" },  { 8, "feet" }
};

void esc(FILE* f, const char* s) {          // the item names are game data : they may carry an apostrophe
    if (!s) return;
    for (; *s; ++s) {
        switch (*s) {
            case '&': fputs("&amp;", f); break;
            case '<': fputs("&lt;", f); break;
            case '>': fputs("&gt;", f); break;
            case '"': fputs("&quot;", f); break;
            default:  fputc(*s, f); break;
        }
    }
}

void bar(FILE* f, int base, int add, int scale) {   // base in steel, what the gear adds in green
    const int b = scale > 0 ? (base * 100) / scale : 0;
    const int a = scale > 0 && add > 0 ? (add * 100) / scale : 0;
    fprintf(f, "<div class='tr'><i class='b1' style='width:%d%%'></i><i class='b2' style='width:%d%%'></i></div>", b, a);
}

}  // namespace


// ---- THE ICONS TRAVEL IN THE PAGE --------------------------------------------------------------------------
// The first cut pointed <img> at the plugin's own AioHud/assets/gearicons/<id>.bmp. The path was right and the
// file was there, and the browser drew NOTHING : those files are BMP **V4** (the header EquipViewer writes, and
// the one texture.cpp reads), and a V4 header with an alpha mask is exactly the BMP variant browsers decline.
// Reported from the page on 2026-09-15.
//
// So the icons are decoded with the plugin's own reader and re-encoded as PNG, inline, as a data: URI. Two
// things are won: the browser has no format left to refuse, and the page becomes SELF-CONTAINED -- it can be
// sent to somebody without the folder beside it, which a bug report always wants.
//
// The PNG is written with STORED deflate blocks : a valid zlib stream that compresses nothing. It costs ~5.5 KB
// per icon, 90 KB for a full set -- irrelevant for a file opened once -- and it removes the only reason this
// writer would otherwise need a compression library.
namespace {

unsigned crc32_of(const unsigned char* p, int n, unsigned crc = 0xFFFFFFFFu) {
    for (int i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (unsigned)(-(int)(crc & 1)));
    }
    return crc;
}
void put_be32(unsigned char* d, unsigned v) { d[0] = (unsigned char)(v >> 24); d[1] = (unsigned char)(v >> 16); d[2] = (unsigned char)(v >> 8); d[3] = (unsigned char)v; }

// One PNG chunk : length, tag, body, CRC over tag+body.
int png_chunk(unsigned char* out, const char* tag, const unsigned char* body, int n) {
    put_be32(out, (unsigned)n);
    memcpy(out + 4, tag, 4);
    if (n) memcpy(out + 8, body, n);
    unsigned c = crc32_of((const unsigned char*)tag, 4);
    c = crc32_of(body, n, c) ^ 0xFFFFFFFFu;
    put_be32(out + 8 + n, c);
    return 12 + n;
}

const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 32x32 ARGB (top-down, 0xAARRGGBB -- what read_gear_icon_bmp returns) -> a base64 PNG, written straight to the
// file so nothing holds the whole thing twice.
void write_icon_data_uri(FILE* f, const u32* px) {
    const int W = 32, H = 32;
    const int RAW = H * (1 + W * 4);                       // one filter byte per row, then RGBA
    static unsigned char raw[32 * (1 + 32 * 4)];
    int o = 0;
    for (int y = 0; y < H; ++y) {
        raw[o++] = 0;                                      // filter 0 : none
        for (int x = 0; x < W; ++x) {
            const u32 c = px[y * W + x];
            raw[o++] = (unsigned char)((c >> 16) & 0xFF);  // R
            raw[o++] = (unsigned char)((c >> 8) & 0xFF);   // G
            raw[o++] = (unsigned char)(c & 0xFF);          // B
            raw[o++] = (unsigned char)((c >> 24) & 0xFF);  // A
        }
    }
    // zlib stream : 2-byte header, one stored block, adler32.
    static unsigned char z[4192];
    int zn = 0;
    z[zn++] = 0x78; z[zn++] = 0x01;
    z[zn++] = 1;                                           // BFINAL=1, BTYPE=00 (stored)
    z[zn++] = (unsigned char)(RAW & 0xFF); z[zn++] = (unsigned char)((RAW >> 8) & 0xFF);
    z[zn++] = (unsigned char)(~RAW & 0xFF); z[zn++] = (unsigned char)((~RAW >> 8) & 0xFF);
    memcpy(z + zn, raw, RAW); zn += RAW;
    unsigned a = 1, b = 0;
    for (int i = 0; i < RAW; ++i) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    put_be32(z + zn, (b << 16) | a); zn += 4;

    static unsigned char png[4192 + 128];
    int pn = 0;
    static const unsigned char SIG[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    memcpy(png, SIG, 8); pn = 8;
    unsigned char ihdr[13];
    put_be32(ihdr, (unsigned)W); put_be32(ihdr + 4, (unsigned)H);
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;   // 8-bit RGBA, no interlace
    pn += png_chunk(png + pn, "IHDR", ihdr, 13);
    pn += png_chunk(png + pn, "IDAT", z, zn);
    pn += png_chunk(png + pn, "IEND", 0, 0);

    fputs("data:image/png;base64,", f);
    for (int i = 0; i < pn; i += 3) {
        const unsigned v = ((unsigned)png[i] << 16)
                         | ((i + 1 < pn ? (unsigned)png[i + 1] : 0u) << 8)
                         |  (i + 2 < pn ? (unsigned)png[i + 2] : 0u);
        fputc(B64[(v >> 18) & 63], f);
        fputc(B64[(v >> 12) & 63], f);
        fputc(i + 1 < pn ? B64[(v >> 6) & 63] : '=', f);
        fputc(i + 2 < pn ? B64[v & 63] : '=', f);
    }
}

}  // namespace

const char* write_doctor_html(const GameState& gs, const char* const* lines, int nd) {
    static char path[MAX_PATH];
    const char* who = gs.me.name[0] ? gs.me.name : "unknown";
    {   // beside AioHud.dll, like the text report -- one file per character, so a dual-box writes two
        char rel[96];
        _snprintf(rel, sizeof(rel), "..\\aiohud_doctor_%s.html", who);
        rel[sizeof(rel) - 1] = 0;
        plugin_path(path, sizeof(path), rel);
    }
    FILE* f = fopen(path, "w");
    if (!f) return 0;

    const CharSheet& cs = party().charsheet();
    const bool fr = tr("en", "fr")[0] == 'f';

    fputs("<!doctype html><html><head><meta charset='utf-8'>", f);
    fprintf(f, "<title>AioHUD %s %s</title>", fr ? "fiche" : "sheet", who);
    fputs("<style>"
      ":root{--g:#0B0E13;--p:#12161D;--r:#1A202A;--s:#0E1218;--l:#222A36;--ls:#1A212B;"
      "--t:#E8ECF2;--d:#8D98A8;--st:#5D6B82;--gold:#D4A93C;--ok:#4FA87A;--no:#D4685B;--none:#6B7688}"
      "*{box-sizing:border-box}"
      "body{background:var(--g);color:var(--t);font:14px/1.5 'Segoe UI',system-ui,sans-serif;"
      "max-width:1000px;margin:0 auto;padding:26px 20px 60px;font-variant-numeric:tabular-nums}"
      "h1{font-size:26px;letter-spacing:-.02em;margin:2px 0 4px}"
      ".eb{font:11px/1 ui-monospace,Consolas,monospace;letter-spacing:.14em;text-transform:uppercase;color:var(--st)}"
      ".sub{color:var(--d);margin:0 0 22px}"
      ".kpis{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:22px}"
      ".kpi{border:1px solid var(--l);border-radius:8px;background:var(--p);padding:9px 13px;min-width:96px}"
      ".kpi span{display:block;font:10px/1 ui-monospace,monospace;letter-spacing:.1em;text-transform:uppercase;color:var(--st)}"
      ".kpi b{display:block;font-size:20px;margin-top:3px}"
      "h2{font-size:12px;font-family:ui-monospace,Consolas,monospace;letter-spacing:.13em;text-transform:uppercase;"
      "color:var(--st);font-weight:500;margin:26px 0 10px;display:flex;justify-content:space-between;gap:10px}"
      ".src{font-size:9.5px;border:1px solid var(--l);border-radius:3px;padding:1px 6px;color:var(--none);text-transform:none;letter-spacing:.04em}"
      ".src.ok{color:var(--ok);border-color:rgba(79,168,122,.3)}"
      ".cols{display:grid;grid-template-columns:repeat(auto-fit,minmax(290px,1fr));gap:14px;align-items:start}"
      ".box{border:1px solid var(--l);border-radius:10px;background:var(--p);padding:15px 17px}"
      ".at{display:grid;grid-template-columns:34px 1fr 96px;gap:9px;align-items:center;margin-bottom:6px}"
      ".at .n{font:11.5px ui-monospace,monospace;color:var(--d)}"
      ".tr{height:7px;border-radius:4px;background:var(--s);overflow:hidden;display:flex}"
      ".tr i{display:block;height:100%}.b1{background:var(--st)}.b2{background:var(--ok)}"
      ".num{font:12px ui-monospace,monospace;text-align:right;color:var(--d)}"
      ".num b{color:var(--t)}.num u{text-decoration:none;color:var(--ok)}"
      ".res{display:grid;grid-template-columns:repeat(4,1fr);gap:5px;margin-top:13px}"
      ".res div{border:1px solid var(--ls);border-radius:6px;background:var(--s);padding:5px 3px;text-align:center}"
      ".res span{display:block;font:9px ui-monospace,monospace;letter-spacing:.06em;text-transform:uppercase;color:var(--st)}"
      ".res b{display:block;font-size:14px;margin-top:1px}"
      ".eq{display:grid;grid-template-columns:repeat(4,1fr);gap:6px;max-width:400px}"
      ".sl{border:1px solid var(--ls);border-radius:8px;background:var(--s);padding:7px 4px 6px;text-align:center;min-width:0}"
      ".sl img{width:36px;height:36px;image-rendering:pixelated;display:block;margin:0 auto}"
      ".sl .ph{width:36px;height:36px;margin:0 auto;border:1px dashed var(--l);border-radius:5px}"
      ".sl em{display:block;font:9px ui-monospace,monospace;letter-spacing:.06em;text-transform:uppercase;color:var(--st);margin-top:4px}"
      ".sl small{display:block;font-size:10px;color:var(--d);margin-top:2px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
      ".jobs{display:grid;grid-template-columns:repeat(auto-fill,minmax(62px,1fr));gap:5px}"
      ".jb{border:1px solid var(--ls);border-radius:7px;background:var(--s);padding:6px 3px;text-align:center}"
      ".jb b{display:block;font:10px ui-monospace,monospace;color:var(--d)}"
      ".jb i{display:block;font-style:normal;font-size:14px;font-weight:600;margin-top:1px}"
      ".jb.m{border-color:var(--gold);background:rgba(212,169,60,.12)}.jb.m b{color:var(--gold)}"
      ".jb.z i{color:var(--none)}"
      "ul.f{margin:0;padding-left:18px;color:var(--d)}ul.f li{margin:5px 0}"
      ".pend{color:var(--none);font-style:italic}"
      "footer{margin-top:30px;padding-top:14px;border-top:1px solid var(--l);color:var(--st);font-size:12px}"
      "@media(max-width:430px){.eq{max-width:none}}"
      "</style></head><body>", f);

    // ---- who ------------------------------------------------------------------------------------------
    fprintf(f, "<div class='eb'>AioHUD %s &middot; doctor</div><h1>", aio_version_string());
    esc(f, who);
    fputs("</h1>", f);
    fprintf(f, "<p class='sub'>%s %s%d / %s%d &middot; zone %d</p>",
            "Job",                                    // same word in both languages -- no ternary to maintain
            (gs.me.mjob > 0 && gs.me.mjob < CS_JOB_N) ? JOB_ABBR[gs.me.mjob] : "?", (int)gs.me.mlvl,
            (gs.me.sjob > 0 && gs.me.sjob < CS_JOB_N) ? JOB_ABBR[gs.me.sjob] : "?", (int)gs.me.slvl,
            (int)gs.zone);

    fputs("<div class='kpis'>", f);
    fprintf(f, "<div class='kpi'><span>HP</span><b>%d</b></div>", (int)gs.me.hp);
    fprintf(f, "<div class='kpi'><span>MP</span><b>%d</b></div>", (int)gs.me.mp);
    fprintf(f, "<div class='kpi'><span>TP</span><b>%d</b></div>", (int)gs.me.tp);
    if (cs.statsOk) {
        fprintf(f, "<div class='kpi'><span>%s</span><b>%u</b></div>", fr ? "Attaque" : "Attack", cs.attack);
        fprintf(f, "<div class='kpi'><span>%s</span><b>%u</b></div>", fr ? "Défense" : "Defense", cs.defense);
    }
    fputs("</div>", f);

    fputs("<div class='cols'>", f);

    // ---- attributes ------------------------------------------------------------------------------------
    fputs("<div class='box'><h2>", f);
    fputs(fr ? "Caractéristiques" : "Attributes", f);
    fputs(cs.statsOk ? "<span class='src ok'>paquet 0x061</span>" : "<span class='src'>0x061</span>", f);
    fputs("</h2>", f);
    if (!cs.statsOk) {
        fputs("<p class='pend'>", f);
        fputs(fr ? "Pas encore reçu. Ce paquet arrive à la connexion, au changement de job ou de zone."
                 : "Not received yet. That packet comes on a login, a job change or a zone.", f);
        fputs("</p>", f);
    } else {
        static const char* const AT[CS_ATTR_N] = { "STR", "DEX", "VIT", "AGI", "INT", "MND", "CHR" };
        int scale = 1;
        for (int a = 0; a < CS_ATTR_N; ++a) { const int t = cs.attr_total(a); if (t > scale) scale = t; }
        for (int a = 0; a < CS_ATTR_N; ++a) {
            fprintf(f, "<div class='at'><span class='n'>%s</span>", AT[a]);
            bar(f, (int)cs.base[a], (int)cs.added[a], scale);
            fprintf(f, "<span class='num'>%d <u>%+d</u> <b>%d</b></span></div>",
                    (int)cs.base[a], (int)cs.added[a], cs.attr_total(a));
        }
        // The resistances, SHOWN. They were held back while all-zero looked like a bad offset ; the player
        // read the game's own Status menu on 2026-09-15 and it showed zero too, with STR and VIT matching to
        // the digit at the same instant. Zero is the real value of a character wearing no resist gear, and a
        // number confirmed against the game is a number this page may print.
        static const char* const ELF[CS_ELEM_N] = { "Feu", "Glace", "Vent", "Terre", "Foudre", "Eau", "Lumière", "Ténèbres" };
        static const char* const ELE[CS_ELEM_N] = { "Fire", "Ice", "Wind", "Earth", "Lightning", "Water", "Light", "Dark" };
        fputs("<div class='res'>", f);
        for (int e = 0; e < CS_ELEM_N; ++e)
            fprintf(f, "<div><span>%s</span><b>%d</b></div>", fr ? ELF[e] : ELE[e], (int)cs.resist[e]);
        fputs("</div>", f);
    }
    fputs("</div>", f);

    // ---- equipment, the game's own window ---------------------------------------------------------------
    fputs("<div class='box'><h2>", f);
    fputs(fr ? "Équipement" : "Equipment", f);
    fputs(gs.equipValid ? "<span class='src ok'>mémoire</span>" : "<span class='src'>lecture en attente</span>", f);
    fputs("</h2><div class='eq'>", f);
    for (int i = 0; i < 16; ++i) {
        const int s = WINDOW[i].pkt;
        const unsigned id = gs.equipValid ? gs.equip.id[s] : 0;
        fputs("<div class='sl'>", f);
        if (id) {
            static u32 px[32 * 32];
            char icon[MAX_PATH]; char rel[64];
            _snprintf(rel, sizeof(rel), "assets%cgearicons%c%u.bmp", '\\', '\\', id); rel[sizeof(rel) - 1] = 0;
            plugin_path(icon, sizeof(icon), rel);
            if (read_gear_icon_bmp(icon, px)) {
                fputs("<img alt='' src='", f);
                write_icon_data_uri(f, px);
                fputs("'>", f);
            } else {
                fputs("<div class='ph'></div>", f);   // not in the cache : an empty frame, never a broken image
            }
        } else {
            fputs("<div class='ph'></div>", f);
        }
        fprintf(f, "<em>%s</em><small title='", WINDOW[i].label);
        const char* nm = id ? item_name(id) : 0;
        esc(f, nm ? nm : (fr ? "vide" : "empty"));
        fputs("'>", f);
        esc(f, nm ? nm : "—");
        fputs("</small></div>", f);
    }
    fputs("</div></div>", f);

    // ---- every job --------------------------------------------------------------------------------------
    fputs("<div class='box' style='grid-column:1/-1'><h2>", f);
    fputs(fr ? "Tous les jobs" : "All jobs", f);
    fputs(cs.jobsOk ? "<span class='src ok'>paquet 0x01B</span>" : "<span class='src'>0x01B</span>", f);
    fputs("</h2>", f);
    if (!cs.jobsOk) {
        fputs("<p class='pend'>", f);
        fputs(fr ? "Pas encore reçu — ce paquet arrive au changement de job ou de zone."
                 : "Not received yet — that packet comes on a job change or a zone.", f);
        fputs("</p>", f);
    } else {
        fputs("<div class='jobs'>", f);
        for (int j = 1; j < CS_JOB_N; ++j) {
            const int lv = (int)cs.jobLvl[j];
            fprintf(f, "<div class='jb%s%s'><b>%s</b><i>%d</i>", cs.mastered[j] ? " m" : "", lv ? "" : " z",
                    JOB_ABBR[j], lv);
            // SPENT, not the reserve : the two sit side by side in the packet and the reserve is capped at 500,
            // so showing it would put "500 JP" on every job the character ever played.
            if (cs.pointsOk && cs.jpSpent[j]) fprintf(f, "<b style='margin-top:2px'>%u JP</b>", cs.jpSpent[j]);
            fputs("</div>", f);
        }
        fputs("</div>", f);
    }
    fputs("</div>", f);

    // ---- what the doctor itself found --------------------------------------------------------------------
    fputs("<div class='box' style='grid-column:1/-1'><h2>", f);
    fputs(fr ? "Ce que le doctor a trouvé" : "What the doctor found", f);
    fprintf(f, "<span class='src%s'>%d</span></h2>", nd ? "" : " ok", nd);
    if (!nd) {
        fputs("<p style='margin:0;color:var(--ok)'>", f);
        fputs(fr ? "Rien à signaler." : "Nothing to report.", f);
        fputs("</p>", f);
    } else {
        fputs("<ul class='f'>", f);
        for (int i = 0; i < nd; ++i) { fputs("<li>", f); esc(f, lines[i]); fputs("</li>", f); }
        fputs("</ul>", f);
    }
    fputs("</div></div>", f);

    fputs("<footer>", f);
    fputs(fr ? "Chaque valeur porte sa source. Un champ non reçu est écrit comme tel, jamais comme un zéro : "
               "ces paquets n'arrivent qu'à la connexion, au changement de job ou de zone."
             : "Every value carries its source. A field that has not arrived says so rather than showing a zero: "
               "these packets only come on a login, a job change or a zone.", f);
    fputs("</footer></body></html>", f);
    fclose(f);
    return path;
}

} // namespace aio
