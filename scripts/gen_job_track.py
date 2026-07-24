#!/usr/bin/env python3
# gen_job_track.py -- per-JOB list of trackable BUFF spells, for the Timers "track per job" config.
#   For each job we list the DISTINCT buff statuses that job can put up via a self/party spell, with the
#   lowest level it becomes available and a user-facing CATEGORY (Enspells / Barspells / Gains / ...).
#   The Timers config shows this as a collapsible checklist (default all-on) ; the box filters a buff out
#   when its status is unchecked for the current job.
# Keyed by STATUS (Refresh I/II/III -> one "Refresh" row). Cures/nukes/enemy enfeebles carry no lingering
# status field, so `status>0 && !(targets&Enemy)` already leaves only real buffs.
# NOTE: job abilities (COR rolls, DNC sambas, RUN runes) carry NO `levels` in res, so they need a separate
#       type->job pass -- not in this generator yet (pure-melee jobs get an empty spell list for now).
# Source : res/spells.lua.  Output : src/model/job_track_gen.h.
import re, os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def find_res():
    for c in [os.path.join(ROOT, '..', '..', 'res'), os.path.join(ROOT, 'res'), r'D:\Windower Tetsouo\res']:
        if os.path.isfile(os.path.join(c, 'spells.lua')):
            return c
    raise SystemExit('res/spells.lua not found')
RES = find_res()
OUT = os.path.join(ROOT, 'src', 'model', 'job_track_gen.h')
ENEMY = 32   # windower target bitflag : Self=1 Player=2 Party=4 Ally=8 NPC=16 Enemy=32

# Blue Magic sub-type metadata from the bluguide addon (id -> (hasSCA=physical, isNuke=magical)). Optional.
BLU_META = {}
for c in [r'D:\Windower Tetsouo\addons\bluguide\res\spellinfo.lua',
          os.path.join(ROOT, '..', '..', 'addons', 'bluguide', 'res', 'spellinfo.lua')]:
    if os.path.isfile(c):
        for ln in open(c, encoding='utf-8', errors='ignore'):
            mm = re.match(r'\s*\[(\d+)\]', ln)
            if mm:
                BLU_META[int(mm.group(1))] = (bool(re.search(r'\bSC[AB]\s*=', ln)), bool(re.search(r'\bNuke\s*=\s*true', ln)))
        break

JOBS = [None, 'WAR','MNK','WHM','BLM','RDM','THF','PLD','DRK','BST','BRD','RNG','SAM',
        'NIN','DRG','SMN','BLU','COR','PUP','DNC','SCH','GEO','RUN','MON']   # index = job id (1..23)

