@echo off
setlocal
set "BRIDGE_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%BRIDGE_VSWHERE%" exit /b 2
for /f "usebackq tokens=*" %%i in (`"%BRIDGE_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "BRIDGE_VSROOT=%%i"
if not defined BRIDGE_VSROOT exit /b 2
call "%BRIDGE_VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=amd64
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /utf-8 /MT /W4 /D_WIN32_WINNT=0x0A00 /I src src\apps\mplayerc\bridge\MediaCache.cpp src\apps\mplayerc\bridge\MediaTimeIndex.cpp tests\bridge\cache_probe.cpp /Fo_bin\bridge\ /Fe_bin\bridge\cache-probe.exe /link /LTCG _bin\lib\Release_x64\Bento4.lib _bin\lib\Release_x64\zlib.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /MT tests\bridge\fixture_parser.cpp /Fo_bin\bridge\ /Fe_bin\bridge\fixture-parser.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /MT /I src src\apps\mplayerc\bridge\MediaTimeIndex.cpp tests\bridge\index_probe.cpp /Fo_bin\bridge\ /Fe_bin\bridge\index-probe.exe /link /LTCG _bin\lib\Release_x64\Bento4.lib _bin\lib\Release_x64\zlib.lib
exit /b %errorlevel%
