# Game data: player memory & party packets

Until host `ffxi[7]` is walked, AioHUD sources live data two ways: the **local player** from memory (always present + accurate) and the **other party members** from inbound packets (the game never sends you your *own* party packet). Code: `model/game_mem.cpp` (memory), `model/party_state.cpp` (packets).

- [Local player struct](player-struct.md) — pointer chain + field offsets for vitals, name, jobs, and buffs read from memory.
- [Player gil](player-gil.md) — `gil = *(*(g+0x50)+4)` (u32), reversed from LuaCore's `get_items('gil')` binding.
- [Player equipment](player-equipment.md) — the 16 equipped item ids via `{index=*(g+0x54), bag=*(g+0x58)}` -> `items_root + bag*0xCA8 + index*0x28`, reversed from `get_items('equipment')`.
- [Key items](key-items.md) — `has(id) = ((u8*)*(g+0x4C))[id] != 0`, a flat `u8[8192]` (one BYTE per id, **not** a bitfield) sitting right before `items_root`; reversed from `get_key_items()` and validated on 869 owned ids.
- [Inventory](inventory.md) — `count_item(id)` across all **18 bags** at `*(g+0x50)` (`bag*0xCA8 + slot*0x28`, slots 1..80, entry 0 reserved) + the `max`/`count`/`enabled` `u8[18]` arrays at `+0x19500`/`+0x19520`/`+0x19540`; reversed from `get_items()`, validated on 707 owned ids and 18/18 bag counts.
- [Encumbrance flags](encumbrance-flags.md) — the `0x01B` job-info packet's u32 bitfield (@0x60): bit `sid` = equip slot `sid` locked, indexed straight by the equipment viewer.
- [Gear icons](gear-icons.md) — 32x32 item icons: AioHUD bundles pre-extracted BMPs; the original EquipViewer decodes them live from the game's ROM DATs (still current, id-indexed).
- [Party member packets](party-packets.md) — inbound `0x0DD`/`0x0DF` layouts for other members, plus the alliance-struct alt.
- [Two traps that cost real time](traps.md) — the dangling self-name pointer and the job-ID-vs-LEVEL offset, with the `//aio` debug recipe.
- [The party member ARRAY in memory](party-array.md) — the Ashita `partymember_t` table (18 slots) for an instant full party at load, plus `allianceinfo_t` leadership.
- [Core offsets verified against LuaCore](luacore-verified-offsets.md) — the 2026-07-19 Ghidra audit: party stride/fields, the member→entity hop, the 0x900 bound, the entity struct, recast constants and the PointWatch RVAs, each checked against the Windower binding that implements it (two entity fields came back contradicted).
- [Target & SUB-target struct](target-substruct.md) — the heap `target_t` (main vs sub reticle) driving the target bars, the `+0x5C` lock-on flag, plus the party-window picker cursor.
- [Party cast bar — the 0x028 action packet](cast-bar.md) — bit-packed action parse for casting bars, with the don't-clear-on-cat-4 gotcha.
- [0x028 action packet — bit layout from the client parser](action-packet.md) — the ground-truth bit layout reversed from the client's own Ghidra-decompiled parser: the 150-bit header, per-target/per-effect strides, add-effect/spike blocks, and why the fixed `150+i*123` step is only safe for `target[0]`.
- [Target debuffs](target-debuffs.md) — tracking debuffs ON a mob from the 0x028/0x029: detect by SPELL id, "no effect" (msg 75) gating, the 32-slot table, sleep/crowd-control inference from the mob's own actions (wake msg 204/param 2, hit/act/DoT wakes), wear-off + duration learning, and the display caps.
- [Action-menu info box](action-menu.md) — zero-tap Magic / Job Ability / Weapon Skill readout with recast tables and the ghost-menu fix.
- [Party-member buffs — the 0x076 packet](member-buffs.md) — self buffs from memory, other members from the packed `0x076` packet, and the buff atlas.
- [Map system (minimap)](map-system.md) — the live zone id (`*(u16*)(*(g+0x40)+2)`), the client world→map-pixel transform + per-submap scale/offset table, sub-map/floor selection, where the zone map image lives (ROM DATs), the entity **heading** offset (`entity+0x18`, radians), and the **Minimap config options** (shape / frame / background / size / zoom / marker size / per-type toggles).
- [PointWatch — XP/CP/ML + Merits](pointwatch.md) — the 0x061/0x063/0x029/0x02D packets + the FFXiMain static-RVA load-time seed (client-version-specific, `//aio pwscan`), and the X/h rate ring.
- [SCH grimoire](grimoire-sch.md) — Arts/Addendum buff ids, stratagem recast id 231, and the level/JP→(interval,maxCharges) charge table.
- [Zone Tracker](zone-tracker.md) — Dynamis granule KIs (0x055) + timer, Abyssea lights/visitant (0x02A), the slot-9 rendered-text Omen objective parser, the generic **0x075 battlefield timer/bars** packet and its `FFXiMain+0x480800` mirror, + the differential-scanner post-mortem.
- [Limbus tracker](limbus.md) — mode 6 in full: the per-wing 0x075 bar labels (`SW_Floor_#3` vs `North_Tower_F1`), the 0x02A run economy (the award id keys on the **wing**, the source is the target **entity index**), the N4/W4/E4/C3 towers, the coffer row, and the dead ends not to re-chase.
- [Limbus currencies have no static](limbus-currency-no-static.md) — the full Ghidra trace of the 0x118 path: the unit totals go into Currency-**menu rows**, never a global. A negative result, written down so it is not re-dug.
- [EmpyPop](empypop.md) — the Abyssea empyrean-NM pop-chain tracker: the generated `nms_gen.h` table, the bounded DFS over pop nodes, the three reads it hangs off (key items / inventory / treasure pool), and the shared live-vs-preview path.
- [Hate List](hate-list.md) — mobs aggro'd on the party via hybrid claim-scan + 0x028 enmity, reusing the shared entity offsets, plus 0x067/0x068 friendly-pet learning.
- [Treasure Pool](treasure-pool.md) — the 0x0D2/0x0D3 lottery-pool packets (item, index, timestamp, lot, lotter), the 5-min expiry rule, and the 23.5k item-name table.
- [Skillchains](skillchains.md) — 0x028 resonance OPEN/CLOSE detection (add-effect animation @bit 272), the property/combo tables, and the usable-move continuation prediction.
- [Timers](timers.md) — self buff durations from the 0x063 order-9 packet (absolute FFXI ticks) + ability/spell recasts from the client tables (g+0x22C/0x230/0x234), the 0x028 buff-caster self-cast filter, the shared-recast_id name-collision disambiguation, and why ROM/119/57.DAT can't supply menu icons.
- [The client clock](client-clock.md) — how the client computes "now" for buff countdowns (a pulled monotonic clock + signed server offset resynced on 0x00A) and the `CEIL(delta/60)` rounding — the two fixes that made AioHUD's timers match the game to the second.
- [Buffs you cast on ALLIES](buffs-on-allies.md) — the Timers `tmMine` rows: 0x028 cat-4/6 detection keyed by (target, spell), the AoE self-mirror exact-timer trick, and the per-job estimation models (Enhancing skill 34, BRD songs skill 40, COR rolls cat 6).
- [Geomancy duration (GEO Indi-)](geomancy-duration.md) — skill 44 is an AURA (0x063 status pulses every ~3s) so the duration is COMPUTED (Base + JP1362×2 + flat Indicolure gear); the self/normal/Entrust cases + the 542-556/612 pulse-noise filter.
- [Target movement speed](movement-speed-analysis.md) — consolidates the in-game captures + the wiki into the method behind the Target box `Spd +NN%` readout (`ui/target.cpp`, base 5.0 yalms/s = 100%). Verbatim wiki background: [movement-speed.md](reference-sheets/movement-speed.md) · screenshot [compteur-de-vitesse.png](compteur-de-vitesse.png) · capture [pol_mnrUtoubLy.png](pol_mnrUtoubLy.png).
- [Moon phases](reference-sheets/moonphase.md) — the id→percent→phase table behind the Vana'diel clock's moon readout (`model/gamestate.h`).

