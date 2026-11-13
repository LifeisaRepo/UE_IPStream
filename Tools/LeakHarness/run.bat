@echo off
rem Runs Build\LeakHarness.exe against the plugin's own FFmpeg DLLs.
rem Usage: run.bat "<url>" [seconds] [--decode]    CSV on stdout, notes on stderr.
setlocal
set "PATH=%~dp0..\..\Plugins\IPStreamMedia\ThirdParty\FFmpeg\bin\Win64;%PATH%"
"%~dp0Build\LeakHarness.exe" %*