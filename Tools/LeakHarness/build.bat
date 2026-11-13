@echo off
rem Builds Build\LeakHarness.exe with MSVC, against the plugin's own FFmpeg headers and import libraries.
setlocal
pushd "%~dp0"
set "FFMPEG=..\..\Plugins\IPStreamMedia\ThirdParty\FFmpeg"

rem Set up the MSVC x64 tools unless this prompt already has them.
where cl.exe >nul 2>nul && goto :build
for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set "VSDIR=%%I"
if not defined VSDIR (
    echo Visual Studio not found.
    popd & exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || (popd & exit /b 1)

:build
if not exist Build mkdir Build
cl.exe /nologo /std:c++17 /EHsc /O2 /W4 /MD /I"%FFMPEG%\include" main.cpp /Fo"Build\\" /Fe"Build\LeakHarness.exe" /link /LIBPATH:"%FFMPEG%\lib\Win64" avformat.lib avcodec.lib avutil.lib psapi.lib winmm.lib
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
