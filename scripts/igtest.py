#!/usr/bin/env python3
"""igtest.py -- drive the in-game test bridge, and cross-check AioHUD against Windower's own view.

WHY. No offline test can prove that an offset is the zone, that the roster is read correctly, or that a buff
list is the real one -- the harness plan says so in as many words, and leaves it to `//aio doctor` plus a human
looking at the screen. But Windower reads the SAME facts through a completely different path (its own memory
layer and packet handlers). So there IS a second witness, and where two independent witnesses can be compared,
a test exists.

HOW. scripts/aiotest/aiotest.lua (a dev-only Windower addon) answers a request file with a snapshot of
Windower's view. This script writes the request, waits for the matching answer, and compares it with what
AioHUD itself just wrote into aiohud_debug.log for the same instant (`//aio doctor`). The assertions live HERE,
not in the addon, so they can be changed and re-run without reloading anything in the game.

USAGE
  python scripts/igtest.py ping
  python scripts/igtest.py snap [--want info,player,party,target,recasts] [--cmd "<game command>"]
  python scripts/igtest.py crosscheck           <- the point of the whole thing
  python scripts/igtest.py selftest             <- prove each check can actually FAIL
  python scripts/igtest.py watch                <- crosscheck in a loop, for a play session

Paths are derived from deploy.local.bat (the same source of truth deploy.bat uses), so this works on any
machine without editing the script.
"""
import os, re, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def windower_root():
    """The Windower folder, from deploy.local.bat -- one source of truth with the deploy script."""
    local = os.path.join(ROOT, 'deploy.local.bat')
    plugins = None
    if os.path.exists(local):
        for line in open(local, encoding='utf-8', errors='replace'):
            m = re.search(r'set\s+"?WINDOWER_PLUGINS=([^"\r\n]+)"?', line, re.I)
            if m:
                plugins = m.group(1).strip()
    if not plugins:
        plugins = r'D:\Windower\plugins'
    return os.path.dirname(plugins)


WROOT = windower_root()
ADDON = os.path.join(WROOT, 'addons', 'aiotest')
REQ = os.path.join(ADDON, 'test_in.txt')
RES = os.path.join(ADDON, 'test_out.txt')
LOG = os.path.join(WROOT, 'plugins', 'aiohud_debug.log')


# ---- the bridge -------------------------------------------------------------------------------------------
def parse_kv(text):
    out = {}
    for line in (text or '').splitlines():
        m = re.match(r'^\s*([\w.]+)\s*=\s*(.*?)\s*$', line)
        if m:
            out[m.group(1)] = m.group(2)
    return out


