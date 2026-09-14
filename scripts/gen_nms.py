#!/usr/bin/env python3
# gen_nms.py -- generate src/model/nms_gen.h from the Empy Pop Tracker NM data (vendor/nms/*.lua).
#
# The NM pop-chain data is (c) 2020 Dean James (Xurion of Bismarck), BSD 3-Clause -- see the licence header at
# the top of every vendor file, and NOTICE-EmpyPop.txt (shipped in dist\ by package.bat).
#
# The Lua is RECURSIVE :  nm.pops[] -> pop.dropped_from -> .pops[] -> ...  We FLATTEN it into a single POOL of
# pop entries ; each entry addresses its children by an index RANGE into that same pool (childFirst/childCount).
# Everything is static const / compile-time -- no tree of pointers, no runtime init, no heap (CLAUDE.md).
# `dropped_from` is a 1:1 wrapper (a name + an optional pops[]) so it folds into the pop entry itself :
# pop.fromName = dropped_from.name, pop's children = dropped_from.pops.
# DO NOT hand-edit the output.
#
# Run:  python scripts/gen_nms.py             (from the repo root)
#       python scripts/gen_nms.py --nms PATH  (explicit vendor\nms\ folder)
#
# vendor\nms\ resolution (the dev repo lives OUTSIDE Windower, so it cannot be derived from __file__) :
#   1. --nms PATH
#   2. $AIOHUD_NMS
#   3. $WINDOWER_ADDONS\AioHUD\vendor\nms (the addons variable every generator shares, genpaths.py)
#   4. the addons\AioHUD\vendor\nms of the WINDOWER root parsed out of WINDOWER_PLUGINS in deploy.local.bat
#      (the repo's existing untracked per-machine config -- same source deploy.bat uses)
#   5. the dev install, genpaths.DEV_ADDONS\AioHUD\vendor\nms
import re, os, sys
import genpaths

ROOT = genpaths.ROOT
OUT  = genpaths.out_path('nms_gen.h')


def nms_dir():
    argv = sys.argv[1:]
    if '--nms' in argv:
        return argv[argv.index('--nms') + 1]
    if os.environ.get('AIOHUD_NMS'):
        return os.environ['AIOHUD_NMS']
    if os.environ.get('WINDOWER_ADDONS'):
        return os.path.join(os.environ['WINDOWER_ADDONS'], 'AioHUD', 'vendor', 'nms')
    # deploy.local.bat :  set "WINDOWER_PLUGINS=D:\Windower Tetsouo\plugins"  ->  ..\addons\AioHUD\vendor\nms
    local = os.path.join(ROOT, 'deploy.local.bat')
    if os.path.isfile(local):
        with open(local, 'r', encoding='utf-8', errors='replace') as f:
            m = re.search(r'set\s+"?WINDOWER_PLUGINS=([^"\r\n]+)"?', f.read(), re.I)
        if m:
            win = os.path.dirname(m.group(1).rstrip('\\/'))
            return os.path.join(win, 'addons', 'AioHUD', 'vendor', 'nms')
    return os.path.join(genpaths.DEV_ADDONS, 'AioHUD', 'vendor', 'nms')


def cstr(s):
    # Work on UTF-8 BYTES so a multi-byte char keeps its exact bytes (the font's utf8_next decodes 2-byte
    # sequences to Latin-1). A hex escape (\xNN) is followed by "" so the compiler can't greedily merge it with
    # a following hex digit ("\x6a" + "1" would become \x6a1 = out of range) -- the empty string breaks parsing.
    out = []
    for by in s.encode('utf-8'):
        if   by == 0x5C: out.append('\\\\')          # backslash
        elif by == 0x22: out.append('\\"')           # quote
        elif 32 <= by < 127: out.append(chr(by))
        else: out.append('\\x%02x""' % by)           # non-ASCII / control byte : escape + string-break
    return ''.join(out)


