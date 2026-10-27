#!/usr/bin/env python
# Generate src/model/ready_moves_gen.h from the BST-HUD addon's addons/BST-HUD/ready_move_mappings.lua and
# scripts/ready_move_icons.txt. Each Beastmaster Ready move -> its kind as BST-HUD shows it (piercing / slashing /
# blunt, an element, magic, buff, debuff) and, from ready_move_icons.txt, the status icon of a buff / debuff (its
# status id) or a kind that replaces BST-HUD's (an element for a move with no status ; the two moves BST-HUD lacks).
# The COST in charges is not taken from here : abilities_gen.h has it (the job ability's mp field, Windower res).
# The BST module draws the icon next to each move (docs/design/bst-module-spec.md).
import io, os, re
import genpaths

SRC = genpaths.addons_dir() + r'\BST-HUD\ready_move_mappings.lua'
ICONS = os.path.join(genpaths.ROOT, 'scripts', 'ready_move_icons.txt')
OUT = genpaths.out_path("ready_moves_gen.h")

kinds = {}
with io.open(SRC, "r", encoding="utf-8") as f:
    for line in f:
        m = re.search(r"\['((?:[^'\\]|\\.)*)'\]\s*=\s*\{\s*damage_type\s*=\s*'([a-z]+)'", line)
        if m:
            kinds[m.group(1)] = m.group(2)

status = {}
with io.open(ICONS, "r", encoding="utf-8") as f:
    for line in f:
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        name, icon = [x.strip() for x in line.split('|', 1)]
        m = re.match(r'status\s+(\d+)$', icon)
        if m:
            status[name] = int(m.group(1))
            kinds.setdefault(name, 'magic')
        else:
            kinds[name] = icon

rows = sorted((en, kinds[en], status.get(en, 0)) for en in kinds)

with io.open(OUT, "w", encoding="utf-8", newline="\n") as o:
    o.write("// ready_moves_gen.h -- AUTO-GENERATED from addons/BST-HUD/ready_move_mappings.lua + scripts/ready_move_icons.txt (do not edit).\n")
    o.write("// Regenerate: python scripts/gen_ready_moves.py\n")
    o.write("#pragma once\n#include <string.h>\n\nnamespace aio {\n\n")
    o.write("// kind : what BST-HUD shows, or ready_move_icons.txt's ; status : the status icon id (res/buffs.lua), 0 = none\n")
    o.write("struct ReadyMoveRow { const char* en; const char* kind; unsigned short status; };\n")
    o.write("static const ReadyMoveRow READY_MOVES[] = {\n")
    for en, kind, st in rows:
        o.write('    {"%s", "%s", %d},\n' % (en.replace('\\', '\\\\').replace('"', '\\"'), kind, st))
    o.write("};\n")
    o.write("static const int READY_MOVES_N = %d;\n\n" % len(rows))
    o.write("// a Ready move's row by its English name (sorted by name), 0 when the table does not know it\n")
    o.write("inline const ReadyMoveRow* ready_move_row(const char* en) {\n")
    o.write("    int lo = 0, hi = READY_MOVES_N - 1;\n")
    o.write("    while (lo <= hi) { int mid = (lo + hi) >> 1; const int c = strcmp(READY_MOVES[mid].en, en);\n")
    o.write("        if (c == 0) return &READY_MOVES[mid];\n")
    o.write("        if (c < 0) lo = mid + 1; else hi = mid - 1; }\n")
    o.write("    return 0;\n}\n")
    o.write("inline const char* ready_move_kind(const char* en) { const ReadyMoveRow* r = ready_move_row(en); return r ? r->kind : 0; }\n\n")
    o.write("} // namespace aio\n")

print("wrote %d ready moves (%d with a status icon) -> %s" % (len(rows), len(status), genpaths.rel(OUT)))
