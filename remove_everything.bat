@echo off
rem Completely removes the first-person mod from Cry of Fear:
rem   - restores the original cryoffear\cl_dlls\client.dll and hl.dll
rem   - deletes the mod's backup, settings, log and generated body model
rem   - removes the mod's settings (cl_fp*, cl_pp*) and its F7/F6 toggle binds from config.cfg
rem Your saves and the game's own settings are not touched.
rem Usage: remove_everything.bat ["path\to\Cry of Fear"]
setlocal EnableDelayedExpansion
set "GAME="
if not "%~1"=="" if exist "%~1\cryoffear\cl_dlls\client.dll" set "GAME=%~f1"
if not defined GAME if exist "%~dp0cryoffear\cl_dlls\client.dll" set "GAME=%~dp0."
if not defined GAME if exist "%~dp0..\cryoffear\cl_dlls\client.dll" set "GAME=%~dp0.."
if not defined GAME if exist "%ProgramFiles(x86)%\Steam\steamapps\common\Cry of Fear\cryoffear\cl_dlls\client.dll" set "GAME=%ProgramFiles(x86)%\Steam\steamapps\common\Cry of Fear"
if not defined GAME set /p "GAME=Paste the path of your Cry of Fear folder (the one with cof.exe): "
for %%G in ("%GAME%") do set "GAME=%%~fG"
set "CF=%GAME%\cryoffear"
if not exist "%CF%\cl_dlls\client.dll" (
  echo "%GAME%" doesn't look like a Cry of Fear folder.
  pause
  exit /b 1
)

tasklist | findstr /i /b "cof.exe CoFLaunchApp.exe" >nul && (
  echo Cry of Fear is running. Close it first, then run this again.
  pause
  exit /b 1
)

echo This removes the first-person mod and ALL of its settings from:
echo   %GAME%
echo Your saves and the game's own settings stay.
if not defined FP_YES (
  choice /c YN /m "Continue"
  if errorlevel 2 exit /b 0
)

rem 1. The game's original client.dll
if exist "%CF%\cl_dlls\client_cof.dll" (
  move /y "%CF%\cl_dlls\client_cof.dll" "%CF%\cl_dlls\client.dll" >nul || goto failed
) else if exist "%CF%\cl_dlls\client.dll.original" (
  copy /y "%CF%\cl_dlls\client.dll.original" "%CF%\cl_dlls\client.dll" >nul || goto failed
)
if exist "%CF%\cl_dlls\client.dll.original" del /q "%CF%\cl_dlls\client.dll.original"
if exist "%CF%\cl_dlls\hl_cof.dll" (
  move /y "%CF%\cl_dlls\hl_cof.dll" "%CF%\cl_dlls\hl.dll" >nul || goto failed
) else if exist "%CF%\cl_dlls\hl.dll.original" (
  copy /y "%CF%\cl_dlls\hl.dll.original" "%CF%\cl_dlls\hl.dll" >nul || goto failed
)
if exist "%CF%\cl_dlls\hl.dll.original" del /q "%CF%\cl_dlls\hl.dll.original"
echo  - original client.dll and hl.dll restored

rem 2. Files the mod created
for %%F in (fpbody.cfg fpbody.cfg.bak fpbody.log fpcheats.cfg) do if exist "%CF%\%%F" del /q "%CF%\%%F"
if exist "%CF%\models\fpbody" rd /s /q "%CF%\models\fpbody"
echo  - mod settings, log and generated body model deleted

rem 2b. Maps (and the files they use) copied in from other games by the F8 Maps tab
if exist "%CF%\fpmaps_installed.txt" (
  for /f "usebackq delims=" %%L in ("%CF%\fpmaps_installed.txt") do if exist "%CF%\%%L" del /q "%CF%\%%L"
  del /q "%CF%\fpmaps_installed.txt"
  echo  - maps copied in from other games removed
)
if exist "%CF%\fpmaps_games.txt" del /q "%CF%\fpmaps_games.txt"

rem 3. The mod's lines in the game's config files (the game has no cl_fp* / cl_pp* settings of its own)
for %%C in ("%GAME%\config.cfg" "%CF%\config.cfg") do (
  if exist "%%~C" (
    findstr /v /r /i /c:"^cl_fp" /c:"^cl_pp" /c:"^fp_unlock_cheats" /c:"pp_toggle" /c:"ssao_toggle" /c:"fp_menu" "%%~C" > "%%~C.tmp"
    move /y "%%~C.tmp" "%%~C" >nul
  )
)
echo  - mod settings and toggle binds removed from config.cfg

echo.
echo Done. Cry of Fear is back to the original.
pause
exit /b 0

:failed
echo Couldn't restore client.dll. Try right-click ^> Run as administrator.
pause
exit /b 1
