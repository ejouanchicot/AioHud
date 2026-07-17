@echo off
REM Build the AioHUD plugin (clean layered src\ tree) -> build\AioHud.dll  (32-bit, MSVC).
REM Layers:  src\gfx (D3D backend)  src\ui (widgets + HUD)  src\model  src\plugin (IPlugin glue)
REM Iterate:  //unload AioHud  ->  deploy.bat  ->  //load AioHud
setlocal
set "ROOT=%~dp0"
REM --- x86 MSVC toolchain. CI (msvc-dev-cmd) already puts cl on PATH -> skip. Else locate VS with vswhere
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
set "PROBES="
set "PROBEDEF="
if exist "%ROOT%src\plugin\aiohud_probes.cpp" ( set "PROBES=%ROOT%src\plugin\aiohud_probes.cpp" & set "PROBEDEF=/DAIOHUD_PROBES" )

if not exist "%ROOT%build" mkdir "%ROOT%build"

REM --- version resource : parse AIOHUD_VERSION ("MAJ.MIN.PAT") and compile aiohud.rc so the DLL carries a REAL
REM     file version (Windower prints it at load, instead of 0.0.0.0). Non-numeric ("dev") -> 0.0.0. Skipped
REM     gracefully if rc.exe isn't on PATH (build still succeeds, just without the version stamp).
set "VMAJ=0" & set "VMIN=0" & set "VPAT=0"
echo %AIOHUD_VERSION%| findstr /r "^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$" >nul && for /f "tokens=1-3 delims=." %%a in ("%AIOHUD_VERSION%") do (set "VMAJ=%%a" & set "VMIN=%%b" & set "VPAT=%%c")
set "AIORES="
where rc.exe >nul 2>nul && (
    rc /nologo /fo "%ROOT%build\aiohud.res" /dAIO_VMAJ=%VMAJ% /dAIO_VMIN=%VMIN% /dAIO_VPAT=%VPAT% "%ROOT%src\plugin\aiohud.rc" >nul
    if exist "%ROOT%build\aiohud.res" set AIORES="%ROOT%build\aiohud.res"
)

REM /W4 /permissive- : high warnings + strict conformance (catches shadowing, dead code, bad conversions).
REM /std:c++17 : pin the standard (VS2017). _CRT_SECURE_NO_WARNINGS : silence MSVC's C4996 nags on sprintf/sscanf
REM -- reviewed safe here (fixed buffers ; the string ops already use bounded _snprintf/lstrcpynA ; sscanf is numeric).
REM /wd4456 : local-scope shadowing is idiomatic here (immediate-mode UI redeclares ty/x/y per control block ;
REM nested loop counters `i`). Reviewed benign. Kept ON : C4457 (shadows a param), C4244 (lossy conversion), etc.
cl /nologo /LD /O2 /MT /EHsc- /utf-8 /W4 /permissive- /std:c++17 /wd4456 /D_CRT_SECURE_NO_WARNINGS /DAIOHUD_VERSION=\"%AIOHUD_VERSION%\" %PROBEDEF% /I"%ROOT%include" /I"%ROOT%src" ^
   "%ROOT%src\gfx\noise.cpp" "%ROOT%src\gfx\draw.cpp" "%ROOT%src\gfx\texture.cpp" "%ROOT%src\gfx\font.cpp" "%ROOT%src\gfx\window.cpp" ^
   "%ROOT%src\model\layout.cpp" "%ROOT%src\model\party_state.cpp" "%ROOT%src\model\party_state_zonetracker.cpp" "%ROOT%src\model\party_state_pointwatch.cpp" "%ROOT%src\model\party_state_hate.cpp" "%ROOT%src\model\party_state_skillchain.cpp" "%ROOT%src\model\party_state_roster.cpp" "%ROOT%src\model\party_state_empypop.cpp" "%ROOT%src\model\game_mem.cpp" "%ROOT%src\model\map_dat.cpp" "%ROOT%src\model\zones.cpp" "%ROOT%src\model\vana_clock.cpp" "%ROOT%src\model\paths.cpp" "%ROOT%src\model\ui_config.cpp" "%ROOT%src\model\skillchain.cpp" "%ROOT%src\model\resistances.cpp" ^
   "%ROOT%src\ui\palette.cpp" "%ROOT%src\ui\edit_box.cpp" "%ROOT%src\ui\liquid_bars.cpp" "%ROOT%src\ui\player.cpp" "%ROOT%src\ui\party.cpp" "%ROOT%src\ui\target.cpp" "%ROOT%src\ui\minimap.cpp" "%ROOT%src\ui\factory.cpp" "%ROOT%src\ui\config_controls.cpp" "%ROOT%src\ui\party_config.cpp" "%ROOT%src\ui\target_config.cpp" "%ROOT%src\ui\player_config.cpp" "%ROOT%src\ui\minimap_config.cpp" "%ROOT%src\ui\ws_config.cpp" "%ROOT%src\ui\sc_config.cpp" "%ROOT%src\ui\tp_config.cpp" "%ROOT%src\ui\hl_config.cpp" "%ROOT%src\ui\pw_config.cpp" "%ROOT%src\ui\grim_config.cpp" "%ROOT%src\ui\zt_config.cpp" "%ROOT%src\ui\tm_config.cpp" "%ROOT%src\ui\ep_config.cpp" "%ROOT%src\ui\box_style.cpp" "%ROOT%src\ui\config_page.cpp" "%ROOT%src\ui\hud.cpp" ^
   "%ROOT%src\ui\hud_skillchains.cpp" "%ROOT%src\ui\hud_treasure.cpp" "%ROOT%src\ui\hud_hatelist.cpp" "%ROOT%src\ui\hud_pointwatch.cpp" "%ROOT%src\ui\hud_grimoire.cpp" "%ROOT%src\ui\hud_zonetracker.cpp" "%ROOT%src\ui\hud_empypop.cpp" "%ROOT%src\ui\hud_debuffs.cpp" "%ROOT%src\ui\hud_timers.cpp" ^
   "%ROOT%src\plugin\aiohud.cpp" %PROBES% %AIORES% ^
   /Fo"%ROOT%build\\" /Fe"%ROOT%build\AioHud.dll" ^
   /link /DEF:"%ROOT%src\plugin\aiohud.def" user32.lib kernel32.lib gdi32.lib /OUT:"%ROOT%build\AioHud.dll"

REM cl returns nonzero on ANY compile/link error -> trust the exit code, NOT just the DLL's
REM existence (a stale DLL from a previous build would otherwise mask a failed compile).
if errorlevel 1 ( echo [build] FAILED -- compile/link error above ^(DLL NOT updated^) & exit /b 1 )
if exist "%ROOT%build\AioHud.dll" ( echo [build] OK -^> %ROOT%build\AioHud.dll ) else ( echo [build] FAILED & exit /b 1 )
