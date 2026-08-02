@echo off
rem Build the jump game DLL.  Usage: build.bat [Platform] [Toolset]
rem   Set Q2J_BUILD_CONFIG=Debug beforehand for a debug build (default Release).
rem   Set Q2J_SKIP_TESTS=1 to skip the unit tests.
rem   Set Q2J_SKIP_VERSION_CHECK=1 to skip VERSION / changelog alignment check.
setlocal EnableExtensions

if not defined Q2J_BUILD_CONFIG set "Q2J_BUILD_CONFIG=Release"

set "ROOT=%~dp0"
pushd "%ROOT%" >nul

if not "%Q2J_SKIP_VERSION_CHECK%"=="1" (
    echo [VERSION] Checking VERSION / jump_version.h / CHANGELOG.md...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\check-version.ps1"
    if errorlevel 1 (
        echo [ERROR] Version check failed.
        popd >nul
        exit /b 1
    )
    echo.
)

set "SOLUTION=src\game.sln"
if not exist "%SOLUTION%" (
    echo [ERROR] Could not find "%SOLUTION%".
    popd >nul
    exit /b 1
)

set "CONFIG=%Q2J_BUILD_CONFIG%"

set "PLATFORM=%~1"
if "%PLATFORM%"=="" set "PLATFORM=x64"

set "TOOLSET=%~2"
if "%TOOLSET%"=="" set "TOOLSET=v143"

rem The vcxproj sets OutDir to ../dist/, so artifacts always land here.
set "OUTDIR=dist"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDEVCMD="

if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find Common7\Tools\VsDevCmd.bat`) do (
        set "VSDEVCMD=%%I"
    )
)

if not defined VSDEVCMD if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" (
    set "VSDEVCMD=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
)
if not defined VSDEVCMD if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" (
    set "VSDEVCMD=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
)

if not defined VSDEVCMD (
    echo [ERROR] Could not locate VsDevCmd.bat.
    echo         Install Visual Studio 2022 with the "Desktop development with C++" workload.
    popd >nul
    exit /b 1
)

call "%VSDEVCMD%" -host_arch=x64 -arch=x64 >nul
if errorlevel 1 (
    echo [ERROR] Failed to initialise the Visual Studio build environment.
    popd >nul
    exit /b 1
)

where msbuild >nul 2>nul
if errorlevel 1 (
    echo [ERROR] msbuild was not found after environment setup.
    popd >nul
    exit /b 1
)

echo [BUILD] Solution  : %SOLUTION%
echo [BUILD] Config    : %CONFIG%
echo [BUILD] Platform  : %PLATFORM%
echo [BUILD] Toolset   : %TOOLSET%
echo [BUILD] Output    : %OUTDIR%
echo.

msbuild "%SOLUTION%" /m /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /p:PlatformToolset=%TOOLSET%
set "BUILD_EXIT=%ERRORLEVEL%"

if %BUILD_EXIT% neq 0 (
    echo.
    echo [ERROR] Build failed. If the errors mention jsoncpp or fmt, the vcpkg
    echo         manifest has not been restored - see README.md.
    popd >nul
    exit /b %BUILD_EXIT%
)

if not exist "%OUTDIR%\game_x64.dll" (
    echo [ERROR] Build succeeded but "%OUTDIR%\game_x64.dll" was not produced.
    popd >nul
    exit /b 1
)

rem --- Unit tests over the engine-free logic layer ---------------------------
if "%Q2J_SKIP_TESTS%"=="1" goto :done
if not exist "tests\jump_tests.vcxproj" goto :done

echo.
echo [TEST] Building and running the logic tests...
msbuild "tests\jump_tests.vcxproj" /nologo /v:minimal /p:Configuration=%CONFIG% /p:Platform=%PLATFORM%
if errorlevel 1 (
    echo [ERROR] Test build failed.
    popd >nul
    exit /b 1
)

"%OUTDIR%\jump_tests_x64.exe"
if errorlevel 1 (
    echo.
    echo [ERROR] Unit tests FAILED - the DLL was built, but do not ship it.
    popd >nul
    exit /b 1
)

:done
echo.
echo [OK] %OUTDIR%\game_x64.dll is ready.
echo      play.bat   - install locally and launch
echo      deploy.bat - push to the server share

popd >nul
exit /b 0
