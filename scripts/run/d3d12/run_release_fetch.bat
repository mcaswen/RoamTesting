@echo off
call "%~dp0..\..\common.bat" release-d3d12-fetch %*
exit /b %errorlevel%