# ---------------------------------------------------------------------------------------------------------
# A minimal recursive-descent parser for the Lua table literal these files `return`. Regex can't nest, and a
# silent transcription bug is the whole risk of a data port -- so parse it properly and fail loudly instead.
# Handles only what the vendor data uses: --[[ ]] / -- comments, 'strings', numbers, {k = v, [array items]}.
# ---------------------------------------------------------------------------------------------------------
class LuaParser(object):
    def __init__(self, text, where):
        self.s, self.i, self.where = text, 0, where

    def fail(self, msg):
        line = self.s.count('\n', 0, self.i) + 1
        sys.exit('[gen_nms] %s:%d: %s' % (self.where, line, msg))

    def ws(self):
        while self.i < len(self.s):
            c = self.s[self.i]
            if c in ' \t\r\n':
                self.i += 1
            elif self.s.startswith('--[[', self.i):      # block comment (the licence header)
                e = self.s.find(']]', self.i)
                if e < 0: self.fail('unterminated --[[ comment')
                self.i = e + 2
            elif self.s.startswith('--', self.i):        # line comment (the --Item Name annotations)
                e = self.s.find('\n', self.i)
                self.i = len(self.s) if e < 0 else e + 1
            else:
                return

    def eat(self, tok):
        self.ws()
        if not self.s.startswith(tok, self.i):
            self.fail('expected %r, got %r' % (tok, self.s[self.i:self.i + 20]))
        self.i += len(tok)

    def peek(self, tok):
        self.ws()
        return self.s.startswith(tok, self.i)

    def string(self):
        self.ws()
        q = self.s[self.i]
        if q not in '"\'': self.fail('expected a string')
        self.i += 1
        out = []
        while True:
            if self.i >= len(self.s): self.fail('unterminated string')
            c = self.s[self.i]
            if c == '\\':
                out.append(self.s[self.i + 1]); self.i += 2
            elif c == q:
                self.i += 1; return ''.join(out)
            else:
                out.append(c); self.i += 1

    def number(self):
        self.ws()
        m = re.match(r'-?\d+', self.s[self.i:])
        if not m: self.fail('expected a number')
        self.i += m.end()
        return int(m.group(0))

    def value(self):
        self.ws()
        if self.peek('{'):  return self.table()
        if self.s[self.i] in '"\'': return self.string()
        return self.number()

    def table(self):
        # Returns a dict for k=v tables, a list for array-style tables (the vendor data never mixes the two).
        self.eat('{')
        d, a = {}, []
        while True:
            self.ws()
            if self.peek('}'):
                self.eat('}'); break
            m = re.match(r'([A-Za-z_]\w*)\s*=', self.s[self.i:])
            if m:
                self.i += m.end()
                d[m.group(1)] = self.value()
            else:
                a.append(self.value())
            self.ws()
            if self.peek(',') or self.peek(';'):
                self.i += 1
            elif not self.peek('}'):
                self.fail('expected , or } , got %r' % self.s[self.i:self.i + 20])
        if d and a: self.fail('mixed key/array table -- unexpected in this data')
        return d if d or not a else a

    def parse_return(self):
        self.ws()
        self.eat('return')
        v = self.value()
        self.ws()
        if self.i != len(self.s): self.fail('trailing junk after the returned table')
        return v


def load_index(nd):
    # index.lua :  local nms = { 'alfard', 'amphitrite', ... }   -- the authoritative key set.
    p = os.path.join(nd, 'index.lua')
    if not os.path.isfile(p):
        sys.exit('[gen_nms] vendor\\nms\\index.lua not found at "%s"\n'
                 '          pass --nms PATH, set AIOHUD_NMS or WINDOWER_ADDONS, or create deploy.local.bat' % p)
    with open(p, 'r', encoding='utf-8', errors='replace') as f:
        text = f.read()
    m = re.search(r'local\s+nms\s*=\s*\{(.*?)\}', text, re.S)
    if not m: sys.exit('[gen_nms] could not find the `local nms = {...}` list in index.lua')
    keys = re.findall(r"'([^']+)'", m.group(1))
    if not keys: sys.exit('[gen_nms] parsed 0 keys from index.lua -- format changed?')
    return keys


