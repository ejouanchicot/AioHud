// t_geardat.cpp -- the gear-icon ROM decode decisions (src/gfx/gear_dat.h).
//
// WHY THIS EXISTS. On 2026-09-10 a client patch grew every item record in the icon DATs from 0xC00 to 0x1400
// bytes. The decoder trusted 0xC00, read the wrong record for every id, and SUCCEEDED : blank icons, other items'
// art, garbage, drawn and cached to disk. Reported three days later as "Quicksilver ne s'affiche pas, tout le reste
// s'affiche" -- everything else was already cached. Nothing in the project could have caught it : this suite never
// looked at the decoder, and from the inside every wrong read was a success.
//
// WHAT IS REAL HERE. The file sizes are the 11 DATs measured on the patched install (2026-09-13) ; the pre-patch
// sizes are the ranges at 0xC00, as verified on 2026-07-19 ; the reference fingerprints are checked against the
// bundled BMPs in this repo. The DATs themselves are 150 MB of game data and are not in the repo, so the stride
// cases build a DAT in memory with the measured record structure (id at +0, 32x32 8-bpp header at +0x295).
// The decode was pinned pixel-for-pixel against all 1323 bundled icons out of the suite, on the real files.
#include "check.h"
#include "gfx/gear_dat.h"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace aio;

static unsigned char rotr3(unsigned char x) { return (unsigned char)((x >> 3) | (x << 5)); }   // the inverse of gear_rotl3

static void put32(unsigned char* r, long o, unsigned v) { for (int i = 0; i < 4; ++i) r[o + i] = rotr3((unsigned char)(v >> (8 * i))); }
static void put16(unsigned char* r, long o, unsigned v) { for (int i = 0; i < 2; ++i) r[o + i] = rotr3((unsigned char)(v >> (8 * i))); }

// A record head as the game writes it : id, then a BITMAPINFOHEADER at +0x295. Everything else zero (encoded 0 = 0).
static void make_record(unsigned char* r, unsigned id, unsigned bi = 40, unsigned w = 32, unsigned h = 32, unsigned planes = 1, unsigned bpp = 8) {
    put32(r, 0, id);
    put32(r, 0x295, bi); put32(r, 0x299, w); put32(r, 0x29D, h); put16(r, 0x2A1, planes); put16(r, 0x2A3, bpp);
}

// An in-memory DAT : `records` records of `stride` bytes for range `d`, plus `extraBytes` of tail.
struct MemDat { std::vector<unsigned char> b; };
static MemDat build_dat(const GearDat& d, long stride, long records, long headerAt = 0x295) {
    MemDat m; m.b.assign((size_t)(stride * records), 0);
    for (long i = 0; i < records; ++i) {
        unsigned char* r = &m.b[(size_t)(i * stride)];
        put32(r, 0, (unsigned)(gear_dat_first(d) + i));
        put32(r, headerAt, 40); put32(r, headerAt + 4, 32); put32(r, headerAt + 8, 32); put16(r, headerAt + 12, 1); put16(r, headerAt + 14, 8);
    }
    return m;
}
static bool mem_read(void* ctx, long off, unsigned char* buf, long n) {
    const MemDat* m = (const MemDat*)ctx;
    if (off < 0 || off + n > (long)m->b.size()) return false;
    memcpy(buf, &m->b[(size_t)off], (size_t)n);
    return true;
}

// Bundled BMP -> top-down pixels, the orientation the decode returns.
static bool read_bundled(unsigned id, uint32_t* px) {
    char path[512]; std::snprintf(path, sizeof(path), "%s", __FILE__);
    char* cut = strstr(path, "tests\\t_geardat.cpp"); if (!cut) cut = strstr(path, "tests/t_geardat.cpp");
    if (!cut) return false;
    std::snprintf(cut, sizeof(path) - (size_t)(cut - path), "assets/gearicons/%u.bmp", id);
    FILE* f = std::fopen(path, "rb"); if (!f) return false;
    unsigned char b[0x107A]; const size_t got = std::fread(b, 1, sizeof(b), f); std::fclose(f);
    if (got != sizeof(b) || b[0] != 'B' || b[1] != 'M') return false;
    const unsigned off = (unsigned)b[10] | ((unsigned)b[11] << 8);
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
        const unsigned char* p = b + off + ((31 - y) * 32 + x) * 4;
        px[y * 32 + x] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    return true;
}

