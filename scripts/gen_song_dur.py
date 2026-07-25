#!/usr/bin/env python3
"""Generate src/model/song_dur_gen.h -- the BRD song-duration gear table.

THE RULE (docs/game-data/reference-sheets/song_resume.html, the BG-Wiki Category:Song sheet) is what makes this table derivable instead of
reverse-engineered: a point of song POTENCY is also duration.

    "Le gear Song+ (potence) ajoute +10% de duree par 1 Song+"
    "Tous les bonus de duree d'equipement sont additifs entre eux, et additifs avec le bonus
     de duree des effets Song+ potence."

So m1 = 1 + SUM(explicit "Song effect duration +N%")
          + SUM(10% per point of "All songs +N")                  (every song)
          + SUM(10% per point of '"<Song>"+N' for THIS family)    (that family only)
          + 0.05 BRD job-point gift.
Troubadour's x2 is applied afterwards, on the total -- it is NOT part of m1.

That collapses a long-standing confusion. The earlier hand table carried a "family" column that was believed to
be potency and therefore NOT counted as duration ; it WAS potency, and potency IS duration. Both readings were
half right, which is why the model sat 22-37% under the server for months : the family term was dropped, and so
was every "All songs +N" instrument.

DYNAMIC SOURCE. Potency is stated numerically on the item itself, so it is read straight out of Windower's
res/item_descriptions.lua and covers EVERY item in the game -- including ones no reverse could have known.
Explicit "+N%" duration lines are read from the same file (12 items state one). Only the items whose text is
qualitative ("Increases song effect duration", 19 of them) need a value from the reference sheet ; they are
resolved by (name, level) so a new tier picks itself up.

Validated against five server measurements (//aio songlog): Minuet 343 vs 342 real, Madrigal 355 vs 352,
March 355 vs 352, Gavotte 162 vs 159, Capriccio 162 vs 158.

Run:  python scripts/gen_song_dur.py
"""
import re, io, os, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT  = ROOT / "src" / "model" / "song_dur_gen.h"

# Windower's resource folder. Overridable for a machine that installed it elsewhere.
RES = pathlib.Path(os.environ.get("WINDOWER_RES", r"D:\Windower Tetsouo\res"))

FAMILIES = {1: "Paeon", 2: "Ballad", 3: "Minne", 4: "Minuet", 5: "Madrigal", 6: "Prelude", 7: "Mambo",
            8: "March", 9: "Etude", 10: "Carol", 11: "Hymnus", 12: "Mazurka", 13: "Sirvente",
            14: "Scherzo", 15: "Operetta"}
BY_NAME = dict((v, k) for k, v in FAMILIES.items())

# EXPLICIT "Song effect duration +N%" per the reference sheet (docs/game-data/reference-sheets/song_resume.html, BG-Wiki Category:Song).
# The sheet -- not the item text -- is authoritative here: 19 items describe their duration qualitatively
# ("Increases song effect duration"), and several state NOTHING at all yet do carry one. Brioso Slippers +4
# is the case that proves it: no duration line in game, +15% on the sheet, and dropping it put the model 15
# points under a server measurement. Keyed by (name, level) as res/items.lua spells them -- so a new tier of
# an existing piece is picked up by adding one line, and the ilvl stages of a relic/mythic resolve themselves.
SHEET_DURATION = {
    ("Legato Dagger", 99): 5, ("Kali", 99): 5, ("Marsyas", 99): 50,
    ("Carnwenhan", 75): 10, ("Carnwenhan", 80): 20, ("Carnwenhan", 85): 30,
    ("Carnwenhan", 90): 40, ("Carnwenhan", 95): 40, ("Carnwenhan", 99): 50,
    ("Daurdabla", 90): 25, ("Daurdabla", 95): 30, ("Daurdabla", 99): 30,
    ("Aoidos' Hngrln. +1", 89): 5, ("Aoidos' Hngrln. +2", 89): 10, ("Aoidos' Matinee", 84): 10,
    ("Fili Hongreline", 99): 11, ("Fili Hongreline +1", 99): 12,
    ("Fili Hongreline +2", 99): 13, ("Fili Hongreline +3", 99): 14,
    ("Mdk. Shalwar +1", 99): 10,
    ("Inyanga Shalwar", 99): 12, ("Inyanga Shalwar +1", 99): 15, ("Inyanga Shalwar +2", 99): 17,
    ("Brioso Slippers", 99): 10, ("Brioso Slippers +1", 99): 11, ("Brioso Slippers +2", 99): 13,
    ("Brioso Slippers +3", 99): 15, ("Brioso Slippers +4", 99): 15,
}