def main():
    nd   = nms_dir()
    keys = load_index(nd)

    nms = []
    for key in keys:
        p = os.path.join(nd, key + '.lua')
        if not os.path.isfile(p):
            sys.exit('[gen_nms] index.lua lists %r but "%s" does not exist' % (key, p))
        with open(p, 'r', encoding='utf-8', errors='replace') as f:
            t = LuaParser(f.read(), key + '.lua').parse_return()
        if not isinstance(t, dict) or 'name' not in t or 'pops' not in t:
            sys.exit('[gen_nms] %s.lua: expected a table with `name` and `pops`' % key)
        nms.append((key, t))
    nms.sort(key=lambda r: r[0])                       # sorted by key : binary-searchable + stable iteration

    # --- flatten the recursion into one pool ------------------------------------------------------------
    # Children of a node are emitted CONTIGUOUSLY (reserve the range, then recurse) so a parent can address
    # them with a (first,count) pair. Depth-first would interleave siblings and break the range.
    pool  = []                                         # (id, isKI, fromName, childFirst, childCount)
    stats = {'maxdepth': 0, 'notype': []}

    def emit(pops, depth, where):
        stats['maxdepth'] = max(stats['maxdepth'], depth)
        first = len(pool)
        for _ in pops:
            pool.append(None)                          # reserve the contiguous block for this sibling set
        for n, pop in enumerate(pops):
            if not isinstance(pop, dict): sys.exit('[gen_nms] %s: a pops[] entry is not a table' % where)
            for k in ('id', 'dropped_from'):
                if k not in pop: sys.exit('[gen_nms] %s: pop id=%r is missing `%s`' % (where, pop.get('id'), k))
            # 4 pops in the 2020 vendor data omit `type` entirely (apademak 3252, glavoid 2922/2924,
            # orthrus 3237). Upstream's empypop.lua tests `if pop.type == 'key item'`, so a nil type falls
            # through to the item branch -- and all 4 ids resolve to sensible ITEM names. Match that, loudly.
            if 'type' not in pop:
                stats['notype'].append('%s id=%d' % (where, pop['id']))
                pop['type'] = 'item'
            if pop['type'] not in ('item', 'key item'):
                sys.exit('[gen_nms] %s: unknown pop type %r' % (where, pop['type']))
            if not isinstance(pop['id'], int) or not (0 <= pop['id'] <= 65535):
                sys.exit('[gen_nms] %s: bad pop id %r' % (where, pop['id']))
            df = pop['dropped_from']
            if not isinstance(df, dict) or 'name' not in df:
                sys.exit('[gen_nms] %s: pop id=%d has a dropped_from without a `name`' % (where, pop['id']))
            kids = df.get('pops', [])
            if not isinstance(kids, list): sys.exit('[gen_nms] %s: dropped_from.pops is not an array' % where)
            cf, cc = emit(kids, depth + 1, where) if kids else (0, 0)
            pool[first + n] = (pop['id'], pop['type'] == 'key item', df['name'], cf, cc)
        return first, len(pops)

    rows = []
    for (key, t) in nms:
        if not isinstance(t['pops'], list) or not t['pops']:
            sys.exit('[gen_nms] %s.lua: `pops` is empty or not an array' % key)
        pf, pc = emit(t['pops'], 1, key + '.lua')
        coll = t.get('collectable', 0)
        cnt  = t.get('collectable_target_count', 0)
        if coll and not (0 <= coll <= 65535): sys.exit('[gen_nms] %s.lua: bad collectable %r' % (key, coll))
        rows.append((key, t['name'], coll, cnt, pf, pc))

    if any(e is None for e in pool): sys.exit('[gen_nms] internal: a reserved pool slot was never filled')
    if len(pool) > 65535:            sys.exit('[gen_nms] pool exceeds 65535 entries -- widen the index type')

    # --- round-trip check : walk the emitted pool back into a tree and compare to the parsed Lua ---------
    def walk(first, count):
        out = []
        for n in range(first, first + count):
            (pid, ki, nm, cf, cc) = pool[n]
            out.append({'id': pid, 'type': 'key item' if ki else 'item',
                        'dropped_from': ({'name': nm, 'pops': walk(cf, cc)} if cc else {'name': nm})})
        return out
    for (key, t), (k2, _n, _c, _ct, pf, pc) in zip(nms, rows):
        assert key == k2
        if walk(pf, pc) != t['pops']:
            sys.exit('[gen_nms] %s.lua: FLATTEN ROUND-TRIP MISMATCH -- the emitted pool does not rebuild the '
                     'source tree' % key)

    # --- cross-check every id against the real name tables (report, never silently drop) -----------------
    def table_ids(path, arr):
        with open(path, 'r', encoding='utf-8', errors='replace') as f:
            txt = f.read()
        m = re.search(re.escape(arr) + r'\[\]\s*=\s*\{(.*?)\n\};', txt, re.S)
        return set(int(x) for x in re.findall(r'\{(\d+),', m.group(1))) if m else set()
    kis    = table_ids(genpaths.model_path('keyitems_gen.h'),  'KEY_ITEMS')     # the TRACKED tables (inputs)
    items  = table_ids(genpaths.model_path('itemnames_gen.h'), 'ITEM_NAMES')
    unres  = []
    for (pid, ki, nm, _cf, _cc) in pool:
        if pid not in (kis if ki else items):
            unres.append(('key item' if ki else 'item', pid, nm))
    for (key, _n, coll, _ct, _pf, _pc) in rows:
        if coll and coll not in items:
            unres.append(('collectable', coll, key))
    for s in stats['notype']:
        print('[gen_nms] NOTE: %s has no `type` in the vendor data -- treated as an item (upstream semantics)' % s)
    for (kind, pid, nm) in unres:
        print('[gen_nms] UNRESOLVED %-11s id=%-5d (%s)' % (kind, pid, nm))

    # --- emit ------------------------------------------------------------------------------------------
    out = []
    out.append('// nms_gen.h -- GENERATED by scripts/gen_nms.py from the Empy Pop Tracker NM data (vendor/nms/*.lua).')
    out.append('// DO NOT hand-edit ; regenerate via the script.')
    out.append('//')
    out.append('// NM pop-chain data derived from Empy Pop Tracker, (c) 2020 Dean James (Xurion of Bismarck),')
    out.append('// BSD 3-Clause. Full licence text : NOTICE-EmpyPop.txt (shipped alongside the plugin).')
    out.append('//')
    out.append('// The source data is a RECURSIVE tree (pops -> dropped_from -> pops -> ...). It is FLATTENED here into')
    out.append('// one POOL of pop entries : every entry addresses its children as an index RANGE (childFirst/childCount)')
    out.append('// into POPS[] itself. A sibling set is always contiguous. All static const -- zero runtime init, no heap.')
    out.append('//')
    out.append('// Ids only : resolve a name via aio::key_item_name(id) (keyitems_gen.h) when isKI, else aio::item_name(id)')
    out.append('// (itemnames_gen.h). Names are lowercase as they ship in res/ -- title-case at DRAW time, not here.')
    out.append('// `fromName` ("Adamastor, Forced (C-4)") is free text carrying the map position -- not derivable.')
    out.append('#pragma once')
    out.append('')
    out.append('namespace aio {')
    out.append('')
    out.append('// One link in a pop chain : the item/KI you need, and the mob/NPC it drops from.')
    out.append('// childFirst/childCount = the pops needed to force-spawn THAT mob (0 = it is a natural/timed spawn).')
    out.append('struct NmPop {')
    out.append('    unsigned short id;          // key-item id when isKI, else item id')
    out.append('    unsigned char  isKI;        // 1 = key item (Lua type == \'key item\') ; 0 = item')
    out.append('    const char*    fromName;    // free text : mob/NPC + map position')
    out.append('    unsigned short childFirst;  // index into POPS[]')
    out.append('    unsigned short childCount;')
    out.append('};')
    out.append('')
    out.append('struct Nm {')
    out.append('    const char*    key;         // lowercase lookup key ("briareus", "arch dynamis lord")')
    out.append('    const char*    en;          // display name ("Briareus")')
    out.append('    unsigned short collectable; // item id of the collectable, or 0 if the NM has none')
    out.append('    unsigned short collectableTarget; // target count, or 0')
    out.append('    unsigned short popFirst;    // index into POPS[]')
    out.append('    unsigned short popCount;')
    out.append('};')
    out.append('')
    out.append('// The flattened pop pool. Indices are stable ; only nms_gen.h may reference them.')
    out.append('static const NmPop POPS[] = {')
    for n, (pid, ki, nm, cf, cc) in enumerate(pool):
        out.append('    /* %3d */ {%d, %d, "%s", %d, %d},' % (n, pid, 1 if ki else 0, cstr(nm), cf, cc))
    out.append('};')
    out.append('static const int POPS_N = %d;' % len(pool))
    out.append('')
    out.append('// Sorted by key (binary-searched) -- also the stable order the config row_selector iterates.')
    out.append('static const Nm NMS[] = {')
    for (key, en, coll, cnt, pf, pc) in rows:
        out.append('    {"%s", "%s", %d, %d, %d, %d},' % (cstr(key), cstr(en), coll, cnt, pf, pc))
    out.append('};')
    out.append('static const int NMS_N = %d;' % len(rows))
    out.append('')
    out.append('// Case-sensitive lookup by lowercase key ("briareus"), or 0 if unknown.')
    out.append('inline const Nm* nm_by_key(const char* key) {')
    out.append('    if (!key) return 0;')
    out.append('    int lo = 0, hi = NMS_N - 1;')
    out.append('    while (lo <= hi) { const int mid = (lo + hi) >> 1;')
    out.append('        const char* a = NMS[mid].key; const char* b = key;')
    out.append('        while (*a && *a == *b) { ++a; ++b; }')
    out.append('        const int c = (int)(unsigned char)*a - (int)(unsigned char)*b;')
    out.append('        if (c == 0) return &NMS[mid]; if (c < 0) lo = mid + 1; else hi = mid - 1; }')
    out.append('    return 0;')
    out.append('}')
    out.append('')
    out.append('} // namespace aio')
    out.append('')
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out))
    print('[gen_nms] %d NMs, %d pop entries, max depth %d, %d unresolved id(s) -> %s'
          % (len(rows), len(pool), stats['maxdepth'], len(unres), OUT))


if __name__ == '__main__':
    main()
