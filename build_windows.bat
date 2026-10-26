@echo off
setlocal
cd /d "%~dp0"
where cmake >nul 2>nul || (echo ERROR: Install CMake 3.28+ and add it to PATH. & pause & exit /b 1)
where git >nul 2>nul || (echo ERROR: Install Git and add it to PATH. & pause & exit /b 1)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 || goto :error
cmake --build build --config Release --target Editor --parallel 4 || goto :error
start "" "build\bin\Release\Editor.exe"
exit /b 0
:error
echo.
echo Build failed. Check messages above. Requires Visual Studio 2022 with Desktop development with C++.
pause
exit /b 1
