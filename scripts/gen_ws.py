#!/usr/bin/env python
# Generate src/model/weapon_skills_gen.h from Windower res/weapon_skills.lua.
# WS examine cache (FFXiMain+0x634590) holds the raw WS id (< 0x200) while hovering
# a Weapon Skill in the "ability " menu. We only need id -> English name (no MP).
import os, pathlib

# Output goes to THIS repo, resolved from the script's own location. It used to be an absolute path into
# `D:\Windower Tetsouo\plugins\_aiohud_re\src\model\` -- a runtime folder that was renamed to `AioHud`
# and that never held `src/` anyway, so this generator wrote into a phantom path and the table it produces
# was effectively un-regenerable. Windower's res/ stays absolute (it lives outside the repo) but is
# overridable with WINDOWER_RES so another machine can run this without editing the script (genpaths.py).
import genpaths
RES  = pathlib.Path(genpaths.res_dir())

import re, io

RES = str(RES / "weapon_skills.lua")
OUT = genpaths.out_path("weapon_skills_gen.h")

# The DAMAGE element of the ELEMENTAL weapon skills (magical + hybrid), from BG Wiki (Category:Elemental_Weapon_Skill
# and Category:Hybrid_Weapon_Skill, both last edited 2026-05-27 ; read 2026-11-17). res' own `element` field is NOT
# this : Viper Bite, Rudra's Storm, Shijin Spiral, Ukko's Fury... carry one and are physical, while Savage Blade and
# Shining Blade share 6 -- the action menu painted Shining Blade earth (its Scission) before this table. Windower
# element ids : 0 Fire 1 Ice 2 Wind 3 Earth 4 Thunder 5 Water 6 Light 7 Dark. Starburst / Sunburst are "Light // Dark"
# on the wiki : kept Light. Any other WS : physical (-1).
ELEMENTAL = {
    # hybrid
    "Blade: Teki": 5, "Blade: To": 1, "Blade: Chi": 3, "Tachi: Goten": 4, "Tachi: Kagero": 0, "Tachi: Jinpu": 2,
    "Tachi: Koki": 6, "Flaming Arrow": 0, "Hot Shot": 0,
    # magical
    "Gust Slash": 2, "Cyclone": 2, "Aeolian Edge": 2, "Energy Steal": 7, "Energy Drain": 7,
    "Burning Blade": 0, "Red Lotus Blade": 0, "Shining Blade": 6, "Seraph Blade": 6, "Sanguine Blade": 7,
    "Frostbite": 1, "Freezebite": 1, "Herculean Slash": 1,
    "Cloudsplitter": 4, "Primal Rend": 6,
    "Dark Harvest": 7, "Shadow of Death": 7, "Infernal Scythe": 7,
    "Thunder Thrust": 4, "Raiden Thrust": 4,
    "Blade: Ei": 7, "Blade: Yu": 5,
    "Shining Strike": 6, "Seraph Strike": 6, "Flash Nova": 6,
    "Rock Crusher": 3, "Earth Crusher": 3, "Cataclysm": 7, "Vidohunir": 7, "Omniscience": 7, "Garland of Bliss": 6,
    "Starburst": 6, "Sunburst": 6,
    "Wildfire": 0, "Trueflight": 6, "Leaden Salute": 7,
}

rows = []
with io.open(RES, "r", encoding="utf-8") as f:
    for line in f:
        m = re.search(r'\[(\d+)\]\s*=\s*\{[^}]*?\bid=(\d+)\b[^}]*?\ben="((?:[^"\\]|\\.)*)"', line)
        if not m:
            continue
        wid = int(m.group(2))
        raw = m.group(3)
        en = raw.replace('\\', '\\\\').replace('"', '\\"')
        sk = re.search(r'\bskill=(\d+)', line)
        rows.append((wid, en, ELEMENTAL.get(raw, -1), int(sk.group(1)) if sk else 0))
        ELEMENTAL.pop(raw, None)

rows.sort(key=lambda r: r[0])
if ELEMENTAL: raise SystemExit("ELEMENTAL names not in res (typo / renamed ?) : %s" % sorted(ELEMENTAL))

with io.open(OUT, "w", encoding="utf-8", newline="\n") as o:
    o.write("// weapon_skills_gen.h -- AUTO-GENERATED from Windower res/weapon_skills.lua (do not edit).\n")
    o.write("// Regenerate: python scripts/gen_ws.py\n")
    o.write("#pragma once\n\nnamespace aio {\n\n")   # no include : this table uses only `unsigned`/`const char*` (model must not pull gfx/)
    o.write("// elem : the DAMAGE element of an elemental (magical / hybrid) WS, Windower ids 0 Fire .. 7 Dark ; -1 physical (BG Wiki,\n")
    o.write("// see scripts/gen_ws.py). skill : res skill (1 H2H 2 Dagger 3 Sword 4 GS 5 Axe 6 GA 7 Scythe 8 Polearm 9 Katana 10 GK\n")
    o.write("// 11 Club 12 Staff 25 Archery 26 Marksmanship).\n")
    o.write("struct WSRow { unsigned id; const char* en; signed char elem; unsigned char skill; };\n")
    o.write("static const WSRow WEAPON_SKILLS[] = {\n")
    for wid, en, el, sk in rows:
        o.write('    {%d, "%s", %d, %d},\n' % (wid, en, el, sk))
    o.write("};\n")
    o.write("static const int WEAPON_SKILLS_N = %d;\n\n" % len(rows))
    o.write("inline const WSRow* ws_info(unsigned id) {\n")
    o.write("    int lo = 0, hi = WEAPON_SKILLS_N - 1;\n")
    o.write("    while (lo <= hi) { int mid = (lo + hi) >> 1;\n")
    o.write("        if (WEAPON_SKILLS[mid].id == id) return &WEAPON_SKILLS[mid];\n")
    o.write("        if (WEAPON_SKILLS[mid].id < id) lo = mid + 1; else hi = mid - 1; }\n")
    o.write("    return 0;\n}\n\n")
    o.write("} // namespace aio\n")

print("wrote %d weapon skills -> %s" % (len(rows), OUT))