# --- categories (order = enum value ; keep in sync with the C header) ---
CATS = [   # (enum, EN, FR)
    ('TC_REFRESH',  'Refresh',        'Refresh'),
    ('TC_HASTE',    'Haste / Flurry', 'Haste / Flurry'),
    ('TC_PROTECT',  'Protect / Shell','Protect / Shell'),
    ('TC_REGEN',    'Regen',          'Regen'),
    ('TC_ENSPELL',  'Enspells',       'Enchantements'),
    ('TC_BARSPELL', 'Barspells',      'Barspells'),
    ('TC_GAIN',     'Gains',          'Gains'),
    ('TC_SPIKES',   'Spikes',         'Spikes'),
    ('TC_DEFENSE',  'Defensive',      'Defensif'),
    ('TC_SONG',     'Songs',          'Chants'),
    ('TC_GEO',      'Geomancy',       'Geomancie'),
    ('TC_NINJUTSU', 'Ninjutsu',       'Ninjutsu'),
    ('TC_BLUE',     'Blue Magic',     'Magie Bleue'),
    # --- Blue Magic sub-schools (from the bluguide addon : SCA=physical, Nuke=magical, res-status=buff) ---
    ('TC_BLU_BUFF', 'Blue: Buff',     'Bleu: Buff'),
    ('TC_BLU_PHYS', 'Blue: Physical', 'Bleu: Physique'),
    ('TC_BLU_MAG',  'Blue: Magical',  'Bleu: Magique'),
    ('TC_BLU_DEB',  'Blue: Debuff',   'Bleu: Debuff'),
    ('TC_ENHANCE',  'Enhancing',      'Amelioration'),
    # --- non-buff spell schools (recast-tracked) ---
    ('TC_HEAL',     'Healing',        'Soins'),
    ('TC_NUKE',     'Elemental',      'Elementaire'),
    ('TC_ENFEEBLE', 'Enfeebling',     'Affaiblissement'),
    ('TC_DARK',     'Dark Magic',     'Magie Noire'),
    ('TC_DIVINE',   'Divine',         'Divine'),
    ('TC_SUMMON',   'Summons',        'Invocations'),
    ('TC_UTILITY',  'Utility',        'Utilitaire'),
    # --- ability categories (job abilities, not spells) ---
    ('TC_ROLL',     'Rolls',          'Rolls'),
    ('TC_SAMBA',    'Sambas',         'Sambas'),
    ('TC_DANCE',    'Dances',         'Danses'),
    ('TC_RUNE',     'Runes',          'Runes'),
    ('TC_WARD',     'Wards',          'Wards'),
    ('TC_STRAT',    'Stratagems',     'Stratagemes'),
    ('TC_BPACT',    'Blood Pacts',    'Pactes'),
    ('TC_JA',       'Job Abilities',  'Aptitudes'),
    # --- non-spell status buffs (food / conquest / synthesis events) : no job CASTS these, so they never come from
    #     spells.lua -- injected via EXTRA_FAM below so the family filter can show/hide them. ---
    ('TC_FOOD',     'Food',           'Nourriture'),
    ('TC_AFTERMATH','Aftermath',      'Aftermath'),
    ('TC_SIGNET',   'Signet',         'Signet'),
    ('TC_CRAFT',    'Craft',          'Artisanat'),
    ('TC_OTHER',    'Other',          'Autre'),
]
CAT_IDX = {c[0]: i for i, c in enumerate(CATS)}
JOBIDX = {a: i for i, a in enumerate(JOBS) if a}   # abbrev -> job id

BAR_NAMES  = ('Bar',)   # Barfire, Barfira, Barsleep(ra), Baraero...
EN_NAMES   = ('Enfire','Enblizzard','Enthunder','Enstone','Enwater','Enaero','Enlight','Endark')
SPIKES     = ('Blaze Spikes','Ice Spikes','Shock Spikes','Dread Spikes')
DEFENSE    = ('Phalanx','Stoneskin','Blink','Aquaveil')   # NB: Temper is OFFENSIVE (grants Double Attack), NOT defensive -> falls to Enhancing

def category(name, sptype, skill):
    n = name
    # --- AUTHORITATIVE school first : the game's own `type` / `skill` fields, NEVER a name guess. This is what
    #     keeps a spell out of the WRONG school (e.g. a BLU "Barrier Tusk" can't leak into Bar-spells). ---
    if sptype == 'BardSong':     return 'TC_SONG'
    if sptype == 'Geomancy':     return 'TC_GEO'
    if sptype == 'Ninjutsu':     return 'TC_NINJUTSU'
    if sptype == 'BlueMagic':    return 'TC_BLUE'
    if sptype == 'SummonerPact': return 'TC_SUMMON'
    if skill == 33:              return 'TC_HEAL'      # Healing
    if skill == 36:              return 'TC_NUKE'      # Elemental
    if skill == 35:              return 'TC_ENFEEBLE'  # Enfeebling
    if skill == 37:              return 'TC_DARK'       # Dark
    if skill == 32:              return 'TC_DIVINE'     # Divine
    if skill == 34:              # Enhancing -> fine sub-families by FFXI's consistent naming, SAFELY (only inside this school)
        if n.startswith('Refresh'):                          return 'TC_REFRESH'
        if n.startswith('Haste') or n.startswith('Flurry'):  return 'TC_HASTE'
        if n.startswith('Protect') or n.startswith('Shell'): return 'TC_PROTECT'
        if n.startswith('Regen'):                            return 'TC_REGEN'
        if n.startswith('Gain-') or n.startswith('Boost-'):  return 'TC_GAIN'    # Boost-X = the low-tier of Gain-X (SAME buff status)
        if any(n.startswith(b) for b in EN_NAMES):           return 'TC_ENSPELL'
        if n.startswith('Bar'):                              return 'TC_BARSPELL' # Barrier Tusk is BlueMagic (caught above) -> 'Bar' is safe here
        if any(n.startswith(s) for s in SPIKES):             return 'TC_SPIKES'   # Dread Spikes is Dark (caught above)
        if any(n.startswith(d) for d in DEFENSE):            return 'TC_DEFENSE'
        return 'TC_ENHANCE'                                                       # legit Enhancing with no named family -> catch-all
    return 'TC_OTHER'

