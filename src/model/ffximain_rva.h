// ffximain_rva.h -- the FFXiMain.dll static addresses, held as DATA instead of compile-time constants.
//
// WHY THIS FILE EXISTS. Everything AioHUD reads hangs off one of two anchors : LuaCore's data root `g`
// (a Windower DLL -- a GAME patch does not touch it) or a hard-coded RVA inside FFXiMain.dll. The RVAs
// are the fragile half : FFXiMain is RECOMPILED at every client patch, so adding a few globals ahead of
// ours slides every address after them. On 2026-08-12 that slide was 0x40 bytes and it killed the party
// selection cursor, the cost/Next box and PointWatch -- silently, while thirty LuaCore-fed features kept
// working perfectly, which is exactly why it read as a widget bug instead of a patch.
//
// A constant cannot recover from that ; a variable can. Each static here carries a SEED (the last known
// good address) and is re-derived at runtime when it stops being what it claims to be. Adoption is never
// on resemblance -- each static has a proof that nothing else in the image can satisfy :
//
//   FM_TARGET_T   a pointer whose Targets[0].ServerId is the id of the entity it points at, that entity
//                 being the one entity_array holds at its own index. Three facts confirming each other.
//   FM_PW_BLOCK   packet 0x061's body is copied VERBATIM into it (proven by disassembly, see
//   FM_PW_MERIT   docs/game-data/luacore-verified-offsets.md), and we already parse that packet. So the
//                 packet is a ground truth that survives every patch : search for the exact values that
//                 just arrived. This one needs NOTHING from the player.
//   FM_MENU_PTR   a pointer to an object whose def carries the inline "menu" tag, AND whose menu NAME
//                 changes as menus open and close. The decoys (an 'inline' / 'logwindo' slot, a stale
//                 'ability' cache) never go empty ; the focused slot does.
//   FM_EXAM_*     proposed from a shift already proven by another static, then CONFIRMED by decoding the
//                 value back to a real spell / ability name. A plausible integer is not evidence ; an id
//                 that turns into the name of what you are highlighting is.
//
// Findings are cached to disk against a CLIENT FINGERPRINT, so the healing happens once, not every login,
// and is discarded automatically the next time the client changes.
#pragma once

namespace aio {

enum FmStatic {
    FM_TARGET_T = 0,   // -> target_t (heap). T0 id @+0x04, T0 entity @+0x08, flags @+0x50, bt @+0x7C
    FM_MENU_PTR,       // -> the focused menu object. 0 = no menu open ; *(ptr+0x04) = the menu def
    FM_EXAM_SPELL,     // u32 : the Magic menu's highlighted spell id
    FM_EXAM_ABIL,      // u32 : the ability menu's highlighted action, id + 0x200 (JA) or raw (WS)
    FM_PW_BLOCK,       // mirror of packet 0x061 body+0x10 : EXP cur/req @+0, Master Level @+0x55, exemplar cur/req @+0x58
    FM_PW_MERIT,       // mirror of packet 0x063 order 2 : Limit Points u16 @+0, merit count @+2, max merits @+4
    FM_N
};

unsigned    fm_rva(FmStatic s);          // the RVA in use (the seed until something better is PROVEN)
unsigned    fm_addr(FmStatic s);         // ffximain_base() + fm_rva(s) ; 0 when the module isn't loaded
const char* fm_name(FmStatic s);         // human label, for logs and //aio doctor
bool        fm_healed(FmStatic s);       // re-derived this session, or restored from the cache
bool        fm_confirmed(FmStatic s);    // the address PROVED itself (vs merely proposed from a shift)

// Adopt a re-derived address : logs it, marks it, and persists the whole set. `how` names the proof
// ("packet 0x061", "structural signature", ...) -- an adoption with no stated proof is a bug waiting to
// be trusted. A no-op adoption (same rva) only upgrades the confirmed flag.
void fm_adopt(FmStatic s, unsigned rva, const char* how, bool confirmed = true);

// Ground truth from the packets, handed over as soon as one arrives. The check itself is DEFERRED by a
// few frames : our packet hook runs before the client has written its own statics, so testing immediately
// would compare against the values the patch is about to overwrite.
void fm_pw_expect(unsigned xpCur, unsigned xpTnl, unsigned ml, unsigned epCur, unsigned epTnml);
void fm_pw_merit_expect(unsigned lp, unsigned merits, unsigned maxMerits);

// Once per frame, from the poller. Runs whatever healing is currently due -- nothing when everything is
// healthy, which is the normal case and costs a handful of reads.
void fm_tick();

// One report line per static, for //aio doctor : address, state, and how it got there.
int  fm_report(char out[][160], int maxOut);

// SizeOfImage ^ TimeDateStamp of the loaded FFXiMain : changes when, and only when, the client is patched.
unsigned fm_fingerprint();

// TEST HOOK (//aio rva break) : break every address on purpose, in memory only, so the healers above can
// actually be WATCHED working instead of being trusted. Machinery that runs once a year is machinery that
// is never tested -- unless it can be triggered on demand.
void fm_poison(unsigned delta);

} // namespace aio
