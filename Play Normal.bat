@echo off
rem Switches Cry of Fear back to the original client and starts it.
tasklist | findstr /i /b "cof.exe CoFLaunchApp.exe" >nul && (
  echo Cry of Fear is running. Close it first, then run this again.
  pause
  exit /b 1
)
set FP_NOPAUSE=1
call "%~dp0uninstall.bat" || (pause & exit /b 1)
rem Steam (and cof.exe) always start the copy in Steam's own library. A copy of
rem the game anywhere else is started directly (Steam still has to be running).
set "HERE=%~dp0"
if /i "%HERE:\steamapps\common\=%"=="%HERE%" if exist "%HERE%CoFLaunchApp.exe" (
  start "" /d "%HERE%." "%HERE%CoFLaunchApp.exe" -game cryoffear
  exit /b 0
)
start "" steam://rungameid/223710
