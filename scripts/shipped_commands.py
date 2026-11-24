"""The //aio commands that SHIP, read from the one table that dispatches them (src/plugin/aio_verbs.h).

    python scripts/shipped_commands.py            print the shipped words, in table order
    python scripts/shipped_commands.py --check    exit 1 if .claude/rules/shipped-commands.md misses one of them
                                                  (or names, as shipped, a word the table marks DEV) -- skipped,
                                                  and said, when the rule file is not in this checkout

The rule "a remedy a message or a doc names must be a shipped command" needs a list of what ships ; it used to be
re-counted by hand against a release DLL. The dispatcher's table is now where "shipped" is written, once.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, 'src', 'plugin', 'aio_verbs.h')
RULE = os.path.join(ROOT, '.claude', 'rules', 'shipped-commands.md')


def table():
    src = open(HEADER, encoding='utf-8').read()
    body = src[src.index('static const Spec TABLE[]'):]
    body = body[:body.index('};')]
    rows = re.findall(r'\{\s*"(\w+)",\s*V_\w+,\s*(SHIPPED|DEV)\s*\}', body)
    if not rows:
        sys.exit('shipped_commands: no row found in %s -- the table format changed' % HEADER)
    return rows


def main():
    rows = table()
    shipped = [w for w, f in rows if f == 'SHIPPED']
    dev = [w for w, f in rows if f == 'DEV']
    if '--check' not in sys.argv:
        print(' '.join('`%s`' % w for w in shipped))
        return 0
    if not os.path.exists(RULE):
        print('shipped_commands: %s not in this checkout -- check skipped' % RULE)
        return 0
    rule = open(RULE, encoding='utf-8').read()
    # the paragraph that lists them : the one naming `//aio help`
    para = next((p for p in rule.split('\n\n') if '`//aio help`' in p), '')
    words = set(re.findall(r'`(?://aio )?(\w+)`', para))
    missing = [w for w in shipped if w not in words]
    wrong = [w for w in dev if w in words]
    if missing or wrong:
        if missing: print('shipped but not listed in the rule :', ' '.join(missing))
        if wrong:   print('listed as shipped but DEV in the table :', ' '.join(wrong))
        return 1
    print('shipped_commands: the rule lists all %d shipped words' % len(shipped))
    return 0


if __name__ == '__main__':
    sys.exit(main())
