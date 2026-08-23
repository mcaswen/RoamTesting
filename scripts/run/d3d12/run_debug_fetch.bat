@echo off
call "%~dp0..\..\common.bat" debug-d3d12-fetch %*
exit /b %errorlevel%
