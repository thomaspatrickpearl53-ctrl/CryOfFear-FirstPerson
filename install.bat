@echo off
rem Installs the Cry of Fear first-person mod.
rem
rem Put this folder inside your Cry of Fear folder (next to cof.exe) and run install.bat.
rem It also works from anywhere if Cry of Fear is in the default Steam location, or:
rem   install.bat "D:\Games\Steam\steamapps\common\Cry of Fear"
rem   install.bat reset      (also puts back the mod's default settings)
setlocal
set "ENGINES_URL=https://github.com/thomaspatrickpearl53-ctrl/CryOfFear-FirstPerson/blob/main/docs/ENGINES.md"
set "RESET="
if /i "%~1"=="reset" (set "RESET=1" & shift)

call :find "%~1" || exit /b 1

tasklist | findstr /i /b "cof.exe CoFLaunchApp.exe" >nul && (
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

rem Which engine? Cry of Fear: Enhanced (Xash3D) brings its own cheats and only
rem enables them with the game's original hl.dll, so this mod's hl.dll is
rem installed on the original engine only. Only xash.dll counts: Enhanced's
rem uninstaller leaves its own cof-enhanced folder behind.
set "ENGINE=original"
if exist "%GAME%\xash.dll" set "ENGINE=enhanced"

if "%ENGINE%"=="original" (
  if exist "%~dp0bin\hl.dll" (
    if not exist "%CL%\hl_cof.dll" (
      copy /y "%CL%\hl.dll" "%CL%\hl.dll.original" >nul || goto failed
      move /y "%CL%\hl.dll" "%CL%\hl_cof.dll" >nul || goto failed
    )
    copy /y "%~dp0bin\hl.dll" "%CL%\hl.dll" >nul || goto failed
  )
) else (
  if exist "%CL%\hl_cof.dll" (
    move /y "%CL%\hl_cof.dll" "%CL%\hl.dll" >nul || goto failed
    echo Restored the original hl.dll so Cry of Fear: Enhanced's cheats work.
  )
)

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
if "%ENGINE%"=="enhanced" (
  echo Engine: Cry of Fear: Enhanced ^(Xash3D^). The F8 menu uses Enhanced's cheats.
) else (
  echo Engine: original Cry of Fear. The F8 menu uses this mod's cheats.
)
echo Start Cry of Fear from Steam as usual. To remove the mod, run uninstall.bat.
echo.
echo Using another engine, or installing/removing Cry of Fear: Enhanced later?
echo Read what to do here: %ENGINES_URL%
echo ^(also in docs\ENGINES.md next to this installer^)
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
