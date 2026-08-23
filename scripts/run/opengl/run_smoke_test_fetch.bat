@echo off
call "%~dp0..\..\common.bat" debug-fetch --smoke-test %*
exit /b %errorlevel%
