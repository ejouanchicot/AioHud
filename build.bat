@echo off
REM Build the AioHUD plugin (clean layered src\ tree) -> build\AioTest.dll  (32-bit, MSVC).
REM Layers:  src\gfx (D3D backend)  src\ui (widgets + HUD)  src\model  src\plugin (IPlugin glue)
REM Iterate:  //unload AioTest  ->  deploy.bat  ->  //load AioTest
setlocal
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
set "ROOT=%~dp0"
call "%VCVARS%" x86 >nul
if errorlevel 1 ( echo [build] vcvars failed & exit /b 1 )

if not exist "%ROOT%build" mkdir "%ROOT%build"

cl /nologo /LD /O2 /MT /EHsc- /utf-8 /I"%ROOT%include" /I"%ROOT%src" ^
   "%ROOT%src\gfx\noise.cpp" "%ROOT%src\gfx\draw.cpp" "%ROOT%src\gfx\texture.cpp" "%ROOT%src\gfx\font.cpp" "%ROOT%src\gfx\window.cpp" ^
   "%ROOT%src\model\layout.cpp" "%ROOT%src\model\party_state.cpp" "%ROOT%src\model\game_mem.cpp" "%ROOT%src\model\zones.cpp" "%ROOT%src\model\ui_config.cpp" ^
   "%ROOT%src\ui\palette.cpp" "%ROOT%src\ui\liquid_bars.cpp" "%ROOT%src\ui\party.cpp" "%ROOT%src\ui\factory.cpp" "%ROOT%src\ui\config_page.cpp" "%ROOT%src\ui\hud.cpp" ^
   "%ROOT%src\plugin\aiohud.cpp" ^
   /Fo"%ROOT%build\\" /Fe"%ROOT%build\AioTest.dll" ^
   /link /DEF:"%ROOT%src\plugin\aiohud.def" user32.lib kernel32.lib gdi32.lib /OUT:"%ROOT%build\AioTest.dll"

REM cl returns nonzero on ANY compile/link error -> trust the exit code, NOT just the DLL's
REM existence (a stale DLL from a previous build would otherwise mask a failed compile).
if errorlevel 1 ( echo [build] FAILED -- compile/link error above ^(DLL NOT updated^) & exit /b 1 )
if exist "%ROOT%build\AioTest.dll" ( echo [build] OK -^> %ROOT%build\AioTest.dll ) else ( echo [build] FAILED & exit /b 1 )
