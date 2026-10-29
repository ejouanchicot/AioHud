#!/usr/bin/env python
# Generate src/model/bst_pets_gen.h from addons/BST_JUG_PETS_DATABASE.md : every jug pet (NQ and HQ names) with its
# Ready moves, for the BST box's PREVIEW only (//aio config -> BST -> preview pet ; //aio edit) -- so each pet's name,
# art and moves can be checked out of a fight. The live box never reads it : it takes the moves the game says the pet
# has now (GameState::bst, game_mem.cpp compute_bst). A move name the game does not know (abilities_gen.h, recast 102)
# is dropped and said so.
import io, os, re
import genpaths

SRC = os.path.join(genpaths.addons_dir(), 'BST_JUG_PETS_DATABASE.md')
OUT = genpaths.out_path("bst_pets_gen.h")
MAXM = 8

ab = io.open(genpaths.model_path('abilities_gen.h'), encoding='utf-8').read()
known = dict((m.group(2), int(m.group(1))) for m in re.finditer(r'\{(\d+), "([^"]*)", (\d+), 102\}', ab) if m.group(1) != '72')

pets, names, inmoves = [], None, False
for line in io.open(SRC, encoding='utf-8'):
    line = line.rstrip('\n')
    h = re.match(r'^## \S+\s+(.*)$', line)
    if h:
        title = h.group(1)
        names = None if ('Sommaire' in title) else [re.sub(r'\s*\(HQ\)\s*$', '', n).strip() for n in title.split(' / ')]
        inmoves = False
        if names: pets.append([names, [], '', None])
        continue
    if line.startswith('# '): names = None; inmoves = False; continue
    if names is None: continue
    # its correlation family : "- **Espèce :** Tiger (Beast) · **Job :** WAR ..."
    sp = re.search(r'\*\*Esp\S*ce :\*\*\s*[^(]*\((\w+)\)', line)
    if sp: pets[-1][2] = sp.group(1); continue
    # a pet whose moves are another's ("**Ready Moves :** identiques à Sharpwit Hermes (Head Butt, ...)" : Sweet
    # Caroline) : no table of its own, the other pet's moves taken below. It was dropped with its family, so the box
    # had no correlation for it (the player, 2026-10-29)
    same = re.match(r'^\*\*Ready Moves :\*\*\s*identiques? \S+ ([^(.]+)', line)
    if same: pets[-1][3] = same.group(1).strip(); continue
    if line.startswith('**Ready Moves'): inmoves = True; continue
    if inmoves:
        if line.startswith('|'):
            cell = line.strip('|').split('|')[0].strip()
            if cell and cell != 'Move' and not cell.startswith('---'):
                if cell in known: pets[-1][1].append(cell)
                else: print('  unknown move %r (%s)' % (cell, pets[-1][0][0]))
        elif line.strip() == '---': inmoves = False

by_name = dict((n, mv) for names_, mv, _e, _s in pets for n in names_)
for pet in pets:
    if not pet[1] and pet[3]:
        pet[1] = list(by_name.get(pet[3], []))
        if not pet[1]: print('  %s : the moves of %r not found' % (pet[0][0], pet[3]))
rows = []
for names_, moves, eco, _same in pets:
    if not moves and not eco: continue   # nothing to show : no moves, no family
    for n in names_:
        # in the game's order (ability id), the order GearSwap numbers them in for //gs c rdymove N
        rows.append((n, eco, sorted(moves, key=lambda m: known[m])[:MAXM]))
rows.sort(key=lambda r: r[0])

with io.open(OUT, 'w', encoding='utf-8', newline='\n') as o:
    o.write('// bst_pets_gen.h -- AUTO-GENERATED from addons/BST_JUG_PETS_DATABASE.md (do not edit).\n')
    o.write('// Regenerate: python scripts/gen_bst_pets.py -- the BST box PREVIEW only (each jug pet and its Ready moves).\n')
    o.write('#pragma once\n\nnamespace aio {\n\n')
    o.write('// eco : the pet\'s correlation family (the database\'s "Espece : Tiger (Beast)") ; moves : 0-terminated\n')
    o.write('struct BstPetRow { const char* name; const char* eco; const char* moves[%d]; };\n' % MAXM)
    o.write('static const BstPetRow BST_PETS[] = {\n')
    for n, eco, ms in rows:
        cells = ['"%s"' % m for m in ms] + (['0'] if len(ms) < MAXM else [])   # a pet with no move known : {0}
        o.write('    {"%s", "%s", {%s}},\n' % (n, eco, ', '.join(cells)))
    o.write('};\nstatic const int BST_PETS_N = %d;\n\n} // namespace aio\n' % len(rows))

print('wrote %d pets -> %s' % (len(rows), genpaths.rel(OUT)))
