@echo off
call "%~dp0..\..\common.bat" debug-fetch %*
exit /b %errorlevel%
