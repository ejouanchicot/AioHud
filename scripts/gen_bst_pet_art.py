#!/usr/bin/env python3
"""Convert pet art for the BST box (src/ui/hud_bst.cpp) into the RUNTIME folder, never into the repo.

    python scripts/gen_bst_pet_art.py                     # the BST-HUD addon's art (test art, Eiffel's)
    python scripts/gen_bst_pet_art.py --src <folder>      # your own : <PetNameWithoutSpaces>.png in that folder

Each pet becomes plugins\\AioHud\\assets\\bstpets\\<Key>.raw : 120 x 120, BGRA, top-down, no header (what
load_raw_texture reads). <Key> is the pet's name without spaces nor punctuation ("Blackbeard Randy" ->
BlackbeardRandy), the name the box looks for.

The BST-HUD art is Eiffel's, with no licence written (BST-HUD README) : it is used as TEST art while the player
draws their own (2026-10-26), so it is written next to the deployed plugin only -- not under assets\\ in this repo,
which deploy.bat / package.bat copy and git tracks. Without --src the BST-HUD mapping (pet_image_mappings.lua) gives
the pets that share one picture.
"""
import argparse, io, os, re, sys
import genpaths
from PIL import Image

SIDE = 120


def to_raw(png, out):
    im = Image.open(png).convert("RGBA")
    if im.size != (SIDE, SIDE):
        im = im.resize((SIDE, SIDE), Image.LANCZOS)
    r, g, b, a = im.split()
    open(out, "wb").write(Image.merge("RGBA", (b, g, r, a)).tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", help="a folder of <Key>.png (your own art) ; default : the BST-HUD addon's")
    ap.add_argument("--out", help="default : <Windower>\\plugins\\AioHud\\assets\\bstpets")
    a = ap.parse_args()
    windower = os.path.dirname(genpaths.addons_dir())
    out = a.out or os.path.join(windower, "plugins", "AioHud", "assets", "bstpets")
    os.makedirs(out, exist_ok=True)
    pairs = []   # (key, png)
    if a.src:
        for f in sorted(os.listdir(a.src)):
            if f.lower().endswith(".png"):
                pairs.append((os.path.splitext(f)[0], os.path.join(a.src, f)))
    else:
        hud = os.path.join(genpaths.addons_dir(), "BST-HUD")
        text = io.open(os.path.join(hud, "pet_image_mappings.lua"), encoding="utf-8").read()
        text = re.sub(r"--[^\n]*", "", text)   # its comments hold a sample row ('pet_name' = 'image_file.png')
        # BST-HUD keeps the dots and dashes of a name ('Hip.Familiar', 'Left-HandedYoko') : the box drops every
        # character that is not a letter or a digit (hud_bst.cpp art_key), so the file is named the same way
        for key, png in re.findall(r"\['([^']+)'\]\s*=\s*'([^']+\.png)'", text):
            pairs.append((re.sub(r'[^A-Za-z0-9]', '', key), os.path.join(hud, "images", png)))
        # names BST-HUD spells differently from the game : Redolent Candi (the NQ Snapweed, whose HQ is Alluring Honey)
        # is 'RedolentCandice' there, so the box looking for RedolentCandi found nothing (2026-10-27)
        for key, png in (("RedolentCandi", "AlluringHoney.png"),):
            pairs.append((key, os.path.join(hud, "images", png)))
    n = 0
    for key, png in pairs:
        if not os.path.isfile(png):
            print("  missing", png); continue
        to_raw(png, os.path.join(out, key + ".raw")); n += 1
    print("wrote %d pet image(s) -> %s" % (n, out))
    return 0 if n else 1


if __name__ == "__main__":
    sys.exit(main())
