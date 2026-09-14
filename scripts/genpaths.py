#!/usr/bin/env python3
# genpaths.py -- the ONE place the *_gen.h generators learn where their inputs and their output live.
# Not a generator itself : `import genpaths` from a scripts/gen_*.py (Python puts the script's own folder on
# sys.path, so this resolves from any cwd).
#
# Every generator used to carry its own answer, and they disagreed : WINDOWER_RES here, AIOHUD_RES or FFXI_RES
# there, a bare '..\..\res' that assumed the repo sat inside Windower\plugins\, a hardcoded dev path elsewhere.
# One order now, for all of them :
#   Windower res\    : the generator's own argv form (when it has one)  ->  $WINDOWER_RES     ->  DEV_RES
#   Windower addons\ :                                                   ->  $WINDOWER_ADDONS  ->  DEV_ADDONS
#   output header    : $AIOHUD_GEN_OUT (a DIRECTORY)  ->  <repo>\src\model\   -- check_gen_drift.py sets it to a
#                      temp folder so a drift check never writes over the tracked headers.
# The legacy variable names (AIOHUD_RES, FFXI_RES) are still honoured, AFTER the canonical one.
import os

ROOT       = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODEL      = os.path.join(ROOT, 'src', 'model')
DEV_RES    = r'D:\Windower Tetsouo\res'
DEV_ADDONS = r'D:\Windower Tetsouo\addons'


def _pick(arg, env, legacy, default):
    if arg:
        return arg
    for name in (env,) + tuple(legacy):
        if os.environ.get(name):
            return os.environ[name]
    return default


def res_dir(arg=None, legacy=()):
    """Windower res\\ folder : argv value, else $WINDOWER_RES, else a legacy variable, else the dev install."""
    return _pick(arg, 'WINDOWER_RES', legacy, DEV_RES)


def addons_dir(arg=None, legacy=()):
    """Windower addons\\ folder : argv value, else $WINDOWER_ADDONS, else the dev install."""
    return _pick(arg, 'WINDOWER_ADDONS', legacy, DEV_ADDONS)


def out_path(name):
    """Where a generator WRITES `name` : $AIOHUD_GEN_OUT when set, else the tracked src\\model\\ folder."""
    return os.path.join(os.environ.get('AIOHUD_GEN_OUT') or MODEL, name)


def model_path(name):
    """A tracked header a generator READS as an input (never redirected by $AIOHUD_GEN_OUT)."""
    return os.path.join(MODEL, name)


def rel(path):
    """`path` relative to the repo for a log line ; absolute when it is on another drive (relpath would raise)."""
    try:
        return os.path.relpath(path, ROOT)
    except ValueError:
        return path
