#!/usr/bin/env python
# gen_mounts.py -- src/model/mounts_gen.h from Windower res/mounts.lua : mount id -> English name.
# The client's `mount` list (Abilities -> Mount) holds the mount id itself in each row (live capture 2026-11-17,
# docs/game-data/native-windows.md) ; AioHUD's redrawn list names it from this table.
import io, os, re
import genpaths

RES = os.path.join(genpaths.res_dir(), "mounts.lua")
if not os.path.isfile(RES):
    raise SystemExit("res/mounts.lua not found in %s -- set WINDOWER_RES to your Windower res folder" % genpaths.res_dir())
OUT = genpaths.out_path("mounts_gen.h")

rows = []
with io.open(RES, "r", encoding="utf-8") as f:
    for line in f:
        m = re.search(r'\[(\d+)\]\s*=\s*\{[^}]*?\bid=(\d+)\b[^}]*?\ben="((?:[^"\\]|\\.)*)"', line)
        if m:
            rows.append((int(m.group(2)), m.group(3).replace('\\', '\\\\').replace('"', '\\"')))
rows.sort()

with io.open(OUT, "w", encoding="utf-8", newline="\n") as o:
    o.write("// mounts_gen.h -- AUTO-GENERATED from Windower res/mounts.lua (do not edit).\n")
    o.write("// Regenerate: python scripts/gen_mounts.py\n")
    o.write("#pragma once\n\nnamespace aio {\n\n")
    o.write("struct MountRow { unsigned id; const char* en; };\n")
    o.write("static const MountRow MOUNTS[] = {\n")
    for mid, en in rows:
        o.write('    {%d, "%s"},\n' % (mid, en))
    o.write("};\n")
    o.write("static const int MOUNTS_N = %d;\n\n" % len(rows))
    o.write("inline const char* mount_name(unsigned id) {\n")
    o.write("    for (int i = 0; i < MOUNTS_N; ++i) if (MOUNTS[i].id == id) return MOUNTS[i].en;\n")
    o.write("    return 0;\n}\n\n")
    o.write("} // namespace aio\n")
print("wrote %d mounts -> %s" % (len(rows), OUT))
