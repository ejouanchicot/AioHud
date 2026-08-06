@echo off
REM Copy the built plugin + runtime assets into Windower. The plugin loads its data from plugins\AioHud\ ;
REM since the dev repo now lives OUTSIDE Windower (G:\01_Development\Game_Project\aiohud), that runtime folder is a
REM SEPARATE clean copy -> we sync the assets here too, not just the DLL.
REM NOTE: //unload AioHud in game FIRST -- Windower keeps the DLL file-locked while loaded.
setlocal
set "ROOT=%~dp0"
REM Your Windower plugins folder. Per-machine, without editing this tracked file, create an untracked
REM deploy.local.bat next to it containing e.g.:  set "WINDOWER_PLUGINS=D:\Windower Tetsouo\plugins"
if exist "%ROOT%deploy.local.bat" call "%ROOT%deploy.local.bat"
if not defined WINDOWER_PLUGINS set "WINDOWER_PLUGINS=D:\Windower\plugins"
set "WP=%WINDOWER_PLUGINS%"
set "DATA=%WP%\AioHud"
if not exist "%WP%\" ( echo [deploy] plugins folder not found: "%WP%"  -- set WINDOWER_PLUGINS or create deploy.local.bat & exit /b 1 )

REM 0) MIGRATE the data folder name (once) : _aiohud_re -> AioHud. MUST run before the asset sync below, else robocopy
REM    would create a fresh empty AioHud\ and orphan the old config/profiles still sitting in _aiohud_re\.
if not exist "%DATA%\" if exist "%WP%\_aiohud_re\" ren "%WP%\_aiohud_re" "AioHud"

REM 1) the DLL
copy /Y "%ROOT%build\AioHud.dll" "%WP%\AioHud.dll" >nul
if errorlevel 1 ( echo [deploy] FAILED -- is AioHud still loaded? do //unload AioHud first & exit /b 1 )

REM 2) runtime assets + default layout -> the plugin's data folder.
REM    robocopy is INCREMENTAL (skips unchanged -> near-instant when only code changed) ; *_src\ regen sources excluded.
robocopy "%ROOT%assets" "%DATA%\assets" /E /XD *_src /NFL /NDL /NJH /NJS /NP >nul
mkdir "%DATA%\design\exports" 2>nul
copy /Y "%ROOT%design\exports\layout.json" "%DATA%\design\exports\layout.json" >nul

REM 3) the updater companion addon -> addons\aioupdate\  (package.bat ships it ; deploy did NOT, so the dev box
REM    kept running whatever the last RELEASE installed). That left the most exposed file in the whole chain
REM    never exercised before it reached every user -- and its code only runs on a path you reach when an update
REM    has ALREADY failed. Same three lines package.bat uses.
mkdir "%WP%\..\addons\aioupdate" 2>nul
copy /Y "%ROOT%updater\aioupdate\aioupdate.lua" "%WP%\..\addons\aioupdate\aioupdate.lua" >nul

echo [deploy] OK -^> %WP%\AioHud.dll  (+ assets synced to AioHud\, addon synced to addons\aioupdate\)   (now //load AioHud in game)
