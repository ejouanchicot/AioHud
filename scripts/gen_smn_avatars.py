#!/usr/bin/env python3
# gen_smn_avatars.py -- generate src/model/smn_avatars_gen.h : every avatar and elemental spirit a Summoner calls, with
# its element, its picture (assets\smn\<img>.raw, scripts/gen_smn_art.py) and its Blood Pacts, for the SMN content of
# the Pet box (game_mem.cpp compute_smn, ui/hud_smn.cpp). SPEC docs/design/smn-module-spec.md.
#
# Which pact belongs to which avatar, and whether it is a Rage or a Ward, is NOT in res : res/job_abilities.lua names
# each pact but not its avatar, and files Atomos' and Alexander's pacts as Rage where BG-Wiki says Ward. The lists below
# are the player's "Grimoire des Avatars" (BG-Wiki data), checked on 2026-11-10 against the 14 BG-Wiki "(Avatar)"
# pages : no difference. res gives each pact its ability id ; a name res does not know stops the generator.
#
#   python scripts/gen_smn_avatars.py [path-to-res-folder]
#   (no argument : $WINDOWER_RES, else the dev install -- genpaths.py)
import io, os, re, sys
import genpaths

RES = genpaths.res_dir(sys.argv[1] if len(sys.argv) > 1 else None)
SRC = os.path.join(RES, 'job_abilities.lua')
if not os.path.isfile(SRC):
    raise SystemExit('res/job_abilities.lua not found in %s -- pass its folder as argv[1] or set WINDOWER_RES' % RES)
OUT = genpaths.out_path('smn_avatars_gen.h')

# element : res numbering (0 Fire 1 Ice 2 Wind 3 Earth 4 Lightning 5 Water 6 Light 7 Dark)
# (name, picture, element, Rage pacts, Ward pacts) -- the grimoire's order (by level)
AVATARS = [
    ('Ifrit', 'ifrit', 0,
     ['Inferno', 'Punch', 'Fire II', 'Burning Strike', 'Double Punch', 'Fire IV', 'Flaming Crush', 'Meteor Strike', 'Conflag Strike'],
     ['Crimson Howl', 'Inferno Howl']),
    ('Shiva', 'shiva', 1,
     ['Diamond Dust', 'Axe Kick', 'Blizzard II', 'Double Slap', 'Blizzard IV', 'Rush', 'Heavenly Strike'],
     ['Frost Armor', 'Sleepga', 'Diamond Storm', 'Crystal Blessing']),
    ('Garuda', 'garuda', 2,
     ['Aerial Blast', 'Claw', 'Aero II', 'Aero IV', 'Predator Claws', 'Wind Blade'],
     ['Aerial Armor', 'Whispering Wind', 'Hastega', 'Fleet Wind', 'Hastega II']),
    ('Titan', 'titan', 3,
     ['Earthen Fury', 'Rock Throw', 'Stone II', 'Rock Buster', 'Megalith Throw', 'Stone IV', 'Mountain Buster', 'Geocrush', 'Crag Throw'],
     ['Earthen Ward', 'Earthen Armor']),
    ('Ramuh', 'ramuh', 4,
     ['Judgment Bolt', 'Shock Strike', 'Thunder II', 'Thunderspark', 'Thunder IV', 'Chaotic Strike', 'Thunderstorm', 'Volt Strike'],
     ['Rolling Thunder', 'Lightning Armor', 'Shock Squall']),
    ('Leviathan', 'leviathan', 5,
     ['Tidal Wave', 'Barracuda Dive', 'Water II', 'Tail Whip', 'Water IV', 'Spinning Dive', 'Grand Fall'],
     ['Slowga', 'Spring Water', 'Tidal Roar', 'Soothing Current']),
    ('Carbuncle', 'carbuncle', 6,
     ['Searing Light', 'Poison Nails', 'Meteorite', 'Holy Mist'],
     ['Healing Ruby', 'Shining Ruby', 'Glittering Ruby', 'Healing Ruby II', 'Soothing Ruby', 'Pacifying Ruby']),
    ('Fenrir', 'fenrir', 7,
     ['Howling Moon', 'Moonlit Charge', 'Crescent Fang', 'Eclipse Bite', 'Lunar Bay', 'Impact'],
     ['Lunar Cry', 'Lunar Roar', 'Ecliptic Growl', 'Ecliptic Howl', 'Heavenward Howl']),
    ('Diabolos', 'diabolos', 7,
     ['Ruinous Omen', 'Camisado', 'Nether Blast', 'Night Terror', 'Blindside'],
     ['Somnolence', 'Nightmare', 'Ultimate Terror', 'Noctoshield', 'Dream Shroud', 'Pavor Nocturnus']),
    ('Cait Sith', 'cait_sith', 6,
     ['Regal Scratch', 'Level ? Holy', 'Regal Gash'],
     ["Altana's Favor", 'Raise II', 'Mewing Lullaby', 'Reraise II', 'Eerie Eye']),
    ('Siren', 'siren', 2,
     ['Clarsach Call', 'Welt', 'Roundhouse', 'Sonic Buffet', 'Tornado II', 'Hysteric Assault'],
     ['Lunatic Voice', 'Katabatic Blades', 'Chinook', 'Bitter Elegy', "Wind's Blessing"]),
    ('Atomos', 'atomos', 7, [], ['Deconstruction', 'Chronoshift']),
    ('Alexander', 'alexander', 6, [], ['Perfect Defense']),
    ('Odin', 'odin', 7, ['Zantetsuken'], []),
]
# (name, picture, element, what it casts) -- no Blood Pact : it casts on its own
SPIRITS = [
    ('Fire Spirit', 'fire_spirit', 0, 'Fire I-V, Flare, Burn'),
    ('Ice Spirit', 'ice_spirit', 1, 'Blizzard I-V, Freeze, Frost, Paralyze'),
    ('Air Spirit', 'wind_spirit', 2, 'Aero I-V, Tornado, Choke, Gravity, Silence'),
    ('Earth Spirit', 'earth_spirit', 3, 'Stone I-V, Quake, Rasp, Slow'),
    ('Thunder Spirit', 'thunder_spirit', 4, 'Thunder I-V, Burst, Shock'),
    ('Water Spirit', 'water_spirit', 5, 'Water I-V, Flood, Drown'),
    ('Light Spirit', 'light_spirit', 6, 'Cure, Curaga, Protect, Shell, Haste, Regen, Banish, Holy, Dia, Flash'),
    ('Dark Spirit', 'dark_spirit', 7, 'Bio I-III, Drain, Aspir, Absorb-*, Dispel, Sleepga, Stun'),
]