def field(line, name):
    m = re.search(r'(?<![a-z_])%s=(\d+)' % name, line)
    return int(m.group(1)) if m else None
def sfield(line, name):
    m = re.search(r'%s="([^"]*)"' % name, line)
    return m.group(1) if m else None

# per job : key -> [minLevel, catEnum, name, status, recast]
#   key = ('S', status) for a buff SPELL (dedups tiers : Refresh I/II/III -> one status row)
#       | ('A', name)   for a job ABILITY (each is distinct ; may be a buff, a recast-only, or both)
# status = the buff status id (0 = no buff, recast-only) ; recast = the recast_id (0 = none).
job_buffs = {}
def put(job, key, jl, cat, name, status, recast):
    if not (1 <= job <= 23):
        return
    d = job_buffs.setdefault(job, {})
    cur = d.get(key)
    if cur is None or jl < cur[0]:               # keep the LOWEST level (and its name/cat) per key
        d[key] = [jl, cat, name, status, recast]
    elif cur[0] == jl and len(name) < len(cur[2]):
        d[key][2] = name                          # prefer the shorter base name on a tie

# --- SPELLS : EVERY spell the job can cast (tracked by buff status AND/OR recast). Keyed by BASE name so tiers
#     merge (Cure I-VI -> one row) while distinct spells sharing a status (BLU Metallic Body / Diamondhide =
#     Stoneskin) stay separate. status = the buff status only for a self/party spell (enemy-target -> debuff -> 0,
#     so a nuke/enfeeble is recast-only). ---
for line in open(os.path.join(RES, 'spells.lua'), encoding='utf-8'):
    lv = re.search(r'levels=\{([^}]*)\}', line)
    if not lv:
        continue
    sptype = sfield(line, 'type') or ''
    if sptype == 'Trust':          # trust-summon "spells" : usable by every job -> pure noise, never tracked
        continue
    if re.search(r'\bunlearnable\s*=\s*true', line):   # e.g. Dispelga : listed at lv99 for many jobs but NOT actually learnable
        continue
    st  = field(line, 'status') or 0
    tg  = field(line, 'targets') or 0
    stt = st if (st > 0 and not (tg & ENEMY)) else 0   # buff status only for a self/party spell
    name = sfield(line, 'en') or '#?'
    base = re.sub(r'\s+(I{1,3}|IV|VI?)$', '', name)    # strip a trailing roman tier
    rc   = field(line, 'recast_id') or 0
    cat  = category(name, sptype, field(line, 'skill') or 0)
    if sptype == 'BlueMagic':                          # sub-categorise blue magic via bluguide (else one huge list)
        sca, nuke = BLU_META.get(field(line, 'id') or 0, (False, False))
        cat = 'TC_BLU_BUFF' if stt else 'TC_BLU_PHYS' if sca else 'TC_BLU_MAG' if nuke else 'TC_BLU_DEB'
    # BUFF spells -> merge tiers by BASE name (Refresh I/II/III share status 43 -> one row ; distinct buffs that
    # merely share a status like BLU Metallic Body / Diamondhide keep their own name -> separate).
    # RECAST-ONLY spells (nukes / cures / enfeebles) -> keep the FULL name : each tier (Fire I..VI) has its OWN
    # recast, so they stay distinct rows -- matching Windower Timers.dll.
    key, disp = (('S', base), base) if stt else (('S', name), name)
    for jm in re.finditer(r'\[(\d+)\]=(\d+)', lv.group(1)):
        job, jl = int(jm.group(1)), int(jm.group(2))
        if 1 <= job <= 23:
            put(job, key, jl, cat, disp, stt, rc)

