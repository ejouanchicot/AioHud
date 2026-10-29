#!/usr/bin/env python3
# gen_geo_spells.py -- generate src/model/geo_spells_gen.h from res/spells.lua : every Geomancy spell named "Geo-X"
# or "Indi-X", with its id, its name, its element and the STATUS of the Indi- of the same name, for the GEO box
# (model/geo_state.cpp names the bubble and the Entrust from the 0x028 spell id ; hud_geo.cpp draws that status's
# icon from the buff atlas and colours the name by element).
#
# Why the Indi- status for a Geo- : res gives a Geo- spell no `status` at all -- a luopan carries its effect, nobody
# gets a bearer status from the cast -- yet its effect is the Indi- one (Geo-Haste and Indi-Haste both grant the
# Haste of Indi-Haste, status 580). So Geo-X takes Indi-X's status, matched by the name after the dash.
# `offensive` : the spell's targets field holds the enemy bit (32) -- a Geo- placed on a mob, whose luopan has no
# reason to be near the player (the box then shows no range warning).
#
#   python scripts/gen_geo_spells.py [path-to-res-folder]
#   (no argument : $WINDOWER_RES, else the dev install -- genpaths.py)
import io, os, re, sys
import genpaths

RES = genpaths.res_dir(sys.argv[1] if len(sys.argv) > 1 else None)
SRC = os.path.join(RES, 'spells.lua')
if not os.path.isfile(SRC):
    raise SystemExit('res/spells.lua not found in %s -- pass its folder as argv[1] or set WINDOWER_RES' % RES)
OUT = genpaths.out_path('geo_spells_gen.h')

rows = []
for ln in io.open(SRC, encoding='utf-8', errors='ignore'):
    m = re.search(r'\{id=(\d+),en="((?:Geo|Indi)-[^"]*)"', ln)
    if not m or 'type="Geomancy"' not in ln:
        continue
    el = re.search(r'element=(\d+)', ln)
    st = re.search(r'status=(\d+)', ln)
    tg = re.search(r'targets=(\d+)', ln)
    rows.append([int(m.group(1)), m.group(2), int(el.group(1)) if el else 0, int(st.group(1)) if st else 0,
                 int(tg.group(1)) if tg else 0])
if not rows:
    raise SystemExit('no Geo- / Indi- spell found in res/spells.lua -- wrong res version?')

indi = dict((r[1][len('Indi-'):], r[3]) for r in rows if r[1].startswith('Indi-'))
for r in rows:
    if r[1].startswith('Geo-') and not r[3]:
        r[3] = indi.get(r[1][len('Geo-'):], 0)
        if not r[3]:
            print('  no Indi- of the same name for %r : no icon' % r[1])
rows.sort()

with io.open(OUT, 'w', encoding='utf-8', newline='\n') as o:
    o.write('// geo_spells_gen.h -- AUTO-GENERATED from res/spells.lua (do not edit).\n')
    o.write('// Regenerate: python scripts/gen_geo_spells.py -- every Geo- / Indi- spell for the GEO box (model/geo_state.cpp).\n')
    o.write('#pragma once\n\nnamespace aio {\n\n')
    o.write('// element : res numbering (0 Fire 1 Ice 2 Wind 3 Earth 4 Lightning 5 Water 6 Light 7 Dark) ; status : the Indi- of\n')
    o.write('// the same name\'s (a Geo- has none in res) ; geo : 1 Geo- (luopan), 0 Indi- ; offensive : targets an enemy\n')
    o.write('struct GeoSpellRow { unsigned short id; const char* en; unsigned char element; unsigned short status; unsigned char geo; unsigned char offensive; };\n')
    o.write('static const GeoSpellRow GEO_SPELLS[] = {\n')
    for sid, en, el, st, tg in rows:
        o.write('    {%d, "%s", %d, %d, %d, %d},\n' % (sid, en, el, st, 1 if en.startswith('Geo-') else 0, 1 if tg & 32 else 0))
    o.write('};\nstatic const int GEO_SPELLS_N = %d;\n\n' % len(rows))
    o.write('inline const GeoSpellRow* geo_spell(unsigned id) {\n')
    o.write('    for (int i = 0; i < GEO_SPELLS_N; ++i) if (GEO_SPELLS[i].id == id) return &GEO_SPELLS[i];\n')
    o.write('    return 0;\n}\n')
    o.write('// the Geo- / Indi- whose effect is `status` (the aura AioHUD tracks by its status) ; the Indi- one\n')
    o.write('inline const GeoSpellRow* geo_spell_by_status(unsigned status) {\n')
    o.write('    if (status) for (int i = 0; i < GEO_SPELLS_N; ++i) if (!GEO_SPELLS[i].geo && GEO_SPELLS[i].status == status) return &GEO_SPELLS[i];\n')
    o.write('    return 0;\n}\n\n} // namespace aio\n')

print('wrote %d spells -> %s' % (len(rows), genpaths.rel(OUT)))
