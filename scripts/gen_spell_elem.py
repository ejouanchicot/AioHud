# gen_spell_elem.py -- generate src/model/spell_elem_gen.h from Windower res/spells.lua : the ELEMENT of every spell
# that has one of the eight (0 Fire 1 Ice 2 Wind 3 Earth 4 Lightning 5 Water 6 Light 7 Dark -- the SCElem order).
# Used by the Skillchains box's Magic Burst popup : the burst is drawn in the colour of the element that burst.
#   python scripts/gen_spell_elem.py
import io, re, pathlib
import genpaths

RES = pathlib.Path(genpaths.res_dir())
SRC = str(RES / "spells.lua")
OUT = genpaths.out_path("spell_elem_gen.h")

LINE = re.compile(r'\[(\d+)\]\s*=\s*\{.*?\bid=(\d+)\b')
rows = []
with io.open(SRC, "r", encoding="utf-8") as f:
    for line in f:
        m = LINE.search(line)
        if not m:
            continue
        e = re.search(r'\belement=(-?\d+)', line)
        if not e:
            continue
        el = int(e.group(1))
        if 0 <= el <= 7:
            rows.append((int(m.group(2)), el))
rows.sort()
with io.open(OUT, "w", encoding="utf-8", newline="\n") as o:
    o.write("// spell_elem_gen.h -- AUTO-GENERATED from Windower res/spells.lua (do not edit).\n")
    o.write("// Regenerate: python scripts/gen_spell_elem.py\n")
    o.write("#pragma once\n\nnamespace aio {\n\n")
    o.write("// a spell's element : 0 Fire 1 Ice 2 Wind 3 Earth 4 Lightning 5 Water 6 Light 7 Dark (spells without one are absent)\n")
    o.write("struct SpellElemRow { unsigned short id; unsigned char elem; };\n")
    o.write("static const SpellElemRow SPELL_ELEMS[] = {\n")
    for i in range(0, len(rows), 8):
        o.write("    " + " ".join("{%d, %d}," % r for r in rows[i:i + 8]) + "\n")
    o.write("};\n")
    o.write("static const int SPELL_ELEMS_N = %d;\n\n" % len(rows))
    o.write("inline int spell_elem(unsigned id) {   // -1 : none / unknown\n"
            "    int lo = 0, hi = SPELL_ELEMS_N - 1;\n"
            "    while (lo <= hi) { int mid = (lo + hi) >> 1;\n"
            "        if (SPELL_ELEMS[mid].id == id) return SPELL_ELEMS[mid].elem;\n"
            "        if (SPELL_ELEMS[mid].id < id) lo = mid + 1; else hi = mid - 1; }\n"
            "    return -1;\n}\n")
    o.write("\n} // namespace aio\n")
print("spell_elem_gen.h : %d spells" % len(rows))
