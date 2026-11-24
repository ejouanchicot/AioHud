#!/usr/bin/env python3
"""build_inc.py -- INCREMENTAL build of build\\AioHud.dll : one .obj per source, recompiled only when the source, a
project header it includes (/showIncludes), the flags or the compiler changed ; then one link. build.bat stays the
FULL build (CI, package.bat, releases) ; this is the edit-loop build.

  python scripts/build_inc.py [-j N] [--full] [--why] [--dry]

  --full   recompile everything (fresh object cache)      --why  say why each source is rebuilt
  --dry    list what would be rebuilt, build nothing

Same compiler flags as build.bat's `cl` line, in the same DEV shape (probes / dev tools / theme kit when their
sources exist ; AIOHUD_NO_DEVTOOLS=1 drops them). One deliberate difference : /Z7 (debug info inside each .obj)
instead of /Zi, because N parallel cl processes cannot share one compiler PDB ; the linker still writes
build\\AioHud.pdb, and /DEBUG /OPT:REF /OPT:ICF /PDBALTPATH match build.bat, so the DLL has the same shape.

SOURCES COME FROM GIT, not from a hand-typed list : `git ls-files src/*.cpp` (+ the dev tree's `git ls-files src/*.cpp`
+ the untracked dev-only files build.bat adds by `if exist`). Untracked = not compiled, as in a clean clone. Each run
compares that list with build.bat's and SAYS SO when they differ (the two builds would then not make the same DLL).

Objects and the dependency database : build\\inc\\ (git-ignored with build\\).
"""
import concurrent.futures as cf, hashlib, json, os, re, subprocess, sys, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
OUT = os.path.join(ROOT, 'build')
INC = os.path.join(OUT, 'inc')
OBJ = os.path.join(INC, 'obj')
DB = os.path.join(INC, 'deps.json')
ENVCACHE = os.path.join(INC, 'msvc_env.json')
VSWHERE = os.path.expandvars(r'%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe')
FALLBACK = r'C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
INCLUDE_LINE = re.compile(r'^[^\\/]*:\s+([A-Za-z]:[\\/].*?)\s*$')   # "Note: including file:   C:\..." (any language)


def say(msg):
    print('[build_inc] ' + msg, flush=True)


# ---------------------------------------------------------------- toolchain
def find_tool(env, exe):
    for d in env.get('Path', env.get('PATH', '')).split(';'):
        if d and os.path.isfile(os.path.join(d, exe)):
            return os.path.join(d, exe)
    return None


