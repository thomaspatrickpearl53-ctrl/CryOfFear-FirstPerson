@echo off
rem Builds cof-fpbody\build\client.dll (32-bit). Needs Visual Studio C++ tools and the HL SDK
rem (git clone https://github.com/ValveSoftware/halflife.git).
rem Usage: build.bat [path-to-halflife-sdk]
setlocal
set "SDK=%~1"
if "%SDK%"=="" set "SDK=%TEMP%\cofsdk\hlsdk"
if not exist "%SDK%\common\cl_entity.h" goto nosdk

for /d %%v in ("%ProgramFiles%\Microsoft Visual Studio\*") do for /d %%e in ("%%v\*") do if exist "%%e\VC\Auxiliary\Build\vcvars32.bat" set "VS=%%e"
if not defined VS goto novs
call "%VS%\VC\Auxiliary\Build\vcvars32.bat" >nul

cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /O2 /MT /LD /EHsc /permissive /W3 /wd4996 /wd4244 /wd4305 ^
  /I"%SDK%\common" /I"%SDK%\engine" /I"%SDK%\public" /I"%SDK%\pm_shared" /I"%SDK%\cl_dll" ^
  src\fpbody.cpp src\fpcam.cpp src\fppost.cpp src\fplight.cpp /Fobuild\ /Febuild\client.dll /link /DEF:src\client.def user32.lib opengl32.lib
exit /b %errorlevel%

:nosdk
echo HL SDK not found at %SDK%
exit /b 1

:novs
echo Visual Studio with C++ tools not found
exit /b 1
