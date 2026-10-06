@echo off
rem Builds build\client.dll (visuals) and build\hl.dll (cheats/spawn menu server side), both 32-bit.
rem Needs Visual Studio C++ tools, Python (for the export-name patch) and the HL SDK
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
if not exist build\server mkdir build\server

rem --- client.dll: body, camera, graphics, menu
cl /nologo /O2 /MT /LD /EHsc /permissive /W3 /wd4996 /wd4244 /wd4305 ^
  /I"%SDK%\common" /I"%SDK%\engine" /I"%SDK%\public" /I"%SDK%\pm_shared" /I"%SDK%\cl_dll" ^
  src\fpbody.cpp src\fpcam.cpp src\fppost.cpp src\fplight.cpp src\fpmenu.cpp src\fpfists.cpp src\fpclothes.cpp src\fpmaps.cpp src\fpchars.cpp src\fpprops.cpp src\fpglload.cpp src\fpcrash.cpp /Fobuild\ /Febuild\client.dll /link /DEF:src\client.def user32.lib opengl32.lib psapi.lib advapi32.lib delayimp.lib /DELAYLOAD:opengl32.dll || exit /b 1

rem --- hl.dll: server wrapper (fp_give, fp_spawn, cheats)
cl /nologo /O2 /MT /LD /EHa /permissive /W3 /wd4996 /wd4244 /wd4305 ^
  /I"src\server\cof" /I"%SDK%\dlls" /I"%SDK%\common" /I"%SDK%\engine" /I"%SDK%\public" /I"%SDK%\pm_shared" ^
  src\server\fpserver.cpp src\server\exports_gen.cpp /Fobuild\server\ /Febuild\hl.dll /link /DEF:src\server\hl.def user32.lib || exit /b 1
py test\fix_exports.py build\hl.dll src\server\hl_export_map.txt || python test\fix_exports.py build\hl.dll src\server\hl_export_map.txt || exit /b 1
exit /b 0

:nosdk
echo HL SDK not found at %SDK%
exit /b 1

:novs
echo Visual Studio with C++ tools not found
exit /b 1
