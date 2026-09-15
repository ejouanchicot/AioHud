@echo off
REM Build the AioHUD plugin (clean layered src\ tree) -> build\AioHud.dll  (32-bit, MSVC).
REM AIOHUD_OUT=<dir> writes the DLL and its objects there instead (package.bat : build\release, so a package never
REM overwrites the dev DLL that deploy.bat copies).
REM Layers:  src\gfx (D3D backend)  src\ui (widgets + HUD)  src\model  src\plugin (IPlugin glue)
REM Iterate:  //unload AioHud  ->  deploy.bat  ->  //load AioHud
setlocal
set "ROOT=%~dp0"
REM --- x86 MSVC toolchain. If cl is already on PATH (a developer prompt) -> skip. Else locate VS with vswhere
REM     (any installed VS, no hardcoded version), falling back to the pinned local VS2017 BuildTools path.
where cl.exe >nul 2>nul && goto :have_cl
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsall.bat"
call "%VCVARS%" x86 >nul
if errorlevel 1 ( echo [build] vcvars failed & exit /b 1 )
:have_cl
if not defined AIOHUD_VERSION set "AIOHUD_VERSION=dev"

REM Dev-only diagnostic probes : compiled in ONLY when the (git-ignored, local) file is present. Public / CI builds
REM (no aiohud_probes.cpp) compile without them -- the call sites in aiohud.cpp are #ifdef AIOHUD_PROBES.
REM AIOHUD_NO_DEVTOOLS=1 (the release shape, package.bat) leaves them out too : it used to drop only dev\, so a
REM package made on a dev machine still carried every probe string and command -- not the release CI builds (2026-09-14).
set "PROBES="
set "PROBEDEF="
if not defined AIOHUD_NO_DEVTOOLS if exist "%ROOT%src\plugin\aiohud_probes.cpp" ( set "PROBES=src\plugin\aiohud_probes.cpp" & set "PROBEDEF=/DAIOHUD_PROBES" )

REM Dev-only TOOLS (session recorder, whole-model dump for the in-game bridge) : compiled in ONLY when the local dev\
REM tree exists -- it is not in the public repository, so CI and every release build without them. Set
REM AIOHUD_NO_DEVTOOLS=1 to build the release shape on a dev machine.
set "DEVSRC="
set "DEVDEF="
if not defined AIOHUD_NO_DEVTOOLS if exist "%ROOT%dev\src\aiohud_devtools.cpp" set DEVSRC="dev\src\aiohud_devtools.cpp" "dev\src\tape_recorder.cpp" "dev\src\igstate.cpp" "dev\src\jobscan.cpp"
if defined DEVSRC set DEVDEF=/DAIOHUD_DEVTOOLS /I"dev\src"

set "OUT=%ROOT%build"
if defined AIOHUD_OUT set "OUT=%AIOHUD_OUT%"
if not exist "%OUT%" mkdir "%OUT%"

REM --- version resource : parse AIOHUD_VERSION ("MAJ.MIN.PAT") and compile aiohud.rc so the DLL carries a REAL
REM     file version (Windower prints it at load, instead of 0.0.0.0). Non-numeric ("dev") -> 0.0.0. Skipped
REM     gracefully if rc.exe isn't on PATH (build still succeeds, just without the version stamp).
set "VMAJ=0" & set "VMIN=0" & set "VPAT=0"
echo %AIOHUD_VERSION%| findstr /r "^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$" >nul && for /f "tokens=1-3 delims=." %%a in ("%AIOHUD_VERSION%") do (set "VMAJ=%%a" & set "VMIN=%%b" & set "VPAT=%%c")
REM DELETE FIRST, then trust rc's EXIT CODE -- not the file's existence. This tested `if exist` on the output,
REM so a FAILED rc left the previous build's .res sitting there and it was linked anyway : the DLL then carried
REM the version of whatever was built last, which is the single most misleading thing a binary can lie about
REM (a tester reports against v1.0.70 while running v1.0.68). It is the same lesson stated twenty lines below
REM for cl, where the exit code is trusted precisely so a stale DLL cannot mask a failed compile.
set "AIORES="
del /q "%OUT%\aiohud.res" 2>nul
where rc.exe >nul 2>nul && (
    rc /nologo /fo "%OUT%\aiohud.res" /dAIO_VMAJ=%VMAJ% /dAIO_VMIN=%VMIN% /dAIO_VPAT=%VPAT% "%ROOT%src\plugin\aiohud.rc" >nul
    if errorlevel 1 ( echo [build] WARNING: rc.exe failed -- DLL will carry version 0.0.0.0 ) else ( set AIORES="%OUT%\aiohud.res" )
)