void test_gear_dat() {
    SECTION("geardat : the byte rotation round-trips");
    { int bad = 0; for (int x = 0; x < 256; ++x) if (gear_rotl3(rotr3((unsigned char)x)) != x) ++bad; CHECK_EQ(bad, 0); }

    SECTION("geardat : a record proves itself by its id AND its icon header");
    {
        unsigned char r[GEAR_HEAD];
        memset(r, 0, sizeof(r)); make_record(r, 18720);                   CHECK(gear_record_ok(r, 18720));   // Quicksilver
        CHECK(!gear_record_ok(r, 18721));                                  // the neighbour's record : the stride is wrong
        memset(r, 0, sizeof(r)); make_record(r, 18720, 40, 32, 32, 1, 32); CHECK(!gear_record_ok(r, 18720));  // 32 bpp : the palette decode would be garbage
        memset(r, 0, sizeof(r)); make_record(r, 18720, 40, 64, 64, 1, 8);  CHECK(!gear_record_ok(r, 18720));  // not 32x32
        memset(r, 0, sizeof(r)); put32(r, 0, 18720);                        CHECK(!gear_record_ok(r, 18720));  // right id, no icon where we read it
        CHECK(!gear_record_ok(0, 18720));
    }

    SECTION("geardat : the table matches the DAT sizes measured after the 2026-09-10 patch");
    {
        // ROM file sizes on the patched install, in table order (measured 2026-09-13).
        static const long POST[] = { 20971520, 20971520, 2621440, 7864320, 31457280, 34078720, 28835840, 5242880, 5242880, 2621440, 81920 };
        int n = 0; const GearDat* t = gear_dat_table(n);
        CHECK_EQ(n, 11);
        for (int i = 0; i < n && i < 11; ++i) {
            long c[3]; const int nc = gear_stride_candidates(POST[i], gear_dat_records(t[i]), c);
            if (t[i].lo == t[i].hi) { CHECK(nc >= 2); CHECK_EQ(c[0], POST[i]); }     // gil : one id, the file holds more -> the size cannot tell
            else                    { CHECK(nc >= 1); CHECK_EQ(c[0], 0x1400); }       // every other range : the size says 0x1400
            // pre-patch : the same range at 0xC00 must still be recognised (an old client, a backup DAT)
            long p[3]; const int np = gear_stride_candidates(gear_dat_records(t[i]) * 0xC00, gear_dat_records(t[i]), p);
            bool has = false; for (int k = 0; k < np; ++k) has |= (p[k] == 0xC00);
            CHECK(has);
        }
        CHECK(gear_dat_for(18720) && strcmp(gear_dat_for(18720)->dat, "118/108") == 0);   // Quicksilver is a weapon
        CHECK(gear_dat_for(0) == 0);
        CHECK(gear_dat_for(0x8000) == 0);
    }

    SECTION("geardat : the stride is PROVEN by the record, whatever the layout");
    {
        GearDat w = { 0x4000, 0x400F, "118/108", 0 };                       // a 16-id weapon range, same shape as the real one
        MemDat post = build_dat(w, 0x1400, 16);
        CHECK_EQ(gear_find_stride((long)post.b.size(), w, 0x4005, mem_read, &post), 0x1400);   // THE BUG : 0xC00 read index 5 at 0x3C00, inside record 3
        CHECK_EQ(gear_find_stride((long)post.b.size(), w, 0x400F, mem_read, &post), 0x1400);   // the last record, whose icon ends at EOF
        MemDat pre = build_dat(w, 0xC00, 16);
        CHECK_EQ(gear_find_stride((long)pre.b.size(), w, 0x4005, mem_read, &pre), 0xC00);      // the old layout still decodes
        MemDat future = build_dat(w, 0x1800, 16);
        CHECK_EQ(gear_find_stride((long)future.b.size(), w, 0x4005, mem_read, &future), 0x1800);   // the NEXT resize, found from the size alone
        MemDat padded = build_dat(w, 0x1400, 16); padded.b.resize(padded.b.size() + 0x200, 0);   // a tail that breaks the division
        CHECK_EQ(gear_find_stride((long)padded.b.size(), w, 0x4005, mem_read, &padded), 0x1400);
        MemDat moved = build_dat(w, 0x1400, 16, 0x2A5);                     // id right, icon header 16 bytes further : the icon moved
        CHECK_EQ(gear_find_stride((long)moved.b.size(), w, 0x4005, mem_read, &moved), 0);
        MemDat junk; junk.b.assign(16 * 0x1400, 0x5A);
        CHECK_EQ(gear_find_stride((long)junk.b.size(), w, 0x4005, mem_read, &junk), 0);       // refuse, never guess
        CHECK_EQ(gear_find_stride((long)post.b.size(), w, 0x3FFF, mem_read, &post), 0);       // an id before the range
        CHECK_EQ(gear_find_stride((long)post.b.size(), w, 0x4005, 0, &post), 0);

        GearDat gil = { 0xFFFF, 0xFFFF, "174/48", 0 };                       // one id, a file of 16 records (measured)
        MemDat g = build_dat(gil, 0x1400, 16);
        CHECK(gear_find_stride((long)g.b.size(), gil, 0xFFFF, mem_read, &g) != 0);
    }

    SECTION("geardat : the icon decode -- palette rotation, doubled alpha, clamp, vertical flip");
    {
        unsigned char data[GEAR_ICON]; memset(data, 0, sizeof(data));
        for (int g = 0; g < 256; ++g) {
            data[g * 4 + 0] = rotr3((unsigned char)g);           // B
            data[g * 4 + 1] = rotr3((unsigned char)(255 - g));   // G
            data[g * 4 + 2] = rotr3((unsigned char)(g ^ 0x5A));  // R
            data[g * 4 + 3] = rotr3((unsigned char)(g / 2));     // A, stored halved
        }
        data[7 * 4 + 3] = rotr3(200);                            // entry 7 : 200 doubled clamps to 255
        for (int r = 0; r < 32; ++r) for (int x = 0; x < 32; ++x)
            data[0x400 + r * 32 + x] = rotr3((unsigned char)((r * 7 + x) & 0xFF));   // DAT row r, bottom-up
        uint32_t px[1024]; gear_decode_icon(data, px);
        int bad = 0;
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
            const int g = ((31 - y) * 7 + x) & 0xFF;             // top-down row y is DAT row 31-y
            const unsigned a = (g == 7) ? 255u : (unsigned)((g / 2) * 2);
            const uint32_t want = (a << 24) | ((uint32_t)(g ^ 0x5A) << 16) | ((uint32_t)(255 - g) << 8) | (uint32_t)g;
            if (px[y * 32 + x] != want) ++bad;
        }
        CHECK_EQ(bad, 0);
    }

    SECTION("geardat : the canary's references are the art actually bundled");
    {
        int n = 0; const GearRef* r = gear_refs(n);
        CHECK(n >= 10);
        int datsCovered = 0; const GearDat* last = 0;
        for (int i = 0; i < n; ++i) {
            uint32_t px[1024];
            const bool have = read_bundled(r[i].id, px);
            CHECK(have);
            if (!have) continue;
            CHECK_EQ(gear_icon_hash(px), r[i].hash);
            int opaque = 0; for (int k = 0; k < 1024; ++k) opaque += (px[k] >> 24) != 0;
            CHECK(opaque > 100);                                 // a blank reference would agree with a blank decode
            const GearDat* d = gear_dat_for(r[i].id);
            CHECK(d != 0);
            if (d != last) { ++datsCovered; last = d; }
            for (int j = 0; j < i; ++j) CHECK(r[j].hash != r[i].hash);   // two identical refs prove less than they look
        }
        CHECK(datsCovered >= 5);
    }

    SECTION("geardat : the cache decisions -- the tester's poisoned Quicksilver");
    {
        // A cached BMP, a ROM the canary trusts, not vouched yet : must NOT be drawn as-is. This is exactly the
        // state of the tester's 18720.bmp after the patch -- a cache hit drew the blank icon all session.
        CHECK(!gear_cache_direct(true, false, true));
        CHECK(gear_cache_direct(true, true, true));              // vouched this session
        CHECK(gear_cache_direct(true, false, false));            // the canary doubts the ROM : the file is the better witness
        CHECK(!gear_cache_direct(false, false, false));          // nothing on disk
        CHECK(!gear_cache_stale(true, true));
        CHECK(gear_cache_stale(true, false));                    // poisoned
        CHECK(gear_cache_stale(false, false));                   // absent
        CHECK(gear_cache_rewrite(true, true));
        CHECK(!gear_cache_rewrite(true, false));                 // a doubted decoder must not overwrite good icons on disk
        CHECK(!gear_cache_rewrite(false, true));
        CHECK(gear_vouch_after(false, false));                   // file already matched the ROM
        CHECK(gear_vouch_after(true, true));                     // repaired on disk
        CHECK(!gear_vouch_after(true, false));                   // READ-ONLY folder : the poison is still on disk -> never vouch it
    }

    SECTION("geardat : the canary's verdict, worst first");
    CHECK_EQ(gear_canary_verdict(false, 0, 0, 0, 0), GCV_OK);
    CHECK_EQ(gear_canary_verdict(true, 3, 1, 1, 1), GCV_NO_ROM);
    CHECK_EQ(gear_canary_verdict(false, 1, 1, 1, 1), GCV_PENDING);
    CHECK_EQ(gear_canary_verdict(false, 0, 1, 1, 1), GCV_BAD_LAYOUT);
    CHECK_EQ(gear_canary_verdict(false, 0, 0, 1, 1), GCV_REF_MISMATCH);
    CHECK_EQ(gear_canary_verdict(false, 0, 0, 1, 0), GCV_IO);
}