# ---- ABILITIES pass : a job ability is trackable via its RECAST (cooldown) even with NO buff status
#      (Steal / Mug / Hide...), so we include EVERY ability the job has -- not just the buff ones. Job assigned
#      by (1) your GearSwap per-job modules (name -> folder + level ; also the full JA list), (2) canonical res
#      `type` for the job-defining families (CorsairRoll->COR, BloodPactWard->SMN, Rune/Ward->RUN, Samba/Jig/
#      Flourish/Step->DNC, Scholar->SCH), (3) a tiny gap-fill. Each entry carries its buff status (0 = none) AND
#      its recast_id. Damage Blood Pacts / mob / pet commands are skipped unless GearSwap explicitly lists them. ----
import glob
TYPE_JOB = {'CorsairRoll':'COR','Samba':'DNC','Waltz':'DNC','Jig':'DNC','Step':'DNC',
            'Flourish1':'DNC','Flourish2':'DNC','Flourish3':'DNC','Rune':'RUN','Ward':'RUN','Effusion':'RUN',
            'Scholar':'SCH','BloodPactWard':'SMN'}
TYPE_CAT = {'CorsairRoll':'TC_ROLL','Samba':'TC_SAMBA','Waltz':'TC_DANCE','Jig':'TC_DANCE',
            'Step':'TC_DANCE','Flourish1':'TC_DANCE','Flourish2':'TC_DANCE','Flourish3':'TC_DANCE','Rune':'TC_RUNE',
            'Ward':'TC_WARD','Effusion':'TC_WARD','Scholar':'TC_STRAT','BloodPactWard':'TC_BPACT'}
GAPFILL = {'Apogee':'SMN','Astral Conduit':'SMN','Astral Flow':'SMN','Mana Cede':'SMN',
           "Warrior's Charge":'WAR',"Assassin's Charge":'THF','Dream Shroud':'RUN'}

# parse GearSwap per-job modules : name -> {job abbrev : level}
gs_map = {}
GS = None
for c in [r'D:\Windower Tetsouo\addons\GearSwap\data\shared\data\job_abilities',
          os.path.join(RES, '..', 'addons', 'GearSwap', 'data', 'shared', 'data', 'job_abilities')]:
    if os.path.isdir(c):
        GS = c; break
if GS:
    for path in glob.glob(os.path.join(GS, '*', '*.lua')):
        job = os.path.basename(os.path.dirname(path)).upper()
        if job not in JOBIDX:
            continue
        txt = open(path, encoding='utf-8', errors='ignore').read()
        # keys use EITHER quote style : ['Retaliation'] and ["Dancer's Roll"] (double-quoted when the name has
        # an apostrophe) -- match the opening quote and backref it so both are caught (rolls were missed before).
        for m in re.finditer(r"""\[(['"])(.*?)\1\]\s*=\s*\{(.*?)\n\s{4}\}""", txt, re.S):
            nm = m.group(2); lvm = re.search(r'level\s*=\s*(\d+)', m.group(3))
            gs_map.setdefault(nm, {})[job] = int(lvm.group(1)) if lvm else 1

# res job_abilities.lua recast_id that disagrees with the live client (verified in-game) -> use the client's value,
# else the tracked recast key never matches what the game reports. Keep in sync with gen_actions.py ABIL_RECAST_OVERRIDE.
ABIL_RECAST_OVERRIDE = {}   # empty : the DNC-jig cross-job recast collision is handled in hud_timers (hide phantom slot)
# res abilities : name -> (recast_id, status, targets, type)
res_ab = {}
for line in open(os.path.join(RES, 'job_abilities.lua'), encoding='utf-8'):
    nm = sfield(line, 'en')
    if nm:
        rc_v = ABIL_RECAST_OVERRIDE.get(nm, field(line, 'recast_id') or 0)
        res_ab[nm] = (rc_v, field(line, 'status') or 0,
                      field(line, 'targets') or 0, sfield(line, 'type') or '')