def ask(want='info,player,party,target', cmds=(), wait_ms=900, timeout=25):
    """Write a request, wait for the answer carrying the same id. Returns the parsed snapshot."""
    rid = str(int(time.time() * 1000))
    lines = ['id=%s' % rid, 'want=%s' % want, 'wait_ms=%d' % wait_ms]
    for i, c in enumerate(cmds, 1):
        lines.append('cmd%d=%s' % (i, c))
    os.makedirs(ADDON, exist_ok=True)
    with open(REQ, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')

    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(RES):
            snap = parse_kv(open(RES, encoding='utf-8', errors='replace').read())
            if snap.get('id') == rid:
                return snap
        time.sleep(0.25)
    raise SystemExit('no answer for request %s within %ds -- is the addon loaded? (//lua load aiotest)' % (rid, timeout))


# ---- AioHUD's own view, from the log it just wrote --------------------------------------------------------
def last_doctor_block():
    """The most recent `//aio doctor` block, parsed into the handful of fields it states as numbers."""
    if not os.path.exists(LOG):
        raise SystemExit('no log at %s' % LOG)
    text = open(LOG, encoding='utf-8', errors='replace').read()
    # The HEADER and the FOOTER of a doctor block both start "=== AIO DOCTOR : " -- the footer is
    # "=== AIO DOCTOR : N problem(s) ===". Taking the last match blindly starts the block AT its own footer and
    # parses nothing, which is how this function first returned only `problems`. Keep the last real header.
    starts = [m.start() for m in re.finditer(r'=== AIO DOCTOR : (?!\d+ problem)', text)]
    if not starts:
        raise SystemExit('no AIO DOCTOR block in the log -- run //aio doctor once')
    block = text[starts[-1]:]
    d = {}
    m = re.search(r'link\s*:\s*inGame=(\d+)\s+selfId=([0-9A-F]+)\s+roster=(\d+)\s+zone=(\d+)\s+job=(\d+)', block)
    if m:
        d['inGame'], d['selfId'], d['roster'], d['zone'], d['job'] = m.groups()
    m = re.search(r'reads\s*:\s*buffsOk=(\d+)\s+nbuff=(\d+)\s+equipValid=(\d+)\s+mapEnt=(\d+)', block)
    if m:
        d['buffsOk'], d['nbuff'], d['equipValid'], d['mapEnt'] = m.groups()
    m = re.search(r'registry\s*:\s*(\d+) module\(s\) checked, (\d+) finding\(s\), periodic watcher (\w+)', block)
    if m:
        d['reg_modules'], d['reg_findings'], d['reg_watcher'] = m.groups()
    m = re.search(r'=== AIO DOCTOR : (\d+) problem\(s\) ===', block)
    if m:
        d['problems'] = m.group(1)
    return d


# ---- the cross-checks ------------------------------------------------------------------------------------
def collect():
    """One measurement : ask AioHUD to report, snapshot Windower at the same moment, pair the facts up."""
    snap = ask(want='info,player,party,recasts', cmds=('aio doctor',), wait_ms=1400)
    doc = last_doctor_block()
    pairs = []

    def chk(name, ours, theirs, why):
        pairs.append((name, ours, theirs, why))

    # 1. THE ZONE. The offset behind this is the one no offline test can vouch for, and a client patch moves it.
    chk('zone', doc.get('zone'), snap.get('info.zone'),
        'AioHUD reads it from game memory ; Windower has its own path')
    # 2. WHO WE ARE. A wrong self id makes every "is this me?" decision wrong -- buff ownership, filters, cache file.
    chk('self id', doc.get('selfId'), snap.get('player.id'),
        'the cache file name and every mine/not-mine filter key on this')
    # 3. THE JOB. Drives the tracked-buff list and the auto-profile.
    chk('main job', doc.get('job'), snap.get('player.main_job_id'), 'drives the tracked list and auto-profiles')
    # 4. THE ROSTER SIZE. AioHUD walks a member array ; Windower asks its own party structure.
    chk('party size', doc.get('roster'), snap.get('party.named'),
        'AioHUD walks the member array, Windower reads its own party table')
    # 5. THE BUFF COUNT. The self buff array (0xFF-terminated u16[32]) against Windower's list.
    chk('buff count', doc.get('nbuff'), snap.get('buffs.n'), 'our 0xFF-terminated u16[32] against their list')
    return pairs, snap, doc


def compare(pairs, perturb=None):
    """Judge the pairs. `perturb` breaks ONE of them on purpose -- see selftest()."""
    rows, fails = [], 0
    for i, (name, ours, theirs, why) in enumerate(pairs):
        t = theirs
        if perturb == i:
            t = (str(theirs) + '_X') if theirs is not None else 'X'   # a value that cannot match
        ok = (ours is not None and t is not None and str(ours) == str(t))
        if not ok:
            fails += 1
        rows.append(('PASS' if ok else 'FAIL', name, str(ours), str(t), why))
    return rows, fails


def selftest():
    """PROVE THE HARNESS CAN FAIL. A green test that has never gone red proves only that it ran.

    This is the recipe's step 3 (docs/audits/plan-harnais-2026-09-09.md) applied to the bridge itself: take ONE
    real measurement, then break each comparison in turn and require it to report FAIL. If a check stays green
    with a value that cannot match, that check is decorative -- which is worse than absent, because it is
    counted as coverage.
    """
    pairs, _, _ = collect()
    rows, fails = compare(pairs)
    print('-- baseline : %d passed, %d failed' % (len(rows) - fails, fails))
    if fails:
        print('   the baseline is not green -- fix that before trusting a mutation result')
        return 1
    bad = 0
    for i, (name, _, _, _) in enumerate(pairs):
        _, f = compare(pairs, perturb=i)
        status = 'bites' if f == 1 else 'DOES NOT BITE'
        if f != 1:
            bad += 1
        print('   %-12s -> %s' % (name, status))
    print('   %d of %d checks report a mismatch as expected' % (len(pairs) - bad, len(pairs)))
    return 1 if bad else 0


def crosscheck(verbose=True):
    """Ask AioHUD to report, snapshot Windower at the same moment, compare. Returns (passed, failed, lines)."""
    pairs, snap, doc = collect()
    rows, fails = compare(pairs)

    if verbose:
        print('-- in-game cross-check : AioHUD (doctor) vs Windower (addon)')
        print('   %-12s %-12s %-12s %s' % ('check', 'AioHUD', 'Windower', ''))
        for st, name, a, b, why in rows:
            print('   [%s] %-10s %-12s %-12s %s' % (st, name, a, b, '' if st == 'PASS' else '<- ' + why))
        extra = []
        for k, label in (('info.logged_in', 'logged_in'), ('player.name', 'name'),
                         ('reg_modules', 'registry modules'), ('reg_watcher', 'periodic watcher'),
                         ('problems', 'doctor problems')):
            v = snap.get(k, doc.get(k))
            if v is not None:
                extra.append('%s=%s' % (label, v))
        print('   context : ' + '  '.join(extra))
        print('   %d passed, %d failed' % (len(rows) - fails, fails))
    return len(rows) - fails, fails, rows


def main():
    what = sys.argv[1] if len(sys.argv) > 1 else 'crosscheck'
    if what == 'ping':
        snap = ask(want='info', wait_ms=200)
        print('bridge alive -- addon %s, zone=%s, logged_in=%s'
              % (snap.get('addon_version'), snap.get('info.zone'), snap.get('info.logged_in')))
    elif what == 'snap':
        want = 'info,player,party,target,recasts'
        cmds = []
        if '--want' in sys.argv:
            want = sys.argv[sys.argv.index('--want') + 1]
        if '--cmd' in sys.argv:
            cmds = [sys.argv[sys.argv.index('--cmd') + 1]]
        snap = ask(want=want, cmds=cmds, wait_ms=1200)
        for k in sorted(snap):
            print('%-28s %s' % (k, snap[k]))
    elif what == 'selftest':
        sys.exit(selftest())
    elif what == 'crosscheck':
        _, fails, _ = crosscheck()
        sys.exit(1 if fails else 0)
    elif what == 'watch':
        n = 0
        while True:
            n += 1
            print('\n=== pass %d ===' % n)
            try:
                crosscheck()
            except SystemExit as e:
                print('   ' + str(e))
            time.sleep(30)
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