## External reference pages — moved to [`reference-sheets/`](reference-sheets/README.md)

The verbatim game/wiki material that used to sit in this folder now lives one level down. The split is the
point of it: **`game-data/` is what WE reversed** — offsets, packet bit layouts, struct fields, none of it
findable anywhere else — while `reference-sheets/` is **what the game and the wiki already say**, kept only
because our models were derived from it and must stay re-checkable against it.

Mixed together, ~2 000 lines of wiki tables drowned the reversed pages they were supposed to support.

Moved there: `bard.md` · `carnwenhan.md` · `composure.md` · `enhancing-magic.md` · `enhancing-duration-gear.md`
· `fili-attire-set.md` · `lethargy-armor-set.md` · `song-potency.md` · `movement-speed.md` · `moonphase.md`,
plus the four HTML sheets.

Two duplicates were deleted rather than moved: `enhancing-magic-wiki.md` was **byte-identical** to
`enhancing-magic.md` (this index claimed one was a "curated RE summary" — it was the same wiki dump), and
`fili-attire-set-brd.md` was a strict subset of `fili-attire-set.md`.

### Still here, because they ARE reversed

- [song-duration-items.txt](song-duration-items.txt) · [enhancing-duration-items.txt](enhancing-duration-items.txt)
  — per-item percentages lifted out of `Timers.dll` by static analysis. Not wiki content: the game states most
  of these qualitatively or not at all.
- [song-duration.md](song-duration.md) — the BRD duration model, its measurements, and the rule that had it
  wrong for months (**potency IS duration**, +10 % per point of `Song+`).
- [chants-barde-comment-ca-marche.md](chants-barde-comment-ca-marche.md) — the same mechanics in plain French,
  written here rather than copied.