for name, (rc, st, tg, typ) in res_ab.items():
    if typ == 'Monster':
        continue                                              # mob TP move, never a player ability
    if typ in ('BloodPactRage', 'PetCommand') and name not in gs_map:
        continue                                              # damage BPs / pet commands : only if GearSwap lists it
    jobs = dict(gs_map.get(name, {}))                         # GearSwap first -> REAL per-job levels
    if typ in TYPE_JOB:
        jobs.setdefault(TYPE_JOB[typ], 1)                     # canonical type job : add if GearSwap missed it
    if not jobs and name in GAPFILL:
        jobs[GAPFILL[name]] = 1
    if not jobs:
        continue
    stt = st if (st and not (tg & ENEMY)) else 0              # buff status only if self/party ; else recast-only
    cat = TYPE_CAT.get(typ, 'TC_JA')
    for j, l in jobs.items():
        put(JOBIDX[j], ('A', name), l, cat, name, stt, rc)

# ---- GLOBAL buff-family table : ONE row per distinct buff STATUS across ALL jobs, for the job-agnostic
#      (family-organised) Timers filter. Deduped by STATUS ; the family name is the SHORTEST base name that
#      carries that status (Shell V + Shellra V share status 41 -> "Shell"). Recast-only entries (nukes, cures,
#      Steal...) are NOT here -- they stay per-job (a cooldown only ever shows on the job that owns it). ----
# ONE row per (family, status). A buff stays under ITS OWN school : if a status is granted by spells of two
# schools (e.g. status 71 : WHM "Sneak" (Enhancing) AND NIN "Monomi: Ichi" (Ninjutsu)), it appears once under
# EACH, so no family gets emptied by a cross-school "absorption". The runtime filter keys on the STATUS, so the
# two rows share ONE on/off state -- consistent, just listed under both schools where a player would look for it.
fam = {}   # (catEnum, status) -> shortest base name for that (family, status)
for _job, _d in job_buffs.items():
    for _v in _d.values():
        _jl, _cat, _name, _status, _recast = _v
        if _status <= 0:
            continue
        _k = (_cat, _status)
        if _k not in fam or len(_name) < len(fam[_k]):
            fam[_k] = _name

# --- inject non-spell status buffs : Food / Aftermath / conquest (Signet...) / synthesis Imagery. No job CASTS
#     these, so they never appear in spells.lua -- but they DO show up in the Timers 0x063 self-buff list, and until
#     now there was no way to hide them. Adding them here gives each its own family-filter row (one on/off per
#     status, same as every other row), grouped under its own category so a group-chip can toggle the whole set.
#     status ids are static game data (see src/model/buffs_gen.h). ---
EXTRA_FAM = [
    ('TC_FOOD',      251, 'Food'),
    ('TC_AFTERMATH', 270, 'Aftermath: Lv.1'),
    ('TC_AFTERMATH', 271, 'Aftermath: Lv.2'),
    ('TC_AFTERMATH', 272, 'Aftermath: Lv.3'),   # 3 tiers only ; status 273 "Aftermath" (generic) is legacy/unused -> not listed
    ('TC_SIGNET',    253, 'Signet'),
    ('TC_SIGNET',    256, 'Sanction'),
    ('TC_SIGNET',    268, 'Sigil'),
    ('TC_SIGNET',    512, 'Ionis'),
    ('TC_CRAFT',     235, 'Fishing'),
    ('TC_CRAFT',     236, 'Woodworking'),
    ('TC_CRAFT',     237, 'Smithing'),
    ('TC_CRAFT',     238, 'Goldsmithing'),
    ('TC_CRAFT',     239, 'Clothcraft'),
    ('TC_CRAFT',     240, 'Leathercraft'),
    ('TC_CRAFT',     241, 'Bonecraft'),
    ('TC_CRAFT',     242, 'Alchemy'),
    ('TC_CRAFT',     243, 'Cooking'),
]
for _cat, _status, _name in EXTRA_FAM:
    fam.setdefault((_cat, _status), _name)   # setdefault : never clobber a real spell family that already owns this status

