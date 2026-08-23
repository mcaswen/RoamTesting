@echo off
call "%~dp0..\..\common.bat" relwithdebinfo-fetch %*
exit /b %errorlevel%
