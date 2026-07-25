#!/usr/bin/env python3
"""Generate src/model/song_dur_gen.h -- the BRD song-duration gear table.

SOURCE OF TRUTH: scratchpad/timers_song.txt, the reverse of Timers.dll FUN_10007a40. That file's
"CORRECTED / COMPLETE per-item FLAT song-duration table" (2026-07-12 addendum) supersedes an earlier
partial list; the first RE pass had truncated the function at 0x10008200 and missed the whole tail,
where the +3/+4 gear tiers live.

The table was originally transcribed into song_dur.h BY HAND and the transcription lost data:
  - every item that also carries a per-family extra had its FLAT column zeroed (Fili Calot +3 is
    +10 flat AND +10% Madrigal -- it was stored as flat 0),
  - two Carnwenhan ilvl stages (0x4D07, 0x4D74) were dropped, which is a straight -50%,
  - and song_dur_m1_pct then ignored the family column outright.
Measured against the server that added up to a 22-37% underestimate on every ally song row.

Generating it removes the hand step. Run:  python scripts/gen_song_dur.py
"""
import re, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC  = ROOT / "scratchpad" / "timers_song.txt"
OUT  = ROOT / "src" / "model" / "song_dur_gen.h"

# Carnwenhan stages named only in the addendum's prose, not in the table body:
#   "CARNWENHAN (main/sub) by ilvl stage: ... 0x4D07/0x4D74/0x5051/0x5052/0x506A/0x4DF5 +50"
EXTRA_FLAT = {0x4D07: (50, "Carnwenhan", "main/sub"), 0x4D74: (50, "Carnwenhan", "main/sub")}

# Per-family EXTRA %, i.e. a bonus that applies ONLY when the song being sung is of that family.
# m1 = 1 + SUM(flat) + SUM(family-extra for THIS song) + 0.05 JP gift -- verbatim from the RE.
# Family ids: 1 Paeon 2 Ballad 3 Minne 4 Minuet 5 Madrigal 6 Prelude 7 Mambo 8 March 9 Etude
#             10 Carol 11 Hymnus 12 Mazurka 13 Sirvente 14 Scherzo 15 Operetta
#
# TWO sources, merged. The addendum's prose lists only six 'decoded examples', but the earlier HAND
# transcription into song_dur.h had carried a family column for 94 items -- real decoded data, and the
# only place it survives (it is what makes Cornette +10% Minuet, matching the addendum's own remark).
# The hand table is the base; the addendum's examples are layered on top and win on conflict, since the
# hand table could hold only ONE family per item and the addendum shows some carry two.
FAMILY_EXTRA = {
    0x2B41: [(5, 10)], 0x2B55: [(4, 10)], 0x2B69: [(8, 10)], 0x2B7D: [(2, 10)], 0x2B91: [(14, 10)],
    0x43C0: [(4, 10)], 0x43C1: [(5, 10)], 0x43C2: [(5, 10)], 0x43C3: [(5, 10)], 0x43C4: [(5, 10)],
    0x43C5: [(8, 20)], 0x43C6: [(6, 10)], 0x43C7: [(7, 10)], 0x43C8: [(5, 10)], 0x43C9: [(5, 10)],
    0x43CA: [(3, 10)], 0x43CB: [(9, 10)], 0x43CC: [(5, 10)], 0x43CD: [(1, 10)], 0x43CE: [(1, 30)],
    0x43CF: [(9, 10)], 0x43D0: [(8, 30)], 0x43D1: [(10, 10)], 0x43D2: [(5, 10)], 0x43D3: [(11, 30)],
    0x43D4: [(5, 10)], 0x43D5: [(5, 10)], 0x43D6: [(5, 10)], 0x43D7: [(8, 10)], 0x43D8: [(5, 10)],
    0x43D9: [(4, 20)], 0x43DA: [(7, 20)], 0x43DB: [(5, 10)], 0x43DC: [(5, 10)], 0x43DD: [(3, 10)],
    0x43DE: [(3, 20)], 0x43DF: [(5, 20)], 0x43E0: [(9, 20)], 0x43E1: [(10, 20)], 0x43E2: [(6, 20)],
    0x45A9: [(1, 20)], 0x45AA: [(9, 20)], 0x45AB: [(8, 10)], 0x45AC: [(8, 10)], 0x45AD: [(5, 10)],
    0x45AE: [(12, 20)], 0x45AF: [(5, 10)], 0x45B0: [(11, 20)], 0x45B1: [(5, 10)], 0x45B2: [(5, 10)],
    0x45B3: [(5, 10)], 0x45B4: [(5, 10)], 0x45B5: [(5, 20)], 0x45B6: [(4, 20)], 0x45B7: [(10, 20)],
    0x45B8: [(1, 20)], 0x45B9: [(7, 10)], 0x45BA: [(7, 20)], 0x45BB: [(5, 10)], 0x45BC: [(5, 10)],
    0x45BD: [(8, 20)], 0x45BE: [(5, 10)], 0x45BF: [(5, 10)], 0x45C0: [(3, 30)], 0x5A09: [(1, 10)],
    0x5A36: [(5, 10)], 0x5A79: [(4, 10)], 0x5ABC: [(8, 10)], 0x5B42: [(14, 10)], 0x5B58: [(1, 20)],
    0x5B85: [(5, 10)], 0x5BC8: [(4, 10)], 0x5C0B: [(8, 10)], 0x5C4E: [(2, 10)], 0x5C91: [(14, 10)],
    0x63D9: [(9, 10)], 0x63DA: [(9, 20)], 0x652D: [(3, 10)], 0x652E: [(3, 20)], 0x6570: [(7, 10)],
    0x6571: [(7, 20)], 0x6584: [(10, 10)], 0x6585: [(10, 20)], 0x668F: [(5, 10)], 0x6886: [(5, 10)],
    0x6887: [(5, 10)], 0x69BE: [(8, 10)], 0x69BF: [(8, 10)], 0x6A77: [(2, 10)], 0x6A78: [(2, 10)],
    0x6B25: [(14, 10)], 0x6B26: [(14, 10)], 0x6C18: [(1, 10)], 0x6C2D: [(1, 10)]
}
for _iid, _fams in {0x5BC8: [(4, 10)], 0x5C0B: [(8, 10), (10, 10)], 0x5B85: [(5, 10)],
                    0x6585: [(10, 20)], 0x5397: [(10, 20)], 0x2F84: [(14, 10)]}.items():
    FAMILY_EXTRA[_iid] = _fams   # addendum wins : it is the later, fuller decode

