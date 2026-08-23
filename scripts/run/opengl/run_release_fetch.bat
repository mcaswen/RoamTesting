@echo off
call "%~dp0..\..\common.bat" release-fetch %*
exit /b %errorlevel%
