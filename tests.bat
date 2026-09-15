@echo off
REM Offline test suite -> build\tests.exe. Exit code = number of failed checks (0 = all green).
REM
REM No D3D8 device, no real game memory, no Windower host : rendering needs a live device, and no test can prove a
REM memory offset is right. What this DOES run : the pure rules (Timers, focus, the FFXiMain address healers,
REM durations, skillchains, the layout parser...) and the REAL model and Timers builder against a fake game
REM (testsake_game.*) fed bit-exact packets for every handled packet id (testsake_packets.h).
REM
REM Same toolchain resolution as build.bat, on purpose : a suite that builds with a different compiler than the
REM product proves less than it appears to.
setlocal
set "ROOT=%~dp0"

where cl.exe >nul 2>nul && goto :have_cl
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsall.bat"
call "%VCVARS%" x86 >nul
if errorlevel 1 ( echo [tests] vcvars failed & exit /b 1 )
:have_cl

if not exist "%ROOT%build\t" mkdir "%ROOT%build\t"

REM /Od : these are logic tests, not benchmarks -- build speed matters more than the generated code.
REM /fsanitize=address : the suite runs the REAL model (tests\fake_game.cpp), whose tables are fixed arrays walked
REM by counts. Twice on 2026-09-13 a capacity was raised and an array beside it was not, and neither showed as a
REM wrong row -- they wrote past their end, on a static and on the stack. Only the sanitizer turns that into a
REM failure, so it is on for every run, not a mode someone has to remember. /MP : the model is large.
REM Relative inputs from the repository root (see build.bat : the spelled-out paths passed cmd's line limit in a deep
REM checkout). /Fd : the compiler PDB beside the objects -- without it vc140.pdb landed in the repository root.
pushd "%ROOT%"
cl /nologo /MP /EHsc /W4 /WX /permissive- /std:c++17 /Od /Zi /fsanitize=address /wd4456 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS ^
   /I"include" /I"src" /I"tests" ^
   "tests\test_main.cpp" "tests\t_json.cpp" "tests\t_clip.cpp" "tests\t_retry.cpp" "tests\t_skillchain.cpp" "tests\t_durations.cpp" "tests\t_config.cpp" "tests\t_limbus.cpp" "tests\t_omen.cpp" "tests\t_buffgroups.cpp" "tests\t_songslot.cpp" "tests\t_songslots.cpp" "tests\t_flipwatch.cpp" "tests\t_capwatch.cpp" "tests\t_allygroup.cpp" "tests\t_focusrules.cpp" "tests\t_castmatch.cpp" "tests\t_debuffrules.cpp" "tests\t_geardat.cpp" ^
   "tests\t_timers.cpp" "tests\t_timersrules.cpp" "tests\t_roster.cpp" "tests\t_battletarget.cpp" "tests\t_treasure.cpp" "tests\t_pointwatch.cpp" "tests\t_packets.cpp" "tests\t_charsheet.cpp" "tests\t_rvarules.cpp" "tests\fake_game.cpp" ^
   "src\model\skillchain.cpp" "src\model\ui_config.cpp" ^
   "src\model\timers_build.cpp" "src\model\model_clock.cpp" "src\model\flipwatch.cpp" "src\model\capwatch.cpp" "src\model\zones.cpp" "src\model\sentinel.cpp" ^
   "src\model\party_state.cpp" "src\model\party_state_roster.cpp" "src\model\party_state_zonetracker.cpp" "src\model\party_state_pointwatch.cpp" "src\model\party_state_hate.cpp" "src\model\party_state_skillchain.cpp" "src\model\party_state_empypop.cpp" ^
   user32.lib kernel32.lib ^
   /Fo"%ROOT%build\t\\" /Fd"%ROOT%build\t\\" /Fe"%ROOT%build\tests.exe"
set "CLRC=%errorlevel%"
popd
if not "%CLRC%"=="0" ( echo [tests] BUILD FAILED & exit /b 1 )

"%ROOT%build\tests.exe"
REM NOT `if errorlevel 1` : cmd compares that as a SIGNED value, so a crash (0xC00000FD stack overflow,
REM 0xC0000005 access violation) reads as negative and slips through as success. Caught the first time this
REM suite was proved -- neutralising the JSON depth bound crashed the runner and the script printed OK.
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" ( echo [tests] FAILED ^(exit code %RC%^) & exit /b 1 )

