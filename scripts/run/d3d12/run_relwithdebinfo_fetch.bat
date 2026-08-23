@echo off
call "%~dp0..\..\common.bat" relwithdebinfo-d3d12-fetch %*
exit /b %errorlevel%
