#!/usr/bin/env python
# gen_actions.py -- generate src/model/spells_gen.h + abilities_gen.h from Windower res.
# (Reconstructed 2026-07 after the original was lost from the repo; validated to reproduce the committed
#  tables byte-for-byte on the res build it was regenerated against.)
#
#   spells_gen.h    <- res/spells.lua         SpellRow{id, en, cast_ms = cast_time*1000, mp = mp_cost, recast_id, status, trust, icon}
#   abilities_gen.h <- res/job_abilities.lua  AbilRow {id, en, mp = mp_cost, recast_id, status}
# status : the status the action gives (0 none) -- the action menu's icon fallback (the game's own status icon) when
# Windower's plugins\icons has no picture for it. A Geo- spell has none in res : it takes its Indi- twin's (same
# effect). trust : 1 for a Trust (Alter Ego) spell. icon : res icon_id (0 when none) -- Windower's Trust pictures
# follow it (1000 + 9 * weapon + colour), so a Trust without a file borrows / rebuilds one (hud_actmenu.cpp).
#
# Every entry with an id + English name is emitted, sorted by id, plus a binary-search accessor.
#   python scripts/gen_actions.py
import os, pathlib

# Output goes to THIS repo, resolved from the script's own location. It used to be an absolute path into
# `D:\Windower Tetsouo\plugins\_aiohud_re\src\model\` -- a runtime folder that was renamed to `AioHud`
# and that never held `src/` anyway, so this generator wrote into a phantom path and the table it produces
# was effectively un-regenerable. Windower's res/ stays absolute (it lives outside the repo) but is
# overridable with WINDOWER_RES so another machine can run this without editing the script (genpaths.py).
import genpaths
RES  = pathlib.Path(genpaths.res_dir())

import re, io

SPELLS_RES = str(RES / "spells.lua")
ABILS_RES  = str(RES / "job_abilities.lua")
SPELLS_OUT = genpaths.out_path("spells_gen.h")
ABILS_OUT  = genpaths.out_path("abilities_gen.h")

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
            st  = field(line, "status")
            trust = 1 if 'type="Trust"' in line else 0
            ic  = re.search(r'\bicon_id=(-?\d+)', line)
            icv = int(ic.group(1)) if ic and int(ic.group(1)) > 0 else 0
            rows.append((iid, en, cast_ms, int(mp) if mp else 0, rcv, int(st) if st else 0, trust, icv))
    rows.sort(key=lambda r: r[0])
    if not is_abil:   # Geo-X : the status of Indi-X (res leaves the Geo- bubbles without one)
        by_name = dict((r[1], r) for r in rows)
        rows = [r if r[5] or not r[1].startswith('Geo-') or ('Indi-' + r[1][4:]) not in by_name
                else r[:5] + (by_name['Indi-' + r[1][4:]][5],) + r[6:] for r in rows]
    return rows

def emit(out, res, struct, arrname, nname, rowfmt, accessor):
    with io.open(out, "w", encoding="utf-8", newline="\n") as o:
        base = os.path.basename(out)   # not split('\\') : a '/'-separated path put the WHOLE path in the banner
        src  = os.path.basename(res)
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
          "struct SpellRow { unsigned id; const char* en; unsigned cast_ms; unsigned mp; unsigned recast_id; unsigned short status; unsigned char trust; unsigned short icon; };",
          "SPELLS", "SPELLS_N",
          lambda r: '    {%d, "%s", %d, %d, %d, %d, %d, %d},\n' % r, sp_acc)
na = emit(ABILS_OUT, ABILS_RES,
          "struct AbilRow { unsigned id; const char* en; unsigned mp; unsigned recast_id; unsigned short status; };",
          "ABILS", "ABILS_N",
          lambda r: '    {%d, "%s", %d, %d, %d},\n' % (r[0], r[1], r[3], r[4], r[5]), ab_acc)
print("wrote %d spells -> %s" % (ns, SPELLS_OUT))
print("wrote %d abilities -> %s" % (na, ABILS_OUT))
