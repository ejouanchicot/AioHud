"""check_repo.py -- the static checks a commit should not be able to lose.

Three things, each of which HAS gone wrong in this repo and cost real time:

  1. DOC LINKS. A relative markdown link in a tracked doc must point at a file that exists.
     (`song-duration.md` pointed two levels up instead of three for months.)

  2. PROBE COMMANDS. A doc that tells the reader to run `//aio <cmd>` must name a command the
     SHIPPED plugin carries -- or the page must carry the "Sondes citees ici" note saying which of
     them are dev-only, archived, or gone. CLAUDE.md states the rule ("a remedy a doc names must be
     a shipped command"); 106 mentions across 24 pages had drifted away from it.

  3. SPRING UIDS. `ease(<number>, ...)` is forbidden: every control passes CTRL_ID (its file:line),
     or ctrl_uid_i(CTRL_ID, i) inside a loop. Hand-picked numbers collided in practice, which is why
     CTRL_ID exists at all (CLAUDE.md rule 9). Nine had survived in config_page.cpp.

Usage:  python scripts/check_repo.py          -> prints findings, exit 1 if any
        python scripts/check_repo.py --quiet   -> only the summary line
Docs under docs/audits/ are records of a past state, not instructions, and are exempt from (2).
"""
import io, os, re, subprocess, sys, glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
QUIET = '--quiet' in sys.argv
PROBE_NOTE = 'Sondes citees ici'

def read(p):
    return io.open(p, encoding='utf-8', errors='replace').read()

def tracked():
    try:
        out = subprocess.check_output(['git', 'ls-files'], text=True)
    except Exception:
        return []
    return [f for f in out.split('\n') if f]

def verbs(path):
    """The //aio words a command dispatcher answers to."""
    if not os.path.exists(path):
        return set()
    t = read(path)
    v = set(re.findall(r'strstr\(buf,\s*"([a-z0-9_]+)"\)', t))
    v |= set(re.findall(r'aio_verb\(buf,\s*"([a-z0-9_]+)"\)', t))
    v |= set(re.findall(r'tok_arg\(buf,\s*"([a-z0-9_]+)"\)', t))
    return v

# sub-commands and arguments, not commands of their own
ARGS = {'party', 'profile', 'timers', 'out', 'in', 'pop', 'config', 'edit', 'layout', 'update', 'help',
        'demo', 'alliance1', 'alliance2', 'off', 'on', 'reset', 'list', 'save', 'load', 'delete', 'all',
        'hp', 'mp', 'tp', 'lay', 'sim', 'res', 'corners', 'debugging'}

problems = []

# ---- 1. markdown links ------------------------------------------------------------------------
def prose(t):
    """Strip fenced blocks and inline code: a C++ lambda `[](unsigned){...}` is not a markdown link."""
    t = re.sub(r'```.*?```', '', t, flags=re.S)
    return re.sub(r'`[^`\n]*`', '', t)

docs = [f for f in tracked() if f.endswith('.md')]
for d in docs:
    for m in re.finditer(r'\[[^\]]*\]\(([^)#]+?)(?:#[^)]*)?\)', prose(read(d))):
        tgt = m.group(1).strip()
        if tgt.startswith(('http', 'mailto:', '../../releases')):   # last one: GitHub's own relative repo link
            continue
        if not os.path.exists(os.path.normpath(os.path.join(os.path.dirname(d), tgt))):
            problems.append('%s : lien mort -> %s' % (d, tgt))

# ---- 2. //aio commands in living docs ---------------------------------------------------------
shipped = verbs('src/plugin/aiohud.cpp')
living = [f for f in docs if f.startswith('docs/game-data/') or f.startswith('docs/songs/')]
for d in living:
    t = read(d)
    named = set(m.group(1) for m in re.finditer(r'//aio\s+([a-z0-9]+)', t))
    unshipped = sorted(c for c in named if c not in ARGS and c not in shipped)
    if unshipped and PROBE_NOTE not in t:
        problems.append('%s : cite %s, absente(s) du plugin livre, sans la note "%s"'
                        % (d, ', '.join('//aio ' + c for c in unshipped), PROBE_NOTE))

# ---- 3. hand-picked spring uids ---------------------------------------------------------------
for f in tracked():
    if not f.startswith('src/') or not f.endswith(('.cpp', '.h')):
        continue
    for i, ln in enumerate(read(f).split('\n'), 1):
        if re.search(r'\bease\(\s*[0-9]', ln):
            problems.append('%s:%d : ease() avec un uid en dur -- passer CTRL_ID '
                            '(ou ctrl_uid_i(CTRL_ID, i) dans une boucle)' % (f, i))

if problems:
    if not QUIET:
        for p in problems:
            print('  %s' % p)
    print('check_repo: %d probleme(s)' % len(problems))
    sys.exit(1)
print('check_repo: OK (%d docs, liens + commandes + uids)' % len(docs))
