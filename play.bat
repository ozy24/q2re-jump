@echo off
rem Install the built DLL into the local Quake II install and launch it.
rem   Override the paths with Q2J_GAME_DIR / Q2J_EXE, and the command line with
rem   Q2J_LAUNCH_ARGS.
setlocal EnableExtensions

set "SRC=%~dp0dist\game_x64.dll"

if not defined Q2J_GAME_DIR set "Q2J_GAME_DIR=G:\Program Files (x86)\Steam\steamapps\common\Quake 2\rerelease"
if not defined Q2J_EXE set "Q2J_EXE=%Q2J_GAME_DIR%\quake2ex_steam.exe"
if not defined Q2J_LAUNCH_ARGS set "Q2J_LAUNCH_ARGS=+set deathmatch 1 +map jumptest1"

set "DST_DIR=%Q2J_GAME_DIR%\baseq2"
set "DST=%DST_DIR%\game_x64.dll"

rem Paths are always echoed quoted: an unquoted one containing parentheses -
rem "Program Files (x86)" - closes an if-block early and breaks the script.
if not exist "%SRC%" (
    echo ERROR: Source not found: "%SRC%"
    echo        Run build.bat first.
    pause
    exit /b 1
)

if not exist "%DST_DIR%\" (
    echo ERROR: Destination folder not found: "%DST_DIR%"
    echo        Set Q2J_GAME_DIR to your Quake II rerelease install.
    pause
    exit /b 1
)

copy /y "%SRC%" "%DST%"
if errorlevel 1 (
    echo.
    echo ERROR: Copy failed. This almost always means Quake II is still running
    echo        and has the DLL locked. Quit the game and try again.
    pause
    exit /b 1
)

echo Installed to "%DST%".

rem The KEX user-data folder shadows the install folder. If a DLL is already
rem there it is the one the game will actually load, so leaving it stale means
rem silently testing old code. Keep the two in step rather than warning about
rem it - set Q2J_SKIP_USERDATA=1 to opt out.
set "USERDATA_DLL=%USERPROFILE%\OneDrive\Saved Games\Nightdive Studios\Quake II\baseq2\game_x64.dll"
if "%Q2J_SKIP_USERDATA%"=="1" goto :launch
if not exist "%USERDATA_DLL%" goto :launch

copy /y "%SRC%" "%USERDATA_DLL%" >nul
if errorlevel 1 (
    echo.
    echo WARNING: could not update the user-data copy:
    echo          "%USERDATA_DLL%"
    echo          The game loads that one in preference, so you may be
    echo          running an older build. Quit the game and try again.
    echo.
) else (
    echo Also updated the user-data copy the game loads in preference.
)

:launch
echo Launching Quake II...
start "" "%Q2J_EXE%" %Q2J_LAUNCH_ARGS%

exit /b 0
