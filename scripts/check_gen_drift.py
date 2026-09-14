#!/usr/bin/env python3
"""check_gen_drift.py -- do the generators still reproduce the committed src/model/*_gen.h ?

Runs every *_gen.h generator into a TEMP folder (never over the tracked headers), then compares each result with
the header in the tree and prints one line per header :

    SAME        byte-identical once line endings are normalised (git stores LF, the checkout is CRLF)
    DRIFT       the generator now produces something else : +added/-removed lines and the first differing lines
    CANNOT RUN  an input is missing on this machine -- named. Information, not a failure of the check.
    CRASH       the inputs are all there and the generator still failed (or wrote nothing, or wrote over a
                tracked header instead of the temp folder)

Exit code 1 on any DRIFT or CRASH, else 0.

DRIFT has two very different causes and the check cannot tell them apart -- read the diff :
  * Windower's res\\ was updated since the header was generated (new items, renamed mob skills). The generator is
    fine ; regenerating is a data change to review, not a fix.
  * the generator rotted, or someone hand-edited the header. That is the bug this check exists for.

Usage (from anywhere) :
    python scripts/check_gen_drift.py                 # every header
    python scripts/check_gen_drift.py spells trusts   # only headers whose name contains one of the words
    python scripts/check_gen_drift.py --keep          # keep the temp folder (its path is printed) to diff by hand
    python scripts/check_gen_drift.py --lines 12      # show up to 12 differing lines per drifting header (default 6)

Inputs : Windower res\\ from $WINDOWER_RES and addons\\ from $WINDOWER_ADDONS, else the dev install (genpaths.py).
The check resolves them ONCE and hands the same folders to every generator, so "missing" means the same thing to
both. Generators that write a file are redirected with $AIOHUD_GEN_OUT ; the ones that print to stdout are captured.

Banners were checked for things that would drift on every run (a timestamp, an absolute input path) : none of the
21 headers carries one, so only line endings are normalised. A count in a banner (song_dur_gen.h "131 items")
is content -- it moves when the table does.
"""
import difflib, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import genpaths

ROOT, MODEL = genpaths.ROOT, genpaths.MODEL

# (generator, [headers it produces], 'file' | 'stdout', [required inputs])
# An input is (base, relative path) with base 'res' | 'addons' | 'repo'. List EVERY input the output depends on,
# optional ones included : a generator that silently skips a missing optional input (bluguide, GearSwap) still
# produces a DIFFERENT table, and that must read as CANNOT RUN, not as drift.
GENERATORS = [
    ('gen_actions.py',       ['spells_gen.h', 'abilities_gen.h'], 'file',
        [('res', 'spells.lua'), ('res', 'job_abilities.lua')]),
    ('gen_action_status.py', ['action_status_gen.h'], 'file',
        [('res', 'spells.lua'), ('res', 'job_abilities.lua')]),
    ('gen_buff_names.py',    ['buffs_gen.h'], 'file', [('res', 'buffs.lua')]),
    ('gen_enh_dur.py',       ['enh_dur_listed_gen.h'], 'stdout', [('res', 'item_descriptions.lua')]),
    ('gen_geo_dur.py',       ['geo_dur_gen.h'], 'stdout', [('res', 'item_descriptions.lua')]),
    ('gen_itemnames.py',     ['itemnames_gen.h'], 'file', [('res', 'items.lua')]),
    ('gen_job_track.py',     ['job_track_gen.h'], 'file',
        [('res', 'spells.lua'), ('res', 'job_abilities.lua'), ('addons', 'bluguide/res/spellinfo.lua'),
         ('addons', 'GearSwap/data/shared/data/job_abilities')]),
    ('gen_keyitems.py',      ['keyitems_gen.h'], 'file', [('res', 'key_items.lua')]),
    ('gen_mobskills.py',     ['mobskills_gen.h'], 'file', [('res', 'monster_abilities.lua')]),
    ('gen_nms.py',           ['nms_gen.h'], 'file', [('addons', 'AioHUD/vendor/nms/index.lua')]),
    ('gen_overwrites.py',    ['overwrites_gen.h'], 'file', [('res', 'spells.lua')]),
    ('gen_regen_dur.py',     ['regen_dur_gen.h'], 'stdout', [('res', 'item_descriptions.lua')]),
    ('gen_resistances.py',   ['resistances_gen.h'], 'file',
        [('addons', 'sheolhelper/resistances.lua'), ('addons', 'sheolhelper/types.lua'),
         ('addons', 'AioHUD/modules/sheolhelper.lua')]),
    ('gen_skillchain.py',    ['skillchain_gen.h'], 'file',
        [('repo', 'scripts/skills.lua'), ('repo', 'src/model/abilities_gen.h')]),
    ('gen_song_dur.py',      ['song_dur_gen.h'], 'file', [('res', 'item_descriptions.lua'), ('res', 'items.lua')]),
    ('gen_song_family.py',   ['song_family_gen.h'], 'stdout', [('res', 'spells.lua')]),
    ('gen_tb_buffs.py',      ['tb_buff_gen.h'], 'stdout', [('res', 'spells.lua')]),
    ('gen_tb_debuffs.py',    ['tb_debuff_gen.h'], 'stdout', [('addons', 'AioHUD/vendor/targetbar/tb_spells.lua')]),
    ('gen_trusts.py',        ['trusts_gen.h'], 'file', [('res', 'spells.lua'), ('repo', 'src/model/trusts_gen.h')]),
    ('gen_ws.py',            ['weapon_skills_gen.h'], 'file', [('res', 'weapon_skills.lua')]),
]


