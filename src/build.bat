@echo off
rem Kept so the VS Code task and old muscle memory keep working. The real build
rem lives at the repo root, alongside play.bat and deploy.bat.
call "%~dp0..\build.bat" %*
exit /b %ERRORLEVEL%