ENTRY = re.compile(r'\[(\d+)\] = \{id=\d+,en="((?:[^"\\]|\\.)*)"')
LEVEL = re.compile(r'\[(\d+)\] = \{id=\d+,[^\n]*?\blevel=(\d+)')
EXPLICIT = re.compile(r'Song effect duration \+(\d+)%')
ALL_SONGS = re.compile(r'All songs \+(\d+)')
ONE_SONG = re.compile(r'\\"([A-Za-z\' ]+)\\"\+(\d+)')
VAGUE = re.compile(r'Increases song effect duration')

def read(fn):
    p = RES / fn
    if not p.exists():
        sys.exit("missing %s -- point WINDOWER_RES at your Windower res/ folder" % p)
    return io.open(str(p), encoding="utf-8", errors="ignore").read()

def main():
    ditems = read("item_descriptions.lua")
    nitems = read("items.lua")
    desc  = dict((int(m.group(1)), m.group(2)) for m in ENTRY.finditer(ditems))
    name  = dict((int(m.group(1)), m.group(2)) for m in ENTRY.finditer(nitems))
    level = dict((int(m.group(1)), int(m.group(2))) for m in LEVEL.finditer(nitems))

    rows, vague_unresolved, mismatches = [], [], []
    for iid, d in sorted(desc.items()):
        flat, fams = 0, []
        key = (name.get(iid, ""), level.get(iid, 0))
        sheet = SHEET_DURATION.get(key)
        if sheet is not None:
            flat += sheet
            # Cross-check against the item's own text where it states a number. A divergence means the sheet
            # and the game disagree -- report it rather than silently trusting either.
            m = EXPLICIT.search(d)
            if m and int(m.group(1)) != sheet:
                mismatches.append((iid, key[0], sheet, int(m.group(1))))
        else:
            m = EXPLICIT.search(d)
            if m:
                flat += int(m.group(1))
            elif VAGUE.search(d):
                vague_unresolved.append((iid, name.get(iid, "?"), level.get(iid, 0)))
        m = ALL_SONGS.search(d)
        if m:
            flat += 10 * int(m.group(1))     # a point of potency on every song = +10% duration on every song
        for mm in ONE_SONG.finditer(d):
            fam = BY_NAME.get(mm.group(1))
            if fam:
                fams.append((fam, 10 * int(mm.group(2))))
        if flat or fams:
            rows.append((iid, flat, fams, name.get(iid, "?")))

    if mismatches:
        print("WARNING: sheet and item text disagree on %d item(s):" % len(mismatches))
        for iid, n, sh, it in mismatches:
            print("   %-28s id=%-6d sheet +%d%% vs in-game +%d%%" % (n, iid, sh, it))
    if vague_unresolved:
        # LOUD, not silent: an unpriced item is a hole in every duration it touches. The sheet needs a line.
        print("WARNING: %d item(s) say 'Increases song effect duration' with no number and no sheet entry:"
              % len(vague_unresolved))
        for iid, n, lv in vague_unresolved:
            print("   %-28s id=%-6d level=%d" % (n, iid, lv))

    maxfam = max([len(f) for _, _, f, _ in rows] + [1])
    out = ["// song_dur_gen.h -- GENERATED by scripts/gen_song_dur.py. Do NOT hand-edit ; regenerate.",
           "// Built from Windower res/item_descriptions.lua (+ the reference sheet for the items whose text is",
           "// qualitative). Song POTENCY is duration: +10% per point, per the BG-Wiki Category:Song rule --",
           "//   m1 = 1 + SUM(explicit +N%) + SUM(10% per 'All songs +N') + SUM(10% per '\"<Song>\"+N' of THIS",
           "//            family) + 0.05 BRD JP gift.        Troubadour's x2 multiplies the TOTAL, not m1.",
           "// `flat` already folds 'All songs +N' in, since that applies to every song exactly like an explicit %.",
           "// %d items carry song duration ; %d of them through a family-specific potency." % (len(rows), sum(1 for r in rows if r[2])),
           "#pragma once",
           "namespace aio {",
           "",
           "struct SongDurFam { unsigned char family, pct; };",
           "struct SongDurItem { unsigned short id; unsigned char flat; SongDurFam fam[%d]; };" % maxfam,
           "static const SongDurItem SONG_DUR[] = {"]
    for iid, flat, fams, nm in rows:
        cells = ["{%d,%d}" % (a, b) for a, b in fams] + ["{0,0}"] * (maxfam - len(fams))
        out.append("    {%5d,%4d,{%s}},   // %s" % (iid, flat, ",".join(cells), nm))
    out += ["};",
            "static const int SONG_DUR_N = (int)(sizeof(SONG_DUR) / sizeof(SONG_DUR[0]));",
            "",
            "} // namespace aio"]
    OUT.write_text("\n".join(out) + "\n", encoding="utf-8")
    print("wrote %s : %d items (%d with a family-specific term)" % (OUT, len(rows), sum(1 for r in rows if r[2])))

if __name__ == "__main__":
    main()
