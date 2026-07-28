@echo off
REM Build the jump test map.
REM
REM Needs ericw-tools (qbsp/vis/light) and a directory of Quake II .wal
REM textures. Both default to the q2-relighter checkout; override with
REM   set Q2_TOOLS=...  and  set Q2_GAMEDATA=...

setlocal

if not defined Q2_TOOLS set "Q2_TOOLS=E:\code\projects\q2-relighter\tools\ericw-tools"
if not defined Q2_GAMEDATA set "Q2_GAMEDATA=E:\code\projects\q2-relighter\gamedata\baseq2"

REM Where the remaster loads custom maps from.
if not defined Q2_MAPS_DIR set "Q2_MAPS_DIR=%USERPROFILE%\OneDrive\Saved Games\Nightdive Studios\Quake II\baseq2\maps"

cd /d "%~dp0"

if not exist "%Q2_TOOLS%\qbsp.exe" (
    echo ERROR: qbsp.exe not found under %Q2_TOOLS%
    echo Set Q2_TOOLS to your ericw-tools directory.
    exit /b 1
)

echo === generating jumptest1.map ===
python make_jumptest.py || exit /b 1

echo === qbsp ===
"%Q2_TOOLS%\qbsp.exe" -q2bsp -path "%Q2_GAMEDATA%" jumptest1.map jumptest1.bsp || exit /b 1

echo === vis ===
"%Q2_TOOLS%\vis.exe" jumptest1.bsp || exit /b 1

echo === light ===
"%Q2_TOOLS%\light.exe" -path "%Q2_GAMEDATA%" -extra4 -bounce 8 jumptest1.bsp || exit /b 1

if exist "%Q2_MAPS_DIR%" (
    echo === installing to %Q2_MAPS_DIR% ===
    copy /y jumptest1.bsp "%Q2_MAPS_DIR%\jumptest1.bsp" >nul || exit /b 1
    echo Installed. In game:  map jumptest1
) else (
    echo Maps dir not found ^(%Q2_MAPS_DIR%^) - skipping install.
    echo Copy jumptest1.bsp there by hand.
)

endlocal
