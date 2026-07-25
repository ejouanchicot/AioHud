@echo off
REM Offline test suite -> build\tests.exe. Exit code = number of failed checks (0 = all green).
REM
REM Only the PURE core is linked here : no D3D8 device, no game memory, no Windower host. That is not a
REM limitation to work around -- it is the boundary of what a test can honestly decide. Rendering needs a live
REM device, and no test can prove a memory offset is right. What this DOES cover is the logic that has shipped
REM broken more than once : the layout parser (both blocking defects of the 2026-07-25 audit lived there),
REM skillchain resolution, and the duration model.
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
cl /nologo /EHsc /W4 /permissive- /std:c++17 /Od /wd4456 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS ^
   /I"%ROOT%include" /I"%ROOT%src" ^
   "%ROOT%tests\test_main.cpp" "%ROOT%tests\t_json.cpp" "%ROOT%tests\t_skillchain.cpp" "%ROOT%tests\t_durations.cpp" ^
   "%ROOT%src\model\skillchain.cpp" ^
   /Fo"%ROOT%build\t\\" /Fe"%ROOT%build\tests.exe"
if errorlevel 1 ( echo [tests] BUILD FAILED & exit /b 1 )

"%ROOT%build\tests.exe"
REM NOT `if errorlevel 1` : cmd compares that as a SIGNED value, so a crash (0xC00000FD stack overflow,
REM 0xC0000005 access violation) reads as negative and slips through as success. Caught the first time this
REM suite was proved -- neutralising the JSON depth bound crashed the runner and the script printed OK.
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" ( echo [tests] FAILED ^(exit code %RC%^) & exit /b 1 )
echo [tests] OK
exit /b 0
