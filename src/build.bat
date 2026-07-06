@echo off
setlocal enabledelayedexpansion
echo Building Quake 2 Rerelease Game DLL...

REM ---------------------------------------------------------------------------
REM Config: point Q2_DIR at your Quake II rerelease install (the folder that
REM contains quake2ex_steam.exe). Leave blank to only build the DLL.
REM Override without editing this file:  set Q2_DIR=D:\Games\Quake 2 && build.bat
REM ---------------------------------------------------------------------------
if not defined Q2_DIR set "Q2_DIR=C:\Program Files (x86)\Steam\steamapps\common\Quake 2\rerelease"
set "Q2_MOD_DIR=baseq2"
set "Q2_EXE=quake2ex_steam.exe"
set "Q2_LAUNCH_ARGS=+set deathmatch 1 +set timelimit 10 +map q2dm1"

REM --- Locate Visual Studio 2022 (any edition) via vswhere -------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Install Visual Studio 2022 with the
    echo        "Desktop development with C++" workload.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
if not defined VS_PATH (
    echo ERROR: No VS 2022 install with the C++ toolset was found.
    exit /b 1
)
call "%VS_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo ERROR: Failed to initialise the MSVC build environment.
    exit /b 1
)

REM --- Build ----------------------------------------------------------------
msbuild game.sln /p:Configuration=Release /p:Platform=x64 /m
if errorlevel 1 (
    echo.
    echo Build failed. Check the output above for errors.
    exit /b 1
)

echo.
echo Build successful! Game DLL created: ..\dist\game_x64.dll

REM --- Optional: deploy + launch --------------------------------------------
set "DEST=%Q2_DIR%\%Q2_MOD_DIR%"
if not exist "%DEST%\" (
    echo.
    echo Q2_DIR not found ^(%Q2_DIR%^) - skipping copy/launch.
    echo Set Q2_DIR to your Quake II install to auto-deploy.
    exit /b 0
)

echo Copying game_x64.dll to "%DEST%" ...
copy /Y "..\dist\game_x64.dll" "%DEST%\" >nul
if errorlevel 1 (
    echo Warning: Failed to copy DLL. Check permissions.
    exit /b 1
)
echo DLL copied successfully!

if exist "%Q2_DIR%\%Q2_EXE%" (
    echo Launching Quake 2...
    start "" "%Q2_DIR%\%Q2_EXE%" %Q2_LAUNCH_ARGS%
)
exit /b 0
