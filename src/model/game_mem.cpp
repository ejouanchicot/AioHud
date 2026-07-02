// game_mem.cpp -- see game_mem.h.
#include "model/game_mem.h"
#include "model/gamestate.h"
#include "windower_debug.h"
#include <windows.h>

namespace aio {

using windower::safe_read;

// --- game-memory anchors : ONE source of truth for the pointer chains every reader hangs off.
// Module bases are fixed after load (cached) ; the data root `g` is a heap ptr that can be 0 while
// zoning (read each call). All return 0 -> the caller no-ops. Offsets live HERE, nowhere else. ---
u32 ffximain_base() { static u32 b = 0; if (!b) b = (u32)GetModuleHandleA("FFXiMain.dll"); return b; }
u32 luacore_base()  { static u32 b = 0; if (!b) b = (u32)GetModuleHandleA("LuaCore.dll");  return b; }
u32 data_root()   { u32 lc = luacore_base(); u32 g = 0; return (lc && safe_read(lc + 0x1C8400, &g) && valid_ptr(g)) ? g : 0; }   // g = *(LuaCore+0x1C8400)
u32 party_ptr()   { u32 g = data_root(); u32 pp = 0; return (g && safe_read(g + 0x248, &pp) && valid_ptr(pp)) ? pp : 0; }         // *(g+0x248) = &member[0]+4
u32 entity_array(){ u32 g = data_root(); u32 e  = 0; return (g && safe_read(g + 0x24,  &e)  && valid_ptr(e))  ? e  : 0; }         // *(g+0x24) = entity array

// player = *(g + 0x3C).
static u32 player_struct() {
    u32 g = data_root();
    u32 pl = 0;
    return (g && safe_read(g + 0x3C, &pl) && valid_ptr(pl)) ? pl : 0;
}

bool read_player_vitals(float& hpFrac, float& mpFrac, float& tpFrac) {
    u32 pl = player_struct();
    if (!pl) return false;
    u32 mhp = 0, hpp = 0, mpp = 0, tp = 0;
    safe_read(pl + 0x60, &mhp);                     // max_hp -> validity gate
    if (mhp == 0 || mhp > 0x100000) return false;   // struct not ready yet
    // hpp/mpp = the game's own % (0..100), so the bar always reads "full = full",
    // adapting to the CURRENT max (gear/food included).
    safe_read(pl + 0x64, &hpp);  safe_read(pl + 0x70, &mpp);  safe_read(pl + 0x74, &tp);
    hpp &= 0xFF;  mpp &= 0xFF;  if (hpp > 100) hpp = 100;  if (mpp > 100) mpp = 100;
    hpFrac = hpp / 100.0f;
    mpFrac = mpp / 100.0f;
    tpFrac = (float)tp / 3000.0f;  if (tpFrac > 1) tpFrac = 1; if (tpFrac < 0) tpFrac = 0;
    return true;
}

bool read_player(PlayerInfo& o) {
    u32 pl = player_struct();
    if (!pl) return false;
    u32 mhp = 0; safe_read(pl + 0x60, &mhp);
    if (mhp == 0 || mhp > 0x100000) return false;
    u32 hp = 0, mp = 0, hpp = 0, mpp = 0, tp = 0, mj = 0, sj = 0;
    safe_read(pl + 0x5C, &hp);  safe_read(pl + 0x64, &hpp);
    safe_read(pl + 0x68, &mp);  safe_read(pl + 0x70, &mpp);
    safe_read(pl + 0x74, &tp);  safe_read(pl + 0x94, &mj);  safe_read(pl + 0x9C, &sj);   // job ids: u32 fields (main@+0x94, lvl@+0x98, sub@+0x9C)
    o.hp = (int)hp;  o.mp = (int)mp;  o.tp = (int)tp;
    o.hpp = hpp & 0xFF;  o.mpp = mpp & 0xFF;
    if (o.hpp > 100) o.hpp = 100;  if (o.mpp > 100) o.mpp = 100;  if (o.tp > 3000) o.tp = 3000;
    o.mjob = mj & 0xFF;  o.sjob = sj & 0xFF;
    u32 id = 0; safe_read(pl + 0x00, &id); o.id = id;   // server id @+0x00
    int i = 0;                                          // name : ASCII at player+0x08
    for (; i < 19; ++i) { u32 c = 0; if (!safe_read(pl + 0x08 + i, &c)) break; char ch = (char)(c & 0xFF); if (!ch) break; o.name[i] = ch; }
    o.name[i] = 0;
    return true;
}

// Player buffs, reversed from LuaCore get_player (FUN_10072040). The same player struct
// pl = *(g + 0x3C) holds a 32-entry uint16 status-icon array at pl+0x1C (loop runs the ushorts
// in [pl+0x1C, pl+0x5C) ); 0xFF marks an EMPTY slot. We compact the non-empty ids in slot order.
int read_player_buffs(unsigned short* out, int maxN) {
    if (!out || maxN <= 0) return 0;
    u32 pl = player_struct();
    if (!pl) return 0;
    u32 mhp = 0; safe_read(pl + 0x60, &mhp);
    if (mhp == 0 || mhp > 0x100000) return 0;           // struct not ready yet
    int n = 0;
    for (int i = 0; i < 32 && n < maxN; ++i) {
        u32 v = 0; if (!safe_read(pl + 0x1C + i * 2, &v)) break;
        v &= 0xFFFF;
        if (v == 0xFF) continue;                        // empty slot (game's sentinel)
        out[n++] = (unsigned short)v;
    }
    return n;
}

// allianceinfo_t : g=*(LuaCore+0x1C8400) ; pp=*(g+0x248) (=&member[0]+4) ; allianceinfo = *(pp).
// Fields (verified in-game 2026-06-26): +0x00 alliance leader id, +0x04/+0x08/+0x0C party
// 1/2/3 leader ids. A member is that role iff its serverid matches.
bool read_party_leaders(PartyLeaders& o) {
    o.alliance = o.p1 = o.p2 = o.p3 = 0;
    u32 pp = party_ptr();
    if (!pp) return false;
    u32 ai = 0;
    if (!safe_read(pp, &ai) || !valid_ptr(ai)) return false;            // allianceinfo_t = *(g+0x248)
    safe_read(ai + 0x00, &o.alliance);
    safe_read(ai + 0x04, &o.p1);
    safe_read(ai + 0x08, &o.p2);
    safe_read(ai + 0x0C, &o.p3);
    return true;
}

// Target + sub-target. Reversed in-game (2026-06-27, retail) matching Ashita's target_t
// layout (see plugins/sdk/ffxi/target.h). A STATIC pointer in FFXiMain.dll points at a heap
// target_t (ASLR-shifted base resolved at runtime). Located via the //aio tgt2 probe.
//   *(FFXiMain.dll + 0x57876C)            -> target_t base (heap)
//   target_t + 0x04   u32  Targets[0].ServerId   = the ACTIVE reticle (sub when <st> open, else main)
//   target_t + 0x2C   u32  Targets[1].ServerId   = the LOCKED main (while a <st> cursor is open)
//   target_t + 0x50   u32  flags ; bit 0x00010000 = sub-target CURSOR open (clears on BOTH
//                          confirm and cancel ; NB: the byte at +0x78 is sticky -> do NOT use it)
// id 0x04000000 is the "nothing" sentinel. (Old flat cache lived at FFXiMain+0x487F60.)
static const u32 TARGET_T_PTR_RVA = 0x57876C;
static const u32 T0_ID_OFF = 0x04, T1_ID_OFF = 0x2C, FLAGS_OFF = 0x50, LOCK_OFF = 0x5C;
static const u32 SUB_CURSOR_BIT = 0x00010000;
static const u32 NO_TARGET = 0x04000000;
// LOCK_OFF (u32) : 1 while the main target is LOCK-ON'd, 0 otherwise. Reversed 2026-07-02 via //aio tlock
// (dump target_t targeting-vs-locked -> +0x5C is the only byte that flips ; the locked id is T0 @+0x04).

bool read_target(TargetInfo& o) {
    o.id = o.sid = 0; o.locked = false;
    u32 ffm = ffximain_base();
    if (!ffm) return false;
    u32 tp = 0; safe_read(ffm + TARGET_T_PTR_RVA, &tp);
    if (!valid_ptr(tp)) return true;                    // target system not ready
    u32 t0 = 0, t1 = 0, flags = 0, lk = 0;
    safe_read(tp + T0_ID_OFF, &t0);                     // active reticle
    safe_read(tp + T1_ID_OFF, &t1);                     // locked main (valid during sub-target)
    safe_read(tp + FLAGS_OFF, &flags);
    safe_read(tp + LOCK_OFF,  &lk);                     // lock-on flag (1 = locked)
    if (flags & SUB_CURSOR_BIT) { o.sid = t0; o.id = t1; }   // <st> cursor open : sub = reticle, main = locked
    else                        { o.id = t0; }               // normal : main = reticle, no sub
    if (o.id  == NO_TARGET) o.id  = 0;
    if (o.sid == NO_TARGET) o.sid = 0;
    o.locked = (lk != 0) && (o.id != 0);                // locked ON the main target
    return true;
}

// Action menu (reversed 2026-06-27). Statics in FFXiMain :
//   +0x5EED6C  u32  live-menu pointer : 0 closed, heap ptr while any menu is open ; *(ptr+0x04) = menu DEF
//   +0x634F28  u32  "examined SPELL" id   (the game writes it in the Magic menu to show MP Cost / recast)
//   +0x634590  u32  "examined ABILITY" id + 0x200 (Job-Ability / Weapon-Skill menus ; -0x200 = raw id)
//
// ZERO-TAP menu type (reversed 2026-06-27 via //aio menu) : the def carries the menu's INTERNAL NAME
// inline -- two 8-byte fields at def+0x46 : a constant "menu    " tag then the menu name at def+0x4E.
//   "magic   " -> spell list (examine cache 0x634F28 holds the highlighted spell id)
//   "ability " -> the JOB-ABILITY *and* WEAPON-SKILL lists -- SAME menu name. The examine cache
//                 0x634590 disambiguates by FFXI's unified id space : Job Abilities are id + 0x200
//                 (araw >= 0x200), Weapon Skills are the raw id in the low range (araw < 0x200).
//   "abiselec" -> the Abilities CATEGORY selector (no item examined -> cache is 0xFFFFFFFF) -> ignored
// This replaces the old "learn which def changed the cache" trick : no cursor tap needed, and it is
// stable across sessions (it's read from the def, not the per-session heap pointer value).
static const u32 MENU_PTR_RVA = 0x5EED6C, EXAM_SPELL_RVA = 0x634F28, EXAM_ABIL_RVA = 0x634590;
static const u32 MENU_NAME_OFF = 0x4E, MENU_TAG_OFF = 0x46;   // def+0x46 = "menu    ", def+0x4E = name

// read `n` (<=8) bytes of inline ASCII at addr into out (NUL-terminated).
static void read_tag(u32 addr, char* out, int n) {
    int i = 0; for (; i < n; ++i) { u32 c = 0; if (!safe_read(addr + i, &c)) break; out[i] = (char)(c & 0xFF); }
    out[i] = 0;
}

bool read_action_menu(int& type, unsigned& id, unsigned& cursor, bool& examValid) {
    type = 0; id = 0; cursor = 0; examValid = false;
    u32 ffm = ffximain_base();
    if (!ffm) return false;
    u32 mptr = 0; safe_read(ffm + MENU_PTR_RVA, &mptr);
    if (!valid_ptr(mptr)) return false;                   // no menu open
    safe_read(mptr + 0x4C, &cursor);                      // 1-based highlight index -> stale-examine detection
    u32 def = 0; safe_read(mptr + 0x04, &def);            // menu category definition
    if (!valid_ptr(def)) return false;
    // The menu's shared examine-DESCRIPTION object (*(mptr+0x0C), a singleton) is the structural "is there a
    // real examinable item here" signal. Reversed via //aio menu : on a no-magic job's EMPTY magic list it
    // reads len@+0x30 = 0 and sentinel@+0x34 = 0xFFFFFFFF ; on a real spell/trust it holds len > 0 and +0x34 = 0
    // (plus the description text). Unlike the static examine cache (0x634F28) it is NOT left stale, so it tells a
    // real selection from a ghost on OPEN and on RE-OPEN of the same item (where the frozen-value test fails).
    { u32 desc = 0; safe_read(mptr + 0x0C, &desc);
      if (valid_ptr(desc)) { u32 dlen = 0, dsent = 0xFFFFFFFF; safe_read(desc + 0x30, &dlen); safe_read(desc + 0x34, &dsent);
                             examValid = (dlen != 0) && (dsent != 0xFFFFFFFF); } }
    char tag[9]; read_tag(def + MENU_TAG_OFF, tag, 4);    // self-validate : real menu defs start "menu"
    if (tag[0] != 'm' || tag[1] != 'e' || tag[2] != 'n' || tag[3] != 'u') return false;
    char nm[9]; read_tag(def + MENU_NAME_OFF, nm, 8);     // 8-byte menu name -> menu type

    if (nm[0]=='m' && nm[1]=='a' && nm[2]=='g' && nm[3]=='i' && nm[4]=='c') {        // "magic   " (real spells AND trusts)
        u32 spell = 0; safe_read(ffm + EXAM_SPELL_RVA, &spell);
        type = 1; id = (spell == 0 || spell > 0x4000) ? 0 : spell; return true;     // menu IS open (frame shows) ;
        // id 0 = nothing valid examined yet. The caller filters the GHOST via the live-examine check + the
        // box draws an EMPTY frame whenever the magic menu is open (id may be a trust / stale until proven live).
    }
    if (nm[0]=='a' && nm[1]=='b' && nm[2]=='i' && nm[3]=='l') {                      // "ability " (JA + WS list)
        u32 araw = 0; safe_read(ffm + EXAM_ABIL_RVA, &araw);                         // NB "abiselec" (nm[3]='s') is excluded
        if (araw >= 0x200 && araw <= 0x200 + 0x4000) { type = 2; id = araw - 0x200; return true; }  // Job Ability
        if (araw >= 1 && araw < 0x200)               { type = 3; id = araw;         return true; }  // Weapon Skill
        return false;                                                               // 0 / 0xFFFFFFFF : not populated
    }
    return false;                                         // any other menu : no box
}

// Ability/Job-Ability RECAST, reversed from LuaCore's get_ability_recasts (FUN_1006FF00). The client
// keeps two parallel 32-slot arrays hung off the data root g = *(LuaCore+0x1C8400) :
//   *(g + 0x22C) -> int32[32]  remaining recast in 1/60 s (frames)   ; seconds = timer / 60
//   *(g + 0x230) -> stride-8 entries, byte[0] = the slot's recast_id
// Windower builds recasts[id] = timer/60. We do the inverse : given the highlighted JA's recast_id,
// scan the 32 slots for an ACTIVE one (timer>0) whose id matches -> its remaining seconds (0 = ready).
// recast_id comes from abilities_gen.h (caller side). This is the menu's exact "Next".
unsigned ability_recast_sec(unsigned recast_id) {
    u32 g = data_root(); if (!g) return 0;
    u32 idsP = 0, timersP = 0;
    safe_read(g + 0x230, &idsP); safe_read(g + 0x22C, &timersP);
    if (!valid_ptr(idsP) || !valid_ptr(timersP)) return 0;
    for (int s = 0; s < 32; ++s) {
        u32 t = 0; safe_read(timersP + s * 4, &t);
        if ((int)t <= 0 || t > 60u * 7200u) continue;          // empty/ready slot, or garbage (>2h)
        u32 idb = 0; safe_read(idsP + s * 8, &idb);
        if ((idb & 0xFF) == recast_id) return (t + 59) / 60;   // ceil to whole seconds (the "Next")
    }
    return 0;                                                  // not on recast
}

// Spell RECAST ("Next" for the Magic menu), reversed from LuaCore's get_spell_recasts (FUN_1006FE80 --
// the cclosure pushed just before the "get_spell_recasts" setfield ; the memory's old FUN_100732B0 guess
// was wrong, that one is get_abilities). Far simpler than abilities : NO 32-slot scan -- a flat array.
//   base = *(g + 0x234) -> ushort[1024], indexed directly by recast_id, remaining recast in 1/60 s.
//   Windower builds recasts[id] = base[id] ; seconds = base[id] / 60. (0x234 sits right after the
//   ability timers/ids at 0x22C/0x230.) recast_id comes from spells_gen.h (SpellRow::recast_id).
unsigned spell_recast_sec(unsigned recast_id) {
    if (recast_id >= 0x400) return 0;                          // array is exactly 1024 entries
    u32 g = data_root(); if (!g) return 0;
    u32 base = 0; if (!safe_read(g + 0x234, &base) || !valid_ptr(base)) return 0;
    u32 v = 0; if (!safe_read(base + recast_id * 2, &v)) return 0;   // 32-bit read, keep the low ushort
    v &= 0xFFFF;                                               // little-endian : this entry, ignore the next
    if (v == 0 || v > 60u * 7200u) return 0;                   // ready, or garbage (>2h)
    return (v + 59) / 60;                                     // ceil to whole seconds (the "Next")
}

// Per-frame snapshot : read each pointer-chain ONCE so widgets never touch memory in draw().
// See gamestate.h. read_player gates "in game" (vitals populate before names after a zone).
void poll_game_state(GameState& gs) {
    PlayerInfo me;
    if (!read_player(me)) { gs.inGame = false; return; }     // not ready (zoning) -> keep last-good
    gs.inGame = true;
    gs.me = me;
    gs.hp = me.hpp / 100.0f;
    gs.mp = me.mpp / 100.0f;
    gs.tp = me.tp / 3000.0f; if (gs.tp > 1.0f) gs.tp = 1.0f; if (gs.tp < 0.0f) gs.tp = 0.0f;

    TargetInfo tg;
    if (read_target(tg)) { gs.targetId = tg.id; gs.subTargetId = tg.sid; gs.targetLocked = tg.locked; }
    else                 { gs.targetId = gs.subTargetId = 0; gs.targetLocked = false; }

    PartyLeaders ld;
    if (read_party_leaders(ld)) { gs.allianceLeader = ld.alliance; gs.partyLead1 = ld.p1; gs.partyLead2 = ld.p2; gs.partyLead3 = ld.p3; }
    else                        { gs.allianceLeader = gs.partyLead1 = gs.partyLead2 = gs.partyLead3 = 0; }

    int mt = 0; unsigned ma = 0, mc = 0; bool mev = false;
    if (read_action_menu(mt, ma, mc, mev)) { gs.menuType = mt; gs.menuAction = ma; gs.menuCursor = mc; gs.menuExamValid = mev; }
    else                                   { gs.menuType = 0; gs.menuAction = 0; gs.menuCursor = mc; gs.menuExamValid = false; }
    // RAW ability examine, read EVERY frame -> a change = the game examined a real ability, used to tell a
    // live Job-Ability/WS selection from a stale one (the Magic box uses menuExamValid instead ; see party.cpp).
    { u32 ffm2 = ffximain_base(); u32 ea = 0;
      if (ffm2) safe_read(ffm2 + EXAM_ABIL_RVA, &ea);
      gs.examAbilRaw = ea; }

    // party-window picker : the focused menu is "partywin" with a 1-based cursor index at +0x4C.
    // (Reversed via //aio pcur: +0x4C tracks the hovered member, +0x08 = its row object.)
    gs.partyMenuSel = 0;
    u32 ffm = ffximain_base();
    if (ffm) {
        u32 mptr = 0; safe_read(ffm + MENU_PTR_RVA, &mptr);
        u32 def = 0; if (valid_ptr(mptr)) safe_read(mptr + 0x04, &def);
        if (valid_ptr(def)) {
            char nm[6] = {0}; for (int i = 0; i < 5; ++i) { u32 c = 0; safe_read(def + MENU_NAME_OFF + i, &c); nm[i] = (char)(c & 0xFF); }
            if (nm[0]=='p' && nm[1]=='a' && nm[2]=='r' && nm[3]=='t' && nm[4]=='y') {     // "partywin"
                u32 idx = 0; safe_read(mptr + 0x4C, &idx);
                if (idx >= 1 && idx <= 6) gs.partyMenuSel = (int)idx;
            }
        }
    }
}

} // namespace aio