def msvc_env():
    try:
        env = json.load(open(ENVCACHE, encoding='utf-8'))
        if find_tool(env, 'cl.exe') and find_tool(env, 'link.exe'):
            return env
    except (OSError, ValueError):
        pass
    vc = FALLBACK
    if os.path.isfile(VSWHERE):
        r = subprocess.run([VSWHERE, '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                            '-property', 'installationPath'], capture_output=True, text=True)
        if r.stdout.strip():
            vc = os.path.join(r.stdout.strip(), r'VC\Auxiliary\Build\vcvarsall.bat')
    out = subprocess.run('call "%s" x86 >nul && set' % vc, shell=True, capture_output=True, text=True).stdout
    env = dict(l.split('=', 1) for l in out.splitlines() if '=' in l)
    if not find_tool(env, 'cl.exe'):
        sys.exit('[build_inc] no MSVC x86 toolchain (vcvarsall %s)' % vc)
    os.makedirs(INC, exist_ok=True)
    json.dump(env, open(ENVCACHE, 'w', encoding='utf-8'))
    return env


# ---------------------------------------------------------------- what build.bat builds
def dev_shape():
    if os.environ.get('AIOHUD_NO_DEVTOOLS'):
        return [], []
    srcs, defs = [], []
    if os.path.exists(os.path.join(ROOT, r'src\plugin\aiohud_probes.cpp')):
        srcs.append(r'src\plugin\aiohud_probes.cpp'); defs.append('/DAIOHUD_PROBES')
    if os.path.exists(os.path.join(ROOT, r'dev\src\aiohud_devtools.cpp')):
        r = subprocess.run(['git', '-C', os.path.join(ROOT, 'dev'), 'ls-files', 'src/*.cpp'], capture_output=True, text=True)
        srcs += ['dev\\' + p.replace('/', '\\') for p in r.stdout.split()]
        defs += ['/DAIOHUD_DEVTOOLS', r'/Idev\src']
    if all(os.path.exists(os.path.join(ROOT, 'src', 'ui', f)) for f in ('theme_kit.cpp', 'frame_bake.cpp')):
        srcs += [r'src\ui\theme_kit.cpp', r'src\ui\frame_bake.cpp']; defs.append('/DAIOHUD_THEMEKIT')
    return srcs, defs


def sources():
    r = subprocess.run(['git', 'ls-files', 'src/*.cpp'], cwd=ROOT, capture_output=True, text=True, check=True)
    tracked = [p.replace('/', '\\') for p in r.stdout.split()]
    extra, defs = dev_shape()
    return tracked + [s for s in extra if s not in tracked], defs, tracked


def bat_list_drift(tracked):
    """build.bat's hand-typed list vs git : a difference means the two builds do not make the same DLL."""
    text = open(os.path.join(ROOT, 'build.bat'), encoding='utf-8', errors='replace').read()
    dev_only = {r'src\plugin\aiohud_probes.cpp', r'src\ui\theme_kit.cpp', r'src\ui\frame_bake.cpp'}   # `if exist` in build.bat
    bat = set(re.findall(r'"(src\\[^"%]+\.cpp)"', text)) - dev_only
    git = set(tracked)
    return sorted(git - bat), sorted(bat - git)


def flags(defs):
    ver = os.environ.get('AIOHUD_VERSION', 'dev')
    return ['/nologo', '/c', '/O2', '/MT', '/EHsc-', '/utf-8', '/W4', '/WX', '/permissive-', '/std:c++17', '/wd4456',
            '/Z7', '/D_CRT_SECURE_NO_WARNINGS', '/DAIOHUD_VERSION="%s"' % ver] + defs + ['/Iinclude', '/Isrc', '/showIncludes']


def objname(src):
    return os.path.join(OBJ, src.replace('\\', '_').replace('/', '_')[:-4] + '.obj')


# ---------------------------------------------------------------- compile / link
def compile_one(cl, fl, env, src):
    t = time.perf_counter()
    r = subprocess.run([cl] + fl + ['/Fo' + objname(src), src], cwd=ROOT, env=env, capture_output=True,
                       encoding='oem', errors='replace')
    deps, msgs = [], []
    for l in (r.stdout + r.stderr).splitlines():
        m = INCLUDE_LINE.match(l)
        if m:
            p = os.path.normcase(os.path.abspath(m.group(1)))
            if p.startswith(os.path.normcase(ROOT)):          # project headers only ; the toolchain is in the key
                deps.append(os.path.relpath(p, ROOT))
        elif l.strip() and l.strip() != os.path.basename(src):
            msgs.append(l)
    return src, r.returncode, sorted(set(deps)), msgs, time.perf_counter() - t


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    a = sys.argv[1:]
    jobs = int(a[a.index('-j') + 1]) if '-j' in a else (os.cpu_count() or 4)
    t0 = time.perf_counter()
    env = msvc_env()
    cl, link = find_tool(env, 'cl.exe'), find_tool(env, 'link.exe')
    srcs, defs, tracked = sources()
    only_git, only_bat = bat_list_drift(tracked)
    if only_git or only_bat:
        say('WARNING build.bat and git disagree -- the full build would NOT make this DLL :')
        for p in only_git: say('  tracked, missing from build.bat : ' + p)
        for p in only_bat: say('  in build.bat, not tracked       : ' + p)
    fl = flags(defs)
    key = hashlib.sha1((' '.join(fl) + '|' + cl + '|' + str(os.path.getmtime(cl))).encode()).hexdigest()
    db = {}
    if '--full' not in a:
        try:
            db = json.load(open(DB, encoding='utf-8'))
        except (OSError, ValueError):
            db = {}
    if db.get('key') != key:
        if db:
            say('flags or compiler changed -> everything recompiles')
        db = {'key': key, 'deps': {}}
    os.makedirs(OBJ, exist_ok=True)

    stale, why = [], {}
    mt = {}
    def mtime(p):
        if p not in mt:
            try: mt[p] = os.path.getmtime(os.path.join(ROOT, p))
            except OSError: mt[p] = float('inf')    # a vanished header : rebuild
        return mt[p]
    for s in srcs:
        o = objname(s)
        if not os.path.isfile(o) or s not in db['deps']:
            stale.append(s); why[s] = 'no object'; continue
        ot = os.path.getmtime(o)
        newer = [d for d in [s] + db['deps'][s] if mtime(d) > ot]
        if newer:
            stale.append(s); why[s] = 'newer : ' + ', '.join(newer[:3]) + (' (+%d)' % (len(newer) - 3) if len(newer) > 3 else '')
    say('%d sources (%d from git), %d to compile, -j %d' % (len(srcs), len(tracked), len(stale), jobs))
    if '--why' in a or '--dry' in a:
        for s in stale: say('  %-40s %s' % (s, why[s]))
    if '--dry' in a:
        return 0

    failed = []
    if stale:
        tc = time.perf_counter()
        with cf.ThreadPoolExecutor(jobs) as ex:
            for src, rc, deps, msgs, dt in ex.map(lambda s: compile_one(cl, fl, env, s), stale):
                if msgs:
                    print('\n'.join(msgs))
                if rc == 0:
                    db['deps'][src] = deps
                else:
                    failed.append(src); db['deps'].pop(src, None)
                    try: os.remove(objname(src))   # never link a stale object for a source that failed
                    except OSError: pass
        json.dump(db, open(DB, 'w', encoding='utf-8'), indent=0)
        say('compiled %d in %.1f s' % (len(stale), time.perf_counter() - tc))
    if failed:
        say('FAILED -- %d source(s) do not compile : %s (DLL NOT updated)' % (len(failed), ', '.join(failed)))
        return 1

    dll = os.path.join(OUT, 'AioHud.dll')
    if not stale and os.path.isfile(dll) and db.get('linked') == key and \
            os.path.getmtime(dll) >= max(os.path.getmtime(objname(s)) for s in srcs) and db.get('srcset') == sorted(srcs):
        st = os.stat(dll)
        say('up to date (%.1f s) -> %s (%s, %d bytes)' % (time.perf_counter() - t0, dll, time.strftime('%H:%M:%S', time.localtime(st.st_mtime)), st.st_size))
        return 0

    # version resource (same rule as build.bat : delete first, trust rc's exit code)
    res = os.path.join(INC, 'aiohud.res')
    ver = os.environ.get('AIOHUD_VERSION', 'dev')
    vm = re.match(r'^(\d+)\.(\d+)\.(\d+)$', ver)
    maj, mnr, pat = vm.groups() if vm else ('0', '0', '0')
    rcx = find_tool(env, 'rc.exe')
    resargs = []
    if rcx:
        if os.path.exists(res): os.remove(res)
        r = subprocess.run([rcx, '/nologo', '/fo', res, '/dAIO_VMAJ=' + maj, '/dAIO_VMIN=' + mnr, '/dAIO_VPAT=' + pat,
                            r'src\plugin\aiohud.rc'], cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
        if r.returncode == 0: resargs = [res]
        else: say('WARNING rc.exe failed -- DLL will carry version 0.0.0.0')

    tl = time.perf_counter()
    db['linked'] = None                      # a link that fails half-way must not read as "up to date" next run
    json.dump(db, open(DB, 'w', encoding='utf-8'), indent=0)
    r = subprocess.run([link, '/nologo', '/DLL', '/MACHINE:X86', r'/DEF:src\plugin\aiohud.def'] + [objname(s) for s in srcs] + resargs +
                       ['user32.lib', 'kernel32.lib', 'gdi32.lib', '/OUT:' + dll, '/DEBUG', '/OPT:REF', '/OPT:ICF',
                        '/PDB:' + os.path.join(OUT, 'AioHud.pdb'), '/PDBALTPATH:%_PDB%', '/IMPLIB:' + os.path.join(INC, 'AioHud.lib')],
                       cwd=ROOT, env=env, capture_output=True, encoding='oem', errors='replace')
    out = [l for l in (r.stdout + r.stderr).splitlines() if l.strip() and not l.rstrip().endswith('AioHud.exp')]   # "Creating library ... AioHud.exp", any language
    if out: print('\n'.join(out))
    if r.returncode != 0:
        say('FAILED -- link error (DLL NOT updated)')
        return 1
    db['linked'], db['srcset'] = key, sorted(srcs)
    json.dump(db, open(DB, 'w', encoding='utf-8'), indent=0)
    st = os.stat(dll)
    say('link %.1f s ; total %.1f s -> %s (%s, %d bytes)' % (time.perf_counter() - tl, time.perf_counter() - t0, dll,
        time.strftime('%H:%M:%S', time.localtime(st.st_mtime)), st.st_size))
    return 0


if __name__ == '__main__':
    sys.exit(main())