ids = {}
for ln in io.open(SRC, encoding='utf-8', errors='ignore'):
    m = re.search(r'\{id=(\d+),en="([^"]*)"', ln)
    if m and 'type="BloodPact' in ln:
        ids[m.group(2)] = int(m.group(1))
missing = [p for a in AVATARS for p in a[3] + a[4] if p not in ids]
if missing:
    raise SystemExit('Blood Pacts res/job_abilities.lua does not name : %s' % ', '.join(missing))

def cstr(s):
    return '"%s"' % s.replace('\\', '\\\\').replace('"', '\\"')

pacts = []   # (avatar index, ability id, ward)
for i, a in enumerate(AVATARS):
    for p in a[3]: pacts.append((i, ids[p], 0))
    for p in a[4]: pacts.append((i, ids[p], 1))

with io.open(OUT, 'w', encoding='utf-8', newline='\n') as o:
    o.write('// smn_avatars_gen.h -- AUTO-GENERATED by scripts/gen_smn_avatars.py (do not edit) : the avatars and spirits\n')
    o.write('// of the SMN content of the Pet box, their element, picture and Blood Pacts (the player\'s grimoire, BG-Wiki\n')
    o.write('// data ; the ability ids from res/job_abilities.lua). SPEC docs/design/smn-module-spec.md.\n')
    o.write('#pragma once\n\nnamespace aio {\n\n')
    o.write('// element : res numbering (0 Fire 1 Ice 2 Wind 3 Earth 4 Lightning 5 Water 6 Light 7 Dark) ; img : assets\\smn\\<img>.raw ;\n')
    o.write('// spells : what a spirit casts (0 for an avatar)\n')
    o.write('struct SmnAvatarRow { const char* en; const char* img; unsigned char element; unsigned char spirit; const char* spells; };\n')
    o.write('static const SmnAvatarRow SMN_AVATARS[] = {\n')
    for a in AVATARS:
        o.write('    {%s, %s, %d, 0, 0},\n' % (cstr(a[0]), cstr(a[1]), a[2]))
    for s in SPIRITS:
        o.write('    {%s, %s, %d, 1, %s},\n' % (cstr(s[0]), cstr(s[1]), s[2], cstr(s[3])))
    o.write('};\nstatic const int SMN_AVATARS_N = %d;\n\n' % (len(AVATARS) + len(SPIRITS)))
    o.write('// avatar : SMN_AVATARS index ; abil : res/job_abilities.lua id ; ward : 1 Blood Pact: Ward, 0 Rage (BG-Wiki\'s,\n')
    o.write('// not res\'s type) ; in the grimoire\'s order, by level\n')
    o.write('struct SmnPactRow { unsigned char avatar; unsigned short abil; unsigned char ward; };\n')
    o.write('static const SmnPactRow SMN_PACTS[] = {\n')
    for av, ab, w in pacts:
        o.write('    {%d, %d, %d},\n' % (av, ab, w))
    o.write('};\nstatic const int SMN_PACTS_N = %d;\n\n' % len(pacts))
    o.write('// the row whose name is `name`, ignoring case and spaces (a spirit\'s entity name may run its words together) ;\n')
    o.write('// -1 for a name that is no avatar\n')
    o.write('inline int smn_avatar_index(const char* name) {\n')
    o.write('    if (!name || !name[0]) return -1;\n')
    o.write('    for (int i = 0; i < SMN_AVATARS_N; ++i) {\n')
    o.write('        const char* a = SMN_AVATARS[i].en; const char* b = name;\n')
    o.write('        for (;;) {\n')
    o.write('            while (*a == \' \') ++a;\n')
    o.write('            while (*b == \' \') ++b;\n')
    o.write('            const char ca = (*a >= \'A\' && *a <= \'Z\') ? (char)(*a + 32) : *a, cb = (*b >= \'A\' && *b <= \'Z\') ? (char)(*b + 32) : *b;\n')
    o.write('            if (ca != cb) break;\n')
    o.write('            if (!ca) return i;\n')
    o.write('            ++a; ++b;\n')
    o.write('        }\n')
    o.write('    }\n')
    o.write('    return -1;\n}\n\n} // namespace aio\n')

print('wrote %d avatars / spirits, %d pacts -> %s' % (len(AVATARS) + len(SPIRITS), len(pacts), genpaths.rel(OUT)))
