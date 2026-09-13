// gear_dat.h -- the DECISIONS behind the gear-icon ROM decode, pure and covered by tests/t_geardat.cpp.
//
// WHY THIS IS ITS OWN HEADER. On 2026-09-10 a client patch grew every item record in the icon DATs from 0xC00 to
// 0x1400 bytes. The decoder trusted 0xC00 (a constant ported from EquipViewer), so it read the wrong record for
// every id and SUCCEEDED -- blank icons, other items' art, garbage, all drawn and cached to disk. Nothing in the
// project noticed: the offline suite never looked at the decoder, and from the inside every wrong read was a
// success. What fixed it was making each read prove itself (the record carries its own item id and icon
// header). Those proofs are decisions, and a decision that once shipped broken belongs where a test can pin it.
//
// Nothing here touches a file, a device or a clock : every byte is handed in, every read goes through a
// callback. texture.cpp drives the real file ; the tests drive a DAT built in memory.
#pragma once
#include <cstdint>

namespace aio {

// id-range -> (ROM DAT relative path, id offset). Straight from EquipViewer's item_dat_map ; verified complete
// against the DATs on 2026-07-19 and again after the 2026-09-10 patch (every record carries its own id).
struct GearDat { unsigned lo, hi; const char* dat; int off; };
inline const GearDat* gear_dat_table(int& n) {
    static const GearDat T[] = {
        { 0x0001, 0x0FFF, "118/106", -1 },   // General Items
        { 0x1000, 0x1FFF, "118/107",  0 },   // Usable Items
        { 0x2000, 0x21FF, "118/110",  0 },   // Automaton Items
        { 0x2200, 0x27FF, "301/115",  0 },   // General Items 2
        { 0x2800, 0x3FFF, "118/109",  0 },   // Armor Items
        { 0x4000, 0x59FF, "118/108",  0 },   // Weapon Items
        { 0x5A00, 0x6FFF, "286/73",   0 },   // Armor Items 2
        { 0x7000, 0x73FF, "217/21",   0 },   // Maze / Basic Items
        { 0x7400, 0x77FF, "288/80",   0 },   // Instinct Items
        { 0xF000, 0xF1FF, "288/67",   0 },   // Monipulator Items
        { 0xFFFF, 0xFFFF, "174/48",   0 },   // Gil
    };
    n = (int)(sizeof(T) / sizeof(T[0]));
    return T;
}
inline const GearDat* gear_dat_for(unsigned id) {
    int n = 0; const GearDat* t = gear_dat_table(n);
    for (int i = 0; i < n; ++i) if (id >= t[i].lo && id <= t[i].hi) return &t[i];
    return 0;
}
inline long gear_dat_first(const GearDat& d)   { return (long)d.lo + d.off; }                 // the id at record 0
inline long gear_dat_records(const GearDat& d) { return (long)d.hi - gear_dat_first(d) + 1; }  // records the range spans

// Every byte of a record is bit-rotated on disk : decoded = rotate-left-by-3(encoded).
inline unsigned char gear_rotl3(unsigned char x) { return (unsigned char)(((x & 0x1F) << 3) | (x >> 5)); }

// A record's head : the bytes before the icon's pixel block. The item id sits at +0, a 40-byte
// BITMAPINFOHEADER at +0x295, so the palette starts at +0x2BD. All ENCODED, as read from disk.
static const long GEAR_HEAD = 0x2BD;
static const long GEAR_ICON = 0x800;   // 256-entry BGRA palette + 32x32 indices

// THE PROOF that a record is the one we meant : it names `id`, and it carries a 32x32 8-bpp icon header.
// Measured 2026-09-13 over every record of all 11 DATs : both hold in 100 % of them. The id alone proves the
// STRIDE ; the header proves the icon OFFSET and the FORMAT the palette decode below assumes -- a patch that moved
// the icon or switched it to 32 bpp would pass an id-only check and decode to garbage.
inline bool gear_record_ok(const unsigned char* head, unsigned id) {
    if (!head) return false;
    auto u32at = [head](long o) {
        return (unsigned)gear_rotl3(head[o]) | ((unsigned)gear_rotl3(head[o + 1]) << 8)
             | ((unsigned)gear_rotl3(head[o + 2]) << 16) | ((unsigned)gear_rotl3(head[o + 3]) << 24);
    };
    auto u16at = [head](long o) { return (unsigned)gear_rotl3(head[o]) | ((unsigned)gear_rotl3(head[o + 1]) << 8); };
    return u32at(0) == id
        && u32at(0x295) == 40 && u32at(0x299) == 32 && u32at(0x29D) == 32   // biSize, width, height
        && u16at(0x2A1) == 1 && u16at(0x2A3) == 8;                           // planes, bpp
}

// The record sizes worth trying, best first : the one the FILE SIZE implies (survives the next resize without a
// code change), then the two ever observed. Only plausible sizes (>= 0xC00, a multiple of 0x400), no duplicates.
inline int gear_stride_candidates(long fileSize, long records, long out[3]) {
    int n = 0;
    auto add = [&](long s) {
        if (s < 0xC00 || (s & 0x3FF)) return;
        for (int i = 0; i < n; ++i) if (out[i] == s) return;
        out[n++] = s;
    };
    if (fileSize > 0 && records > 0 && fileSize % records == 0) add(fileSize / records);
    add(0x1400);   // since the 2026-09-10 patch
    add(0xC00);    // before it
    return n;
}

// Reads `n` bytes at `off` into `buf` ; false on any short read. Injected so the choice below is testable.
typedef bool (*GearReadFn)(void* ctx, long off, unsigned char* buf, long n);

// The stride whose record at `id`'s index passes gear_record_ok, or 0 : an unknown layout, which the caller must
// REFUSE rather than guess. Never a constant -- that is the bug this header exists to prevent coming back.
inline long gear_find_stride(long fileSize, const GearDat& d, unsigned id, GearReadFn rd, void* ctx) {
    const long index = (long)id - gear_dat_first(d);
    if (index < 0 || !rd) return 0;
    long cand[3]; const int nc = gear_stride_candidates(fileSize, gear_dat_records(d), cand);
    unsigned char head[GEAR_HEAD];
    for (int k = 0; k < nc; ++k) {
        const long s = cand[k];
        if (index * s + GEAR_HEAD + GEAR_ICON > fileSize) continue;   // the whole icon must fit, not just the head
        if (rd(ctx, index * s, head, GEAR_HEAD) && gear_record_ok(head, id)) return s;
    }
    return 0;
}

// The icon block (0x800 encoded bytes) -> 32*32 0xAARRGGBB, TOP-DOWN. Palette bytes rotl3-decoded, alpha doubled
// and clamped ; indices rotl3-decoded ; DAT rows are bottom-up, so flipped. Pinned pixel-for-pixel against all
// 1323 bundled icons on 2026-09-13.
inline void gear_decode_icon(const unsigned char* data, uint32_t* out) {
    unsigned char pal[256][4];
    for (int g = 0; g < 256; ++g) {
        pal[g][0] = gear_rotl3(data[g * 4 + 0]);   // B
        pal[g][1] = gear_rotl3(data[g * 4 + 1]);   // G
        pal[g][2] = gear_rotl3(data[g * 4 + 2]);   // R
        const int a = gear_rotl3(data[g * 4 + 3]) * 2; pal[g][3] = (unsigned char)(a < 256 ? a : 255);
    }
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
        const unsigned char* c = pal[gear_rotl3(data[0x400 + (31 - y) * 32 + x])];
        out[y * 32 + x] = ((uint32_t)c[3] << 24) | ((uint32_t)c[2] << 16) | ((uint32_t)c[1] << 8) | (uint32_t)c[0];
    }
}

