#!/usr/bin/env python3
"""Convert pet art for the BST content of the Pet box (src/ui/hud_bst.cpp).

    python scripts/gen_bst_pet_art.py                     # the BST-HUD addon's art (test art, Eiffel's) -> runtime folder
    python scripts/gen_bst_pet_art.py --src <folder>      # your own : <PetNameWithoutSpaces>.png in that folder
    python scripts/gen_bst_pet_art.py --src <folder> --map scripts/bst_pet_art_map.txt   # your own, any file names
    python scripts/gen_bst_pet_art.py --src <folder> --map scripts/bst_pet_art_map.txt --repo   # the player's own
                                                          # drawings INTO THE REPO (assets\\bstpets\\), shipped by
                                                          # deploy.bat / package.bat

--repo (the player's ask, 2026-10-29 : "on veut que ça fasse partie d'AioHUD" ; docs/design/pet-module-spec.md §2.5)
writes ONE picture per drawing, named after the first pet of its map line, and assets\\bstpets\\pets.txt : one
"<Pet> = <picture>" line per pet, which the box reads to find a pet's picture (45 drawings serve 97 pets : one copy
per pet would weigh twice as much). Only the player's drawings go there ; the BST-HUD test art never does.

Each pet becomes plugins\\AioHud\\assets\\bstpets\\<Key>.raw : 120 x 120, BGRA, top-down, no header (what
load_raw_texture reads), and <Key>_solo.raw : the same picture with its biggest creature only (a drawing's small prey
of the corner taken out, gen_bst_icons.py biggest_creature), which the box shows when its correlation line is on too
(the prey is on that line then: the player's ask, 2026-10-29). <Key> is the pet's name without spaces nor punctuation ("Blackbeard Randy" ->
BlackbeardRandy), the name the box looks for.

The BST-HUD art is Eiffel's, with no licence written (BST-HUD README) : it is used as TEST art while the player
draws their own (2026-10-26), so it is written next to the deployed plugin only -- not under assets\\ in this repo,
which deploy.bat / package.bat copy and git tracks. Without --src the BST-HUD mapping (pet_image_mappings.lua) gives
the pets that share one picture.
"""
import argparse, io, os, re, sys
import genpaths
from PIL import Image
from gen_bst_icons import biggest_creature   # the creature only, for <Key>_solo.raw

SIDE = 120


def to_raw(png, out):
    im = Image.open(png).convert("RGBA")
    if im.size != (SIDE, SIDE):
        im = im.resize((SIDE, SIDE), Image.LANCZOS)
    r, g, b, a = im.split()
    open(out, "wb").write(Image.merge("RGBA", (b, g, r, a)).tobytes())


def write_pic(png, out, name):
    """<name>.raw and <name>_solo.raw (the biggest creature only) of one drawing."""
    to_raw(png, os.path.join(out, name + ".raw"))
    solo = biggest_creature(png, SIDE)
    r, g, b, al = solo.split()
    open(os.path.join(out, name + "_solo.raw"), "wb").write(Image.merge("RGBA", (b, g, r, al)).tobytes())


def write_repo(src, mapfile):
    """The player's drawings into the repo's assets\\bstpets : one picture per map line, named after its first pet, and
    pets.txt (every pet -> its picture)."""
    out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets", "bstpets")
    os.makedirs(out, exist_ok=True)
    index, n = [], 0
    for line in io.open(mapfile, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line: continue
        pic, keys = (x.strip() for x in line.rsplit("=", 1))
        keys = keys.split()
        png = os.path.join(src, pic)
        if not keys: continue
        if not os.path.isfile(png):
            print("  missing", png); continue
        write_pic(png, out, keys[0]); n += 1
        index += ["%s = %s" % (k, keys[0]) for k in keys]
    with io.open(os.path.join(out, "pets.txt"), "w", encoding="ascii", newline="\n") as f:
        f.write("# <Pet> = <picture> : the BST box's art index (scripts/gen_bst_pet_art.py --repo ; src/ui/hud_bst.cpp art_file).\n")
        f.write("# Written by the script from scripts/bst_pet_art_map.txt : edit the map, not this file.\n")
        f.write("\n".join(index) + "\n")
    print("wrote %d picture(s) for %d pet(s) -> %s" % (n, len(index), out))
    return 0 if n else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", help="a folder of <Key>.png (your own art) ; default : the BST-HUD addon's")
    ap.add_argument("--map", help="with --src : a file of '<picture> = <key> <key> ...' lines (scripts/bst_pet_art_map.txt) ; "
                    "only the pets it names are written, the others keep their art")
    ap.add_argument("--out", help="default : <Windower>\\plugins\\AioHud\\assets\\bstpets")
    ap.add_argument("--repo", action="store_true", help="with --src and --map : into the repo's assets\\bstpets, one "
                    "picture per drawing and the pets.txt index")
    a = ap.parse_args()
    if a.repo:
        if not (a.src and a.map):
            print("--repo needs --src and --map (the player's own drawings only)"); return 2
        return write_repo(a.src, a.map)
    windower = os.path.dirname(genpaths.addons_dir())
    out = a.out or os.path.join(windower, "plugins", "AioHud", "assets", "bstpets")
    os.makedirs(out, exist_ok=True)
    pairs = []   # (key, png)
    if a.src and a.map:
        for line in io.open(a.map, encoding="utf-8"):
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line: continue
            pic, keys = (x.strip() for x in line.rsplit("=", 1))
            for key in keys.split():
                pairs.append((key, os.path.join(a.src, pic)))
    elif a.src:
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
        write_pic(png, out, key); n += 1
    print("wrote %d pet image(s) -> %s" % (n, out))
    return 0 if n else 1


if __name__ == "__main__":
    sys.exit(main())
