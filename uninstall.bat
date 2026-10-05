@echo off
rem Removes the Cry of Fear first-person mod and restores the original client.dll.
rem Same folder detection as install.bat; optionally: uninstall.bat "path\to\Cry of Fear"
setlocal
set "GAME="
if not "%~1"=="" if exist "%~1\cryoffear\cl_dlls\client.dll" set "GAME=%~f1"
if not defined GAME if exist "%~dp0cryoffear\cl_dlls\client.dll" set "GAME=%~dp0."
if not defined GAME if exist "%~dp0..\cryoffear\cl_dlls\client.dll" set "GAME=%~dp0.."
if not defined GAME if exist "%ProgramFiles(x86)%\Steam\steamapps\common\Cry of Fear\cryoffear\cl_dlls\client.dll" set "GAME=%ProgramFiles(x86)%\Steam\steamapps\common\Cry of Fear"
if not defined GAME set /p "GAME=Paste the path of your Cry of Fear folder (the one with cof.exe): "
for %%G in ("%GAME%") do set "GAME=%%~fG"

tasklist /fi "imagename eq cof.exe" | find /i "cof.exe" >nul && (
  echo Cry of Fear is running. Close it first, then run this again.
  if not defined FP_NOPAUSE pause
  exit /b 1
)

set "CL=%GAME%\cryoffear\cl_dlls"
if not exist "%CL%\client_cof.dll" (
  echo The mod isn't installed in "%GAME%".
  if not defined FP_NOPAUSE pause
  exit /b 0
)
move /y "%CL%\client_cof.dll" "%CL%\client.dll" >nul || (
  echo Couldn't restore client.dll. Try right-click ^> Run as administrator.
  if not defined FP_NOPAUSE pause
  exit /b 1
)
echo Original Cry of Fear restored. Your mod settings stay in cryoffear\fpbody.cfg in case you reinstall.
echo (Steam "Verify integrity of game files" also restores the original.)
if not defined FP_NOPAUSE pause
