// item_text.h -- an item's TEXT and FLAGS from its record in the client's item DATs (2026-11-17), pure (no I/O).
// Used by the NPC answers' item card (hud_actmenu.cpp) : the client's own description window 'iteminfo' shows the item
// at its handler+0x24 ; AioHUD draws the same card from the record. Measured on the 2026-11 DATs (decoded record =
// rotate-left-by-3 of every byte, gear_dat.h), cross-checked against Windower's res/items.lua :
//   +0x00 u32 the id           +0x04 u16 FLAGS (= res flags : 0x8000 Rare, 0x4000 Ex)
//   armour / weapon : +0x10 u16 the LEVEL, +0x18 u32 the JOBS mask (bit n = job n)
//   a STRING TABLE whose place depends on the item kind (+0x1C general / usable, +0x30 armour, +0x3C weapon) :
//     u32 count, then count x (u32 offset, u32 kind) ; a kind-0 string's text is at table + offset + 0x1C,
//     kind 1 is a number (the article). Order : name, article, log name, log plural, description.
// The text is the client's encoding : ASCII, 0x0A a line break, EF 1F..26 an element glyph, other bytes >= 0x80 the
// lead of a 2-byte character (Japanese ; kept out of an English card).
#pragma once
#include <string.h>
#include <stdio.h>

namespace aio {

// Equipment fields (2026-11-17, Ark Tachi / Warp Ring / Nexus Cape / Warp Cudgel against the client's own card) :
//   +0x12 u16 SLOTS mask, +0x14 u16 RACES mask (0x1FE = all)
//   weapon (string table +0x3C) : +0x20 DMG, +0x22 Delay, +0x26 SKILL ; charges +0x2C u8, equip delay +0x2E u16 s,
//                                 reuse +0x30 u32 s
//   armour (string table +0x30) : charges +0x20 u8, equip delay +0x22 u16 s, reuse +0x24 u32 s
// The client's enchantment line : "<charges/max delay/[reuse, delay]>" (an unused item : charges = max).
struct ItemText {
    unsigned id = 0, flags = 0, jobs = 0;
    int  level = 0;               // > 0 : an equipment piece
    bool weapon = false;
    unsigned slots = 0, races = 0, skill = 0;
    int  charges = 0, equipDelay = 0, reuse = 0;   // charges > 0 : an enchantment
    char name[64] = {};
    char logName[96] = {};        // "ark tachi" : the client's card title is this, its first letter raised ("Ark tachi")
    char desc[640] = {};
};
// the card's title as the client writes it : the log name, first letter upper case (else the short name)
inline void item_title(const ItemText& t, char* out, int cap) {
    const char* s = t.logName[0] ? t.logName : t.name;
    snprintf(out, cap, "%s", s);
    if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 32);
}
inline bool item_rare(const ItemText& t) { return (t.flags & 0x8000) != 0; }
inline bool item_ex(const ItemText& t)   { return (t.flags & 0x4000) != 0; }

namespace item_text_detail {
inline unsigned u32at(const unsigned char* r, int n, int o) { return o >= 0 && o + 4 <= n ? (unsigned)r[o] | (unsigned)r[o + 1] << 8 | (unsigned)r[o + 2] << 16 | (unsigned)r[o + 3] << 24 : 0u; }
// the client's text -> printable ASCII ; elements spelled out, line breaks kept as '\n'
inline void decode(const unsigned char* s, int max, char* out, int cap) {
    static const char* const EL[8] = { "Fire", "Ice", "Wind", "Earth", "Lightning", "Water", "Light", "Dark" };
    int o = 0;
    for (int i = 0; i < max && s[i] && o < cap - 1; ++i) {
        const unsigned char c = s[i];
        if (c == 0x0A) { out[o++] = '\n'; continue; }
        if (c == 0xEF && i + 1 < max && s[i + 1] >= 0x1F && s[i + 1] <= 0x26) {
            const char* e = EL[s[i + 1] - 0x1F]; while (*e && o < cap - 1) out[o++] = *e++;
            ++i; continue;
        }
        if (c >= 0x80) { ++i; continue; }                       // a 2-byte character : skipped
        if (c >= 0x20 && c < 0x7F) out[o++] = (char)c;
    }
    out[o] = 0;
}
} // namespace item_text_detail

