#!/usr/bin/env python3
"""Build assets/job_icons.raw -- the job-emblem atlas the party and player badges sample.

    python scripts/gen_job_icons.py            # regenerate assets/job_icons.raw
    python scripts/gen_job_icons.py --check    # rebuild in memory and diff against the shipped file

WHY THIS FILE EXISTS. CLAUDE.md has always described assets/job_icons_src/ as a regeneration SOURCE, and
there was no regenerator: two separate audits (2026-07-25, 2026-08-06) recorded job_icons.raw as an asset
nobody could rebuild. It was written on 2026-09-09 to add one emblem, and its first job was to reproduce
the shipped atlas BIT FOR BIT from the 22 PNGs that were already there -- a generator that cannot
reproduce what shipped is a generator that quietly redraws every other icon the first time you use it.
Run --check after touching anything here.

THE FORMAT (docs/design/party-visual-system.md 9c):
  512x192 BGRA, an 8x3 grid of 64 px cells, and every emblem is a WHITE MASK -- RGB forced to 255, alpha
  kept -- because the HUD draws it with MODULATE and tints it by the member's role colour. A cell that
  carries real colours would fight that tint.

THE ORDER IS THE JOB ID, not the folder listing: cell = job_id - 1, with the ids of JOBS[] in
model/party_state_roster.cpp. Slot 23 is SPC, the internal sentinel for special trusts (Monberaux and
the other chemists/beasts), which is why the 23rd emblem is sourced from ALC.png.
"""
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:                                        # the one dependency, and the one failure worth naming
    sys.exit("PIL/Pillow is required:  pip install pillow")

CELL, COLS, ROWS = 64, 8, 3
W, H = CELL * COLS, CELL * ROWS                            # 512 x 192

# (job id, source PNG). The id IS the cell index + 1 -- keep this list in step with JOBS[] in
# model/party_state_roster.cpp. A name here that has no PNG is a hard error, never a silently empty cell.
JOBS = [
    (1,  "WAR"), (2,  "MNK"), (3,  "WHM"), (4,  "BLM"), (5,  "RDM"), (6,  "THF"),
    (7,  "PLD"), (8,  "DRK"), (9,  "BST"), (10, "BRD"), (11, "RNG"), (12, "SAM"),
    (13, "NIN"), (14, "DRG"), (15, "SMN"), (16, "BLU"), (17, "COR"), (18, "PUP"),
    (19, "DNC"), (20, "SCH"), (21, "GEO"), (22, "RUN"),
    # 23 = SPC in JOBS[], the sentinel a special trust is given (the party array carries no job for a
    # trust). Monberaux is the one that made this visible: he resolves to SPC, cell 22 was empty, and the
    # badge drew nothing at all. The art is an alchemist's, hence the filename.
    (23, "ALC"),
]

ROOT = Path(__file__).resolve().parent.parent
SRC  = ROOT / "assets" / "job_icons_src"
OUT  = ROOT / "assets" / "job_icons.raw"


def build() -> bytes:
    """The atlas, as the plugin reads it: BGRA, top-down, no header."""
    sheet = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    for job, name in JOBS:
        png = SRC / (name + ".png")
        if not png.is_file():
            sys.exit("missing source: %s" % png)
        im = Image.open(png).convert("RGBA")
        if im.size != (CELL, CELL):
            im = im.resize((CELL, CELL), Image.LANCZOS)
        # WHITE MASK: keep the shape, throw the colours away. Done channel by channel rather than by
        # pasting a white block through the alpha, so a source whose RGB is already white is untouched.
        r, g, b, a = im.split()
        white = Image.new("L", im.size, 255)
        im = Image.merge("RGBA", (white, white, white, a))
        cell = job - 1
        sheet.paste(im, ((cell % COLS) * CELL, (cell // COLS) * CELL))
    r, g, b, a = sheet.split()
    return Image.merge("RGBA", (b, g, r, a)).tobytes()      # BGRA in memory order


def main() -> int:
    data = build()
    if len(data) != W * H * 4:
        sys.exit("built %d bytes, expected %d" % (len(data), W * H * 4))

    if "--check" in sys.argv:
        if not OUT.is_file():
            print("no %s to compare against" % OUT); return 1
        have = OUT.read_bytes()
        if have == data:
            print("identical to %s (%d bytes)" % (OUT.name, len(data))); return 0
        # Say WHICH cells differ. "the file changed" is not actionable; "cell 22 changed" is.
        diff = []
        for cell in range(COLS * ROWS):
            cx, cy = (cell % COLS) * CELL, (cell // COLS) * CELL
            for y in range(CELL):
                off = ((cy + y) * W + cx) * 4
                if have[off:off + CELL * 4] != data[off:off + CELL * 4]:
                    diff.append(cell); break
        print("DIFFERS from %s -- cells %s" % (OUT.name, diff))
        return 1

    OUT.write_bytes(data)
    print("wrote %s  (%d bytes, %d emblems)" % (OUT, len(data), len(JOBS)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
