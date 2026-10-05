@echo off
rem Installs the Cry of Fear first-person mod.
rem
rem Put this folder inside your Cry of Fear folder (next to cof.exe) and run install.bat.
rem It also works from anywhere if Cry of Fear is in the default Steam location, or:
rem   install.bat "D:\Games\Steam\steamapps\common\Cry of Fear"
rem   install.bat reset      (also puts back the mod's default settings)
setlocal
set "RESET="
if /i "%~1"=="reset" (set "RESET=1" & shift)

call :find "%~1" || exit /b 1

tasklist /fi "imagename eq cof.exe" | find /i "cof.exe" >nul && (
  echo Cry of Fear is running. Close it first, then run this again.
  if not defined FP_NOPAUSE pause
  exit /b 1
)

set "CL=%GAME%\cryoffear\cl_dlls"
set "FIRST="
if not exist "%CL%\client_cof.dll" (
  rem First install: keep the original client twice - as the forward target and as a backup.
  set "FIRST=1"
  copy /y "%CL%\client.dll" "%CL%\client.dll.original" >nul || goto failed
  move /y "%CL%\client.dll" "%CL%\client_cof.dll" >nul || goto failed
)
copy /y "%~dp0bin\client.dll" "%CL%\client.dll" >nul || goto failed

rem Settings: on first install (or "reset"), use the mod's defaults. Later updates keep yours.
if defined FIRST set "RESET=1"
if not exist "%GAME%\cryoffear\fpbody.cfg" set "RESET=1"
if defined RESET (
  if exist "%GAME%\cryoffear\fpbody.cfg" copy /y "%GAME%\cryoffear\fpbody.cfg" "%GAME%\cryoffear\fpbody.cfg.bak" >nul
  copy /y "%~dp0config\fpbody.cfg" "%GAME%\cryoffear\fpbody.cfg" >nul || goto failed
  echo Default settings applied.
)

echo.
echo Installed into "%GAME%".
echo Start Cry of Fear from Steam as usual. To remove the mod, run uninstall.bat.
if not defined FP_NOPAUSE pause
exit /b 0

:failed
echo Install failed. If Cry of Fear is in Program Files, try right-click ^> Run as administrator.
if not defined FP_NOPAUSE pause
exit /b 1

rem ---------------------------------------------------------------------------
:find
set "GAME="
if not "%~1"=="" if exist "%~1\cryoffear\cl_dlls\client.dll" set "GAME=%~f1"
if not defined GAME if exist "%~dp0cryoffear\cl_dlls\client.dll" set "GAME=%~dp0."
if not defined GAME if exist "%~dp0..\cryoffear\cl_dlls\client.dll" set "GAME=%~dp0.."
if not defined GAME if exist "%ProgramFiles(x86)%\Steam\steamapps\common\Cry of Fear\cryoffear\cl_dlls\client.dll" set "GAME=%ProgramFiles(x86)%\Steam\steamapps\common\Cry of Fear"
if not defined GAME (
  echo Couldn't find Cry of Fear.
  set /p "GAME=Paste the path of your Cry of Fear folder (the one with cof.exe): "
)
for %%G in ("%GAME%") do set "GAME=%%~fG"
if not exist "%GAME%\cryoffear\cl_dlls\client.dll" (
  echo "%GAME%" doesn't look like a Cry of Fear folder.
  if not defined FP_NOPAUSE pause
  exit /b 1
)
exit /b 0
