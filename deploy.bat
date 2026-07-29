@echo off
rem Push the built DLL to the server share, keeping a timestamped backup.
rem   Override the target with Q2J_DEPLOY_DIR.
setlocal EnableDelayedExpansion

set "SRC_DIR=%~dp0dist\"
set "BASE=game_x64"

if not defined Q2J_DEPLOY_DIR set "Q2J_DEPLOY_DIR=\\192.168.16.191\Quake2\baseq2"

set "DST_DIR=%Q2J_DEPLOY_DIR%"
set "DST=%DST_DIR%\%BASE%.dll"

rem Paths are always echoed quoted: an unquoted one containing parentheses
rem closes an if-block early and breaks the script.
if not exist "%SRC_DIR%%BASE%.dll" (
    echo ERROR: Source not found: "%SRC_DIR%%BASE%.dll"
    echo        Run build.bat first.
    pause
    exit /b 1
)

if not exist "%DST_DIR%\" (
    echo ERROR: Destination folder not found or not accessible: "%DST_DIR%"
    pause
    exit /b 1
)

REM --- Back up (rename) the existing DLL. If the game is running the file is
REM --- locked and this rename fails - that is our first chance to bail out,
REM --- before anything has been changed.
set "BACKUP="
if exist "%DST%" (
    for /f %%i in ('powershell -nop -c "Get-Date -Format 'yyyyMMdd_HHmmss'"') do set "TIMESTAMP=%%i"
    set "BACKUP=%DST%.!TIMESTAMP!"
    echo Backing up existing file to: !BACKUP!
    move /y "%DST%" "!BACKUP!"
    if errorlevel 1 (
        echo.
        echo ============================================================
        echo   DEPLOY FAILED - could not replace the existing DLL
        echo ============================================================
        echo   "%DST%"
        echo   could not be renamed. This almost always means the game
        echo   is currently running and has the DLL locked.
        echo.
        echo   Quit Quake II on the server, then run deploy again.
        echo   ^(No changes were made.^)
        echo ============================================================
        echo.
        pause
        exit /b 1
    )
)

REM --- Copy the freshly built DLL into place.
copy /y "%SRC_DIR%%BASE%.dll" "%DST%"
if errorlevel 1 (
    echo.
    echo ============================================================
    echo   DEPLOY FAILED - could not copy the new DLL
    echo ============================================================
    echo   Target: "%DST%"
    echo   The file is locked ^(the game may be running^) or the
    echo   destination is not accessible.
    if defined BACKUP if exist "!BACKUP!" (
        echo   Restoring the previous DLL from backup...
        move /y "!BACKUP!" "%DST%" >nul
    )
    echo ============================================================
    echo.
    pause
    exit /b 1
)

REM --- Auxiliary build artifacts. These are not loaded by the game, so a
REM --- failure here is a warning rather than a failed deploy.
set "AUX_FAILED=0"
for %%E in (exp lib pdb) do (
    if exist "%SRC_DIR%%BASE%.%%E" (
        copy /y "%SRC_DIR%%BASE%.%%E" "%DST_DIR%\%BASE%.%%E" >nul
        if !errorlevel! neq 0 (
            echo WARNING: Failed to copy %BASE%.%%E ^(non-critical^)
            set "AUX_FAILED=1"
        ) else (
            echo Copied %BASE%.%%E
        )
    )
)

echo.
echo Deploy successful.
echo   Source:      %SRC_DIR%%BASE%.dll
echo   Destination: %DST%
if "!AUX_FAILED!"=="1" (
    echo   Note: the DLL deployed OK but some auxiliary files could not be copied.
)
exit /b 0
