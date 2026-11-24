#!/bin/sh
# scripts/guardrails.sh -- the BLOCKING guardrails, ONE copy for CI and for the pre-commit hook.
#
# They lived only in .github/workflows/build.yml, so the pre-commit (tests + check_repo) let through what CI then
# refused after the push -- twice on 2026-11-24 (a flag spelled `abDone`, a field spelled `unloaded`). The same file
# now runs in both places : CI = `bash scripts/guardrails.sh`, the hook = the same line. Exit 1 = a violation.
cd "$(git rev-parse --show-toplevel)" || exit 1
hits="$(git grep -nE 'static bool +[A-Za-z_]*[Tt]ried|[A-Za-z_]*[Tt]ried_? *= *true|if *\( *! *[A-Za-z_]*[Tt]ried_? *\)' -- 'src/*' ':!src/ui/tex_retry.h' \
        | grep -vE ':[0-9]+:[[:space:]]*(//|\*)' | grep -v 'rule10-ok' || true)"
if [ -n "$hits" ]; then printf '%s\n' "$hits"
  echo "::error::one-shot 'tried' latch (rule 10) -- use the bounded retry in ui/tex_retry.h, or justify it inline with 'rule10-ok: <reason>'"
  exit 1; fi
# THE OTHER SPELLINGS. The guard above is indexed on the word "tried" -- and this project also writes the
# same one-shot shape as `done` and `loaded` (gfx/font.cpp, model/ui_config.cpp), which is how the audit
# of 2026-08-06 could still point at latches AFTER this guard had already been widened once, for the `_`
# suffix. A guard that recognises only one synonym is a guard you have to remember, i.e. not a guard.
# Every current hit is a legitimate bounded retry or a plain data field and now carries an inline
# `rule10-ok: <reason>`, so this starts at ZERO and goes straight to blocking, per the rule below.
hits2="$(git grep -nE 'static bool +[A-Za-z_]*([Dd]one|[Ll]oaded)|[A-Za-z_]*([Dd]one|[Ll]oaded)_? *= *true' -- 'src/*' ':!src/ui/tex_retry.h' \
         | grep -vE ':[0-9]+:[[:space:]]*(//|\*)' | grep -v 'rule10-ok' || true)"
if [ -n "$hits2" ]; then printf '%s\n' "$hits2"
  echo "::error::possible one-shot latch spelled done/loaded (rule 10) -- bounded retry, or justify it inline with 'rule10-ok: <reason>'"
  exit 1; fi
echo "rule 10 : clean (tried / done / loaded)"
# NO 64-BIT SIZE IN A debug::log FORMAT. windower::debug::log formats through wvsprintfA, which knows no %ll /
# %I64 : the 64-bit argument is read as 32 bits and every argument after it shifts -- a %s then reads a number
# as a pointer. 2026-11-24 : the new //aio doctor dump line used %llX and CRASHED THE GAME in user32 (the next
# %s read 0x00041C62). Starts at zero, so it blocks ; cast to unsigned and use %08X.
hits3="$(git grep -nE 'debug::log\("[^"]*%[-0-9.+ #]*(ll|I64)' -- 'src/*' 'include/*' || true)"
if [ -n "$hits3" ]; then printf '%s\n' "$hits3"
  echo "::error::%ll / %I64 in a debug::log format -- wvsprintfA has no 64-bit size and it crashes ; cast to unsigned, use %08X"
  exit 1; fi
echo "debug::log formats : no 64-bit size"
# RULE 6 -- widgets read the once-per-frame snapshot ; only the poller touches game memory. Promoted from the
# advisory step on 2026-11-24, the day its count reached zero : Skillchains read the usable WS / JA / BLU lists
# from the client in its draw path (now GameState.wsBits / jaBits / blu). read_gear_icon_bmp and
# read_item_record_head read FILES (gfx/texture.h : a cached BMP, the item DAT), not game memory -- excluded by name.
hits4="$(git grep -nE '\b(read_[a-z_]+|zone_id) *\(' -- 'src/ui/*.cpp' \
         | grep -vE '\b(read_gear_icon_bmp|read_item_record_head) *\(' || true)"
if [ -n "$hits4" ]; then printf '%s\n' "$hits4"
  echo "::error::game-memory read inside src/ui (rule 6) -- the poller fills GameState, the widget reads f.game"
  exit 1; fi
echo "rule 6 : no game-memory read in src/ui"
# THE LAST THREE ADVISORY RULES, promoted 2026-11-24 : each counted zero hits that day (the advisory CI step is gone).
# Rule 9 -- every control passes CTRL_ID ; a hand-picked number silently shares a spring.
hits5="$(git grep -nE 'ease\( *[0-9]' -- 'src/*' || true)"
if [ -n "$hits5" ]; then printf '%s\n' "$hits5"
  echo "::error::hand-picked ease() uid (rule 9) -- use CTRL_ID / ctrl_uid_i"
  exit 1; fi
# windower::debug::log goes through wvsprintfA, which has NO floating point : a %f prints garbage -- on exactly the
# diagnostic capture that mattered, which then reads like the bug not reproducing. Integer math for tenths / percents.
hits6="$(git grep -nE 'debug::log\("[^"]*%[-0-9.+ ]*[fgeFGE][^a-zA-Z]' -- 'src/*' 'include/*' || true)"
if [ -n "$hits6" ]; then printf '%s\n' "$hits6"
  echo "::error::%f in a debug::log format -- wvsprintfA has no floating point, use integer math"
  exit 1; fi
# A generated header with no banner is one somebody will hand-edit (the gen_guard hook reads the banner too).
hits7="$(git grep -L 'GENERATED' -- 'src/model/*_gen.h' || true)"
if [ -n "$hits7" ]; then printf '%s\n' "$hits7"
  echo "::error::generated header without a 'GENERATED' banner"
  exit 1; fi
echo "rule 9, %f in logs, generated banners : clean"
