@echo off
REM Build the jumptest maps.
REM   build_jumptest.bat            all maps
REM   build_jumptest.bat jumptest4  just one
REM
REM Needs ericw-tools (qbsp/vis/light) and a directory of Quake II .wal
REM textures. Both default to the q2-relighter checkout; override with
REM   set Q2_TOOLS=...  and  set Q2_GAMEDATA=...

setlocal EnableExtensions EnableDelayedExpansion

if not defined Q2_TOOLS set "Q2_TOOLS=E:\code\projects\q2-relighter\tools\ericw-tools"
if not defined Q2_GAMEDATA set "Q2_GAMEDATA=E:\code\projects\q2-relighter\gamedata\baseq2"

REM Where the remaster loads custom maps from.
if not defined Q2_MAPS_DIR set "Q2_MAPS_DIR=%USERPROFILE%\OneDrive\Saved Games\Nightdive Studios\Quake II\baseq2\maps"

cd /d "%~dp0"

if not exist "%Q2_TOOLS%\qbsp.exe" (
    echo ERROR: qbsp.exe not found under "%Q2_TOOLS%"
    echo        Set Q2_TOOLS to your ericw-tools directory.
    exit /b 1
)

set "MAPS=%*"
if "%MAPS%"=="" set "MAPS=jumptest1 jumptest2 jumptest3 jumptest4 jumptest5 jumptest6 jumptest7 jumptest8 jumptest9 jumptest10 jumptest11 jumptest12"

echo === generating .map source ===
python make_jumptest.py %MAPS% || exit /b 1

for %%M in (%MAPS%) do (
    echo.
    echo === %%M ===
    "%Q2_TOOLS%\qbsp.exe" -q2bsp -path "%Q2_GAMEDATA%" %%M.map %%M.bsp >nul || (
        echo ERROR: qbsp failed for %%M
        exit /b 1
    )
    "%Q2_TOOLS%\vis.exe" %%M.bsp >nul || (
        echo ERROR: vis failed for %%M
        exit /b 1
    )
    "%Q2_TOOLS%\light.exe" -path "%Q2_GAMEDATA%" -extra4 -bounce 8 %%M.bsp >nul || (
        echo ERROR: light failed for %%M
        exit /b 1
    )
    echo compiled %%M.bsp

    if exist "%Q2_MAPS_DIR%\" (
        copy /y %%M.bsp "%Q2_MAPS_DIR%\%%M.bsp" >nul || (
            echo ERROR: could not install %%M.bsp
            exit /b 1
        )
        echo installed to maps dir
    )
)

if not exist "%Q2_MAPS_DIR%\" (
    echo.
    echo Maps dir not found ^("%Q2_MAPS_DIR%"^) - skipped install.
)

echo.
echo Done. In game:  map jumptest1
endlocal
