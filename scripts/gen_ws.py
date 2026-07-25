#!/usr/bin/env python
# Generate src/model/weapon_skills_gen.h from Windower res/weapon_skills.lua.
# WS examine cache (FFXiMain+0x634590) holds the raw WS id (< 0x200) while hovering
# a Weapon Skill in the "ability " menu. We only need id -> English name (no MP).
import os, pathlib

# Output goes to THIS repo, resolved from the script's own location. It used to be an absolute path into
# `D:\Windower Tetsouo\plugins\_aiohud_re\src\model\` -- a runtime folder that was renamed to `AioHud`
# and that never held `src/` anyway, so this generator wrote into a phantom path and the table it produces
# was effectively un-regenerable. Windower's res/ stays absolute (it lives outside the repo) but is
# overridable with WINDOWER_RES so another machine can run this without editing the script.
ROOT = pathlib.Path(__file__).resolve().parent.parent
RES  = pathlib.Path(os.environ.get('WINDOWER_RES', r'D:\Windower Tetsouo\res'))

import re, io

RES = str(RES / "weapon_skills.lua")
OUT = str(ROOT / "src" / "model" / "weapon_skills_gen.h")

rows = []
with io.open(RES, "r", encoding="utf-8") as f:
    for line in f:
        m = re.search(r'\[(\d+)\]\s*=\s*\{[^}]*?\bid=(\d+)\b[^}]*?\ben="((?:[^"\\]|\\.)*)"', line)
        if not m:
            continue
        wid = int(m.group(2))
        en = m.group(3).replace('\\', '\\\\').replace('"', '\\"')
        rows.append((wid, en))

rows.sort(key=lambda r: r[0])

with io.open(OUT, "w", encoding="utf-8", newline="\n") as o:
    o.write("// weapon_skills_gen.h -- AUTO-GENERATED from Windower res/weapon_skills.lua (do not edit).\n")
    o.write("// Regenerate: python scripts/gen_ws.py\n")
    o.write("#pragma once\n\nnamespace aio {\n\n")   # no include : this table uses only `unsigned`/`const char*` (model must not pull gfx/)
    o.write("struct WSRow { unsigned id; const char* en; };\n")
    o.write("static const WSRow WEAPON_SKILLS[] = {\n")
    for wid, en in rows:
        o.write('    {%d, "%s"},\n' % (wid, en))
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