ROW = re.compile(r"^(0x[0-9a-fA-F]+),\+(\d+),([^,]+),(\S+)\s*$")

def main():
    if not SRC.exists():
        sys.exit("missing %s -- the Timers.dll reverse dump is the source of truth" % SRC)
    text = SRC.read_text(encoding="utf-8", errors="ignore").splitlines()

    # Take the CORRECTED table only: everything after the addendum banner. The file also contains the
    # earlier partial list, and parsing both would silently reintroduce the superseded values.
    start = next((i for i, l in enumerate(text) if "CORRECTED / COMPLETE per-item FLAT" in l), None)
    if start is None:
        sys.exit("the CORRECTED table banner is gone from %s -- refusing to parse the superseded list" % SRC)

    flats = {}
    for line in text[start:]:
        m = ROW.match(line)
        if not m:
            continue
        iid = int(m.group(1), 16)
        if iid in flats:
            sys.exit("duplicate item 0x%04X in the source table" % iid)
        flats[iid] = (int(m.group(2)), m.group(3).strip(), m.group(4).strip())
    for iid, v in EXTRA_FLAT.items():
        flats.setdefault(iid, v)

    fam_only = sorted(set(FAMILY_EXTRA) - set(flats))   # family extra but no flat % of its own
    for iid in fam_only:
        flats[iid] = (0, "(family-extra only)", "?")

    rows = []
    for iid in sorted(flats):
        pct, name, slot = flats[iid]
        rows.append((iid, pct, FAMILY_EXTRA.get(iid, []), name, slot))

    maxfam = max((len(f) for _, _, f, _, _ in rows), default=0)
    out = []
    out.append("// song_dur_gen.h -- GENERATED by scripts/gen_song_dur.py from scratchpad/timers_song.txt")
    out.append("// (the reverse of Timers.dll FUN_10007a40). Do NOT hand-edit ; regenerate.")
    out.append("//")
    out.append("// m1 = 1 + SUM(flat %) + SUM(family-extra % for THIS song's family) + 0.05 BRD JP gift.")
    out.append("// Song-duration gear is a QUALITATIVE res stat (no number in res/items.lua), so the percentages")
    out.append("// can only come from the RE. %d items, %d of them carrying a family-gated extra." % (len(rows), sum(1 for r in rows if r[2])))
    out.append("#pragma once")
    out.append("namespace aio {")
    out.append("")
    out.append("struct SongDurFam { unsigned char family, pct; };")
    out.append("struct SongDurItem { unsigned short id; unsigned char flat; SongDurFam fam[%d]; };" % max(maxfam, 1))
    out.append("static const SongDurItem SONG_DUR[] = {")
    for iid, pct, fams, name, slot in rows:
        f = ",".join("{%d,%d}" % (a, b) for a, b in fams)
        pad = max(maxfam, 1) - len(fams)
        f = ",".join([x for x in ([f] if f else []) + ["{0,0}"] * pad])
        out.append("    {0x%04x,%3d,{%s}},   // %s (%s)" % (iid, pct, f, name, slot))
    out.append("};")
    out.append("static const int SONG_DUR_N = (int)(sizeof(SONG_DUR) / sizeof(SONG_DUR[0]));")
    out.append("")
    out.append("} // namespace aio")
    OUT.write_text("\n".join(out) + "\n", encoding="utf-8")
    print("wrote %s : %d items (%d with a family extra)" % (OUT, len(rows), sum(1 for r in rows if r[2])))

if __name__ == "__main__":
    main()
