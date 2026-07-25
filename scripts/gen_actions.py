#!/usr/bin/env python
# gen_actions.py -- generate src/model/spells_gen.h + abilities_gen.h from Windower res.
# (Reconstructed 2026-07 after the original was lost from the repo; validated to reproduce the committed
#  tables byte-for-byte on the res build it was regenerated against.)
#
#   spells_gen.h    <- res/spells.lua         SpellRow{id, en, cast_ms = cast_time*1000, mp = mp_cost, recast_id}
#   abilities_gen.h <- res/job_abilities.lua  AbilRow {id, en, mp = mp_cost, recast_id}
#
# Every entry with an id + English name is emitted, sorted by id, plus a binary-search accessor.
#   python scripts/gen_actions.py
import os, pathlib

# Output goes to THIS repo, resolved from the script's own location. It used to be an absolute path into
# `D:\Windower Tetsouo\plugins\_aiohud_re\src\model\` -- a runtime folder that was renamed to `AioHud`
# and that never held `src/` anyway, so this generator wrote into a phantom path and the table it produces
# was effectively un-regenerable. Windower's res/ stays absolute (it lives outside the repo) but is
# overridable with WINDOWER_RES so another machine can run this without editing the script.
ROOT = pathlib.Path(__file__).resolve().parent.parent
RES  = pathlib.Path(os.environ.get('WINDOWER_RES', r'D:\Windower Tetsouo\res'))

import re, io

SPELLS_RES = str(RES / "spells.lua")
ABILS_RES  = str(RES / "job_abilities.lua")
SPELLS_OUT = str(ROOT / "src" / "model" / "spells_gen.h")
ABILS_OUT  = str(ROOT / "src" / "model" / "abilities_gen.h")

LINE = re.compile(r'\[(\d+)\]\s*=\s*\{.*?\bid=(\d+)\b.*?\ben="((?:[^"\\]|\\.)*)"', re.S)

def field(line, name):
    m = re.search(r'\b' + name + r'=([\d.]+)', line)
    return m.group(1) if m else None

# res job_abilities.lua recast_id that DISAGREES with the live client (verified in-game) -> use the client's value.
# The recast list the game exposes is keyed by the CLIENT's recast_id ; a wrong res value makes us resolve the
# timer to the wrong ability name. (Empty for now : the DNC-jig case is handled in hud_timers by hiding the
# cross-job phantom recast slot, since Chocobo Jig II drives BOTH rc 218 and rc 242.)
ABIL_RECAST_OVERRIDE = {}

def parse(path):
    is_abil = 'job_abilities' in path.lower()
    rows = []
    with io.open(path, "r", encoding="utf-8") as f:
        for line in f:
            m = LINE.search(line)
            if not m:
                continue
            iid = int(m.group(2))
            en  = m.group(3).replace('\\', '\\\\').replace('"', '\\"')
            ct  = field(line, "cast_time")
            mp  = field(line, "mp_cost")
            rc  = field(line, "recast_id")
            cast_ms = int(round(float(ct) * 1000)) if ct is not None else 0
            rcv = int(rc) if rc else 0
            if is_abil:
                rcv = ABIL_RECAST_OVERRIDE.get(iid, rcv)
            rows.append((iid, en, cast_ms, int(mp) if mp else 0, rcv))
    rows.sort(key=lambda r: r[0])
    return rows

def emit(out, res, struct, arrname, nname, rowfmt, accessor):
    with io.open(out, "w", encoding="utf-8", newline="\n") as o:
        base = out.split('\\')[-1]
        src  = res.split('\\')[-1]
        o.write("// %s -- AUTO-GENERATED from Windower res/%s (do not edit).\n" % (base, src))
        o.write("// Regenerate: python scripts/gen_actions.py\n")
        o.write("#pragma once\n\nnamespace aio {\n\n")
        o.write(struct + "\n")
        o.write("static const %s %s[] = {\n" % (struct.split()[1], arrname))
        rows = parse(res)
        for r in rows:
            o.write(rowfmt(r))
        o.write("};\n")
        o.write("static const int %s = %d;\n\n" % (nname, len(rows)))
        o.write(accessor)
        o.write("\n} // namespace aio\n")
    return len(parse(res))

sp_acc = ("inline const SpellRow* spell_info(unsigned id) {\n"
          "    int lo = 0, hi = SPELLS_N - 1;\n"
          "    while (lo <= hi) { int mid = (lo + hi) >> 1;\n"
          "        if (SPELLS[mid].id == id) return &SPELLS[mid];\n"
          "        if (SPELLS[mid].id < id) lo = mid + 1; else hi = mid - 1; }\n"
          "    return 0;\n}\n")
ab_acc = ("inline const AbilRow* abil_info(unsigned id) {\n"
          "    int lo = 0, hi = ABILS_N - 1;\n"
          "    while (lo <= hi) { int mid = (lo + hi) >> 1;\n"
          "        if (ABILS[mid].id == id) return &ABILS[mid];\n"
          "        if (ABILS[mid].id < id) lo = mid + 1; else hi = mid - 1; }\n"
          "    return 0;\n}\n")

ns = emit(SPELLS_OUT, SPELLS_RES,
          "struct SpellRow { unsigned id; const char* en; unsigned cast_ms; unsigned mp; unsigned recast_id; };",
          "SPELLS", "SPELLS_N",
          lambda r: '    {%d, "%s", %d, %d, %d},\n' % r, sp_acc)
na = emit(ABILS_OUT, ABILS_RES,
          "struct AbilRow { unsigned id; const char* en; unsigned mp; unsigned recast_id; };",
          "ABILS", "ABILS_N",
          lambda r: '    {%d, "%s", %d, %d},\n' % (r[0], r[1], r[3], r[4]), ab_acc)
print("wrote %d spells -> %s" % (ns, SPELLS_OUT))
print("wrote %d abilities -> %s" % (na, ABILS_OUT))