def norm(data):
    """Bytes -> list of lines with line endings normalised (what git compares, given autocrlf)."""
    return data.replace(b'\r\n', b'\n').decode('utf-8', errors='replace').split('\n')


def snapshot():
    out = {}
    for n in os.listdir(MODEL):
        if n.endswith('_gen.h'):
            with open(os.path.join(MODEL, n), 'rb') as f:
                out[n] = f.read()
    return out


def diff_summary(old, new, nlines):
    d = list(difflib.unified_diff(old, new, lineterm='', n=0))
    body = [l for l in d if not l.startswith(('---', '+++', '@@'))]
    add = sum(1 for l in body if l.startswith('+'))
    rem = sum(1 for l in body if l.startswith('-'))
    shown = ['        ' + (l if len(l) <= 130 else l[:127] + '...') for l in body[:nlines]]
    if len(body) > nlines:
        shown.append('        ... %d more differing line(s)' % (len(body) - nlines))
    return add, rem, shown


def main():
    sys.stdout.reconfigure(errors='replace')   # a diff line must never kill the report on a cp1252 console
    args = sys.argv[1:]
    keep = '--keep' in args
    nlines = 6
    if '--lines' in args:
        nlines = int(args[args.index('--lines') + 1])
        del args[args.index('--lines'):args.index('--lines') + 2]
    words = [a for a in args if not a.startswith('--')]

    bases = {'res': genpaths.res_dir(), 'addons': genpaths.addons_dir(), 'repo': ROOT}
    tmp = tempfile.mkdtemp(prefix='aiohud_gendrift_')
    env = dict(os.environ, WINDOWER_RES=bases['res'], WINDOWER_ADDONS=bases['addons'], AIOHUD_GEN_OUT=tmp,
               PYTHONIOENCODING='utf-8', PYTHONDONTWRITEBYTECODE='1')
    for legacy in ('AIOHUD_RES', 'FFXI_RES', 'AIOHUD_NMS'):   # the canonical folders above must win everywhere
        env.pop(legacy, None)

    print('res    : %s' % bases['res'])
    print('addons : %s' % bases['addons'])
    print('output : %s' % tmp)
    print('')

    before = snapshot()
    counts = {'SAME': 0, 'DRIFT': 0, 'CANNOT RUN': 0, 'CRASH': 0}
    failed = False
    for script, headers, mode, inputs in GENERATORS:
        if words and not any(w in h for h in headers for w in words):
            continue
        missing = [os.path.join(bases[b], p.replace('/', os.sep)) for b, p in inputs
                   if not os.path.exists(os.path.join(bases[b], p.replace('/', os.sep)))]
        if missing:
            for h in headers:
                print('%-22s CANNOT RUN  (%s) missing: %s' % (h, script, '; '.join(missing)))
                counts['CANNOT RUN'] += 1
            continue

        # cwd = the temp folder : a generator that still writes somewhere RELATIVE drops it there, harmlessly.
        r = subprocess.run([sys.executable, os.path.join(HERE, script)], cwd=tmp, env=env, capture_output=True)
        crash = None
        if r.returncode != 0:
            tail = r.stderr.decode('utf-8', 'replace').strip().splitlines()[-1:] or \
                   r.stdout.decode('utf-8', 'replace').strip().splitlines()[-1:]
            crash = 'exit %d: %s' % (r.returncode, tail[0] if tail else '(no output)')
        elif mode == 'stdout':
            with open(os.path.join(tmp, headers[0]), 'wb') as f:
                f.write(r.stdout)

        # a generator that ignored $AIOHUD_GEN_OUT just wrote over the tracked tree : put it back, and say so.
        after = snapshot()
        clobbered = sorted(n for n in before if after.get(n) != before[n])
        for n in clobbered:
            with open(os.path.join(MODEL, n), 'wb') as f:
                f.write(before[n])
        if clobbered and not crash:
            crash = 'wrote over the tracked %s (restored) instead of $AIOHUD_GEN_OUT' % ', '.join(clobbered)

        for h in headers:
            gen = os.path.join(tmp, h)
            why = crash
            if not why and (not os.path.isfile(gen) or os.path.getsize(gen) == 0):
                why = 'exit 0 but produced no %s' % h
            if why:
                print('%-22s CRASH       (%s) %s' % (h, script, why))
                counts['CRASH'] += 1
                failed = True
                continue
            with open(os.path.join(MODEL, h), 'rb') as f:
                old = norm(f.read())
            with open(gen, 'rb') as f:
                new = norm(f.read())
            if old == new:
                print('%-22s SAME' % h)
                counts['SAME'] += 1
            else:
                add, rem, shown = diff_summary(old, new, nlines)
                print('%-22s DRIFT       (%s) +%d -%d line(s)' % (h, script, add, rem))
                for l in shown:
                    print(l)
                counts['DRIFT'] += 1
                failed = True

    print('')
    print('%d SAME, %d DRIFT, %d CANNOT RUN, %d CRASH' % (counts['SAME'], counts['DRIFT'], counts['CANNOT RUN'],
                                                        counts['CRASH']))
    if keep:
        print('generated headers kept in %s' % tmp)
    else:
        shutil.rmtree(tmp, ignore_errors=True)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