// FNV-1a over the 1024 pixels as little-endian bytes : a fingerprint small enough to compile in.
inline uint32_t gear_icon_hash(const uint32_t* px) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 32 * 32; ++i)
        for (int b = 0; b < 4; ++b) { h ^= (px[i] >> (8 * b)) & 0xFF; h *= 16777619u; }
    return h;
}

// ---- the canary's references ----------------------------------------------------------------------------
// Ten bundled icons (two per DAT the bundle covers) with the fingerprint of the art they had when bundled.
// COMPILED IN, not read from assets/gearicons/ : that folder is also the decode cache, which the repair below
// rewrites from the ROM -- a reference the decoder can overwrite is not a second witness. tests/t_geardat.cpp
// pins every one against the BMP in the repo.
struct GearRef { unsigned id; uint32_t hash; const char* name; };
inline const GearRef* gear_refs(int& n) {
    static const GearRef R[] = {
        { 608, 0x4482EE57u, "Fetich Arms" },   // 118/106
        { 609, 0x2D99C78Du, "Fetich Legs" },   // 118/106
        { 4309, 0xB92BE610u, "Cave Cherax" },   // 118/107
        { 4313, 0x1173462Du, "Blindfish" },   // 118/107
        { 10297, 0x3E856A40u, "Sortiarius Earring" },   // 118/109
        { 10299, 0x22FC93F8u, "Sabong Earring" },   // 118/109
        { 16480, 0xD9D35AB8u, "Thief's Knife" },   // 118/108
        { 16513, 0x93954BE5u, "Tuck" },   // 118/108
        { 23040, 0x65D18FB8u, "Pummeler's Mask +2" },   // 286/73
        { 23058, 0x75A2FBB5u, "Maxixi Tiara +2" },   // 286/73
    };
    n = (int)(sizeof(R) / sizeof(R[0]));
    return R;
}

