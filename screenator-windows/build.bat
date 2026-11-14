@echo off
rem Build Screenator for Windows using the MSYS2 mingw-w64 toolchain.
setlocal
set HERE=%~dp0
if not exist C:\msys64\usr\bin\bash.exe (
  echo MSYS2 was not found at C:\msys64.
  exit /b 1
)
C:\msys64\usr\bin\bash.exe -lc "cd '%HERE:\=/%' && ./build.sh"
if errorlevel 1 exit /b 1
for %%I in ("%HERE%..\build-windows") do set OUTPUT_DIR=%%~fI
echo.
echo Output: %OUTPUT_DIR%\dist\Screenator.exe