// `rec` = the DECODED record head (at least 0x280 bytes), `id` = the item it must name. False when it does not, or no
// string table is found -- never a guess.
inline bool item_text_parse(const unsigned char* rec, int n, unsigned id, ItemText& out) {
    using namespace item_text_detail;
    out = ItemText();
    if (!rec || n < 0x100 || u32at(rec, n, 0) != id) return false;
    out.id = id; out.flags = (unsigned)rec[4] | (unsigned)rec[5] << 8;
    int T = -1, cnt = 0;
    for (int o = 0x10; o <= 0xA0; o += 4) {
        const unsigned c = u32at(rec, n, o);
        if (c >= 2 && c <= 9 && u32at(rec, n, o + 4) == 4 + 8 * c) { T = o; cnt = (int)c; break; }
    }
    if (T < 0) return false;
    int at0[9], k0 = 0;                                         // the text strings, in order : name, log name, plural, description
    for (int i = 0; i < cnt; ++i) {
        const unsigned off = u32at(rec, n, T + 4 + 8 * i), kind = u32at(rec, n, T + 8 + 8 * i);
        if (kind != 0) continue;
        const int at = T + (int)off + 0x1C;
        if (at <= 0 || at >= n) continue;
        at0[k0++] = at;
    }
    if (k0 < 1) return false;
    decode(rec + at0[0], n - at0[0], out.name, sizeof(out.name));
    if (k0 >= 3) decode(rec + at0[1], n - at0[1], out.logName, sizeof(out.logName));
    if (k0 >= 2) decode(rec + at0[k0 - 1], n - at0[k0 - 1], out.desc, sizeof(out.desc));
    if (T >= 0x30) {                                            // armour / weapon : level, jobs, slots, races, enchantment
        const int lv = (int)(rec[0x10] | rec[0x11] << 8);
        if (lv >= 1 && lv <= 99) {
            out.level = lv; out.jobs = u32at(rec, n, 0x18);
            out.slots = (unsigned)(rec[0x12] | rec[0x13] << 8); out.races = (unsigned)(rec[0x14] | rec[0x15] << 8);
            out.weapon = T >= 0x3C;
            const int e = out.weapon ? 0x2C : 0x20;             // the enchantment block
            if (out.weapon) out.skill = (unsigned)(rec[0x26] | rec[0x27] << 8);
            out.charges = rec[e];
            out.equipDelay = (int)(rec[e + 2] | rec[e + 3] << 8);
            out.reuse = (int)u32at(rec, n, e + 4);
            if (out.charges <= 0 || out.charges > 255 || out.reuse < 0 || out.reuse > 7 * 86400) out.charges = 0;
        }
    }
    return out.name[0] != 0;
}

// the client's second line : "(Great Katana)All Races" for a weapon, "[Back]All Races" for armour
inline void item_kind_line(const ItemText& t, char* out, int cap) {
    out[0] = 0; if (t.level <= 0 || cap < 8) return;
    const char* kind = 0;
    if (t.weapon) {
        switch (t.skill) {
        case 1: kind = "(Hand-to-Hand)"; break;   case 2: kind = "(Dagger)"; break;       case 3: kind = "(Sword)"; break;
        case 4: kind = "(Great Sword)"; break;    case 5: kind = "(Axe)"; break;          case 6: kind = "(Great Axe)"; break;
        case 7: kind = "(Scythe)"; break;         case 8: kind = "(Polearm)"; break;      case 9: kind = "(Katana)"; break;
        case 10: kind = "(Great Katana)"; break;  case 11: kind = "(Club)"; break;        case 12: kind = "(Staff)"; break;
        case 25: kind = "(Archery)"; break;       case 26: kind = "(Marksmanship)"; break; case 27: kind = "(Throwing)"; break;
        case 41: kind = "(Stringed Instrument)"; break; case 42: kind = "(Wind Instrument)"; break; case 45: kind = "(Handbell)"; break;
        case 48: kind = "(Fishing)"; break;       default: kind = 0; break;
        }
        if (!kind && (t.slots & 0x8)) kind = "[Ammo]";
    } else {
        const unsigned s = t.slots;
        if (s & 0x10) kind = "[Head]"; else if (s & 0x20) kind = "[Body]"; else if (s & 0x40) kind = "[Hands]";
        else if (s & 0x80) kind = "[Legs]"; else if (s & 0x100) kind = "[Feet]"; else if (s & 0x200) kind = "[Neck]";
        else if (s & 0x400) kind = "[Waist]"; else if (s & 0x1800) kind = "[Ear]"; else if (s & 0x6000) kind = "[Ring]";
        else if (s & 0x8000) kind = "[Back]"; else if (s & 0x2) kind = "[Shield]"; else if (s & 0x8) kind = "[Ammo]";
        else if (s & 0x4) kind = "[Ranged]";
    }
    const unsigned r = t.races & 0x1FE;
    const char* races = r == 0x1FE ? "All Races" : 0;
    char rb[96] = "";
    if (!races) {
        static const struct { unsigned m; const char* n; } R[] = {
            { 0x006, "Hume" }, { 0x018, "Elvaan" }, { 0x060, "Tarutaru" }, { 0x002, "Hume M" }, { 0x004, "Hume F" },
            { 0x008, "Elvaan M" }, { 0x010, "Elvaan F" }, { 0x020, "Tarutaru M" }, { 0x040, "Tarutaru F" }, { 0x080, "Mithra" }, { 0x100, "Galka" } };
        unsigned left = r; int o = 0;
        for (unsigned i = 0; i < sizeof(R) / sizeof(R[0]) && o < (int)sizeof(rb) - 16; ++i)
            if ((left & R[i].m) == R[i].m) { o += snprintf(rb + o, sizeof(rb) - o, "%s%s", o ? " " : "", R[i].n); left &= ~R[i].m; }
        races = rb;
    }
    snprintf(out, cap, "%s%s", kind ? kind : "", races);
}
// "<1/1 0:30/[1:00:00, 0:30]>" ; empty when the item has no enchantment
inline void item_enchant_line(const ItemText& t, char* out, int cap) {
    out[0] = 0; if (t.charges <= 0 || cap < 16) return;
    auto tm = [](int s, char* b, int c) {
        if (s >= 3600) snprintf(b, c, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60); else snprintf(b, c, "%d:%02d", s / 60, s % 60);
    };
    char d[16], r[16]; tm(t.equipDelay, d, sizeof(d)); tm(t.reuse, r, sizeof(r));
    snprintf(out, cap, "<%d/%d %s/[%s, %s]>", t.charges, t.charges, d, r, d);
}

} // namespace aio