// ---- the canary's verdict ---------------------------------------------------------------------------------
enum GearCanaryVerdict {
    GCV_PENDING = 0,    // still probing (bounded : a handful of frames at session start)
    GCV_OK,             // every DAT proved its layout and every reference decoded to its bundled art
    GCV_NO_ROM,         // no install found : nothing to check (doctor already says so)
    GCV_IO,             // a DAT could not be read (still retried)
    GCV_BAD_LAYOUT,     // a DAT opened but no record size proves it : that range decodes nothing
    GCV_REF_MISMATCH    // layouts fine, but references decode to other art : an item-icon pack, or a decode bug
};
// Worst finding wins, in the order a person should hear about them.
inline int gear_canary_verdict(bool noRom, int pending, int badLayout, int io, int refDiff) {
    if (noRom)         return GCV_NO_ROM;
    if (pending > 0)   return GCV_PENDING;
    if (badLayout > 0) return GCV_BAD_LAYOUT;
    if (refDiff > 0)   return GCV_REF_MISMATCH;
    if (io > 0)        return GCV_IO;
    return GCV_OK;
}

// ---- the cache decisions (src/ui/player.cpp) --------------------------------------------------------------
// Draw a cached BMP without asking the ROM ? Yes once the ROM has vouched for it this session ; and ALSO whenever
// the ROM itself is in doubt (canary not OK) -- then the file on disk is the better witness, exactly what a cache
// hit was before vouching existed. The one case that must NOT draw directly is the one that poisoned the tester's
// Quicksilver : a cached file, a trustworthy ROM, and no vouch yet.
inline bool gear_cache_direct(bool bmp, bool vouched, bool romTrusted) { return bmp && (vouched || !romTrusted); }
// After a successful decode : the cached file is stale when it is absent or differs from the ROM's pixels.
inline bool gear_cache_stale(bool bmp, bool sameAsRom) { return !bmp || !sameAsRom; }
// Rewrite the cache only from a trusted ROM : a decoder the canary doubts must not overwrite good icons on disk.
inline bool gear_cache_rewrite(bool stale, bool romTrusted) { return stale && romTrusted; }
// Vouch only for what is ON DISK now. A stale file whose rewrite failed (read-only folder) stays unvouched, or the
// next load would serve the poisoned BMP as a trusted hit.
inline bool gear_vouch_after(bool stale, bool wrote) { return !stale || wrote; }

} // namespace aio