REM ---- DEV ONLY : the vigie PAGE. Two self-contained node tests, run here because nothing ran them at all --
REM      they were invoked by hand, which is how a test rots. test_vigie_render.js is NOT here : it needs a
REM      saved /api/state, so it stays a manual step (curl .../api/state > state.json && node ... state.json).
REM      THE EXIT IS AT TOP LEVEL, not inside the else-block : `exit /b 1` from inside a parenthesised block
REM      does not reach the caller, and the first draft printed FAILED while still exiting 0 -- the very same
REM      defect this file was fixed for an hour earlier, in the replay loop. Latch, then exit outside.
set "JSFAIL="
REM      ...and they live in the PRIVATE dev tree, which a clone does not have. A clean worktree caught this:
REM      the step ran, node could not find the file, and the suite failed on a machine that had done nothing
REM      wrong. The replay step above already guards this way ; this one has to as well.
if not exist "%ROOT%dev\scripts\test_vigie_logic.js" (
    echo [tests] vigie page tests : skipped ^(no local dev\scripts^)
    goto :vigiedone
)
where node >nul 2>nul
if errorlevel 1 echo [tests] vigie page tests : skipped ^(no node^)
if not errorlevel 1 for %%J in (test_vigie_logic.js test_vigie_sweepstates.js) do (
    node "%ROOT%dev\scripts\%%J" >"%ROOT%build\vigie_%%~nJ.txt" 2>&1
    if errorlevel 1 (
        type "%ROOT%build\vigie_%%~nJ.txt"
        echo [tests] vigie page test FAILED : %%J
        set "JSFAIL=%%J"
    )
)
if defined JSFAIL (
    echo [tests] FAILED -- vigie page : %JSFAIL%
    exit /b 1
)
if not defined JSFAIL where node >nul 2>nul && echo [tests] vigie page : 2 test^(s^) ok
:vigiedone

REM ---- DEV ONLY : session replays. The replayer and the recorded tapes live in the local dev\ tree, which is not in
REM      the public repository ; a clone without it skips this step and SAYS so.
if not exist "%ROOT%dev\fixtures\tapes\*.aiotape" (
    echo [tests] session replays : skipped ^(no local dev\fixtures\tapes^)
    goto :done
)
call "%ROOT%dev\replay\build_replay.bat" >nul
if errorlevel 1 ( echo [tests] replay BUILD FAILED -- run dev\replay\build_replay.bat & exit /b 1 )
REM `exit /b 1` FROM INSIDE THIS FOR LOOP DOES NOT REACH THE CALLER. Measured 2026-09-15 : a golden was corrupted
REM on purpose, the replay printed "session replay FAILED" and stopped the script -- and tests.bat still exited 0,
REM so every caller (and CI) read a broken replay as a pass. The failure is latched in a variable instead and the
REM script exits AFTER the loop, at top level, where the code is known to propagate. Proved by the same mutation.
set "NREPLAY=0"
set "REPLAYFAIL="
for %%T in ("%ROOT%dev\fixtures\tapes\*.aiotape") do (
    if not exist "%%~dpnT.golden.txt" (
        echo [tests] %%~nxT has no golden -- keep it with dev\scripts\tape.py keep
        set "REPLAYFAIL=%%~nxT"
    ) else (
        "%ROOT%build\replay\replay.exe" "%%T" --mask "%ROOT%build\replay\partystate_mask.bin" --fields "%ROOT%build\replay\partystate_fields.txt" --golden "%%~dpnT.golden.txt" > "%ROOT%build\replay\last_run.txt"
        if errorlevel 1 (
            type "%ROOT%build\replay\last_run.txt"
            echo [tests] session replay FAILED : %%~nxT
            set "REPLAYFAIL=%%~nxT"
        ) else (
            set /a NREPLAY+=1
        )
    )
)
if defined REPLAYFAIL (
    echo [tests] FAILED -- session replay
    exit /b 1
)
echo [tests] session replays : %NREPLAY% tape(s) reproduce their golden

:done
echo [tests] OK
exit /b 0