REM /W4 /permissive- : high warnings + strict conformance (catches shadowing, dead code, bad conversions).
REM /std:c++17 : pin the standard (VS2017). _CRT_SECURE_NO_WARNINGS : silence MSVC's C4996 nags on sprintf/sscanf
REM -- reviewed safe here (fixed buffers ; the string ops already use bounded _snprintf/lstrcpynA ; sscanf is numeric).
REM
REM /WX : warnings ARE errors. Only worth turning on because the tree builds at ZERO warnings (2026-08-06).
REM Before that day it emitted 39 permanent ones, which is the state in which a NEW warning is invisible --
REM and that is exactly how a real rendering bug shipped once (C4244, a pixel width truncated to an integer
REM by a stray u32 in a declaration list, sitting unread among 41 others).
REM
REM /wd4456 : local-scope shadowing. KEPT, but the old justification was only half true and is worth stating
REM properly, because it was measured on 2026-08-06 -- the flag was hiding 41 warnings, in TWO different
REM populations:
REM   * 21 in model\ui_config.cpp, which were NOT idiomatic at all : a leftover shared parse scratch declared
REM     `float x, y, s` while nine per-key branches correctly declare their own. In a config PARSER, picking up
REM     the wrong `x` writes one module's coordinate into another's. Those are FIXED (the shared names are now
REM     gx/gy/gsc/gps), not muted -- and the collision had also been suppressing C4101 on the dead leftovers,
REM     since MSVC does not report an unused variable whose name is redeclared in a nested scope.
REM   * 20 in the drawing code (party/player/target/minimap/config_page + two nested `for (int i` counters),
REM     which ARE the immediate-mode idiom : each is confined to one small per-block geometry scope.
REM Those 20 stay suppressed deliberately. Renaming them buys no correctness and carries a real hazard: if one
REM use inside the block is missed, it silently resolves to the OUTER variable and still compiles -- the
REM compiler cannot catch it. C4457 (shadows a PARAMETER) is the dangerous half and is kept ON.
REM RELATIVE source paths, from the repository root. With %ROOT% spelled out on every file the expanded command passed
REM cmd's 8191-character limit as soon as the checkout sat in a deep folder (an agent worktree, 2026-09-14) and cl
REM failed with the cryptic "cannot open '^.obj'" : the line was cut mid-continuation. Relative paths also keep the
REM developer's folder out of __FILE__ in the shipped DLL. Outputs stay absolute (%OUT%).
REM ---- DEBUG SYMBOLS (/Zi + /DEBUG) : a crash dump from a tester is unreadable without them -------------------
REM The shipped DLL had NO PDB at all, anywhere, so no minidump could ever be symbolised -- and a crash report is
REM exactly the case where reading the code is not enough. /Zi produces them ; the DLL keeps its /O2 /MT release
REM shape, and the PDB is archived beside it, never inside the payload (package.bat ships plugins\AioHud.dll only,
REM and *.pdb is gitignored).
REM /OPT:REF /OPT:ICF ARE LOAD-BEARING HERE. The linker enables both by DEFAULT in a release link, and /DEBUG turns
REM that default OFF -- so adding /DEBUG alone would silently stop folding identical code and stripping unreferenced
REM functions : a bigger, different binary than the one that was tested. Re-stating them keeps the output what it was.
REM /PDBALTPATH:%%_PDB%% writes just "AioHud.pdb" into the DLL debug directory instead of this machine full path --
REM same reason the sources are passed relative (keep the developer folder out of the shipped file).
REM MATCHING A DUMP TO ITS PDB IS BY SIGNATURE (GUID + age), never by name or version : a PDB from another build
REM with the same file name is silently refused by the debugger, which is the correct behaviour.
pushd "%ROOT%"
cl /nologo /LD /O2 /MT /EHsc- /utf-8 /W4 /WX /permissive- /std:c++17 /wd4456 /Zi /Fd"%OUT%\AioHud.compiler.pdb" /D_CRT_SECURE_NO_WARNINGS /DAIOHUD_VERSION=\"%AIOHUD_VERSION%\" %PROBEDEF% %DEVDEF% /I"include" /I"src" ^
   "src\gfx\noise.cpp" "src\gfx\draw.cpp" "src\gfx\corner_mask.cpp" "src\gfx\texture.cpp" "src\gfx\font.cpp" "src\gfx\window.cpp" ^
   "src\model\layout.cpp" "src\model\model_clock.cpp" "src\model\model_io.cpp" "src\model\timers_build.cpp" "src\model\party_state.cpp" "src\model\party_state_zonetracker.cpp" "src\model\party_state_pointwatch.cpp" "src\model\party_state_hate.cpp" "src\model\party_state_skillchain.cpp" "src\model\party_state_roster.cpp" "src\model\party_state_empypop.cpp" "src\model\game_mem.cpp" "src\model\ffximain_rva.cpp" "src\model\luacore_root.cpp" "src\model\sentinel.cpp" "src\model\selftest.cpp" "src\model\flipwatch.cpp" "src\model\capwatch.cpp" "src\model\watchdogs.cpp" "src\model\decisions.cpp" "src\model\map_dat.cpp" "src\model\icon_dat.cpp" "src\model\zones.cpp" "src\model\vana_clock.cpp" "src\model\paths.cpp" "src\model\ui_config.cpp" "src\model\skillchain.cpp" "src\model\resistances.cpp" ^
   "src\ui\buff_atlas.cpp" "src\ui\palette.cpp" "src\ui\edit_box.cpp" "src\ui\liquid_bars.cpp" "src\ui\player.cpp" "src\ui\gear_canary.cpp" "src\ui\party.cpp" "src\ui\party_gauges.cpp" "src\ui\target.cpp" "src\ui\minimap.cpp" "src\ui\factory.cpp" "src\ui\config_controls.cpp" "src\ui\party_config.cpp" "src\ui\target_config.cpp" "src\ui\player_config.cpp" "src\ui\minimap_config.cpp" "src\ui\ws_config.cpp" "src\ui\sc_config.cpp" "src\ui\tp_config.cpp" "src\ui\hl_config.cpp" "src\ui\pw_config.cpp" "src\ui\grim_config.cpp" "src\ui\zt_config.cpp" "src\ui\tm_config.cpp" "src\ui\ep_config.cpp" "src\ui\box_style.cpp" "src\ui\config_page.cpp" "src\ui\hud.cpp" "src\ui\hud_preview.cpp" "src\ui\doctor_html.cpp" ^
   "src\ui\hud_skillchains.cpp" "src\ui\hud_treasure.cpp" "src\ui\hud_hatelist.cpp" "src\ui\hud_pointwatch.cpp" "src\ui\hud_grimoire.cpp" "src\ui\hud_zonetracker.cpp" "src\ui\hud_empypop.cpp" "src\ui\hud_debuffs.cpp" "src\ui\hud_timers.cpp" ^
   "src\plugin\aiohud.cpp" %PROBES% %DEVSRC% %AIORES% ^
   /Fo"%OUT%\\" /Fe"%OUT%\AioHud.dll" ^
   /link /DEF:"src\plugin\aiohud.def" user32.lib kernel32.lib gdi32.lib /OUT:"%OUT%\AioHud.dll" ^
   /DEBUG /OPT:REF /OPT:ICF /PDB:"%OUT%\AioHud.pdb" /PDBALTPATH:%%_PDB%%
set "CLRC=%errorlevel%"
popd


REM cl returns nonzero on ANY compile/link error -> trust the exit code, NOT just the DLL's
REM existence (a stale DLL from a previous build would otherwise mask a failed compile).
if not "%CLRC%"=="0" ( echo [build] FAILED -- compile/link error above ^(DLL NOT updated^) & exit /b 1 )
if exist "%OUT%\AioHud.dll" ( echo [build] OK -^> %OUT%\AioHud.dll ) else ( echo [build] FAILED & exit /b 1 )
REM A missing PDB is not a build failure, but it must not pass unnoticed : producing it is the point of /Zi above.
if exist "%OUT%\AioHud.pdb" ( echo [build] symbols -^> %OUT%\AioHud.pdb ) else ( echo [build] WARNING: no AioHud.pdb -- a crash dump from this build cannot be symbolised )