def cesc(s):
    return s.replace('\\', '\\\\').replace('"', '\\"')

L = ['// job_track_gen.h -- GENERATED by scripts/gen_job_track.py (res/spells.lua + res/job_abilities.lua + GearSwap).',
     '// Per-JOB list of trackable entries for the Timers "track per job" config checklist :',
     '//   { status, recast, minLevel, category, name }. A BUFF spell has status>0 ; a job ABILITY may have a buff',
     '//   status AND/OR a recast (cooldown) -- Steal/Mug have recast only. Spells are deduped by status.',
     '#pragma once', 'namespace aio {',
     'enum TrackCat { %s, TC_COUNT };' % ', '.join(c[0] for c in CATS),
     'static const char* const TRACK_CAT_EN[TC_COUNT] = { %s };' % ', '.join('"%s"' % c[1] for c in CATS),
     'static const char* const TRACK_CAT_FR[TC_COUNT] = { %s };' % ', '.join('"%s"' % cesc(c[2]) for c in CATS),
     'struct JobBuff { unsigned short status; unsigned short recast; unsigned short level; unsigned char cat; const char* name; };']

total = 0; nbuf = 0; nrec = 0
present = []   # (job, arrayName, count)
for job in range(1, 24):
    d = job_buffs.get(job)
    if not d:
        continue
    rows = sorted(d.values(), key=lambda v: (v[1], 0 if v[3] else 1, v[0], v[2]))   # by cat, then BUFFS-first (status), then level, then name
    arr = 'JT_%s' % JOBS[job]
    L.append('static const JobBuff %s[] = {' % arr)
    for jl, cat, name, status, recast in rows:
        L.append('    {%d,%d,%d,%d,"%s"},   // %s' % (status, recast, jl, CAT_IDX[cat], cesc(name), cat))
        if status: nbuf += 1
        if recast: nrec += 1
    L.append('};')
    present.append((job, arr, len(rows)))
    total += len(rows)

L.append('// GLOBAL buff families (job-agnostic) : one row per distinct buff STATUS, for the family-organised filter.')
L.append('struct BuffFam { unsigned short status; unsigned char cat; const char* name; };')
L.append('static const BuffFam BUFF_FAM[] = {')
for (_cat, _status) in sorted(fam, key=lambda k: (CAT_IDX[k[0]], fam[k].lower())):
    L.append('    {%d,%d,"%s"},   // %s' % (_status, CAT_IDX[_cat], cesc(fam[(_cat, _status)]), _cat))
L.append('};')
L.append('static const int BUFF_FAM_N = (int)(sizeof(BUFF_FAM) / sizeof(BUFF_FAM[0]));')

L.append('struct JobTrack { const JobBuff* buffs; int n; };')
L.append('static const JobTrack JOB_TRACK[24] = {')
for job in range(0, 24):
    hit = next((p for p in present if p[0] == job), None)
    if hit:
        L.append('    {%s, %d},   // %s' % (hit[1], hit[2], JOBS[job]))
    else:
        L.append('    {0, 0},   // %s' % (JOBS[job] if job and JOBS[job] else '-'))
L.append('};')
L.append('inline const JobBuff* job_track(unsigned job, int& n) {')
L.append('    if (job < 1 || job > 23) { n = 0; return 0; }')
L.append('    n = JOB_TRACK[job].n; return JOB_TRACK[job].buffs; }')
L.append('} // namespace aio')
open(OUT, 'w', encoding='utf-8').write('\n'.join(L) + '\n')
print('wrote %s : %d jobs, %d rows (%d with buff status, %d with recast)' % (OUT, len(present), total, nbuf, nrec))
for job, arr, n in present:
    print('  %-3s : %2d entries' % (JOBS[job], n))
from collections import Counter as _Counter
_fc = _Counter(k[0] for k in fam)
print('BUFF_FAM : %d rows (family x status)' % len(fam))
for _c, _n in sorted(_fc.items(), key=lambda x: -x[1]):
    print('  %-12s %2d' % (_c.replace('TC_', ''), _n))
