@echo off
REM package.bat -- build, then assemble a Windower-root-relative payload into dist\ (a clean, shareable bundle).
REM
REM   The zip made from dist\ is extracted straight into the Windower ROOT (e.g. D:\Windower\), so both
REM   the plugin AND its updater addon land in place in one shot :
REM
REM   dist\plugins\AioHud.dll            ->  <windower>\plugins\AioHud.dll
REM   dist\plugins\AioHud\           ->  <windower>\plugins\AioHud\      (assets\ + design\ the DLL loads at runtime)
REM   dist\addons\aioupdate\             ->  <windower>\addons\aioupdate\    (the //aioupdate Lua companion)
REM
REM Ships ONLY what the game needs at runtime : the DLL + assets\ (minus *_src regen sources) + default layout.json
REM + the Lua addon. Dev-only trees (src\, docs\, scripts\, re\ dumps, build\ objs, screenshots, .git, ...) are NOT
REM copied. Runtime-written files (data\config.txt, data\profiles\, caches) are created fresh in-game and never
REM shipped -- so an update over an existing install never touches the user's settings.
setlocal
set "ROOT=%~dp0"
set "DIST=%ROOT%dist"
set "PLG=%DIST%\plugins"
set "DATA=%PLG%\AioHud"
set "ADD=%DIST%\addons"

REM 1) build first -- a bad build aborts the package (never ship a stale DLL).
call "%ROOT%build.bat"
if errorlevel 1 ( echo [package] build failed -- aborting & exit /b 1 )

REM 2) clean dist\ from scratch so removed assets never linger.
if exist "%DIST%" rmdir /S /Q "%DIST%"
mkdir "%DATA%"

REM 3) the DLL  ->  plugins\AioHud.dll
copy /Y "%ROOT%build\AioHud.dll" "%PLG%\AioHud.dll" >nul

REM 4) runtime assets : the whole assets\ tree EXCEPT any *_src\ (job/window/icon regeneration sources).
REM    robocopy exit codes 0-7 = success ; 8+ = real error (don't treat <8 as failure).
robocopy "%ROOT%assets" "%DATA%\assets" /E /XD *_src /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 ( echo [package] asset copy failed & exit /b 1 )

REM 5) the default box layout (positions/toggles) the plugin reads at startup.
mkdir "%DATA%\design\exports" 2>nul
copy /Y "%ROOT%design\exports\layout.json" "%DATA%\design\exports\layout.json" >nul

REM 5b) the status-icon tool (extract the game's icons to PNG / install your own). Ships next to the data
REM     folder it writes into, so its Windower-root auto-detection works with no arguments.
copy /Y "%ROOT%tools\aioicons.ps1" "%DATA%\aioicons.ps1" >nul

REM 6) third-party licence notice. The EmpyPop NM table (src\model\nms_gen.h) is BSD-3 data (c) 2020 Dean
REM    James (Xurion of Bismarck) ; clause 2 REQUIRES the copyright notice be reproduced in binary
REM    distributions -- so this ships with every zip. Do not drop it.
copy /Y "%ROOT%NOTICE-EmpyPop.txt" "%DATA%\NOTICE-EmpyPop.txt" >nul
if errorlevel 1 ( echo [package] NOTICE-EmpyPop.txt missing -- BSD-3 clause 2 requires it & exit /b 1 )

REM 7) the updater companion addon  ->  addons\aioupdate\
mkdir "%ADD%\aioupdate" 2>nul
copy /Y "%ROOT%updater\aioupdate\aioupdate.lua" "%ADD%\aioupdate\aioupdate.lua" >nul

echo.
echo [package] OK -^> %DIST%   (extract the zip into your Windower root)
echo   dist\plugins\AioHud.dll      (-^> ^<windower^>\plugins\)
echo   dist\plugins\AioHud\      (-^> ^<windower^>\plugins\)
echo   dist\addons\aioupdate\       (-^> ^<windower^>\addons\)
powershell -NoProfile -Command "'  total payload : {0:N1} MB' -f ((Get-ChildItem -Recurse -File '%DIST%' | Measure-Object -Sum Length).Sum/1MB)"
