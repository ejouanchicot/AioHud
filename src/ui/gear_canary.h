// gear_canary.h -- the gear-icon canary : at session start, prove that the game's item-icon DATs still decode to
// the art we know, and report to the safety harness when they do not.
//
// WHY IT EXISTS. On 2026-09-10 a client patch changed the item DAT layout and the decoder read the wrong record
// for every id -- for three days, on every machine, and nothing said a word. The icons that were already cached
// kept working, so the only symptom was one tester's one gun showing an empty cell. A canary would have said it
// the first frame after login : "the weapon DAT no longer decodes to the art it had".
//
// ITS SECOND WITNESS. Ten bundled icons whose art is fingerprinted and COMPILED IN (gfx/gear_dat.h, gear_refs),
// decoded through the production decoder at session start. Plus a structural probe of all 11 DATs (the first id of
// each range must prove its record). The decisions are in gear_dat.h and tested ; this file only schedules the
// probes (one per frame, never on a check callback -- those must not do I/O) and phrases the findings.
//
// IT ALSO GATES THE CACHE. src/ui/player.cpp rewrites cached icons from the ROM only while the verdict is OK : a
// decoder the canary doubts must not overwrite good icons on disk -- that is how the 2026-09-10 bug poisoned them.
#pragma once

namespace aio {

void gear_canary_tick(unsigned nowMs);   // every frame ; one probe at most, nothing once settled
int  gear_canary_verdict_now();          // GearCanaryVerdict (gfx/gear_dat.h)
const char* gear_canary_verdict_name(int v);
void gear_canary_summary(char* out, int cap);   // one line for //aio doctor and //aio geartrace
void gear_register_checks();             // selftest_add("gearicons")

} // namespace aio
